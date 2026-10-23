#!/usr/bin/env python3
"""Inspect locally extracted Ace 5 firmware. Reads files; never uploads or loads them.

GMU block format and VA regions follow OnePlusOSS SM8650 gen7_gmu_vma.
Usage: inspect-adreno750-fw.py /local/firmware-directory > /local/manifest.json
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

NAMES = ('gen70900_aqe.fw', 'gen70900_sqe.fw', 'gen70900_zap.mbn', 'gmu_gen70900.bin')
REGIONS = ((0, 0x4000, 'ITCM'), (0x4000, 0x1000000, 'cache'),
           (0x10004000, 0x10008000, 'DTCM'),
           (0x60000000, 0x80000000, 'noncached'),
           (0xc0000000, 0xe0000000, 'noncached-extended'))

def region(address, size):
    if not size or address & 3 or size & 3 or address + size > 0x100000000:
        raise ValueError(f'invalid GMU range {address:#x}+{size:#x}')
    for low, high, name in REGIONS:
        if low <= address and address + size <= high:
            return name
    raise ValueError(f'unsupported GMU range {address:#x}+{size:#x}')

def parse_gmu(data):
    blocks, ranges, versions = [], [], {}
    offset = 0
    if not 16 <= len(data) <= 1024 * 1024:
        raise ValueError('GMU image size outside inspection limit')
    while offset < len(data):
        if len(data) - offset < 16:
            raise ValueError('truncated GMU header')
        address, size, kind, value = struct.unpack_from('<4I', data, offset)
        payload = offset + 16
        if size > len(data) - payload:
            raise ValueError('truncated GMU payload')
        block = dict(offset=offset, address=hex(address), size=size, type=hex(kind), value=hex(value))
        if size:
            block['region'] = region(address, size)
            for lo, hi in ranges:
                if address < hi and lo < address + size:
                    raise ValueError('overlapping GMU data blocks')
            ranges.append((address, address + size))
            for i, name in enumerate(('core', 'core_dev', 'power', 'power_dev', 'hfi')):
                va = 0x10007fe0 + 4 * i
                if address <= va and va + 4 <= address + size:
                    versions[name] = hex(struct.unpack_from('<I', data, payload + va - address)[0])
        elif kind in (1, 7):
            block['region'] = region(address, value)
            block['reservation_bytes'] = value
        elif kind not in (2, 3, 4, 5, 6):
            raise ValueError(f'unknown GMU metadata type {kind:#x}')
        blocks.append(block)
        offset = payload + size
    if not ranges:
        raise ValueError('GMU image contains no data')
    return dict(blocks=blocks, versions=versions)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = dict(profile='giulia / Adreno750v2 / gen7-9-1', files={})
    for name in NAMES:
        data = (args.directory / name).read_bytes()
        if not data:
            raise ValueError(f'empty firmware: {name}')
        result['files'][name] = dict(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        if name == 'gmu_gen70900.bin':
            result['gmu'] = parse_gmu(data)
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
