"""R2-50 (SUPERVISOR U19/C59 cluster): U19 dragged +1.5 mm out of hv_inner (pads 3/4/7 clear of the band), C59 dragged 1.5 mm north out of it;
POFV 0.25/0.15 in U19.3/.4/.7 and C59.1 (+3V3 -> In4 plane) / C59.2 (GND planes); long runs prefer In5. Based on R2-38 (deferred U19 retried with In5 available). Based on R2-5 (SUPERVISOR_R2_02 priority 1, single attempt)."""
from shapely.geometry import LineString, box
from r2_batch import run
from r2_rip import conflicts, rip, prune_orphans, reconnect, trim_stubs
from r2_common import route_entries
from r2route import SIGNAL_LAYERS_8 as SIGNAL_LAYERS, route
from geometry import CLEARANCE

R6_FROM = [15.657664, 46.642181]
MOVES = [{'ref': 'U19', 'from': [17.087664, 43.435463], 'to': [17.087664, 46.435463], 'reason': 'SUPERVISOR: U19 3 mm south, fully out of hv_inner (outline growth not used: the new edge space is >15 mm from U19)'},
         {'ref': 'C59', 'from': [15.647664, 40.509838], 'to': [15.647664, 43.509838], 'reason': 'SUPERVISOR: C59 moves 3 mm south with U19; C59.1 leaves hv_inner'},
         {'ref': 'R6', 'from': R6_FROM, 'to': [R6_FROM[0], R6_FROM[1] + 3.0], 'reason': 'clear the new U19 footprint (moved 3 mm south with the group)'}]
REGION = [11.5, 38.0, 21.5, 52.0]
ESCAPE = box(12.0, 43.8, 21.5, 52.0)  # y > 43.375 band edge + 0.2 + via radius
TARGETS = ('3', '4', '7')
APPROACH = 'R2-57 8L U19/C59/R6 +3 mm south drag, POFV in-pad, In5 preferred, local hit fixes, fanout escape via south of hv band, then A* F/B/In2/In3'


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
    for u, info in conflicts(b, ('U19', 'C59', 'R6')).items():
        seeds[u] = 'foreign copper hit by dragged U19 pad (' + info['against'] + ')'
    ledger['hits'] = seeds
    for u, why in seeds.items():
        rip(b, ledger, [u], why)
    print('local hits ripped', len(seeds), sorted({b_['net'] for b_ in ledger['removed']}), flush=True)
    nets = {r['net'] for r in ledger['removed']} | {p['net'] for p in b.raw['pads'] if p['ref'] in ('U19', 'C59', 'R6')}
    nets.discard('')
    prune_orphans(b, ledger, nets)

    # fan-out escapes for pins 3/4/7 before anything else claims the space
    from r2route import SMALL
    fan = {}
    for ref, num in (('C59', '1'), ('C59', '2')):
        p = b.pad(ref, num)
        if not b.via_issues_exact(p['net'], p['xy'], *SMALL):
            v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': ref + '.' + num}
            b.add_items([], [v])
            print('POFV', ref, num, p['net'], flush=True)
        else:
            print('POFV blocked', ref, num, sorted({h.get('net', h.get('name')) for h in b.via_issues_exact(p['net'], p['xy'], *SMALL)}, key=str), flush=True)
    for num in TARGETS:
        p = b.pad('U19', num)
        if not b.via_issues_exact(p['net'], p['xy'], *SMALL):
            v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U19.' + num}
            b.add_items([], [v])
            fan[p['net']] = [v['uuid']]
            print('POFV U19', num, p['net'], flush=True)
            continue
        comps, ids = b.components(p['net'])
        src = comps[ids[p['uuid']]]
        bounds = [p['xy'][0] - 2.6, p['xy'][1] - 2.6, p['xy'][0] + 2.6, p['xy'][1] + 2.6]
        res, err = route(b, p['net'], src, None, bounds, layers=SIGNAL_LAYERS, escape_mask=ESCAPE)
        if res:
            b.add_items(res['tracks'], res['vias'])
            fan[p['net']] = [t['uuid'] for t in res['tracks']] + [v['uuid'] for v in res['vias']]
            print('FANOUT', num, p['net'], res['vias'], flush=True)
        else:
            print('FANOUT FAILED', num, p['net'], err, flush=True)
            ledger['routes'].append({'connection': p['net'] + ' U19 fanout', 'net': p['net'], 'status': 'failed', 'reason': err})
    ledger['fanout'] = fan
    reconnect(b, ledger, nets, REGION, [(APPROACH + ', margin 4 mm', 4.0, SIGNAL_LAYERS), (APPROACH + ', margin 10 mm', 10.0, SIGNAL_LAYERS)])
    # roll back fan-outs whose long route failed (avoid dangling vias)
    for net, uu in fan.items():
        comps, ids = b.components(net)
        anchor = next(p['uuid'] for p in b.raw['pads'] if p['ref'] == 'U19' and p['net'] == net)
        if len([c for c in comps.values() if any(e['kind'] == 'pads' for e, _ in c)]) > 1 and any(u in b.items for u in uu):
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback fan-out: long route failed')
    trim_stubs(b, ledger, nets, REGION)


from r2 import STATE, save_json, state
_s = state()
for _k in list(_s['attempts']):
    if _k.split(':')[0] in ('ADS1292_PWDN', 'AD5940_RESET', 'IR_GATE', 'SWEAT_WE', 'NIR_AN', 'EXP_INT', 'AFE4900_RESETZ', 'CHG_STAT', 'CHG_DIS', 'VBUS_DET', 'I2C_SCL', 'I2C_SDA', '+3V3_ANA', 'GND', '+3V3', 'TX5_EN', 'ESP_EN'):
        _s.setdefault('attempts_reset_R2_57_U19', {})[_k] = _s['attempts'][_k]; _s['attempts'][_k] = []
save_json(STATE, _s)
import r2route as _rr
_rr.LAYER_COST[12] = 0.7
rec = run('R2-57', 'U19_C59_3mm_south', plan, moves=MOVES, moved=('U19', 'C59', 'R6'), adopt_if_fewer_opens=True, next_step='fab refresh + report')

