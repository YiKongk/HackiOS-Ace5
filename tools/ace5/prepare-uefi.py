#!/usr/bin/env python3
"""Apply the pinned HackiOS UEFI hook to Mu-Silicium for giulia, without touching a device."""
import argparse
from pathlib import Path
import shutil
import subprocess
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('mu', type=Path)
args = ap.parse_args()
mu = args.mu.resolve()
root = Path(__file__).resolve().parents[2]
base = '42fe563791e51bf5500c2e470ed294ca0c94b97c'
actual = subprocess.check_output(['git', '-C', str(mu), 'rev-parse', 'HEAD'], text=True).strip()
if actual != base:
    raise SystemExit(f'Mu revision mismatch: expected {base}, got {actual}')
patch = str(root / 'uefi/mu-silicium.patch')
check = subprocess.run(['git', '-C', str(mu), 'apply', '--check', patch], capture_output=True)
if check.returncode == 0:
    subprocess.run(['git', '-C', str(mu), 'apply', patch], check=True)
else:
    subprocess.run(['git', '-C', str(mu), 'apply', '--reverse', '--check', patch], check=True)
rel = 'Silicon/Silicium/SiliciumPkg/Library/BootManagerLib'
for name in ('XnuLoader.c', 'XnuLoader.h'):
    target = mu / rel / name
    data = (root / 'uefi/files' / rel / name).read_text().replace('0xF0000000ULL', '0xA8000000ULL')
    target.write_text(data)
# Correct the SKU typo in upstream giulia; this is the model verified over ADB.
p = mu / 'Platforms/OnePlus/giuliaPkg/giulia.dsc'
s = p.read_text().replace('"PJK110"', '"PKG110"')
p.write_text(s)
# User permits logdump only. UEFI uses emulated NVRAM; no partitioning or persistent OS volume is used.
print(f'Prepared giulia UEFI at {mu}; payload reserved at 0xA8000000')
