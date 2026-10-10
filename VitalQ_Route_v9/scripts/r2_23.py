"""R2-23 (SUPERVISOR override of 3-attempt rule for the U6 wall): rip EVERYTHING within 3 mm of U6 (keep U6/U22 ball POFVs and plane stitch vias),
inner-ball fan-out on dedicated inner layers with reserved exit lanes, then outer ring, then the ripped nets. Based on R2-22 (SUPERVISOR A) with corrected order learnt from R2-21:
1) U6 outer-ring balls (B-surface escapes, most constrained) first, 2) inner corridors with STRICT layer per C-channel net
(B3 In3, D4 In2, D2 In4; no all-layer fallback), 3) easy POFV balls, 4) other U6 nets, 5) wall/other nets. Counters reset (regional rip)."""
import math
from shapely.geometry import box, LineString, Point
from r2 import STATE, save_json, state
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2_common import route_entries
from r2route import SIGNAL_LAYERS, SMALL
import r2_u6 as U
WIN = box(21.9, 46.8, 29.5, 54.8)
PLANE_NETS = {'GND', '+3V3', 'VBAT_SYS'}


def array_guard(b, on):
    tmp = {'uuid': 'array-guard', 'name': 'array guard (inner layers)', 'parent': None, 'layers': [6, 8, 10], 'tracks': True, 'vias': True, 'fills': False}
    for l in (6, 8, 10):
        b.keepouts[l] = [(k, s) for k, s in b.keepouts[l] if k['uuid'] != 'array-guard']
        if on:
            b.keepouts[l].append((tmp, U.FIELD.buffer(0.15)))
    b.cache = {}

OUTER_FIRST = ['C1', 'F3', 'E1', 'F1', 'F4', 'F5', 'C5', 'B1', 'A4', 'B5', 'A5', 'D1']
INNER = ['B3', 'D4', 'D2', 'C4', 'E3', 'E4', 'E2', 'B2']
EASY = ['A1', 'A2', 'A3', 'E5', 'F2']
# supervisor grouping (In1 is the GND plane, so 'power' nets go to In4 = Power Layer 5); C-channel needs 3 distinct layers -> AFE_BG on In2
STRICT = {'B3': (8, 2, 0), 'B2': (8, 2, 0), 'D2': (6, 2, 0), 'E3': (6, 2, 0), 'E2': (6, 2, 0), 'D4': (10, 2, 0), 'E4': (10, 2, 0), 'C4': (10, 2, 0)}
LANE_LAYER = {'B3': 8, 'B2': 8, 'D2': 6, 'E3': 6, 'E2': 6, 'D4': 10, 'E4': 10, 'C4': 10}
APP = 'R2-23 3mm-around-U6 rip; dedicated inner layer per inner ball with reserved exit lanes; corridors first, outer ring, then ripped nets; 60 s budget'
REGION = [21.9, 46.8, 29.5, 54.8]


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    ball_xy = {(round(p['xy'][0], 4), round(p['xy'][1], 4)): p['net'] for p in b.raw['pads'] if p['ref'] in ('U6', 'U22')}
    victims = [t['uuid'] for t in b.raw['tracks'] if LineString([t['a'], t['b']]).intersects(WIN)]
    victims += [v['uuid'] for v in b.raw['vias'] if WIN.contains(Point(v['xy'])) and ball_xy.get((round(v['xy'][0], 4), round(v['xy'][1], 4))) != v['net'] and v['net'] not in PLANE_NETS]
    nets = sorted({b.items[u]['net'] for u in victims})
    s = state()
    reset = {k: v for k, v in s['attempts'].items() if k.split(':')[0] in set(nets) | {p['net'] for p in b.raw['pads'] if p['ref'] in ('U6', 'U22')}}
    s.setdefault('attempts_reset_R2_23_override', {}).update(reset)
    for k in reset:
        s['attempts'][k] = []
    save_json(STATE, s)
    rip(b, ledger, victims, 'SUPERVISOR override: rip everything within 3 mm of U6 (x21.9-29.5, y46.8-54.8)')
    print('regional rip', len(victims), 'items on', len(nets), 'nets', flush=True)
    U.add_guard(b)
    placed = {}
    for num in INNER + EASY:
        v = U.ball_via(b, u6, num)
        if not v:
            p = u6[num]
            hits = b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8)
            if hits:
                print('SITE BLOCKED', num, flush=True)
                continue
            v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num}
            b.add_items([], [v])
        placed[num] = v
    # 1) inner balls: dedicated layer inside the array and along a reserved 1.5 mm exit lane
    lanes = {}
    for num in INNER:
        cells, d, _ = U.CH[num]
        pts = [U.cell(num)] + [U.cell(c) for c in cells]
        dx, dy = U.DIRS[d]
        last = pts[-1]
        lanes[num] = LineString(pts + [(last[0] + dx * 2.0, last[1] + dy * 2.0)]).buffer(0.2, cap_style=2)
    def lane_guard(except_ball):
        for l in (0, 2, 6, 8, 10):
            b.keepouts[l] = [(k, s_) for k, s_ in b.keepouts[l] if not str(k['uuid']).startswith('lane-')]
        for num, lane in lanes.items():
            if num == except_ball or num not in placed:
                continue
            ko = {'uuid': 'lane-' + num, 'name': 'reserved exit lane U6.' + num, 'parent': None, 'layers': [LANE_LAYER[num]], 'tracks': True, 'vias': True, 'fills': False}
            b.keepouts[LANE_LAYER[num]].append((ko, lane))
            kv = {'uuid': 'lane-v-' + num, 'name': 'no via in lane U6.' + num, 'parent': None, 'layers': [0], 'tracks': False, 'vias': True, 'fills': False}
            b.keepouts[0].append((kv, lane))
        b.cache = {}
    for num in INNER:
        v = placed.get(num)
        if not v:
            continue
        lay = STRICT[num]
        lane_guard(num)
        other = [l for l in (6, 8, 10) if l != LANE_LAYER[num]]
        tmp = {'uuid': 'lane-other-' + num, 'name': 'other inner layers closed in array+lane', 'parent': None, 'layers': other, 'tracks': True, 'vias': False, 'fills': False}
        for l in other:
            b.keepouts[l].append((tmp, U.GUARD.union(lanes[num])))
        b.cache = {}
        r = U.route_ball(b, ledger, num, v, [(APP + f' U6.{num} layer {LANE_LAYER[num]}, margin 6 mm', 6.0, lay + ((8,) if LANE_LAYER[num] != 8 else (6,)))])
        for l in other:
            b.keepouts[l] = [(k, s_) for k, s_ in b.keepouts[l] if k['uuid'] != tmp['uuid']]
        b.cache = {}
        print('U6', num, v['net'], 'ok' if r else 'FAILED', flush=True)
    lane_guard(None)
    # 2) outer ring (B-surface) escapes, lanes still reserved
    array_guard(b, True)
    for num in OUTER_FIRST:
        p = u6[num]
        comps, ids = b.components(p['net'])
        src = comps[ids[p['uuid']]]
        live = [c for c in comps.values() if c is not src and any(e['kind'] in ('pads', 'zones') for e, _ in c)]
        if not live or p['net'].startswith('unconnected'):
            continue
        dst = max(live, key=lambda c: (any(e['kind'] == 'zones' for e, _ in c), len(c)))
        r = route_entries(b, p['net'], src, dst, p['net'] + ' U6', [(APP + f' (outer U6.{num}), margin 5 mm', 5.0, SIGNAL_LAYERS), (APP + f' (outer U6.{num}), margin 10 mm', 10.0, SIGNAL_LAYERS)], ledger['routes'], small_vias=False)
        print('OUTER', num, p['net'], 'ok' if r else 'FAILED', flush=True)
    array_guard(b, False)
    # 3) easy POFV balls
    for num in EASY:
        v = placed.get(num)
        if not v:
            continue
        r = U.route_ball(b, ledger, num, v, [(APP + f' U6.{num}, margin 5 mm', 5.0, SIGNAL_LAYERS), (APP + f' U6.{num}, margin 10 mm', 10.0, SIGNAL_LAYERS)])
        print('U6', num, v['net'], 'ok' if r else 'FAILED', flush=True)
    array_guard(b, True)
    u6nets = {p['net'] for p in u6.values()} & set(nets)
    prune_orphans(b, ledger, set(nets))
    reconnect(b, ledger, u6nets, REGION, [(APP + ' (remaining U6 nets), margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ' (remaining U6 nets), margin 9 mm', 9.0, SIGNAL_LAYERS)])
    reconnect(b, ledger, set(nets) - u6nets, REGION, [(APP + ' (wall/other nets), margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ' (wall/other nets), margin 9 mm', 9.0, SIGNAL_LAYERS)])
    array_guard(b, False)
    lanes.clear()
    lane_guard(None)
    for num, v in placed.items():
        if v['uuid'] not in b.items:
            continue
        comps, ids = b.components(v['net'])
        comp = comps[ids[v['uuid']]]
        if not (any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp) or any(e['kind'] == 'zones' for e, _ in comp)):
            uu = [v['uuid']] + [e['uuid'] for e, _ in comp if str(e['uuid']).startswith('new-') and e['kind'] in ('tracks', 'vias') and e['uuid'] != v['uuid']]
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback U6.' + num)
            print('ROLLBACK', num, flush=True)
    U.drop_guard(b)


if __name__ == '__main__':
    run('R2-23', 'U6_3mm_rip_dedicated_layers', plan, j8={'original_outline': U.J8_RECT}, adopt_if_fewer_opens=True, next_step='R2-24 (B) U11/U20 mirrored-pad fix; (C) C59')
