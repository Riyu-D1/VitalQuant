"""R2-17: U6 full escape with enforced #04 channel topology. Rip my own R2-14 inner-layer U6 routes that cut through the array,
place J8-notched POFVs, then route each ball only through its assigned channel cells (temporary corridor keepouts)."""
import json
import math
from shapely.geometry import box, LineString
from r2 import ROOT
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2_common import route_entries, open_comps
from r2route import SIGNAL_LAYERS, SMALL
from route_geometry import polyset

J8_RECT = [{'outer': [[27.72, 50.065], [25.18, 50.065], [25.18, 51.335], [27.72, 51.335]], 'holes': []}]
FIELD = box(24.9 - 0.35, 49.8 - 0.35, 26.5 + 0.35, 51.8 + 0.35)
C = {'A': 49.8, 'B': 50.2, 'C': 50.6, 'D': 51.0, 'E': 51.4, 'F': 51.8}
X = {1: 26.5, 2: 26.1, 3: 25.7, 4: 25.3, 5: 24.9}


def cell(n):
    return (X[int(n[1])], C[n[0]])


CH = {  # ball -> (cells after the ball, exit point, allowed layers)
    'B3': (['C3', 'C2', 'C1'], (27.05, 50.6), (8, 6, 2, 0)),
    'D4': (['D3', 'C3', 'C2', 'C1'], (27.05, 50.6), (6, 8, 2, 0)),
    'D2': (['C2', 'C1'], (27.05, 50.6), (10, 0)),
    'E3': (['F3'], (25.7, 52.35), (8, 6, 2, 0)),
    'E4': (['F4'], (25.3, 52.35), (6, 8, 2, 0)),
    'C4': (['C5'], (24.35, 50.6), SIGNAL_LAYERS),
    'E2': (['E1'], (27.05, 51.4), SIGNAL_LAYERS),
    'B2': (['B1'], (27.05, 50.2), SIGNAL_LAYERS),
    'A1': ([], (26.5, 49.25), SIGNAL_LAYERS), 'A2': ([], (26.1, 49.25), SIGNAL_LAYERS), 'A3': ([], (25.7, 49.25), SIGNAL_LAYERS),
    'E5': ([], (24.35, 51.4), SIGNAL_LAYERS), 'F2': ([], (26.1, 52.35), SIGNAL_LAYERS),
}
ORDER = ['B3', 'D4', 'D2', 'E3', 'E4', 'C4', 'E2', 'B2', 'A3', 'A2', 'A1', 'E5', 'F2']
NEW_BALLS = ['B3', 'D4', 'D2', 'E3', 'E4', 'C4', 'E2', 'B2']
AUTH = {'ESP_TX', 'ESP_RX', 'AFE_INP', 'MISO_FL', 'CS_FLASH', '+3V3_ANA', 'TX_5V', 'TX4', '+1V8', 'PD_K'}
WINDOW = [21.0, 45.0, 31.0, 56.0]
APP = 'R2-17 U6 enforced #04 channel topology (corridor keepouts inside array), J8 per-via POFV notches, own R2-14 in-array inner routes ripped first, A*'


def corridor(ball):
    cells, exitp, _ = CH[ball]
    pts = [cell(ball)] + [cell(c) for c in cells] + [exitp]
    return LineString(pts).buffer(0.16, cap_style=2)


def with_corridor(b, ball, fn):
    forb = FIELD.buffer(0.3).difference(corridor(ball))
    tmp = {'uuid': 'corridor-' + ball, 'name': 'R2-17 corridor guard', 'parent': None, 'layers': [6, 8, 10], 'tracks': True, 'vias': False, 'fills': False}
    for l in (6, 8, 10):
        b.keepouts[l].append((tmp, forb))
    b.cache = {}
    try:
        return fn()
    finally:
        for l in (6, 8, 10):
            b.keepouts[l] = [(k, s) for k, s in b.keepouts[l] if k['uuid'] != tmp['uuid']]
        b.cache = {}


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    guard = {'uuid': 'J8-track-guard', 'name': 'J8 guard', 'parent': 'J8', 'layers': [0], 'tracks': True, 'vias': False, 'fills': False}
    b.keepouts[0].append((guard, polyset(J8_RECT)))
    b.cache = {}
    # 1. my own R2-14 inner-layer U6 routes inside the array
    mine = set()
    for t in json.loads((ROOT / 'candidates/R2-14/plan.json').read_text())['tracks']:
        u = t.get('board_uuid')
        if u in b.items and t['layer'] in (6, 8, 10) and LineString([t['a'], t['b']]).intersects(FIELD.buffer(0.25)):
            mine.add(u)
    rip(b, ledger, list(mine), 'R2-17: own R2-14 inner-layer U6 route crossing the ball array (re-routed via its #04 channel)')
    print('ripped own R2-14 in-array segments', len(mine), sorted({r['net'] for r in ledger['removed']}), flush=True)
    seeds = {}
    for num in NEW_BALLS:
        p = u6[num]
        for h in b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8):
            if h['kind'] == 'tracks' and h['net'] in AUTH:
                seeds[h['uuid']] = f"blocks U6.{num} POFV ({h['net']}, authorised)"
            else:
                print('BLOCKER', num, h, flush=True)
    for u, why in seeds.items():
        rip(b, ledger, [u], why)
    placed = {}
    for num in NEW_BALLS:
        p = u6[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8)
        if hits:
            print('SITE BLOCKED', num, [(h['kind'], h.get('net', h.get('name'))) for h in hits], flush=True)
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num}
        b.add_items([], [v])
        placed[num] = v
    print('placed', list(placed), flush=True)
    vias = {num: next((v for v in b.raw['vias'] if math.dist(v['xy'], u6[num]['xy']) < 1e-4 and v['net'] == u6[num]['net']), None) for num in ORDER}
    for num in ORDER:
        v = vias[num]
        if v is None:
            continue
        net = v['net']
        comps, ids = b.components(net)
        src = comps[ids[v['uuid']]]
        live = [c for c in comps.values() if c is not src and any(e['kind'] in ('pads', 'zones') for e, _ in c)]
        if not live:
            continue
        dst = max(live, key=lambda c: (any(e['kind'] == 'zones' for e, _ in c), len(c)))
        lay = CH[num][2]
        with_corridor(b, num, lambda: route_entries(b, net, src, dst, net + ' U6', [(APP + f', ball {num} layers {lay}, margin 3 mm', 3.0, lay), (APP + f', ball {num} all layers, margin 8 mm', 8.0, tuple(sorted(set(lay) | set(SIGNAL_LAYERS))))], ledger['routes'], small_vias=False))
    nets = {r['net'] for r in ledger['removed']}
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, WINDOW, [(APP + ' (ripped nets), margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ' (ripped nets), margin 8 mm', 8.0, SIGNAL_LAYERS)])
    for num, v in placed.items():
        comps, ids = b.components(v['net'])
        comp = comps[ids[v['uuid']]]
        if not (any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp) or any(e['kind'] == 'zones' for e, _ in comp)):
            uu = [v['uuid']] + [e['uuid'] for e, _ in comp if str(e['uuid']).startswith('new-') and e['kind'] in ('tracks', 'vias')]
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback U6.' + num + ' (no completed connection)')
            print('ROLLBACK', num, flush=True)
    b.keepouts[0] = [(k, s) for k, s in b.keepouts[0] if k['uuid'] != 'J8-track-guard']


run('R2-17', 'U6_channels', plan, j8={'original_outline': J8_RECT}, next_step='R2-18 SPI_SCK F3 + U22 C1/D1 (LED3_K re-route, U18 move)')
