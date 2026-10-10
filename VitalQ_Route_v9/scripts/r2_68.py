"""R2-68 single net: IR_GATE U19.7 (pad outside hv_inner). Rip only the 3 segments on its POFV site (BIOZ_SP_PAD, CHG_STAT, EXP_INT),
POFV 0.25/0.15 in U19.7, IR_GATE on In5 west, re-route the 3 ripped nets first on other layers."""
import r2route
import r2_common
from r2_batch import run
from r2_common import route_open
from r2_rip import rip, prune_orphans, reconnect
from r2route import SMALL, SIGNAL_LAYERS_8 as SL
from r2 import STATE, save_json, state

s = state()
for k in list(s['attempts']):
    if k.split(':')[0] in ('IR_GATE', 'BIOZ_SP_PAD', 'CHG_STAT', 'EXP_INT'):
        s.setdefault('attempts_reset_R2_69', {})[k] = s['attempts'][k]
        s['attempts'][k] = []
save_json(STATE, s)
_o = r2route.route
r2_common.route = lambda *a, **k: _o(*a, **{**k, 'time_budget': 120.0, 'max_expand': 3_000_000})
r2route.LAYER_COST[12] = 0.7


def plan(b, ledger, opens):
    p7 = b.pad('U19', '7')
    seeds = {h['uuid'] for h in b.via_issues_exact('IR_GATE', p7['xy'], *SMALL) if h['kind'] == 'tracks'}
    nets = {b.items[u]['net'] for u in seeds}
    rip(b, ledger, list(seeds), 'R2-68: on the U19.7 POFV site')
    prune_orphans(b, ledger, nets)
    left = b.via_issues_exact('IR_GATE', p7['xy'], *SMALL)
    print('ripped', sorted(nets), 'left', sorted({x.get('net', x.get('name')) for x in left}, key=str), flush=True)
    if not left:
        b.add_items([], [{'net': 'IR_GATE', 'xy': list(p7['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U19.7'}])
    reconnect(b, ledger, nets, [10.0, 38.0, 24.0, 50.0], [('R2-68 ripped site blockers, margin 4 mm', 4.0, SL), ('R2-68 ripped site blockers, margin 10 mm', 10.0, SL), ('R2-68 ripped site blockers, margin 20 mm', 20.0, SL)])
    for it in opens:
        if '[IR_GATE]' in ' '.join(e['description'] for e in it['items']):
            r = route_open(b, it, 'IR_GATE R2-69', [('R2-69 U19.7 POFV, In5 west (+In4 local), margin 20 mm', 20.0, SL + (10,))], ledger['routes'], small_vias=True)
            print('OPEN IR_GATE', 'ok' if r else 'FAILED', flush=True)


run('R2-69', 'IR_GATE_POFV_v2', plan, adopt_if_fewer_opens=True, next_step='stop at 00:14')
