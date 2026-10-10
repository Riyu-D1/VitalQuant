"""R2-22: regional re-route retry (SUPERVISOR A) with corrected order learnt from R2-21:
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
WIN = box(22.5, 48.0, 30.5, 56.5)
PLANE_NETS = {'GND', '+3V3', 'VBAT_SYS'}


def array_guard(b, on):
    tmp = {'uuid': 'array-guard', 'name': 'array guard (inner layers)', 'parent': None, 'layers': [6, 8, 10], 'tracks': True, 'vias': True, 'fills': False}
    for l in (6, 8, 10):
        b.keepouts[l] = [(k, s) for k, s in b.keepouts[l] if k['uuid'] != 'array-guard']
        if on:
            b.keepouts[l].append((tmp, U.FIELD.buffer(0.15)))
    b.cache = {}

OUTER_FIRST = ['E1', 'F1', 'F4', 'F5', 'C5', 'B1', 'A4', 'C1', 'F3', 'B5', 'A5', 'D1']
INNER = ['B3', 'D4', 'D2', 'C4', 'E3', 'E4', 'E2', 'B2']
EASY = ['A1', 'A2', 'A3', 'E5', 'F2']
STRICT = {'B3': (8, 2, 0), 'D4': (6, 2, 0), 'D2': (10, 0)}
APP = 'R2-22 regional rip retry: outer-ring B escapes first, strict C-channel layers, then corridors, wall nets last, 60 s budget'
REGION = [22.5, 48.0, 30.5, 56.5]


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    ball_xy = {(round(p['xy'][0], 4), round(p['xy'][1], 4)): p['net'] for p in u6.values()}
    victims = [t['uuid'] for t in b.raw['tracks'] if LineString([t['a'], t['b']]).intersects(WIN)]
    victims += [v['uuid'] for v in b.raw['vias'] if WIN.contains(Point(v['xy'])) and ball_xy.get((round(v['xy'][0], 4), round(v['xy'][1], 4))) != v['net'] and v['net'] not in PLANE_NETS]
    nets = sorted({b.items[u]['net'] for u in victims})
    s = state()
    reset = {k: v for k, v in s['attempts'].items() if k.split(':')[0] in nets}
    s.setdefault('attempts_reset_R2_22_regional', {}).update(reset)
    for k in reset:
        s['attempts'][k] = []
    save_json(STATE, s)
    rip(b, ledger, victims, 'SUPERVISOR regional rip (A, retry) x22.5-30.5 y48-56.5')
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
    array_guard(b, True)
    # 1) outer-ring B-surface escapes first
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
    # 2/3) corridors
    for num in INNER + EASY:
        v = placed.get(num)
        if not v:
            continue
        lay = STRICT.get(num, U.CH[num][2])
        apps = [(APP + f' U6.{num} {lay}, margin 5 mm', 5.0, lay), (APP + f' U6.{num} {lay}, margin 10 mm', 10.0, lay)]
        r = U.route_ball(b, ledger, num, v, apps)
        print('U6', num, v['net'], 'ok' if r else 'FAILED', flush=True)
    array_guard(b, True)
    u6nets = {p['net'] for p in u6.values()} & set(nets)
    prune_orphans(b, ledger, set(nets))
    reconnect(b, ledger, u6nets, REGION, [(APP + ' (remaining U6 nets), margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ' (remaining U6 nets), margin 9 mm', 9.0, SIGNAL_LAYERS)])
    reconnect(b, ledger, set(nets) - u6nets, REGION, [(APP + ' (wall/other nets), margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ' (wall/other nets), margin 9 mm', 9.0, SIGNAL_LAYERS)])
    array_guard(b, False)
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
    run('R2-22', 'U6_regional_reroute_retry', plan, j8={'original_outline': U.J8_RECT}, adopt_if_fewer_opens=True, next_step='R2-23 (B) U11 move; (C) C59 rotations')
