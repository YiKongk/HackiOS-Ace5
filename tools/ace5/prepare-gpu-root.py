#!/usr/bin/env python3
"""Assemble a new local Ace 5 GPU root directory, preserving the firmware baseline.

Run on the host filesystem. No downloads, device access, partition writes or
system image changes. Use mkapfs.py and check-image.sh on the resulting directory.
"""
import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--base', type=Path, required=True, help='Existing firmware-bearing small root directory')
ap.add_argument('--components', type=Path, required=True, help='build-gpu-userland.sh output directory')
ap.add_argument('--ios-root', type=Path, required=True, help='Local extracted iOS 16.5 rootfs')
ap.add_argument('--output', type=Path, required=True, help='New directory; never overwritten')
ap.add_argument('--metal', action='store_true')
ap.add_argument('--app', action='store_true', help='Include the application launch helper; install Triangle into system.apfs separately')
ap.add_argument('--display-test', action='store_true', help='Include the A750 hardware scaler pixel-readback diagnostic')
ap.add_argument('--wallpaper', action='store_true', help='Include PosterBoard; pair with prepare-wallpaper-system.py for PNG wallpaper layers')
ap.add_argument('--compositor-threading', choices=['auto', 'inline'],
                help='auto: Mesa default; inline: avoid driver-thread handoff in backboardd on a single CPU; omitted: preserve base')
ap.add_argument('--jobs', type=Path, help='rc.jobs recipe; defaults to the Metal suite or Settings scenario')
args = ap.parse_args()

if args.app and not args.metal:
    ap.error('--app requires --metal')
if args.output.exists():
    ap.error('output already exists')
if args.output.resolve().is_relative_to(args.base.resolve()):
    ap.error('output must not be inside the baseline directory')
jobs = args.jobs or REPO / 'tools/ace5' / ('rc.jobs.metal' if args.metal else 'rc.jobs.settings')
copies = [(args.components / name, Path('lib') / name) for name in
          ['libOSMesa.8.dylib', 'libredwood-gl.dylib']]
copies += [(args.components / 'gl_test', Path('bin/gl_test')), (jobs, Path('etc/rc.jobs'))]
if args.display_test:
    copies += [(args.components / 'display_test', Path('bin/display_test'))]
if args.metal:
    copies += [(args.components / name, Path('lib') / name) for name in
               ['libredwoodmetal.dylib', 'libredwoodair.dylib']]
    copies += [(args.components / name, Path('bin') / name) for name in
               ['metal_drv_test', 'metal_caps', 'metal_props', 'triangle_app']]
    copies += [(args.ios_root / src, Path('share/metal') / dst) for src, dst in [
        ('System/Library/CoreImage/PortraitFilters.cifilter/default.metallib', 'PortraitFilters.metallib'),
        ('System/Library/Extensions/AppleMetalGLRenderer.bundle/default.metallib', 'AppleMetalGLRenderer.metallib')]]
    copies += [(args.components / 'RedwoodMetal.bundle' / name, Path('lib/RedwoodMetal.bundle') / name)
               for name in ['RedwoodMetal', 'Info.plist']]
if args.app:
    copies += [(args.components / 'battery_test', Path('bin/battery_test'))]
required = [src for src, _ in copies] + [args.base / 'etc/rc', args.base / 'lib/libredwood.dylib']
if args.compositor_threading:
    required.append(args.base / 'LaunchDaemons/com.apple.backboardd.plist')
labels = ['com.apple.sleepd', 'com.apple.configd']
if args.wallpaper:
    # Predecoded PNG layers preserve the stock CAML animation without starting
    # mediaserverd and its audio/watch dependencies just to decode wallpaper.
    labels.append('com.apple.PosterBoard')
    required.append(args.ios_root / 'Applications/PosterBoard.app/PosterBoard')
for label in labels:
    folder = 'LaunchAngels' if label == 'com.apple.PosterBoard' else 'LaunchDaemons'
    required.append(args.ios_root / 'System/Library' / folder / (label + '.plist'))
for path in required:
    if not path.is_file():
        ap.error(f'missing input: {path}')
shutil.copytree(args.base, args.output, symlinks=True)
for src, dst in copies:
    target = args.output / dst
    for parent in dst.parents:
        if (args.output / parent).is_symlink():
            ap.error(f'will not write through a baseline directory symlink: {parent}')
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_symlink():
        target.unlink()
    shutil.copy2(src, target)
# sleepd stops SpringBoard's tight XPC retry loop. configd serves the system
# configuration store used by Preferences. Both binaries are in system.apfs.
job_dir = args.output / 'LaunchDaemons'
if job_dir.is_symlink():
    ap.error('will not write through a baseline LaunchDaemons symlink')
for label in labels:
    target = job_dir / (label + '.plist')
    if target.is_symlink():
        target.unlink()
subprocess.run([sys.executable, str(REPO / 'userland/mkjobs.py'),
                str(args.ios_root.resolve()), str(job_dir.resolve()),
                *labels], cwd=REPO, check=True)
if args.compositor_threading:
    target = job_dir / 'com.apple.backboardd.plist'
    if target.is_symlink():
        target.unlink()
        shutil.copy2(args.base / 'LaunchDaemons/com.apple.backboardd.plist', target)
    job = plistlib.loads(target.read_bytes())
    env = job.setdefault('EnvironmentVariables', {})
    if args.compositor_threading == 'inline':
        # Only change command generation for the window server. GPU execution
        # remains asynchronous, and Metal applications retain their own policy.
        env['GALLIUM_THREAD'] = 'false'
    else:
        env.pop('GALLIUM_THREAD', None)
    target.write_bytes(plistlib.dumps(job))
print(f'Prepared {args.output.resolve()} (no image or device changed)')
