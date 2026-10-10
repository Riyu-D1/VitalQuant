"""R2-52 (user GO #2): U18 dragged dx -0.30 mm (pin-1 CS_FLASH pad clears the U22.C5 POFV site by >= 0.2 mm hole-to-copper);
POFV 0.25/0.15 in U22.C5 (PD_A) routed on In3 to the existing In3 PD_A track; clashes from the drag ripped and re-routed in-batch."""
from shapely.geometry import LineString
from r2_batch import run
from r2_common import route_open
from r2_rip import conflicts, rip, prune_orphans, reconnect
from r2route import SMALL, SIGNAL_LAYERS_8 as SL
import r2route
from r2 import STATE, save_json, state

APP = 'R2-64 U18 dx-0.30 drag + U22.C5 POFV on In3'
MOVES = [{'ref': 'U18', 'from': [26.899594, 44.367033], 'to': [26.599594, 44.367033], 'reason': 'user GO #2: free the U22.C5 (PD_A) POFV site under U18 pin 1'}]
s = state()
for k in list(s['attempts']):
    if k.split(':')[0] in ('PD_A', 'CS_FLASH', 'MISO_FL', '+3V3', 'SPI_SCK', 'SPI_MOSI', 'GND', 'PD_INP', 'PD_K', 'LED1_K', 'CS_MAX86178', 'SWEAT_CE'):
        s.setdefault('attempts_reset_R2_64', {})[k] = s['attempts'][k]
        s['attempts'][k] = []
save_json(STATE, s)


def plan(b, ledger, opens):
    seeds = set(conflicts(b, ('U18',)))
    for t in [t for m in ledger['moves'] for t in m.get('shifted_track_endpoints', [])]:
        if t.get('kind') == 'via':
            if b.via_issues_exact(t['net'], t['xy'], b.items[t['uuid']]['diameter'], b.items[t['uuid']]['drill'], ignore=(t['uuid'],)):
                seeds.add(t['uuid'])
            continue
        item = b.items[t['uuid']]
        bounds = [min(t['a'][0], t['b'][0]) - 1, min(t['a'][1], t['b'][1]) - 1, max(t['a'][0], t['b'][0]) + 1, max(t['a'][1], t['b'][1]) + 1]
        if not b.allowed_local(t['net'], t['layer'], bounds, ignore=(t['uuid'],), width=item['width']).covers(LineString([t['a'], t['b']])):
            seeds.add(t['uuid'])
    nets = {b.items[u]['net'] for u in seeds} | {p['net'] for p in b.raw['pads'] if p['ref'] == 'U18'}
    rip(b, ledger, list(seeds), APP + ': drag clash')
    prune_orphans(b, ledger, nets - {'GND', '+3V3'})
    print('ripped', len(seeds), sorted(nets), flush=True)
    p = b.pad('U22', 'C5')
    hits = b.via_issues_exact(p['net'], p['xy'], *SMALL)
    blk = {h['uuid'] for h in hits if h['kind'] == 'tracks'}
    if blk:
        bn = {b.items[u]['net'] for u in blk}
        rip(b, ledger, list(blk), APP + ': blocks the U22.C5 POFV site')
        prune_orphans(b, ledger, bn)
        nets |= bn
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL)
    print('C5 site hits', sorted({h.get('net', h.get('name')) for h in hits}, key=str), flush=True)
    if not hits:
        b.add_items([], [{'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U22.C5'}])
    reconnect(b, ledger, nets, [21.0, 38.0, 33.0, 50.0], [(APP + ' (drag clash, routed BEFORE PD_A), margin 4 mm', 4.0, SL), (APP + ' (drag clash, before PD_A), margin 10 mm', 10.0, SL), (APP + ' (drag clash, before PD_A), margin 18 mm', 18.0, SL)])
    r2route.LAYER_COST[8] = 0.8
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if '[PD_A]' in t:
            r = route_open(b, it, 'PD_A R2-64', [(APP + ', margin 12 mm', 12.0, SL)], ledger['routes'], small_vias=True)
            print('OPEN PD_A', 'ok' if r else 'FAILED', flush=True)


run('R2-64', 'U18_drag_PD_A_v2', plan, moves=MOVES, moved=('U18',), adopt_if_fewer_opens=True, next_step='R90 move for MISO_AFE')
