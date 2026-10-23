#!/usr/bin/env python3
"""Read-only ADB backup of Ace 5 boot-chain partitions, with remote SHA-256 verification.
Images stay outside Git. This program has no flash, reboot, erase or write-to-device operation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--serial', required=True)
ap.add_argument('--output', type=Path, required=True)
args = ap.parse_args()
adb = ['adb', '-s', args.serial]
def root(command):
    return subprocess.check_output(adb + ['exec-out', 'su', '-c', command], timeout=180)

model = subprocess.check_output(adb + ['shell', 'getprop', 'ro.product.model'], text=True).strip()
if model != 'PKG110':
    raise SystemExit(f'Refusing unexpected model: {model!r}')
if 'uid=0(' not in root('id').decode():
    raise SystemExit('Root required')
args.output.mkdir(parents=True, exist_ok=True, mode=0o700)
manifest_path = args.output / 'manifest.json'
if manifest_path.exists():
    raise SystemExit('Existing manifest: choose a new output directory to preserve the backup')
manifest = {'model': model, 'partitions': {}}
manifest['fingerprint'] = subprocess.check_output(adb + ['shell', 'getprop', 'ro.build.fingerprint'], text=True).strip()
manifest['slot'] = subprocess.check_output(adb + ['shell', 'getprop', 'ro.boot.slot_suffix'], text=True).strip()
for base in ('boot', 'init_boot', 'vendor_boot', 'dtbo', 'vbmeta', 'vbmeta_system', 'vbmeta_vendor', 'recovery', 'xbl', 'xbl_config', 'uefi'):
    for slot in ('a', 'b'):
        name = f'{base}_{slot}'
        device = f'/dev/block/by-name/{name}'
        output = args.output / f'{name}.img'
        if output.exists():
            raise SystemExit(f'Refusing to overwrite {output}')
        size = int(root(f'blockdev --getsize64 {device}').strip())
        if not 0 < size <= 512 * 1024 * 1024:
            raise SystemExit(f'Unexpected partition size: {name}: {size}')
        part = output.with_suffix('.img.part')
        with part.open('xb') as stream:
            subprocess.run(adb + ['exec-out', 'su', '-c', f'cat {device}'], stdout=stream, check=True, timeout=180)
        digest = hashlib.sha256(part.read_bytes()).hexdigest()
        remote_digest = root(f'sha256sum {device}').decode().split()[0]
        if part.stat().st_size != size or digest != remote_digest:
            raise SystemExit(f'Backup verification failed: {name}')
        part.rename(output)
        manifest['partitions'][name] = {'bytes': size, 'sha256': digest}
        # Keep a partial inventory for interrupted backups without claiming completion.
        (args.output / 'manifest.partial.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'{name}: {size} bytes SHA-256 verified', flush=True)
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
(args.output / 'manifest.partial.json').unlink()
print(f'Verified {len(manifest["partitions"])} partitions: {manifest_path}')
