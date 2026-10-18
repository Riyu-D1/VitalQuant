"""Batch 1: GND stitching. Verify sites on the 0.05mm grid, then emit board edits."""
import sys, json
sys.path.insert(0, 'scripts')
import numpy as np
from router import Router

r = Router('geom.json')
blocked, goal, viaok = r.masks_for('GND', 0.1016, 0.1016, vrad=0.15)
REV = {0: 'F', 1: 'B', 2: 'G2', 3: 'L3', 4: 'P4', 5: 'G5'}

OPS = [
    # via-in-pad GND BGA pads (0.3/0.15)
    {'via_in_pad': ('U6', 'B4', 25.3, 50.2)},
    {'via_in_pad': ('U6', 'D5', 24.9, 51.0)},
    {'via_in_pad': ('U6', 'D1', 26.5, 51.0)},
    {'via_in_pad': ('U7', 'C4', 37.7859, 11.4808)},
    {'via_in_pad': ('U7', 'D5', 37.3859, 11.0808)},
    {'via_in_pad': ('U7', 'E6', 36.9859, 10.6808)},
    # stitching vias + F tracks
    {'track': ('F', 25.18, 52.565, 25.70, 52.90, 0.1016), 'via': (25.70, 52.90, 0.4, 0.2)},
    {'track': ('F', 37.8699, 15.7945, 38.40, 15.85, 0.1016), 'via': (38.40, 15.85, 0.4, 0.2)},
    # GND stub bridge on F
    {'track': ('F', 37.386, 10.681, 37.786, 10.681, 0.1016)},
]

ok = True
for op in OPS:
    if 'via_in_pad' in op:
        ref, no, x, y = op['via_in_pad']
        c = r.cell(x, y)
        v = bool(viaok[c[1], c[0]])
        print(f'via-in-pad {ref}.{no} @({x},{y}) viaok={v}')
        if not v:
            # find why: which layers have foreign copper within vdil at this cell
            for li in range(NL := 6):
                pass
            ok = ok and False
    if 'via' in op:
        x, y, d, dr = op['via']
        c = r.cell(x, y)
        v = bool(viaok[c[1], c[0]])
        print(f'via @({x},{y}) {d}/{dr} viaok={v}')
        ok = ok and v
    if 'track' in op:
        lname, x1, y1, x2, y2, w = op['track']
        li = {'F': 0, 'B': 1}[lname]
        n = int(max(abs(x2 - x1), abs(y2 - y1)) / r.res) + 1
        bad = 0
        for i in range(n + 1):
            f = i / max(n, 1)
            c = r.cell(x1 + (x2 - x1) * f, y1 + (y2 - y1) * f)
            if blocked[li][c[1], c[0]]:
                bad += 1
        print(f'track {lname} ({x1},{y1})->({x2},{y2}) blocked_cells={bad}')
        ok = ok and (bad == 0)
print('ALL_OK' if ok else 'SOME_BLOCKED')
