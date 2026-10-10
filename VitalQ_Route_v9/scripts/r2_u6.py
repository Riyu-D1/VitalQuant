"""Shared U6 helpers for the R2-17 sub-batches: #04 channel corridors (with exits extended past the guard), J8 guard, POFV placement."""
import math
from shapely.geometry import box, LineString
from route_geometry import polyset
from r2_rip import rip
from r2_common import route_entries
from r2route import SIGNAL_LAYERS, SMALL

J8_RECT = [{'outer': [[27.72, 50.065], [25.18, 50.065], [25.18, 51.335], [27.72, 51.335]], 'holes': []}]
FIELD = box(24.9 - 0.35, 49.8 - 0.35, 26.5 + 0.35, 51.8 + 0.35)
GUARD = FIELD.buffer(0.3)
ROW = {'A': 49.8, 'B': 50.2, 'C': 50.6, 'D': 51.0, 'E': 51.4, 'F': 51.8}
COL = {1: 26.5, 2: 26.1, 3: 25.7, 4: 25.3, 5: 24.9}
CH = {
    'A1': ([], 'N', SIGNAL_LAYERS), 'A2': ([], 'N', SIGNAL_LAYERS), 'A3': ([], 'N', SIGNAL_LAYERS),
    'E5': ([], 'W', SIGNAL_LAYERS), 'F2': ([], 'S', SIGNAL_LAYERS), 'F3': ([], 'S', SIGNAL_LAYERS),
    'E4': (['F4'], 'S', SIGNAL_LAYERS), 'C4': (['C5'], 'W', SIGNAL_LAYERS), 'E2': (['E1'], 'E', SIGNAL_LAYERS), 'B2': (['B1'], 'E', SIGNAL_LAYERS),
    'B3': (['C3', 'C2', 'C1'], 'E', (8, 6, 2, 0)), 'D4': (['D3', 'C3', 'C2', 'C1'], 'E', (6, 8, 2, 0)),
    'D2': (['C2', 'C1'], 'E', (10, 0)), 'E3': (['F3'], 'S', (8, 6, 2, 0)),
}
DIRS = {'N': (0, -1), 'S': (0, 1), 'E': (1, 0), 'W': (-1, 0)}


def cell(n):
    return (COL[int(n[1])], ROW[n[0]])


def corridor(ball):
    cells, d, _ = CH[ball]
    pts = [cell(ball)] + [cell(c) for c in cells]
    dx, dy = DIRS[d]
    last = pts[-1]
    pts.append((last[0] + dx * 1.2, last[1] + dy * 1.2))
    return LineString(pts).buffer(0.16, cap_style=2)


def add_guard(b):
    g = {'uuid': 'J8-track-guard', 'name': 'J8 guard (#05: no F track in J8 area)', 'parent': 'J8', 'layers': [0], 'tracks': True, 'vias': False, 'fills': False}
    b.keepouts[0].append((g, polyset(J8_RECT)))
    b.cache = {}


def drop_guard(b):
    b.keepouts[0] = [(k, s) for k, s in b.keepouts[0] if k['uuid'] != 'J8-track-guard']
    b.cache = {}


def route_ball(b, ledger, ball, via, approaches):
    """Route via's component to the net's main component, constrained to the ball's corridor inside the array (inner layers)."""
    net = via['net']
    comps, ids = b.components(net)
    src = comps[ids[via['uuid']]]
    live = [c for c in comps.values() if c is not src and any(e['kind'] in ('pads', 'zones') for e, _ in c)]
    if not live:
        return 'done'
    dst = max(live, key=lambda c: (any(e['kind'] == 'zones' for e, _ in c), len(c)))
    forb = GUARD.difference(corridor(ball))
    tmp = {'uuid': 'corridor-' + ball, 'name': 'R2-17 corridor', 'parent': None, 'layers': [6, 8, 10, 12], 'tracks': True, 'vias': False, 'fills': False}
    for l in (6, 8, 10, 12):
        b.keepouts[l].append((tmp, forb))
    b.cache = {}
    try:
        return route_entries(b, net, src, dst, net + ' U6', approaches, ledger['routes'], small_vias=False)
    finally:
        for l in (6, 8, 10, 12):
            b.keepouts[l] = [(k, s) for k, s in b.keepouts[l] if k['uuid'] != tmp['uuid']]
        b.cache = {}


def ball_via(b, u6, ball):
    return next((v for v in b.raw['vias'] if math.dist(v['xy'], u6[ball]['xy']) < 1e-4 and v['net'] == u6[ball]['net']), None)


def shift(dx, dy):
    """Re-centre the U6 grid helpers after moving U6 by (dx, dy)."""
    global FIELD, GUARD
    for k in ROW:
        ROW[k] += dy
    for k in COL:
        COL[k] += dx
    FIELD = box(min(COL.values()) - 0.35, min(ROW.values()) - 0.35, max(COL.values()) + 0.35, max(ROW.values()) + 0.35)
    GUARD = FIELD.buffer(0.3)
