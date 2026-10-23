#!/usr/bin/env python3
"""Summarize --render-trace output bracketed by rc.jobs.uiperf markers.

Reports compositor completion cadence, NOT panel refresh rate or GPU-only time.
Compare identical workloads and trace flags; idle/boot frames are excluded.
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import statistics

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('logs', nargs='+', type=Path)
ap.add_argument('--snapshots', action='store_true', help='Merge ordered overlapping live snapshots from one boot')
ap.add_argument('--merged-output', type=Path, help='Save recovered log for tools/frames.py; requires --snapshots')
args = ap.parse_args()
if args.merged_output and args.merged_output.resolve() in {path.resolve() for path in args.logs}:
    ap.error('--merged-output must not overwrite an input log')

def percentile(values, percent):
    return sorted(values)[max(0, math.ceil(len(values) * percent / 100) - 1)]

def cadence(times):
    gaps = [b - a for a, b in zip(times, times[1:])]
    if not gaps or min(gaps) < 0 or times[-1] <= times[0]:
        raise ValueError('invalid frame times')
    return {
        'frames': len(times), 'span_ms': round(times[-1] - times[0], 3),
        'frames_per_second': round(1000 * len(gaps) / (times[-1] - times[0]), 2),
        'median_interval_ms': round(statistics.median(gaps), 3),
        'p95_interval_ms': round(percentile(gaps, 95), 3),
        'intervals_over_25ms_percent': round(100 * sum(x > 25 for x in gaps) / len(gaps), 2),
    }

def summarize(path, content=None):
    lines = (content if content is not None else path.read_text(errors='replace')).splitlines()
    result = {'log': str(path), 'metric': 'compositor completion cadence; not panel FPS', 'scenes': {}}
    for scene in ('DESKTOP', 'SETTINGS'):
        def marker(suffix):
            indices = [i for i, line in enumerate(lines)
                       if line == '$ /redwood/bin/hello_dyn ' + scene + suffix]
            if len(indices) != 1:
                raise ValueError(f'{scene}{suffix}: expected one marker, found {len(indices)}')
            return indices[0]
        start, end = marker('_BEGIN'), marker('_END')
        if start >= end:
            raise ValueError(f'{scene}: markers out of order')
        segment = lines[start:end]
        frames = [m for line in segment if (m := re.search(
            r'redwood-gl: f (\d+) at ([\d.]+) s: (\d+) draws.*; (\d+) in place, (\d+) copied;', line))]
        if len(frames) < 2:
            raise ValueError(f'{scene}: not enough frame records')
        ids = [int(m[1]) for m in frames]
        if any(b != a + 1 for a, b in zip(ids, ids[1:])):
            raise ValueError(f'{scene}: missing, duplicate or interleaved frame records')
        times = [float(m[2]) * 1000 for m in frames]
        slow = Counter(m[1] for line in segment if (m := re.search(r'tl: slow (\S+) \d+ at', line)))
        stats = {
            **cadence(times),
            'mean_draws': round(statistics.mean(int(m[3]) for m in frames), 2),
            'cpu_copied_frames': sum(int(m[5]) for m in frames),
            'slow_calls': dict(slow),
        }
        # Kernel and userspace logs are separate sections. Match their monotonic
        # clocks, not textual ordering; deduplicate forwarded kernel records.
        presents = {}
        loads = {}
        for line in lines:
            m = re.search(r'tl: v (\d+) ready \d+ shown \d+ done (\d+)', line)
            if m and times[0] <= int(m[2]) / 1000 <= times[-1]:
                presents[int(m[1])] = int(m[2]) / 1000
            m = re.search(r'tl: u at (\d+) 1:(\d+)', line)
            if m and times[0] <= int(m[1]) / 1000 <= times[-1]:
                loads[int(m[1])] = int(m[2])
        if len(presents) >= 2:
            keys = sorted(presents)
            if any(b != a + 1 for a, b in zip(keys, keys[1:])):
                raise ValueError(f'{scene}: incomplete presentation trace')
            stats['panel_buffer_updates'] = cadence([presents[k] for k in keys])
        if loads:
            stats['cpu1_busy_mean_percent'] = round(statistics.mean(loads.values()), 2)
        result['scenes'][scene.lower()] = stats
    return result

try:
    if args.snapshots:
        recovered = args.logs[0].read_bytes()
        previous = recovered
        for path in args.logs[1:]:
            current = path.read_bytes()
            # The loader retains a boot prefix and a rolling console tail.
            # Require an exact overlap rather than silently joining lost data.
            anchor = previous[-4096:]
            offset = current.rfind(anchor)
            if not anchor or offset < 0:
                raise ValueError(f'{path}: snapshots do not overlap; cannot recover complete trace')
            recovered += current[offset + len(anchor):]
            previous = current
        if args.merged_output:
            args.merged_output.write_bytes(recovered)
        results = [summarize(args.merged_output or args.logs[0], recovered.decode(errors='replace'))]
    else:
        if args.merged_output:
            ap.error('--merged-output requires --snapshots')
        results = [summarize(path) for path in args.logs]
    print(json.dumps(results, indent=2))
except ValueError as error:
    ap.error(str(error))
