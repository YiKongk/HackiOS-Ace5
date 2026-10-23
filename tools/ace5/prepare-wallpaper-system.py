#!/usr/bin/env python3
"""Stage stock iOS 16 wallpaper with PNG layers for the Ace 5 single-core boot.

Run on macOS with sips. Uses local system assets only; keeps source files and the
original HEIC layers intact. Changes only CAML image references in a new system
tree. Layer geometry, filters and animations remain unchanged. Pack and verify
the new APFS image before uploading it to RAM.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from urllib.parse import quote, unquote
import xml.etree.ElementTree as ET

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--base', type=Path, required=True, help='Existing staged RAM system directory')
ap.add_argument('--output', type=Path, required=True, help='New directory on the same filesystem')
args = ap.parse_args()
relative = Path('Library/Wallpaper/Collections/iOS_16~iphone.wallpaperCollection')
report = args.output.with_name(args.output.name + '-wallpaper.json')
if args.output.exists() or args.output.is_symlink():
    ap.error('output already exists')
if report.exists() or report.is_symlink():
    ap.error('output report already exists')
if args.output.resolve().is_relative_to(args.base.resolve()):
    ap.error('output must not be inside the baseline')
if not (args.base / relative).is_dir() or not shutil.which('sips'):
    ap.error('requires the local iOS 16 collection and macOS sips')

def properties(path):
    result = subprocess.check_output(['sips', '-g', 'pixelWidth', '-g', 'pixelHeight',
                                      '-g', 'hasAlpha', '-g', 'profile', str(path)], text=True)
    return dict(line.strip().split(': ', 1) for line in result.splitlines()[1:] if ': ' in line)

shutil.copytree(args.base, args.output, symlinks=True, copy_function=os.link)
converted = {}
for caml in sorted((args.output / relative).rglob('main.caml')):
    original = caml.read_bytes()
    updated = original
    for uri in sorted({element.get('src', '') for element in ET.fromstring(original).iter()}):
        if not uri.lower().endswith('.heic'):
            continue
        source = caml.parent / unquote(uri)
        if not source.resolve().is_relative_to((args.output / relative).resolve()) or source.is_symlink():
            ap.error(f'nonlocal wallpaper asset: {source}')
        target = source.with_suffix('.png')
        if source not in converted:
            if target.exists() or target.is_symlink():
                ap.error(f'PNG already exists: {target}')
            subprocess.run(['sips', '-s', 'format', 'png', str(source), '--out', str(target)],
                           check=True, stdout=subprocess.DEVNULL)
            before, after = properties(source), properties(target)
            if before != after:
                ap.error(f'image dimensions, alpha or color profile changed: {source}: {before} -> {after}')
            converted[source] = {'source': str(source.relative_to(args.output)),
                                 'png': str(target.relative_to(args.output)), **after,
                                 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                                 'png_sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
            print(f'Prepared {target.relative_to(args.output)}', flush=True)
        new_uri = quote(str(target.relative_to(caml.parent)))
        old_attr = ('src="' + uri + '"').encode()
        if old_attr not in updated:
            ap.error(f'cannot locate CAML image reference: {uri}')
        updated = updated.replace(old_attr, ('src="' + new_uri + '"').encode())
    if updated != original:
        # The staged files are hard links: replace the directory entry before
        # writing so the original system and extracted Apple assets stay intact.
        mode = caml.stat().st_mode & 0o777
        caml.unlink()
        caml.write_bytes(updated)
        caml.chmod(mode)
if not converted:
    ap.error('no HEIC image references found')
report.write_text(json.dumps(list(converted.values()), indent=2) + '\n')
print(f'Prepared {len(converted)} PNG layers; report: {report}; no device changed')
