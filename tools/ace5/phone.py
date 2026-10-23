#!/usr/bin/env python3
"""Ace 5 RAM boot and single-core trials. No flash, erase or slot changes.

The loader may write logdump, as explicitly authorized for this device.
All iOS images are sent to RAM. Other storage and unported drivers stay disabled.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--serial', required=True, help='Original ADB/bootloader serial')
sub = ap.add_subparsers(dest='command', required=True)
enter = sub.add_parser('enter', help='Temporary boot into the giulia loader')
enter.add_argument('--stage', choices=['diagnostic', 'kernel'], default='diagnostic')
ramdisk = sub.add_parser('ramdisk', help='Upload a system image to RAM')
ramdisk.add_argument('--image', type=Path, help='Explicit local system APFS image; defaults to userland/system.apfs')
run = sub.add_parser('run', help='Run single-core iOS, then save logs and framebuffer')
run.add_argument('--seconds', type=int, default=120,
                 help='Return after this many seconds; 0 keeps iOS running until a physical restart')
run.add_argument('--verbose', action='store_true')
run.add_argument('--kernel', type=Path, help='Explicit locally built Mach-O kernel')
run.add_argument('--root-image', type=Path, help='Local small APFS image, e.g. with device firmware')
run.add_argument('--touch', action='store_true', help='Enable the giulia S3910 driver; requires --kernel')
run.add_argument('--gpu-probe', type=int, choices=range(1, 21),
                 help='Ace 5 GPU probe: 1 GCC, 2 GPUCC, 3 CX power, 4 XO clocks/GMU, 5 TCM, 6 streams, 7 firmware, 8 SMMU, 9 operating clocks, 10 GMU boot, 11 HFI query, 12 power tables, 13 core firmware, 14 scheduler memory, 15 GX power, 16 GPU SMMU and firmware, 17 CP initialization, 18 ZAP authentication, 19 nonsecure CP DMA, 20 BR/BV indirect buffer; requires --kernel')
run.add_argument('--bootcpu', type=int, choices=[0, 1], default=0,
                 help='0: validated boot core; 1: experimental separate USB observer core')
run.add_argument('--live-log', action='store_true',
                 help='Save live log snapshots every 10 seconds; requires CPU 1 and a finite run')
run.add_argument('--live-interval', type=float, default=10,
                 help='Seconds between live-log snapshots, 0.1 to 60 (default 10)')
run.add_argument('--gpu-step-delay', type=int, default=0,
                 help='Pause 0 to 3000 ms before each new GPU core register access for tracing')
run.add_argument('--gpu-direct', action='store_true',
                 help='Use a host CP ring instead of GMU HWSCHED; requires --gpu-probe 13 or later')
run.add_argument('--gpu-keep', action='store_true',
                 help='Publish the experimental A750 RedwoodGPU backend; requires direct stage 20 and zero trace delay')
run.add_argument('--render-trace', action='store_true',
                 help='Log each desktop frame and GL calls slower than 20 ms; requires --gpu-keep')
run.add_argument('--frame-timing', action='store_true',
                 help='Log touch, compositor, presentation and CPU timing for tools/frames.py')
run.add_argument('--thread-trace', choices=['SpringBoard', 'backboardd', 'Preferences', 'wifid',
                                          'mediaserverd', 'PosterBoard', 'CollectionsPoster'],
                 help='Sample up to 16 threads with the existing kernel stack sampler every 5 seconds')
run.add_argument('--metal', action='store_true',
                 help='Allow Metal discovery properties; requires --gpu-keep and a root with RedwoodMetal.bundle')
run.add_argument('--gpu-cached', action='store_true',
                 help='Experimental cached buffers with synchronous ownership; requires --gpu-keep')
run.add_argument('--fb-filter', choices=['bilinear', 'nearest'],
                 help='Display scaling filter; nearest trades edge smoothness for less CPU work')
run.add_argument('--gpu-display', action='store_true',
                 help='Experimental A750 hardware scaling directly into the panel; requires --gpu-keep')
selftest = sub.add_parser('selftest')
selftest.add_argument('--kernel', type=Path, help='Explicit locally built Mach-O kernel')
sub.add_parser('screenshot')
sub.add_parser('reboot', help='Return to the unchanged Android boot chain')
args = ap.parse_args()
logs = ROOT / 'logs/giulia'
logs.mkdir(parents=True, exist_ok=True)

def call(command, timeout=20, check=True):
    p = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    output = p.stdout + p.stderr
    if check and (p.returncode or re.search(r'FAILED|^ERROR:', output, re.M)):
        raise RuntimeError(output.strip())
    return output

def fb(*command, timeout=20):
    return call(['fastboot', '-s', 'xnuloader', *map(str, command)], timeout)

def getvar(key):
    answer = fb('getvar', key)
    m = re.search(r'^' + re.escape(key) + r': (.*)$', answer, re.M)
    return m.group(1).strip() if m else ''

def ensure_loader(kernel=False):
    if (getvar('product') != 'xnuloader' or getvar('platform') != 'giulia' or
            getvar('storage-writes') != 'disabled'):
        raise RuntimeError('Requires the guarded giulia loader built by these tools')
    if kernel and getvar('build-stage') != 'kernel':
        raise RuntimeError('Diagnostic loader cannot execute XNU; enter --stage kernel first')

def wait_device(serial):
    for _ in range(60):
        devices = call(['fastboot', 'devices'])
        if any(line.split() and line.split()[0] == serial for line in devices.splitlines()):
            return
        time.sleep(1)
    raise RuntimeError(f'Device {serial} did not appear; no flash fallback is attempted')

def screenshot():
    ensure_loader()
    w, h, stride = map(int, getvar('screen').split())
    fb('oem', 'screenshot')
    raw_path = logs / 'screen.raw'
    fb('get_staged', raw_path, timeout=60)
    data = raw_path.read_bytes()
    if not (0 < w <= 4096 and 0 < h <= 8192 and stride >= w * 4 and len(data) == stride * h):
        raise RuntimeError('Unexpected framebuffer geometry')
    rows = bytearray()
    for y in range(h):
        row = data[y * stride:y * stride + w * 4]
        rgb = bytearray(w * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = row[2::4], row[1::4], row[0::4]
        rows += b'\0' + rgb
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    png = logs / (time.strftime('%Y%m%d-%H%M%S') + '-screen.png')
    png.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                    chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
    raw_path.unlink()
    print(png)

try:
    if args.command == 'enter':
        directory = ROOT / ('out/giulia' if args.stage == 'diagnostic' else 'out/giulia-kernel')
        image = directory / f'hackios-giulia-{args.stage}.img'
        manifest = json.loads(image.with_suffix('.json').read_text())
        if (manifest['stage'] != args.stage or manifest['allowed_partition_write'] != ['logdump'] or
                hashlib.sha256(image.read_bytes()).hexdigest() != manifest['sha256']):
            raise RuntimeError('Image manifest or SHA-256 mismatch')
        adb = ['adb', '-s', args.serial]
        if call(adb + ['get-state'], check=False).strip() == 'device':
            if call(adb + ['shell', 'getprop', 'ro.product.model']).strip() != 'PKG110':
                raise RuntimeError('Connected Android device is not PKG110')
            call(adb + ['reboot', 'bootloader'])
        wait_device(args.serial)
        stock = ['fastboot', '-s', args.serial]
        for key, value in [('product', 'pineapple'), ('unlocked', 'yes')]:
            if not re.search(r'^' + key + ': ' + value + r'$', call(stock + ['getvar', key]), re.M):
                raise RuntimeError('Unexpected or locked bootloader')
        print(call(stock + ['boot', str(image)], timeout=60), flush=True)
        wait_device('xnuloader')
        ensure_loader(kernel=args.stage == 'kernel')
        print('Giulia loader ready; Android partitions were not flashed.')
    elif args.command == 'ramdisk':
        ensure_loader(kernel=True)
        image = args.image.resolve() if args.image else ROOT / 'userland/system.apfs'
        if not image.is_file() or not 0 < image.stat().st_size < 0xffffffff:
            raise RuntimeError('Missing or oversized system image')
        fb('oem', 'ramdisk')
        print(fb('stage', image, timeout=600))
    elif args.command in ('run', 'selftest'):
        ensure_loader(kernel=True)
        payload = ROOT / 'out/giulia-kernel/run-payload.bin'
        seconds = args.seconds if args.command == 'run' else 15
        if not 0 <= seconds <= 3600:
            raise RuntimeError('seconds must be 0..3600')
        if args.command == 'run' and args.live_log and (args.bootcpu!=1 or seconds==0):
            raise RuntimeError('--live-log requires --bootcpu 1 and a finite return timer')
        commandline = (f'cpus=1 loader_return_after={seconds} epoch={int(time.time())} sb_delay=8 '
                       'touchscreen=0 buttons=0 cpufreq=0 gpu=0 wifi=0 adsp=0')
        if args.command == 'run' and args.touch:
            if not args.kernel:
                raise RuntimeError('--touch requires an explicit locally built --kernel')
            commandline += ' ace5_touch=1'
        if args.command == 'run' and args.verbose:
            commandline += ' -v'
        if args.command == 'run' and args.gpu_direct:
            if not args.gpu_probe or args.gpu_probe < 13:
                raise RuntimeError('--gpu-direct requires --gpu-probe 13 or later')
            commandline += ' ace5_gpu_direct=1'
        if args.command == 'run' and args.gpu_keep:
            if args.gpu_probe!=20 or not args.gpu_direct or args.gpu_step_delay:
                raise RuntimeError('--gpu-keep requires --gpu-probe 20 --gpu-direct and zero trace delay')
            commandline += ' ace5_gpu_keep=1'
        if args.command == 'run' and args.render_trace:
            if not args.gpu_keep:
                raise RuntimeError('--render-trace requires --gpu-keep')
            commandline += ' glslow=20000 glframes=1'
        if args.command == 'run' and args.frame_timing:
            commandline += ' touch_lat=1 cpuload=1000'
        if args.command == 'run' and args.metal:
            if not args.gpu_keep:
                raise RuntimeError('--metal requires --gpu-keep')
            commandline += ' metal=1'
        if args.command == 'run' and args.gpu_cached:
            if not args.gpu_keep:
                raise RuntimeError('--gpu-cached requires --gpu-keep')
            commandline += ' gpu_cached=2'
        if args.command == 'run' and args.fb_filter:
            commandline += f' fb_filter={int(args.fb_filter == "bilinear")}'
        if args.command == 'run' and args.gpu_display:
            if not args.gpu_keep:
                raise RuntimeError('--gpu-display requires --gpu-keep')
            commandline += ' fb_gpu=1'
        if args.command == 'run' and args.thread_trace:
            commandline += (f' ustacks={args.thread_trace} ustacks_at=25 ustacks_ms=5000 '
                            'ustacks_count=60 ustacks_tname=* ustacks_threads=16 cpuload=1000')
        if args.command == 'run' and args.gpu_probe:
            if args.gpu_probe>=18 and not args.gpu_direct:
                raise RuntimeError('ZAP stages require --gpu-direct')
            if not args.kernel:
                raise RuntimeError('--gpu-probe requires an explicit locally built --kernel')
            if args.gpu_probe>=3 and (args.bootcpu!=1 or args.seconds==0):
                raise RuntimeError('GPU power probes require --bootcpu 1 and a finite return timer')
            commandline += f' ace5_gpu_probe={args.gpu_probe}'
            if not 0<=args.gpu_step_delay<=3000:
                raise RuntimeError('--gpu-step-delay must be between 0 and 3000 ms')
            if args.gpu_probe>=17 and args.seconds<10+(240*args.gpu_step_delay+999)//1000:
                raise RuntimeError('Return timer is too short for all GPU trace steps and cleanup')
            commandline += f' ace5_gpu_step_delay={args.gpu_step_delay}'
        live_interval=args.live_interval if args.command=='run' else 10
        if not 0.1<=live_interval<=60:
            raise RuntimeError('--live-interval must be between 0.1 and 60 seconds')
        if args.command == 'run' and args.bootcpu:
            commandline += f' bootcpu={args.bootcpu}'
        kernel = args.kernel if args.kernel else ROOT / 'prebuilt/mach.development.bcm2837'
        if not kernel.is_file():
            raise RuntimeError(f'Missing kernel: {kernel}')
        print('Kernel SHA-256:', hashlib.sha256(kernel.read_bytes()).hexdigest(), flush=True)
        command = [sys.executable, str(ROOT / 'xnuloader/mkpayload.py'), '-o', str(payload),
                   f'kernel={kernel.resolve()}']
        if args.command == 'run':
            root_image = args.root_image or ROOT / 'userland/root.apfs'
            if not root_image.is_file() or not 0 < root_image.stat().st_size <= 32 * 1024 * 1024:
                raise RuntimeError('Missing or oversized small root image (limit 32 MiB)')
            print('Root image SHA-256:', hashlib.sha256(root_image.read_bytes()).hexdigest(), flush=True)
            command.append(f'ramdisk={root_image.resolve()}')
        command += ['--cmdline', commandline]
        print(call(command))
        fb('stage', payload, timeout=60)
        print(fb('oem', 'run' if args.command == 'run' else 'selftest'), flush=True)
        if args.command == 'run' and seconds == 0:
            print('XNU launch accepted; automatic return is disabled. '
                  'Use a physical power + volume-up restart to return to Android. '
                  'USB diagnostics are unavailable while XNU owns CPU 0.', flush=True)
            sys.exit(0)
        if args.command == 'run' and args.bootcpu == 0:
            print('XNU owns the boot core; waiting before asking USB for its result.', flush=True)
            time.sleep(seconds + 5)
        deadline = time.monotonic() + seconds + 60
        next_live = time.monotonic() + live_interval
        live_prefix = time.strftime('%Y%m%d-%H%M%S')
        live_count = 0
        while True:
            result = fb('getvar', 'result')
            if 'result: running' not in result:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('Kernel did not return; preserve the screen and inspect the live log')
            if args.command=='run' and args.live_log and time.monotonic()>=next_live:
                response=call(['fastboot','-s','xnuloader','oem','live-log'],check=False)
                # Stock fastboot discards the optional OKAY payload for OEM commands.
                if 'OKAY' in response and 'FAILED' not in response:
                    live_count+=1
                    path=logs/f'{live_prefix}-live-{live_count:03d}.log'
                    fb('get_staged',path)
                    print(f'Live log: {path.name} ({path.stat().st_size} bytes)',flush=True)
                elif 'no live kernel' not in response:
                    raise RuntimeError('Live-log request failed: '+response.strip())
                next_live=time.monotonic()+live_interval
            time.sleep(min(2,live_interval))
        log = logs / (time.strftime('%Y%m%d-%H%M%S') + '-' + args.command + '.log')
        # Fetch immediately: idle "oem log" would discard the staged kernel log.
        fb('get_staged', log)
        print(result, log)
        screenshot()
        saved_log = log.read_bytes().decode('utf-8', errors='replace')
        for line in saved_log.splitlines():
            if re.search(r'ace5-gpu: (stage [0-9]+|probe ended|.*cleanup FAILED)', line):
                print(line, flush=True)
        if 'panic(cpu ' in saved_log or re.search(r'ace5-gpu: (stage [0-9]+ FAILED|.*cleanup FAILED|live failure)', saved_log):
            raise RuntimeError(f'Kernel/probe failure; saved diagnostic log: {log}')
        if args.command == 'run' and args.gpu_probe and f'ace5-gpu: stage {args.gpu_probe} complete' not in saved_log:
            raise RuntimeError(f'GPU probe did not complete requested stage {args.gpu_probe}; see {log}')
        if args.command == 'run' and args.gpu_keep and 'ace5-gpu: persistent startup ready=1' not in saved_log:
            raise RuntimeError(f'A750 backend did not start; see {log}')
    elif args.command == 'screenshot':
        screenshot()
    else:
        ensure_loader()
        print(fb('reboot'))
except (RuntimeError, subprocess.TimeoutExpired, OSError, ValueError) as error:
    raise SystemExit(str(error))
