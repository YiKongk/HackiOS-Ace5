#!/usr/bin/env python3
"""Check positive readback evidence in a complete Ace 5 Metal suite log.

Expected invalid-index requests and unsupported Stencil8 are not test passes.
Missing evidence is a failure even if phone.py successfully returned the kernel.
"""
import argparse
from pathlib import Path
import re

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('log', type=Path)
ap.add_argument('--stress', action='store_true', help='Check the 271-shader/600-frame rc.jobs.metalstress run')
ap.add_argument('--gpu-display', action='store_true', help='Check rc.jobs.gpudisplay, including hardware display coverage and pixel readback')
args = ap.parse_args()
log = args.log.read_text(errors='replace')
kernels = ['MetalFaceMask_InitMinMax', 'MetalFaceMask_CalcCubeInputScaling',
           'MetalFaceMask_InitSpanTable', 'MetalFaceMask_PropogateAssociations',
           'fbs_bistochastize_init', 'fbs_pcg_iter2', 'fbs_pcg_iter3',
           'image_calcminmax0', 'image_calcminmax2']
checks = {name: f'drvtest: dispatched {name}: status 4' in log for name in kernels}
checks.update({
    'standard device discovery': 'caps: device = RedwoodMetalDevice' in log and 'caps: done' in log,
    'fragment reference': 'THE SAME -- a Metal fragment shader drew on the Adreno' in log,
    'index offset': 'THE INDEX BUFFER AND THE OFFSET ARE READ ON THE ADRENO' in log,
    'index widths/bounds': 'the index type is honoured' in log and '6 of 6 drew and 2 of 2 were refused' in log,
    'three depth formats': log.count('drvtest: depth: the far draw was DISCARDED') == 3 and
                          log.count('drvtest: depth: the nearer draw PASSED') == 3,
    'drawable readback': 'triangle: done: --layer mode drew 2 frames with 0 failures' in log,
    'suite completed': 'A750 METAL SUITE COMPLETE' in log,
    'no kernel/GPU failure': not re.search(r'panic\(cpu |ace5-gpu: (live failure|.*cleanup FAILED|stage \d+ FAILED)', log),
    'no mismatching readback': 'NOT THE SAME' not in log,
})
if args.stress or args.gpu_display:
    # This suite exercises presentation over time; compute/index/depth are in
    # the separate bounded suite and must not be inferred from these results.
    checks = {label: checks[label] for label in
              ['standard device discovery', 'drawable readback', 'no kernel/GPU failure',
               'no mismatching readback']}
    drawn = re.findall(r'drvtest: drawall (\S+)\s+status (\d+)', log)
    checks.update({
        'fragment sweep complete': 'drawall: 263 drew, 0 refused at the draw, 2 got no pipeline, '
                                   '6 could not be given the textures they declare (of the 271 fragment shaders' in log,
        '263 distinct completed draws': len({name for name, status in drawn if status == '4'}) == 263 and
                                        all(status == '4' for _, status in drawn),
        'UIKit application ran as mobile': bool(re.search(r'triangle: start: .*uid 501,', log)),
        '600 application frames': 'triangle: done: 600 frames, 0 failures.' in log,
        'no refused application launch': 'battery_test: SpringBoard was asked to open' in log and
                                         'it REFUSES' not in log,
        'Settings launch accepted before and after app': log.count(
            'battery_test: SpringBoard was asked to open ' + ('prefs:' if args.gpu_display else 'prefs:root=WIFI') + ': it does') == 2,
        'no frame failures': not re.search(r'triangle: .*\b[1-9]\d* failures', log),
        'navigation script reached end': ('A750 GPU DISPLAY STRESS COMPLETE' if args.gpu_display else
                                          'A750 METAL STRESS AND SETTINGS COMPLETE') in log,
    })
if args.gpu_display:
    display_cases = ['identity', 'nearest-2x', 'linear-2x', 'cropped-nearest', 'cropped-linear',
                     'panel-nearest', 'panel-linear', 'cropped-clear']
    for name in display_cases:
        checks['display pixels: ' + name] = bool(re.search(
            r'displaytest: ' + re.escape(name) + r' kr=0 fence=[1-9]\d* bad=0 ', log))
    coverage = re.findall(r'GPU display layers=(\d+) CPU fallbacks=(\d+)', log)
    checks.update({
        'display suite complete': 'displaytest: COMPLETE failures=0' in log,
        'at least 600 layers displayed by GPU': bool(coverage) and max(int(n) for n, _ in coverage) >= 600,
        'no CPU display fallback': bool(coverage) and all(int(n) == 0 for _, n in coverage) and
                                   'CPU display fallback ' not in log and 'CPU clear fallback ' not in log,
        'CoreAnimation hardware composition': bool(re.search(r'context for CoreAnimation, by Mesa[^\n]*; FD750;', log)) and 'drawn in place' in log and
            not re.search(r'\b[1-9]\d* pictures? copied out', log),
    })
checks['no test process crash'] = 'drvtest: SIGNAL' not in log
checks['GPU commands drained'] = any(int(wanted) == int(actual) and int(actual) > 5
                                    for wanted, actual in re.findall(
                                        r'direct fence expected=(\d+) actual=(\d+) [^\n]*done=1', log))
checks['GPU shut down cleanly'] = 'ace5-gpu: probe ended; CX off=1;' in log
for label, passed in checks.items():
    print(f'{"PASS" if passed else "FAIL"}: {label}')
raise SystemExit(0 if all(checks.values()) else 1)
