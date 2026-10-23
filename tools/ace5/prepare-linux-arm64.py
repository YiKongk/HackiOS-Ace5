#!/usr/bin/env python3
"""Build native ARM64 BaseTools instead of downloading host binary packages.

Run in Linux ARM64, after installing Mu's Python requirements, clang/lld/llvm,
build-essential, nasm, uuid-dev, gcc-aarch64-linux-gnu and device-tree-compiler.
This uses the pinned Mu checkout and registers explicit local tool overrides.
"""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import yaml

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('mu', type=Path)
args = ap.parse_args()
mu = args.mu.resolve()
if platform.system() != 'Linux' or platform.machine() not in ('aarch64', 'arm64'):
    raise SystemExit('This setup is specifically for Linux ARM64')
pin = '42fe563791e51bf5500c2e470ed294ca0c94b97c'
if subprocess.check_output(['git', '-C', str(mu), 'rev-parse', 'HEAD'], text=True).strip() != pin:
    raise SystemExit('Unexpected Mu revision')
for tool in ('make', 'gcc', 'nasm', 'ndisasm', 'clang', 'lld-link', 'llvm-objcopy'):
    if not shutil.which(tool):
        raise SystemExit(f'Missing tool: {tool}')
subprocess.run(['git', '-C', str(mu), 'submodule', 'update', '--init', '--depth', '1'], check=True)
base = mu / 'Mu_Basecore'
subprocess.run(['git', '-C', str(base), 'submodule', 'update', '--init', '--depth', '1',
                'BaseTools/Source/C/BrotliCompress/brotli',
                'MdeModulePkg/Library/BrotliCustomDecompressLib/brotli',
                'MdeModulePkg/Universal/RegularExpressionDxe/oniguruma',
                'MdePkg/Library/BaseFdtLib/libfdt'], check=True)
subprocess.run(['make', '-C', str(base / 'BaseTools'), '-j2'], check=True)
bin_dir = base / 'BaseTools/Bin'
for config, files in (
        ('basetoolsbin_ext_dep.yaml', list((base / 'BaseTools/Source/C/bin').iterdir())),
        ('nasm_ext_dep.yaml', [Path(shutil.which(t)) for t in ('nasm', 'ndisasm')])):
    dep = yaml.safe_load((bin_dir / config).read_text())
    directory = bin_dir / (dep['name'] + '_extdep')
    dest = directory / 'Linux-ARM-64'
    dest.mkdir(parents=True, exist_ok=True)
    for source in files:
        target = dest / source.name
        if target.is_symlink() and target.resolve() == source.resolve():
            continue
        if target.exists() or target.is_symlink():
            raise SystemExit(f'Refusing to replace existing tool: {target}')
        target.symlink_to(source.resolve())
    # The dependency manager accepts the pinned slot; provenance is explicitly
    # local source/system NASM, not the published archive's hash.
    (directory / 'extdep_state.yaml').write_text(yaml.safe_dump({'version': dep['version']}))
    (directory / 'local-build-provenance.json').write_text(json.dumps({
        'source': 'local Mu BaseTools build / system NASM', 'mu_commit': pin,
        'basetools_commit': subprocess.check_output(['git', '-C', str(base), 'rev-parse', 'HEAD'], text=True).strip(),
        'nasm': subprocess.check_output(['nasm', '-v'], text=True).strip(),
    }, indent=2) + '\n')
(mu / 'Build').mkdir(exist_ok=True)
subprocess.run(['python3', str(mu / 'Platforms/OnePlus/giuliaPkg/DeviceBuild.py'),
                '--update', '-t', 'RELEASE'], cwd=mu, check=True)
