#!/usr/bin/env python3
"""Pack a giulia loader image; never substitute redwood firmware or DTB."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tomllib
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--mu', type=Path, required=True)
ap.add_argument('--dtb', type=Path, required=True)
ap.add_argument('--output', type=Path, required=True)
ap.add_argument('--stage', choices=['diagnostic', 'kernel'], default='diagnostic')
args = ap.parse_args()
root = Path(__file__).resolve().parents[2]
mu, output = args.mu.resolve(), args.output.resolve()
config = tomllib.loads((mu / 'Resources/Configs/giulia.toml').read_text())
fd_base, fd_size = config['uefi_fd']['base'], config['uefi_fd']['size']
if (fd_base, fd_size, config['boot_image']['header_version']) != (0xA7100000, 0x300000, 4):
    raise SystemExit('Unexpected giulia UEFI layout')
fd = (mu / 'Build/giuliaPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd').read_bytes()
if len(fd) != fd_size:
    raise SystemExit('Unexpected UEFI FD size')
dtb = args.dtb.read_bytes()
if len(dtb) < 40 or struct.unpack('>I', dtb[:4])[0] != 0xd00dfeed:
    raise SystemExit('Invalid DTB magic')
if struct.unpack('>I', dtb[4:8])[0] != len(dtb) or b'qcom,pineapple\0' not in dtb:
    raise SystemExit('Invalid DTB size or wrong SoC')
build_dir = root / ('out/giulia' if args.stage == 'diagnostic' else 'out/giulia-kernel')
build = json.loads((build_dir / 'loader-build.json').read_text())
payload = (build_dir / 'usb-payload.bin').read_bytes()
if (build['stage'] != args.stage or build['storage_writes'] or
        build['payload_sha256'] != hashlib.sha256(payload).hexdigest()):
    raise SystemExit('Loader build metadata mismatch; rebuild the requested stage')
if payload[:4] != b'XNUP' or len(payload) >= 0x03f00000:
    raise SystemExit('Invalid payload')
output.parent.mkdir(parents=True, exist_ok=True)
obj = output.parent / 'BootShim.elf'
shim = output.parent / 'BootShim.bin'
subprocess.run(['clang', '--target=aarch64-linux-gnu', '-c', str(mu / 'BootShim/BootShim.S'),
                '-DREQUIRES_KERNEL_HEADER=1', f'-DFD_BASE={fd_base:#x}', f'-DFD_SIZE={fd_size:#x}',
                '-DPAYLOAD_BASE=0xA8000000', '-o', str(obj)], check=True)
subprocess.run(['llvm-objcopy', '-O', 'binary', str(obj), str(shim)], check=True)
blob = shim.read_bytes() + fd + payload
blob += b'\0' * (-len(blob) % 16)
kernel = output.parent / 'giulia-kernel.gz-dtb'
kernel.write_bytes(gzip.compress(blob, mtime=0) + dtb)
subprocess.run([sys.executable, str(mu / 'Resources/Scripts/mkbootimg.py'),
                '--kernel', str(kernel), '--ramdisk', str(mu / 'Resources/ramdisk'),
                '--header_version', '4', '--os_version', config['boot_image']['os_version'],
                '--os_patch_level', config['boot_image']['os_patch_level'], '-o', str(output)], check=True)
data = output.read_bytes()
if data[:8] != b'ANDROID!' or struct.unpack_from('<I', data, 40)[0] != 4:
    raise SystemExit('Invalid boot image header')
manifest = {
    'device': 'OnePlus Ace 5 PKG110 / giulia', 'stage': args.stage,
    'kernel_execution': build['kernel_execution'], 'allowed_partition_write': ['logdump'],
    'loader_sha256': build['loader_sha256'], 'payload_sha256': build['payload_sha256'],
    'mu_commit': subprocess.check_output(['git','-C',str(mu),'rev-parse','HEAD'], text=True).strip(),
    'fd_base': hex(fd_base), 'fd_size': fd_size, 'payload_base': '0xa8000000',
    'dtb_sha256': hashlib.sha256(dtb).hexdigest(), 'image_bytes': len(data),
    'sha256': hashlib.sha256(data).hexdigest(), 'header_version': 4,
}
output.with_suffix('.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(json.dumps(manifest, indent=2))
