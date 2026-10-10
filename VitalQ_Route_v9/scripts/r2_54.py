"""R2-54 (user GO #1): MISO_AFE U6.E2 -> R90.1. The R90.1 pocket has no via site only because In2 PD_A / SWEAT_CE / LED3_K segments pass
under it; rip just those In2 segments, drop a POFV 0.25/0.15 at the R90.1 pad edge (44.9, 48.6), route MISO_AFE (In3 east preferred),
re-route the ripped In2 nets on another inner layer in the same batch."""
from shapely.geometry import Point
import sys
import r2route
from r2_batch import run
from r2_common import route_open
from r2_rip import rip, prune_orphans, reconnect
from r2route import SMALL, SIGNAL_LAYERS_8 as SL
from r2 import STATE, save_json, state

BATCH = sys.argv[1] if len(sys.argv) > 1 else 'R2-54'
APP = BATCH + ' R90.1 POFV after local In2 rip; MISO_AFE on In3'
SITE = (44.9, 48.6)
s = state()
for k in list(s['attempts']):
    if k.split(':')[0] in ('MISO_AFE', 'PD_A', 'SWEAT_CE', 'LED3_K', 'LED2_K'):
        s.setdefault('attempts_reset_' + BATCH, {})[k] = s['attempts'][k]
        s['attempts'][k] = []
save_json(STATE, s)


def plan(b, ledger, opens):
    hits = b.via_issues_exact('MISO_AFE', SITE, *SMALL)
    seeds = {h['uuid'] for h in hits if h['kind'] == 'tracks' and h.get('layer') in (2, 6, 8, 12)}
    nets = {b.items[u]['net'] for u in seeds}
    rip(b, ledger, list(seeds), APP + ': blocks the R90.1 via site')
    prune_orphans(b, ledger, nets)
    left = b.via_issues_exact('MISO_AFE', SITE, *SMALL)
    print('ripped', sorted(nets), 'site left', sorted({h.get('net', h.get('name')) for h in left}, key=str), flush=True)
    if not left:
        b.add_items([], [{'net': 'MISO_AFE', 'xy': list(SITE), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'R90.1'}])
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    e2 = b.pad('U6', 'E2')
    if not any(abs(v['xy'][0] - e2['xy'][0]) < 1e-3 and abs(v['xy'][1] - e2['xy'][1]) < 1e-3 for v in b.raw['vias']):
        h = b.via_issues_exact('MISO_AFE', e2['xy'], *SMALL, ignore_keepouts=j8)
        blk = {x['uuid'] for x in h if x['kind'] == 'tracks'}
        if blk:
            bn = {b.items[u]['net'] for u in blk}
            rip(b, ledger, list(blk), APP + ': blocks the U6.E2 POFV site')
            prune_orphans(b, ledger, bn)
            nets |= bn
            h = b.via_issues_exact('MISO_AFE', e2['xy'], *SMALL, ignore_keepouts=j8)
        print('E2 site', sorted({x.get('net', x.get('name')) for x in h}, key=str), flush=True)
        if not h:
            b.add_items([], [{'net': 'MISO_AFE', 'xy': list(e2['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.E2'}])
    r2route.LAYER_COST[8] = 0.8
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if '[MISO_AFE]' in t:
            _o = r2route.route
            import r2_common as _rc
            _rc.route = lambda *a, **k: _o(*a, **{**k, 'time_budget': 180.0, 'max_expand': 4_000_000})
            r2route.LAYER_COST[10] = 1.2
            r = route_open(b, it, 'MISO_AFE ' + BATCH, [(APP + ' (+In4 local), margin 25 mm', 25.0, SL + (10,))], ledger['routes'], small_vias=True)
            _rc.route = _o
            print('OPEN MISO_AFE', 'ok' if r else 'FAILED', flush=True)
    reconnect(b, ledger, nets, [24.0, 44.0, 49.0, 56.0], [(APP + ' (ripped In2), margin 5 mm', 5.0, SL), (APP + ' (ripped In2), margin 14 mm', 14.0, SL)])


run(BATCH, 'R90_MISO_AFE', plan, adopt_if_fewer_opens=True, j8={'original_outline': [{'outer': [[27.72, 50.065], [25.18, 50.065], [25.18, 51.335], [27.72, 51.335]], 'holes': []}]}, next_step='PD2_INM / U11 / U19-C59')
