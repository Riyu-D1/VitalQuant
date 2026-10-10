"""R2-16: U6 J8-covered balls (SUPERVISOR_R2_04 + #05): B2 B3 C4 D2 D4 E2 E3 E4 as 0.25/0.15 POFV at ball centre with per-via
J8 notches; B4/D1 GND vias normalised to 0.25/0.15 (approved). No F tracks inside the J8 area. Channel layer assignment per #04."""
import math
from shapely.geometry import box
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2_common import route_entries, open_comps
from r2route import SIGNAL_LAYERS, SMALL

J8_RECT = [{'outer': [[27.72, 50.065], [25.18, 50.065], [25.18, 51.335], [27.72, 51.335]], 'holes': []}]
BALLS = ['B3', 'D4', 'D2', 'E3', 'E4', 'C4', 'E2', 'B2']
LAYERS = {'B3': (8, 2, 0), 'D4': (6, 2, 0), 'D2': (10, 0), 'E3': (8, 6, 2, 0)}
AUTH = {'ESP_TX', 'ESP_RX', 'AFE_INP', 'MISO_FL', 'CS_FLASH', '+3V3_ANA', 'TX_5V', 'TX4', '+1V8', 'PD_K'}
RIP_WIN = box(23.5, 45.5, 28.5, 53.0)
WINDOW = [21.0, 45.0, 31.0, 56.0]
APP = 'R2-16 U6 J8-covered POFV with per-via notches (#05), B4/D1 normalised, #04 channel layers (B3 In3, D4 In2, D2 In4, E3 via F3 cell), no F track in J8 area, A*'


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    # F-layer track guard over the J8 area (no new F copper there)
    from route_geometry import polyset
    guard = {'uuid': 'J8-track-guard', 'name': 'J8 guard (R2 policy)', 'parent': 'J8', 'layers': [0], 'tracks': True, 'vias': False, 'fills': False}
    b.keepouts[0].append((guard, polyset(J8_RECT)))
    b.cache = {}
    ledger['modify_vias'], ledger['modify_tracks'] = [], []
    for num in ('B4', 'D1'):
        v = next(x for x in b.raw['vias'] if x['net'] == 'GND' and math.dist(x['xy'], u6[num]['xy']) < 0.01)
        if v['diameter'] > 0.26:
            nv = dict(v, diameter=SMALL[0], drill=SMALL[1], filled=True, capped=True)
            b.remove_items([v['uuid']])
            b.add_items([], [nv])
            ledger['modify_vias'].append({'uuid': v['uuid'], 'net': 'GND', 'from': {'xy': v['xy'], 'diameter': v['diameter'], 'drill': v['drill']}, 'xy': v['xy'], 'diameter': SMALL[0], 'drill': SMALL[1], 'pad': 'U6.' + num, 'reason': 'approved: normalise J8 POFV exception to 0.25/0.15 so neighbouring ball POFVs meet 0.2 hole-to-copper'})
    seeds = {}
    for num in BALLS:
        p = u6[num]
        for h in b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8):
            if h['kind'] == 'tracks' and h['net'] in AUTH and b.items[h['uuid']]['shapes'][h['layer']].intersects(RIP_WIN):
                seeds[h['uuid']] = f"blocks U6.{num} POFV ({h['net']}, authorised PROMPT_R2 R2-4 / SUPERVISOR_R2_04)"
            else:
                print('UNAUTHORISED/NON-TRACK BLOCKER', num, h, flush=True)
                ledger['routes'].append({'connection': 'U6.' + num, 'net': p['net'], 'status': 'failed', 'reason': 'blocker ' + str(h)[:200]})
    for u, why in seeds.items():
        rip(b, ledger, [u], why)
    print('ripped', sorted({r['net'] for r in ledger['removed']}), flush=True)
    placed = {}
    for num in BALLS:
        p = u6[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8)
        if hits:
            print('SITE BLOCKED', num, [(h['kind'], h.get('net', h.get('name'))) for h in hits], flush=True)
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num}
        b.add_items([], [v])
        placed[num] = v
    print('placed', list(placed), flush=True)
    by_ball = {}
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        for num in placed:
            if f'Pad {num} ' in t and 'of U6' in t:
                by_ball[num] = it
    for num in BALLS:
        it = by_ball.get(num)
        if it is None:
            continue
        info = open_comps(b, it)
        if info is None or None in info[2]:
            continue
        net, comps, (ka, kb) = info
        if ka == kb:
            continue
        a, c = comps[ka], comps[kb]
        src, dst = (a, c) if any(e['kind'] == 'vias' and e['uuid'] == placed[num]['uuid'] for e, _ in a) else (c, a)
        lay = LAYERS.get(num, SIGNAL_LAYERS)
        route_entries(b, net, src, dst, net + ' U6', [(APP + f', layers {lay}, margin 3 mm', 3.0, lay), (APP + f', layers {lay}+all, margin 7 mm', 7.0, tuple(sorted(set(lay) | set(SIGNAL_LAYERS))))], ledger['routes'], small_vias=False)
    nets = {r['net'] for r in ledger['removed']}
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, WINDOW, [(APP + ' (ripped nets after U6), margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ' (ripped nets after U6), margin 7 mm', 7.0, SIGNAL_LAYERS)])
    # SPI_SCK F3 last: B-surface escape or POFV if the F3 cell is still free
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if 'Pad F3 [SPI_SCK] of U6' in t:
            info = open_comps(b, it)
            if info and None not in info[2] and info[2][0] != info[2][1]:
                net, comps, (ka, kb) = info
                a, c = comps[ka], comps[kb]
                src, dst = (a, c) if any(e['kind'] == 'pads' and e.get('ref') == 'U6' for e, _ in a) else (c, a)
                route_entries(b, net, src, dst, net + ' U6', [(APP + ' SPI_SCK: B escape or F3 POFV, margin 5 mm', 5.0, SIGNAL_LAYERS)], ledger['routes'], small_vias=True)
    for num, v in placed.items():
        comps, ids = b.components(v['net'])
        comp = comps[ids[v['uuid']]]
        if not (any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp) or any(e['kind'] == 'zones' for e, _ in comp)):
            uu = [v['uuid']] + [e['uuid'] for e, _ in comp if str(e['uuid']).startswith('new-') and e['kind'] in ('tracks', 'vias')]
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback U6.' + num + ' (no completed connection)')
            print('ROLLBACK', num, flush=True)
    b.keepouts[0] = [(k, s) for k, s in b.keepouts[0] if k['uuid'] != 'J8-track-guard']


run('R2-16', 'U6_J8_POFV_notches', plan, j8={'original_outline': J8_RECT}, next_step='R2-17 U22 C1/D1 with LED3_K re-route + U18 move, SPI_SCK leftovers')
