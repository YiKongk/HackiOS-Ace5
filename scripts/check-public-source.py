#!/usr/bin/env python3
"""Read-only checks for tracked source files before a public commit.

Does not scan ignored local images/caches or claim exhaustive secret detection.
Run from a Git checkout; reviews remain necessary before publishing new data.
"""
import ast
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
paths = [Path(p) for p in subprocess.check_output(
    ['git', '-C', str(ROOT), 'ls-files', '-z']).decode().split('\0') if p]
errors = []
counts = {'files': len(paths), 'bytes': 0, 'python': 0, 'shell': 0, 'markdown': 0}
forbidden_dirs = {'local', 'out', 'logs', 'prebuilt', 'xnu-src', '.userland.git', 'fw-gpu'}
forbidden_ext = {'.img', '.dtb', '.efi', '.te', '.fw', '.mbn', '.bin', '.dylib',
                 '.ipsw', '.dmg', '.o', '.obj', '.pyc', '.log', '.apfs'}
source_exceptions = {Path('userland/rc.jobs.apfs')}
secret = re.compile(rb'gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}'
                    rb'|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|AKIA[0-9A-Z]{16}')
private_path = re.compile('/' + r'(?:Users|home)/[A-Za-z0-9_.-]+/')
for rel in paths:
    p = ROOT / rel
    if p.is_symlink():
        errors.append(f'{rel}: tracked symlink needs manual review')
        continue
    if rel.parts[0] in forbidden_dirs or (rel.suffix.lower() in forbidden_ext and rel not in source_exceptions):
        errors.append(f'{rel}: generated/private asset path')
    data = p.read_bytes()
    counts['bytes'] += len(data)
    if b'\0' in data or len(data) > 1024 * 1024:
        errors.append(f'{rel}: binary content or unexpectedly large source')
    if secret.search(data):
        errors.append(f'{rel}: credential-like content (value withheld)')
    try:
        content = data.decode('utf-8')
    except UnicodeDecodeError:
        errors.append(f'{rel}: not UTF-8 text')
        continue
    if private_path.search(content):
        errors.append(f'{rel}: personal absolute path')
    if rel.suffix == '.py':
        counts['python'] += 1
        try:
            ast.parse(content, filename=str(rel))
        except SyntaxError as e:
            errors.append(f'{rel}: Python syntax at line {e.lineno}')
    if rel.suffix == '.sh':
        counts['shell'] += 1
        result = subprocess.run(['bash', '-n', str(p)], capture_output=True)
        if result.returncode:
            errors.append(f'{rel}: shell syntax check failed')
    if rel.suffix == '.md':
        counts['markdown'] += 1
        for raw in re.findall(r'!?\[[^\]\n]*\]\(([^)\n]+)\)', content):
            link = raw.split(' ')[0].strip('<>')
            if re.match(r'\w+://|mailto:|#', link):
                continue
            target = (p.parent / link.split('#')[0]).resolve()
            if not target.exists():
                errors.append(f'{rel}: missing link target {link}')
for message in errors:
    print(message, file=sys.stderr)
print(counts)
if errors:
    raise SystemExit(1)
print('Tracked source checks passed; ignored device assets were not read.')
