"""R2-12: U22 via-in-pad split, part 1 (no U18 move): B1 LED3_K, B2 MAX86178_INT; ripped blockers re-routed first."""
import math
from shapely.geometry import box
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2_common import route_entries, open_comps
from r2route import route, SIGNAL_LAYERS, SMALL

WINDOW = [23.5, 39.5, 29.5, 49.0]
BALLS = ['B1', 'B2']
FIELD = box(25.3 - 0.35, 46.95 - 0.35, 26.9 + 0.35, 48.15 + 0.35)
APP = 'R2-12 U22 B1/B2 0.25/0.15 POFV without moving U18 (C1/D1 deferred to U18-nudge batch); blockers ripped and re-routed first, then fan-out In3>In2>F and A*'


def plan(b, ledger, opens):
    win = box(*WINDOW)
    u22 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U22'}
    seeds = {}
    for num in BALLS:
        p = u22[num]
        for h in b.via_issues_exact(p['net'], p['xy'], *SMALL):
            if h['kind'] == 'tracks' and h.get('layer') in (0, 2) and b.items[h['uuid']]['shapes'][h['layer']].intersects(win):
                seeds[h['uuid']] = f"blocks U22.{num} POFV ({h['net']})"
            elif h['kind'] != 'tracks':
                ledger['issues'].append({'site': num, 'hit': h})
    if ledger['issues']:
        print('non-track blockers', ledger['issues'], flush=True)
        return
    for u, why in seeds.items():
        rip(b, ledger, [u], 'SUPERVISOR_R2_01 Q2 authorised local rip-up: ' + why)
    print('ripped', sorted({r['net'] for r in ledger['removed']}), flush=True)
    placed = {}
    for num in BALLS:
        p = u22[num]
        assert not b.via_issues_exact(p['net'], p['xy'], *SMALL)
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U22.' + num}
        b.add_items([], [v])
        placed[num] = v
    nets = {r['net'] for r in ledger['removed']}
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, WINDOW, [(APP + ' (ripped net first), margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ' (ripped net first), margin 6 mm', 6.0, SIGNAL_LAYERS)])
    exitmask = box(22.5, 44.0, 30.5, 51.0).difference(FIELD)
    fan = {}
    for num, v in placed.items():
        comps, ids = b.components(v['net'])
        src = comps[ids[v['uuid']]]
        res, err = route(b, v['net'], src, None, [v['xy'][0] - 1.6, v['xy'][1] - 1.6, v['xy'][0] + 1.6, v['xy'][1] + 1.6], layers=(8, 6, 0), escape_track_mask=exitmask, plane=False)
        fan[num] = [v['uuid']]
        if res:
            b.add_items(res['tracks'], res['vias'])
            fan[num] += [t['uuid'] for t in res['tracks']] + [x['uuid'] for x in res['vias']]
            print('FANOUT', num, v['net'], flush=True)
        else:
            print('FANOUT FAILED', num, v['net'], err, flush=True)
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if not any(f'Pad {n} ' in t and 'U22' in t for n in BALLS):
            continue
        info = open_comps(b, it)
        if info is None or None in info[2]:
            continue
        net, comps, (ka, kb) = info
        if ka == kb:
            continue
        a, c = comps[ka], comps[kb]
        src, dst = (a, c) if any(e['kind'] == 'pads' and e.get('ref') == 'U22' for e, _ in a) else (c, a)
        route_entries(b, net, src, dst, net + ' U22', [(APP + ', margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ', margin 8 mm', 8.0, SIGNAL_LAYERS)], ledger['routes'], small_vias=False)
    for num, uu in fan.items():
        v = placed[num]
        comps, ids = b.components(v['net'])
        comp = comps[ids[v['uuid']]]
        if not (any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp) or any(e['kind'] == 'zones' for e, _ in comp)):
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback U22.' + num + ' fan-out (no completed connection)')
            print('ROLLBACK', num, flush=True)


run('R2-12', 'U22_B1_B2', plan, next_step='R2-13 U18 nudge + U22 C1/D1 (LED2_K/LED1_K)')
