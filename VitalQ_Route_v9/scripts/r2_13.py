"""R2-13: U18 dx -0.31 nudge (drag-with-tracks) + push/rip of hit foreign copper + U22 C1 LED2_K / D1 LED1_K via-in-pad."""
import math
from shapely.geometry import LineString, box
from r2_batch import run
from r2_rip import conflicts, rip, prune_orphans, reconnect, push
from r2_common import route_entries, open_comps
from r2route import route, SIGNAL_LAYERS, SMALL

MOVES = [{'ref': 'U18', 'from': [27.209594, 44.367033], 'to': [26.899594, 44.367033], 'reason': 'PROMPT_R2 R2-2 / SUPERVISOR_R2_01 Q2: centre U18 pin2/pin3 gap on U22 column x=26.9 for C1/D1 via-in-pad'}]
WINDOW = [23.5, 39.5, 29.5, 49.0]
BALLS = ['C1', 'D1']
FIELD = box(25.3 - 0.35, 46.95 - 0.35, 26.9 + 0.35, 48.15 + 0.35)
APP = 'R2-13 U18 drag dx-0.31; hit foreign F/B copper pushed by the same dx (else ripped, authorised window); U22 C1/D1 0.25/0.15 POFV; ripped nets first, fan-out In3>In2>F, A*'
APPS = [(APP + ', margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ', margin 8 mm', 8.0, SIGNAL_LAYERS)]


def clusters(b, uuids):
    left = set(uuids)
    out = []
    while left:
        u = left.pop()
        grp = {u}
        changed = True
        while changed:
            changed = False
            for v in list(left):
                a, c = b.items[v], None
                for w in grp:
                    c = b.items[w]
                    if a['kind'] == c['kind'] == 'tracks' and a['net'] == c['net'] and a['layer'] == c['layer'] and any(math.dist(p, q) < 1e-4 for p in (a['a'], a['b']) for q in (c['a'], c['b'])):
                        grp.add(v)
                        left.discard(v)
                        changed = True
                        break
        out.append(grp)
    return out


def plan(b, ledger, opens):
    win = box(*WINDOW)
    own, foreign = {}, {}
    for m in ledger['moves']:
        for t in m.get('shifted_track_endpoints', []):
            it = b.items[t['uuid']]
            if t.get('kind') == 'via':
                if b.via_issues_exact(t['net'], t['xy'], it['diameter'], it['drill'], ignore=(t['uuid'],)):
                    own[t['uuid']] = 'dragged U18 via clearance hit'
                continue
            bounds = [min(t['a'][0], t['b'][0]) - 1, min(t['a'][1], t['b'][1]) - 1, max(t['a'][0], t['b'][0]) + 1, max(t['a'][1], t['b'][1]) + 1]
            if not b.allowed_local(t['net'], t['layer'], bounds, ignore=(t['uuid'],), width=it['width']).covers(LineString([t['a'], t['b']])):
                own[t['uuid']] = 'dragged U18 segment violates clearance'
    for u, info in conflicts(b, ('U18',)).items():
        foreign[u] = 'hit by moved U18 pad ' + info['against']
    pushed_ok = 0
    to_rip = dict(own)
    for grp in clusters(b, [u for u in foreign if b.items[u]['kind'] == 'tracks']):
        left = push(b, ledger, grp, -0.31, 0.0, 'follow U18 dx -0.31')
        pushed_ok += len(grp) - len(left)
        for u in left:
            to_rip[u] = foreign[u]
    for u in foreign:
        if b.items.get(u, {}).get('kind') == 'vias':
            to_rip[u] = foreign[u]
    # own dragged segments may now be legal after the pushes
    for u in list(own):
        it = b.items[u]
        if it['kind'] == 'tracks':
            bounds = [min(it['a'][0], it['b'][0]) - 1, min(it['a'][1], it['b'][1]) - 1, max(it['a'][0], it['b'][0]) + 1, max(it['a'][1], it['b'][1]) + 1]
            if b.allowed_local(it['net'], it['layer'], bounds, ignore=(u,), width=it['width']).covers(LineString([it['a'], it['b']])):
                to_rip.pop(u, None)
    u22 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U22'}
    for num in BALLS:
        p = u22[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore=tuple(to_rip))
        trk = [h['uuid'] for h in hits if h['kind'] == 'tracks' and h.get('layer') in (0, 2) and b.items[h['uuid']]['shapes'][h['layer']].intersects(win)]
        for grp in clusters(b, [u for u in trk if u not in to_rip]):
            for u in grp:
                to_rip[u] = f'blocks U22.{num} POFV'
    print('pushed segments', pushed_ok, 'ripping', len(to_rip), sorted({b.items[u]['net'] for u in to_rip if u in b.items}), flush=True)
    for u, why in to_rip.items():
        if u in b.items:
            rip(b, ledger, [u], 'SUPERVISOR_R2_01 Q2 authorised local rip-up: ' + why)
    placed = {}
    for num in BALLS:
        p = u22[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL)
        if hits:
            print('SITE BLOCKED', num, [(h['kind'], h.get('net')) for h in hits], flush=True)
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U22.' + num}
        b.add_items([], [v])
        placed[num] = v
    nets = {r['net'] for r in ledger['removed']} | {p['net'] for p in b.raw['pads'] if p['ref'] == 'U18'}
    nets.discard('')
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, WINDOW, [(APP + ' (ripped/dragged first), margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ' (ripped/dragged first), margin 7 mm', 7.0, SIGNAL_LAYERS)])
    exitmask = box(22.5, 44.0, 30.5, 51.0).difference(FIELD)
    fan = {}
    for num, v in placed.items():
        comps, ids = b.components(v['net'])
        res, err = route(b, v['net'], comps[ids[v['uuid']]], None, [v['xy'][0] - 1.6, v['xy'][1] - 1.6, v['xy'][0] + 1.6, v['xy'][1] + 1.6], layers=(8, 6, 0), escape_track_mask=exitmask, plane=False)
        fan[num] = [v['uuid']]
        if res:
            b.add_items(res['tracks'], res['vias'])
            fan[num] += [t['uuid'] for t in res['tracks']] + [x['uuid'] for x in res['vias']]
            print('FANOUT', num, v['net'], flush=True)
        else:
            print('FANOUT FAILED', num, v['net'], err, flush=True)
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if not (any(f'Pad {n} ' in t and 'U22' in t for n in BALLS) or ('U18' in t and 'U6' not in t)):
            continue
        info = open_comps(b, it)
        if info is None or None in info[2]:
            continue
        net, comps, (ka, kb) = info
        if ka == kb:
            continue
        a, c = comps[ka], comps[kb]
        src, dst = (a, c) if len(a) <= len(c) else (c, a)
        lbl = net + (' U22' if 'U22' in t else ' U18-open')
        route_entries(b, net, src, dst, lbl, APPS, ledger['routes'], small_vias=False)
    for num, uu in fan.items():
        v = placed[num]
        comps, ids = b.components(v['net'])
        comp = comps[ids[v['uuid']]]
        if not (any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp) or any(e['kind'] == 'zones' for e, _ in comp)):
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback U22.' + num + ' fan-out (no completed connection)')
            print('ROLLBACK', num, flush=True)


run('R2-13', 'U18_nudge_U22_C1_D1', plan, moves=MOVES, moved=('U18',), next_step='R2-14 U6 batch A per SUPERVISOR_R2_04')
