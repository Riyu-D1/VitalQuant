"""R2-65: PD2_INM U6.B3 -> U16.10. POFV 0.25/0.15 in U6.B3 (site clear); the U16.10 pocket is sealed by TX4 / TX_5V / EDA_RE_PAD tracks, so rip
only the track segments walling U16.10 (frontier within 0.2 mm), route PD2_INM (In5 preferred), then re-route the ripped nets in-batch."""
from shapely.geometry import Point
import r2route
import r2_common
from r2_batch import run
from r2_common import route_open
from r2_rip import rip, prune_orphans, reconnect
from r2route import SMALL, SIGNAL_LAYERS_8 as SL
from r2 import STATE, save_json, state

APP = 'R2-70 PD2_INM: U6.B3 POFV + U16.10 pocket rip, In5 preferred'
PLANES = {'GND', '+3V3', 'VBAT_SYS'}
s = state()
for k in list(s['attempts']):
    if k.split(':')[0] in ('PD2_INM', 'TX4', 'TX_5V', 'EDA_RE_PAD', 'I2C_SDA_1V8', 'TX3'):
        s.setdefault('attempts_reset_R2_70', {})[k] = s['attempts'][k]
        s['attempts'][k] = []
save_json(STATE, s)
_o = r2route.route
r2_common.route = lambda *a, **k: _o(*a, **{**k, 'time_budget': 150.0, 'max_expand': 4_000_000})
r2route.LAYER_COST[12] = 0.7


def plan(b, ledger, opens):
    p = b.pad('U16', '10')
    bounds = [p['xy'][0] - 2.5, p['xy'][1] - 2.5, p['xy'][0] + 2.5, p['xy'][1] + 2.5]
    al = b.allowed_local('PD2_INM', 2, bounds)
    comp = next(g for g in getattr(al, 'geoms', [al]) if g.distance(Point(p['xy'])) < 0.06)
    seeds = set()
    for idx in b.trees[2].query(comp.buffer(0.25)):
        it, sh = b.solids[2][idx]
        if it['kind'] == 'tracks' and it['net'] not in PLANES and it['net'] != 'PD2_INM' and sh.distance(comp) < 0.2:
            seeds.add(it['uuid'])
    nets = {b.items[u]['net'] for u in seeds}
    rip(b, ledger, list(seeds), APP + ': walls in U16.10')
    prune_orphans(b, ledger, nets)
    print('ripped', len(seeds), sorted(nets), flush=True)
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    b3 = b.pad('U6', 'B3')
    h = b.via_issues_exact('PD2_INM', b3['xy'], *SMALL, ignore_keepouts=j8)
    print('B3 site', sorted({x.get('net', x.get('name')) for x in h}, key=str), flush=True)
    if not h:
        b.add_items([], [{'net': 'PD2_INM', 'xy': list(b3['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.B3'}])
    for it in opens:
        if '[PD2_INM]' in ' '.join(e['description'] for e in it['items']):
            r = route_open(b, it, 'PD2_INM R2-70', [(APP + ' (+In4 local), margin 20 mm', 20.0, SL + (10,))], ledger['routes'], small_vias=True)
            print('OPEN PD2_INM', 'ok' if r else 'FAILED', flush=True)
    reconnect(b, ledger, nets, [8.0, 50.0, 18.0, 60.0], [(APP + ' (ripped walls), margin 5 mm', 5.0, SL), (APP + ' (ripped walls), margin 12 mm', 12.0, SL)])


run('R2-70', 'PD2_INM_U16_pocket_v2', plan, adopt_if_fewer_opens=True, j8={'original_outline': [{'outer': [[27.72, 50.065], [25.18, 50.065], [25.18, 51.335], [27.72, 51.335]], 'holes': []}]}, next_step='remaining opens')
