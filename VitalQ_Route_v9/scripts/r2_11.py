"""R2-11: U18 dx -0.31 nudge (drag-with-tracks) + authorised local rip-up + U22 via-in-pad (SUPERVISOR_R2_02 #3, #01 Q2)."""
import math
from shapely.geometry import LineString, box
from r2_batch import run
from r2_rip import conflicts, rip, prune_orphans, reconnect
from r2_common import route_entries, open_comps
from r2route import route, SIGNAL_LAYERS, SMALL

MOVES = [{'ref': 'U18', 'from': [27.209594, 44.367033], 'to': [26.899594, 44.367033], 'reason': 'PROMPT_R2 R2-2 / SUPERVISOR_R2_01 Q2: centre U18 pin2/pin3 gap on U22 column x=26.9 for C1/D1 via-in-pad'}]
WINDOW = [23.5, 39.5, 29.5, 49.0]
BALLS = ['B1', 'B2', 'C1', 'D1', 'A2']
FIELD = box(25.3 - 0.35, 46.95 - 0.35, 26.9 + 0.35, 48.15 + 0.35)
APP = 'R2-11 U18 drag dx-0.31 + authorised local rip (x23.5-29.5,y39.5-49) + U22 0.25/0.15 POFV at ball centre, fan-out In3>In2>F, then A*'
APPROACHES = [(APP + ', margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ', margin 8 mm', 8.0, SIGNAL_LAYERS)]


def plan(b, ledger, opens):
    win = box(*WINDOW)
    # 1. local clearance hits from the drag
    seeds = {}
    for m in ledger['moves']:
        for t in m.get('shifted_track_endpoints', []):
            if t.get('kind') == 'via':
                it = b.items[t['uuid']]
                if b.via_issues_exact(t['net'], t['xy'], it['diameter'], it['drill'], ignore=(t['uuid'],)):
                    seeds[t['uuid']] = 'dragged U18 via clearance hit'
                continue
            it = b.items[t['uuid']]
            bounds = [min(t['a'][0], t['b'][0]) - 1, min(t['a'][1], t['b'][1]) - 1, max(t['a'][0], t['b'][0]) + 1, max(t['a'][1], t['b'][1]) + 1]
            if not b.allowed_local(t['net'], t['layer'], bounds, ignore=(t['uuid'],), width=it['width']).covers(LineString([t['a'], t['b']])):
                seeds[t['uuid']] = 'dragged U18 segment violates clearance'
    for u, info in conflicts(b, ('U18',)).items():
        seeds[u] = 'foreign copper hit by moved U18 pad (' + info['against'] + ')'
    # 2. U22 POFV sites: rip foreign F/B track blockers inside the authorised window
    u22 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U22'}
    for num in BALLS:
        p = u22[num]
        for h in b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore=tuple(seeds)):
            if h['kind'] == 'tracks' and h.get('layer') in (0, 2) and b.items[h['uuid']]['shapes'][h['layer']].intersects(win):
                seeds[h['uuid']] = f"blocks U22.{num} POFV ({h['net']})"
    ledger['hits'] = seeds
    for u, why in seeds.items():
        if u in b.items:
            rip(b, ledger, [u], 'SUPERVISOR_R2_01 Q2 authorised local rip-up: ' + why)
    print('ripped', len(seeds), sorted({r['net'] for r in ledger['removed']}), flush=True)
    placed = {}
    for num in BALLS:
        p = u22[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL)
        if hits:
            print('SITE BLOCKED', num, [(h['kind'], h.get('net')) for h in hits], flush=True)
            ledger['routes'].append({'connection': 'U22.' + num, 'net': p['net'], 'status': 'failed', 'reason': 'POFV site not legal: ' + str(hits)[:300]})
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U22.' + num}
        b.add_items([], [v])
        placed[num] = v
    # 3. fan-out out of the U22 field
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
    # 4. U22 opens (skip the U6 side: handled in the U6 batches)
    todo = []
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if 'U22' in t and 'U6' not in t:
            todo.append(it)
    for it in todo:
        info = open_comps(b, it)
        if info is None or None in info[2]:
            continue
        net, comps, (ka, kb) = info
        if ka == kb:
            continue
        a, c = comps[ka], comps[kb]
        src, dst = (a, c) if any(e['kind'] == 'pads' and e.get('ref') == 'U22' for e, _ in a) else (c, a)
        route_entries(b, net, src, dst, net + ' U22', APPROACHES, ledger['routes'], small_vias=False)
    # 5. reconnect U18/ripped nets, then U18.7-U18.8
    nets = {r['net'] for r in ledger['removed']} | {p['net'] for p in b.raw['pads'] if p['ref'] == 'U18'}
    nets.discard('')
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, WINDOW, [(APP + ' (ripped/dragged net reconnect), margin 3 mm', 3.0, SIGNAL_LAYERS)])
    # 6. roll back fan-outs that did not complete
    for num, uu in fan.items():
        v = placed[num]
        comps, ids = b.components(v['net'])
        comp = comps[ids[v['uuid']]]
        linked = any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp) or any(e['kind'] == 'zones' for e, _ in comp)
        if not linked:
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback U22.' + num + ' fan-out (no completed connection)')
            print('ROLLBACK', num, flush=True)


run('R2-11', 'U22_via_in_pad_U18_nudge', plan, moves=MOVES, moved=('U18',), next_step='R2-12 U6 batch A (row A, E5, F row, E4, C4, E2, B2) per SUPERVISOR_R2_04')
