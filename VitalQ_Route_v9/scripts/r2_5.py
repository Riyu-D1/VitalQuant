"""R2-5: U19 drag-with-tracks (SUPERVISOR_R2_02 priority 1, single attempt)."""
from shapely.geometry import LineString, box
from r2_batch import run
from r2_rip import conflicts, rip, prune_orphans, reconnect, trim_stubs
from r2_common import route_entries
from r2route import SIGNAL_LAYERS, route
from geometry import CLEARANCE

MOVES = [{'ref': 'U19', 'from': [17.087664, 43.435463], 'to': [17.087664, 44.435463], 'reason': 'SUPERVISOR_R2_02 #1: drag-with-tracks dy=+1.00 (no rotation: rotating 180 would drag pin1/2 tracks across the package); pins 3/4/7 escape south of the hv_inner band'}]
REGION = [12.5, 39.5, 21.5, 48.5]
ESCAPE = box(12.5, 43.8, 21.5, 48.5)  # y > 43.375 band edge + 0.2 + via radius
TARGETS = ('3', '4', '7')
APPROACH = 'R2-5 drag-with-tracks U19 dy+1 (supervisor #02), local hit fixes, fanout escape via south of hv band, then A* F/B/In2/In3'


def plan(b, ledger, opens):
    shifted = [t for m in ledger['moves'] for t in m.get('shifted_track_endpoints', [])]
    seeds = {}
    for t in shifted:
        if t.get('kind') == 'via':
            if b.via_issues(t['net'], t['xy'], b.items[t['uuid']]['diameter'], b.items[t['uuid']]['drill'], ignore=(t['uuid'],)):
                seeds[t['uuid']] = 'dragged via clearance hit'
            continue
        item = b.items[t['uuid']]
        bounds = [min(t['a'][0], t['b'][0]) - 1, min(t['a'][1], t['b'][1]) - 1, max(t['a'][0], t['b'][0]) + 1, max(t['a'][1], t['b'][1]) + 1]
        if not b.allowed_local(t['net'], t['layer'], bounds, ignore=(t['uuid'],), width=item['width']).covers(LineString([t['a'], t['b']])):
            seeds[t['uuid']] = 'dragged U19 segment now violates clearance'
    for u, info in conflicts(b, ('U19',)).items():
        seeds[u] = 'foreign copper hit by dragged U19 pad (' + info['against'] + ')'
    ledger['hits'] = seeds
    for u, why in seeds.items():
        rip(b, ledger, [u], why)
    print('local hits ripped', len(seeds), sorted({b_['net'] for b_ in ledger['removed']}), flush=True)
    nets = {r['net'] for r in ledger['removed']} | {p['net'] for p in b.raw['pads'] if p['ref'] == 'U19'}
    nets.discard('')
    prune_orphans(b, ledger, nets)

    # fan-out escapes for pins 3/4/7 before anything else claims the space
    fan = {}
    for num in TARGETS:
        p = b.pad('U19', num)
        comps, ids = b.components(p['net'])
        src = comps[ids[p['uuid']]]
        bounds = [p['xy'][0] - 2.6, p['xy'][1] - 2.6, p['xy'][0] + 2.6, p['xy'][1] + 2.6]
        res, err = route(b, p['net'], src, None, bounds, escape_mask=ESCAPE)
        if res:
            b.add_items(res['tracks'], res['vias'])
            fan[p['net']] = [t['uuid'] for t in res['tracks']] + [v['uuid'] for v in res['vias']]
            print('FANOUT', num, p['net'], res['vias'], flush=True)
        else:
            print('FANOUT FAILED', num, p['net'], err, flush=True)
            ledger['routes'].append({'connection': p['net'] + ' U19 fanout', 'net': p['net'], 'status': 'failed', 'reason': err})
    ledger['fanout'] = fan
    reconnect(b, ledger, nets, REGION, [(APPROACH + ', margin 3 mm', 3.0, SIGNAL_LAYERS)])
    # roll back fan-outs whose long route failed (avoid dangling vias)
    for net, uu in fan.items():
        comps, ids = b.components(net)
        anchor = next(p['uuid'] for p in b.raw['pads'] if p['ref'] == 'U19' and p['net'] == net)
        if len([c for c in comps.values() if any(e['kind'] == 'pads' for e, _ in c)]) > 1 and any(u in b.items for u in uu):
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback fan-out: long route failed')
    trim_stubs(b, ledger, nets, REGION)


rec = run('R2-5', 'U19_drag', plan, moves=MOVES, moved=('U19',), next_step='R2-6 U7 via-in-pad (B/C/D rows first)')
if not rec['accepted']:
    print('U19 DEFERRED (supervisor #02): drag-with-tracks attempt rejected; parked, moving to U7 via-in-pad')
