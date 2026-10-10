"""R2-21 (SUPERVISOR regional re-route A): rip every track/via in x 22.5-30.5, y 48.0-56.5 on all layers except U6 ball POFVs and
GND/+3V3/VBAT_SYS plane-stitch vias; place all U6 POFVs (J8 notches); route U6 inner balls through #04 corridors (C-channel: B3 In3,
D4 In2, D2 In4), then all other U6 nets, then the ripped wall nets last (array kept free on inner layers). Attempt counters reset (regional rip)."""
import math
from shapely.geometry import box, LineString, Point
from r2 import STATE, save_json, state
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2route import SIGNAL_LAYERS, SMALL
import r2_u6 as U

WIN = box(22.5, 48.0, 30.5, 56.5)
PLANE_NETS = {'GND', '+3V3', 'VBAT_SYS'}
INNER = ['C4', 'D4', 'E3', 'E4', 'B3', 'D2', 'E2', 'B2']
EASY = ['A1', 'A2', 'A3', 'E5', 'F2']
U.CH['B3'] = (['C3', 'C2', 'C1'], 'E', (8, 2, 0))
U.CH['D4'] = (['D3', 'C3', 'C2', 'C1'], 'E', (6, 2, 0))
APP = 'R2-21 regional rip x22.5-30.5 y48-56.5 + ordered re-route (U6 corridors first, wall nets last), 60 s budget'
REGION = [22.5, 48.0, 30.5, 56.5]


def array_guard(b, on):
    tmp = {'uuid': 'array-guard', 'name': 'R2-21 array guard (inner layers)', 'parent': None, 'layers': [6, 8, 10], 'tracks': True, 'vias': True, 'fills': False}
    for l in (6, 8, 10):
        b.keepouts[l] = [(k, s) for k, s in b.keepouts[l] if k['uuid'] != 'array-guard']
        if on:
            b.keepouts[l].append((tmp, U.FIELD.buffer(0.15)))
    b.cache = {}


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    ball_xy = {(round(p['xy'][0], 4), round(p['xy'][1], 4)): p['net'] for p in u6.values()}
    victims = []
    for t in b.raw['tracks']:
        if LineString([t['a'], t['b']]).intersects(WIN):
            victims.append(t['uuid'])
    for v in b.raw['vias']:
        if not WIN.contains(Point(v['xy'])):
            continue
        if ball_xy.get((round(v['xy'][0], 4), round(v['xy'][1], 4))) == v['net'] or v['net'] in PLANE_NETS:
            continue
        victims.append(v['uuid'])
    nets = sorted({b.items[u]['net'] for u in victims})
    s = state()
    reset = {k: v for k, v in s['attempts'].items() if k.split(':')[0] in nets}
    s.setdefault('attempts_reset_R2_21_regional', {}).update(reset)
    for k in reset:
        s['attempts'][k] = []
    save_json(STATE, s)
    rip(b, ledger, victims, 'SUPERVISOR regional rip (A) x22.5-30.5 y48-56.5')
    print('regional rip', len(victims), 'items on', len(nets), 'nets', flush=True)
    U.add_guard(b)
    placed = {}
    for num in INNER + EASY:
        v = U.ball_via(b, u6, num)
        if v:
            placed[num] = v
            continue
        p = u6[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8)
        if hits:
            print('SITE BLOCKED', num, [(h['kind'], h.get('net', h.get('name'))) for h in hits], flush=True)
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num}
        b.add_items([], [v])
        placed[num] = v
    print('POFV', sorted(placed), flush=True)
    for num in INNER + EASY:
        v = placed.get(num)
        if not v:
            continue
        lay = U.CH[num][2]
        r = U.route_ball(b, ledger, num, v, [(APP + f' U6.{num} {lay}, margin 5 mm', 5.0, lay), (APP + f' U6.{num} all layers, margin 10 mm', 10.0, tuple(sorted(set(lay) | set(SIGNAL_LAYERS))))])
        print('U6', num, v['net'], 'ok' if r else 'FAILED', flush=True)
    array_guard(b, True)
    u6nets = {p['net'] for p in u6.values()} & set(nets)
    prune_orphans(b, ledger, set(nets))
    reconnect(b, ledger, u6nets, REGION, [(APP + ' (U6 nets), margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ' (U6 nets), margin 9 mm', 9.0, SIGNAL_LAYERS)])
    reconnect(b, ledger, set(nets) - u6nets, REGION, [(APP + ' (wall/other nets last), margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ' (wall/other nets last), margin 9 mm', 9.0, SIGNAL_LAYERS)])
    array_guard(b, False)
    for num, v in placed.items():
        if v['uuid'] not in b.items:
            continue
        comps, ids = b.components(v['net'])
        comp = comps[ids[v['uuid']]]
        if not (any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp) or any(e['kind'] == 'zones' for e, _ in comp)):
            uu = [v['uuid']] + [e['uuid'] for e, _ in comp if str(e['uuid']).startswith('new-') and e['kind'] in ('tracks', 'vias') and e['uuid'] != v['uuid']]
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback U6.' + num + ' (no completed connection)')
            print('ROLLBACK', num, flush=True)
    U.drop_guard(b)


run('R2-21', 'U6_regional_reroute', plan, j8={'original_outline': U.J8_RECT}, adopt_if_fewer_opens=True, next_step='R2-22 (B) U11 move for mirrored sensor island; then (C) C59 rotations')
