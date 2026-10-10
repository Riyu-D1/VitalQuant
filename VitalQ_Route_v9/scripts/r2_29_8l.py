"""R2-29 = R2-28 with U6 moved +1.5 mm (not +2.5: at +2.5 the U6 F-row balls overlap the J8 Tag-Connect NPTH at (28.99,52.18));
C15/C79 stay (U6 is on B, no courtyard clash at +1.5); U18-clash copper ripped.
R2-28 (SUPERVISOR 8-layer order): convert to 8 copper (F/In1 GND/In2/In3/In4 power/In5/In6 GND/B), keep the R2-27 placement,
blanket rip of the U6/U22/U18 region and re-route with In5 for U6 analog escapes, In3 for AFE digital/SPI, In2 for U22/U18/LED/TX.
R2-27 (SUPERVISOR placement order): U6 (+decaps C15, C79) moved +2.5 mm east away from U22, U18 nudged dx -0.31 (clears U22 C1/D1),
regional rip within 4 mm of old/new U6 and U22 (y >= 43.5, keeps the HV/U19 zone untouched), then the R2-26 lane re-route plus U22 POFVs.
R2-26 = R2-25 with the rip window kept clear of U22 (y >= 48.7; R2-25 lost 8 U22 escapes) and exact via check in the router.
R2-25 = R2-23 + reserved B-layer escape lanes for the outer-ring balls (R2-23 lost E1/F1/C5/B1 to inner-net vias/B tracks).
R2-23 (SUPERVISOR override of 3-attempt rule for the U6 wall): rip EVERYTHING within 3 mm of U6 (keep U6/U22 ball POFVs and plane stitch vias),
inner-ball fan-out on dedicated inner layers with reserved exit lanes, then outer ring, then the ripped nets. Based on R2-22 (SUPERVISOR A) with corrected order learnt from R2-21:
1) U6 outer-ring balls (B-surface escapes, most constrained) first, 2) inner corridors with STRICT layer per C-channel net
(B3 In3, D4 In2, D2 In4; no all-layer fallback), 3) easy POFV balls, 4) other U6 nets, 5) wall/other nets. Counters reset (regional rip)."""
import math
from shapely.geometry import box, LineString, Point
from r2 import STATE, save_json, state
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2_common import route_entries
import r2route
from r2route import SIGNAL_LAYERS_8 as SIGNAL_LAYERS, SMALL
import r2_u6 as U
from shapely.ops import unary_union as _uu
WIN = _uu([box(21.6, 46.8, 30.6, 55.8), box(23.1, 46.8, 32.1, 55.8), box(21.3, 43.5, 30.9, 52.2)])
PLANE_NETS = {'GND', '+3V3', 'VBAT_SYS'}


def array_guard(b, on):
    tmp = {'uuid': 'array-guard', 'name': 'array guard (inner layers)', 'parent': None, 'layers': [6, 8, 10, 12], 'tracks': True, 'vias': True, 'fills': False}
    for l in (6, 8, 10, 12):
        b.keepouts[l] = [(k, s) for k, s in b.keepouts[l] if k['uuid'] != 'array-guard']
        if on:
            b.keepouts[l].append((tmp, U.FIELD.buffer(0.15)))
    b.cache = {}

OUTER_FIRST = ['C1', 'F3', 'E1', 'F1', 'F4', 'F5', 'C5', 'B1', 'A4', 'B5', 'A5', 'D1']
INNER = ['B3', 'D4', 'D2', 'C4', 'E3', 'E4', 'E2', 'B2']
GNDB = ['B4', 'D1', 'D5']
U22B = ['C1', 'D1', 'A2', 'A4', 'B1', 'B2']
EASY = ['A1', 'A2', 'A3', 'E5', 'F2']
# supervisor grouping (In1 is the GND plane, so 'power' nets go to In4 = Power Layer 5); C-channel needs 3 distinct layers -> AFE_BG on In2
# 8L: In5 analog (PD2_INM/PD2_INP), In3 AFE digital (CS/MISO + AFE_BG: the C-channel needs 3 distinct layers), In2 TX/power
STRICT = {'B3': (12, 2, 0), 'B2': (12, 2, 0), 'D2': (8, 2, 0), 'E3': (8, 2, 0), 'E2': (8, 2, 0), 'D4': (6, 2, 0), 'E4': (6, 2, 0), 'C4': (6, 2, 0)}
LANE_LAYER = {'B3': 12, 'B2': 12, 'D2': 8, 'E3': 8, 'E2': 8, 'D4': 6, 'E4': 6, 'C4': 6}
PREF = {}
for _n in ('PD2_INP', 'PD2_INM', 'AFE_BG', 'AFE_INP', 'AFE_INM', 'PD_INP', 'PD_INM'):
    PREF[_n] = 12
for _n in ('CS_AFE4900', 'MISO_AFE', 'SPI_SCK', 'SPI_MOSI', 'AFE_CLK', 'AFE4900_RESETZ', 'AFE4900_ADC_RDY'):
    PREF[_n] = 8
for _n in ('CS_FLASH', 'MISO_FL', 'MX_SDO_1V8', 'LED1_K', 'LED2_K', 'LED3_K', 'TX1', 'TX2', 'TX3', 'TX4', 'TX_5V'):
    PREF[_n] = 6
BASE_COST = dict(r2route.LAYER_COST)
_orig_route = r2route.route


def _route_pref(b, net, *a, **k):
    r2route.LAYER_COST.clear()
    r2route.LAYER_COST.update(BASE_COST)
    if net in PREF:
        for l in (6, 8, 12):
            r2route.LAYER_COST[l] = 1.25
        r2route.LAYER_COST[PREF[net]] = 0.85
    return _orig_route(b, net, *a, **k)


import r2_common
r2_common.route = _route_pref
APP = 'R2-29 8-layer, U6 +1.5 east, region rip; dedicated inner layer per inner ball with reserved exit lanes; corridors first, outer ring, then ripped nets; 60 s budget'
REGION = [21.3, 43.5, 33.1, 55.8]


def plan(b, ledger, opens):
    U.shift(1.5, 0.0)
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    ball_xy = {(round(p['xy'][0], 4), round(p['xy'][1], 4)): p['net'] for p in b.raw['pads'] if p['ref'] in ('U6', 'U22')}
    nets_extra = {r['net'] for m in ledger['moves'] for r in m.get('detached', [])}
    from r2_rip import conflicts
    victims = [t['uuid'] for t in b.raw['tracks'] if LineString([t['a'], t['b']]).intersects(WIN)] + list(conflicts(b, ('U18', 'U6')))
    victims += [v['uuid'] for v in b.raw['vias'] if WIN.contains(Point(v['xy'])) and ball_xy.get((round(v['xy'][0], 4), round(v['xy'][1], 4))) != v['net'] and v['net'] not in PLANE_NETS]
    nets = sorted({b.items[u]['net'] for u in victims} | nets_extra)
    s = state()
    reset = {k: v for k, v in s['attempts'].items() if k.split(':')[0] in set(nets) | {p['net'] for p in b.raw['pads'] if p['ref'] in ('U6', 'U22')}}
    s.setdefault('attempts_reset_R2_29_8L', {}).update(reset)
    for k in reset:
        s['attempts'][k] = []
    save_json(STATE, s)
    rip(b, ledger, victims, 'SUPERVISOR override: rip everything within 3 mm of U6 (x21.9-29.5, y46.8-54.8)')
    print('regional rip', len(victims), 'items on', len(nets), 'nets', flush=True)
    U.add_guard(b)
    placed = {}
    u22 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U22'}
    for num in GNDB:
        p = u6[num]
        if not U.ball_via(b, u6, num) and not b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8):
            b.add_items([], [{'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num}])
    u22v = {}
    for num in U22B:
        p = u22[num]
        v = next((x for x in b.raw['vias'] if math.dist(x['xy'], p['xy']) < 1e-4 and x['net'] == p['net']), None)
        if v is None and not b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8):
            v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U22.' + num}
            b.add_items([], [v])
        if v:
            u22v[num] = v
    print('U22 POFV', sorted(u22v), flush=True)
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
    OUT_DIR = {'E1': 'E', 'F1': 'E', 'C1': 'E', 'B1': 'E', 'C5': 'W', 'B5': 'W', 'F3': 'S', 'F4': 'S', 'F5': 'S', 'A4': 'N', 'A5': 'N'}
    blanes = {}
    for num, d in OUT_DIR.items():
        dx, dy = U.DIRS[d]
        c = U.cell(num)
        blanes[num] = LineString([c, (c[0] + dx * 2.2, c[1] + dy * 2.2)]).buffer(0.16, cap_style=2)

    def lane_guard(except_ball):
        for l in (0, 2, 6, 8, 10, 12):
            b.keepouts[l] = [(k, s_) for k, s_ in b.keepouts[l] if not str(k['uuid']).startswith('lane-')]
        for num, lane in blanes.items():
            if num == except_ball:
                continue
            kb = {'uuid': 'lane-b-' + num, 'name': 'reserved B escape lane U6.' + num, 'parent': None, 'layers': [2], 'tracks': True, 'vias': True, 'fills': False}
            b.keepouts[2].append((kb, lane))
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
        other = [l for l in (6, 8, 10, 12) if l != LANE_LAYER[num]]
        tmp = {'uuid': 'lane-other-' + num, 'name': 'other inner layers closed in array+lane', 'parent': None, 'layers': other, 'tracks': True, 'vias': False, 'fills': False}
        for l in other:
            b.keepouts[l].append((tmp, U.GUARD.union(lanes[num])))
        b.cache = {}
        r = U.route_ball(b, ledger, num, v, [(APP + f' U6.{num} layer {LANE_LAYER[num]}, margin 6 mm', 6.0, lay + tuple(l for l in (6, 8, 12) if l != LANE_LAYER[num])), (APP + f' U6.{num} layer {LANE_LAYER[num]}, margin 11 mm', 11.0, lay + tuple(l for l in (6, 8, 12) if l != LANE_LAYER[num]))])
        for l in other:
            b.keepouts[l] = [(k, s_) for k, s_ in b.keepouts[l] if k['uuid'] != tmp['uuid']]
        b.cache = {}
        print('U6', num, v['net'], 'ok' if r else 'FAILED', flush=True)
    lane_guard(None)
    # 2) outer ring (B-surface) escapes, lanes still reserved
    array_guard(b, True)
    for num in OUTER_FIRST:
        lane_guard(num)
        p = u6[num]
        comps, ids = b.components(p['net'])
        src = comps[ids[p['uuid']]]
        live = [c for c in comps.values() if c is not src and any(e['kind'] in ('pads', 'zones') for e, _ in c)]
        if not live or p['net'].startswith('unconnected'):
            continue
        dst = max(live, key=lambda c: (any(e['kind'] == 'zones' for e, _ in c), len(c)))
        r = route_entries(b, p['net'], src, dst, p['net'] + ' U6', [(APP + f' (outer U6.{num}), margin 5 mm', 5.0, SIGNAL_LAYERS), (APP + f' (outer U6.{num}), margin 10 mm', 10.0, SIGNAL_LAYERS)], ledger['routes'], small_vias=False)
        print('OUTER', num, p['net'], 'ok' if r else 'FAILED', flush=True)
    lane_guard(None)
    array_guard(b, False)
    # 3) easy POFV balls
    for num in EASY:
        v = placed.get(num)
        if not v:
            continue
        r = U.route_ball(b, ledger, num, v, [(APP + f' U6.{num}, margin 5 mm', 5.0, SIGNAL_LAYERS), (APP + f' U6.{num}, margin 10 mm', 10.0, SIGNAL_LAYERS)])
        print('U6', num, v['net'], 'ok' if r else 'FAILED', flush=True)
    for num, v in u22v.items():
        comps, ids = b.components(v['net'])
        src = comps[ids[v['uuid']]]
        live = [c for c in comps.values() if c is not src and any(e['kind'] in ('pads', 'zones') for e, _ in c)]
        if not live:
            continue
        dst = max(live, key=lambda c: (any(e['kind'] == 'zones' for e, _ in c), len(c)))
        r = route_entries(b, v['net'], src, dst, v['net'] + ' U22', [(APP + f' U22.{num}, margin 5 mm', 5.0, SIGNAL_LAYERS), (APP + f' U22.{num}, margin 10 mm', 10.0, SIGNAL_LAYERS)], ledger['routes'], small_vias=False)
        print('U22', num, v['net'], 'ok' if r else 'FAILED', flush=True)
    array_guard(b, True)
    u6nets = ({p['net'] for p in u6.values()} | {p['net'] for p in u22.values()}) & set(nets)
    prune_orphans(b, ledger, set(nets))
    reconnect(b, ledger, u6nets, REGION, [(APP + ' (remaining U6 nets), margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ' (remaining U6 nets), margin 9 mm', 9.0, SIGNAL_LAYERS)])
    reconnect(b, ledger, set(nets) - u6nets, REGION, [(APP + ' (wall/other nets), margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ' (wall/other nets), margin 9 mm', 9.0, SIGNAL_LAYERS)])
    array_guard(b, False)
    lanes.clear()
    blanes.clear()
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


MOVES = [
    {'ref': 'U6', 'from': [25.7, 50.8], 'to': [27.2, 50.8], 'rotate_to': 180.0, 'allow_locked': True, 'detach': True, 'reason': 'SUPERVISOR placement order: move U6 east away from U22; +1.5 mm is the largest east shift that keeps the F-row balls >= 0.2 mm (hole-to-copper) from the J8 NPTH at (28.99,52.18); stays in the skin-side cluster, same side'},
    {'ref': 'U18', 'from': [27.209594, 44.367033], 'to': [26.899594, 44.367033], 'detach': True, 'reason': 'clear U22 C1/D1 for POFV (dx -0.31); a 1.8 mm north move collides with locked D28 and the WSON-8 does not fit between D28 and U22'},
]

if __name__ == '__main__':
    run('R2-29', 'stackup8_U6_plus1p5_regional_reroute', plan, moves=MOVES, moved=('U6', 'U18'), j8={'original_outline': U.J8_RECT}, adopt_if_fewer_opens=True, stackup8=True, next_step='R2-29 fix faults of the adopted 8L candidate; R2-30 U11 rotate; C59')
