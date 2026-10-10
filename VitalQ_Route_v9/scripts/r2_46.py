"""R2-46 (user priority: last opens first): U6 E2 MISO_AFE, F1 AFE_CLK, B3 PD2_INM. POFV 0.25/0.15 at the ball centre, escape on In3/In5 or
In4 (power layer, local; pocket analysis shows In3/In5 walled, In4 open), via up near the destination. The SPI_MOSI segment on the F1 POFV
site is ripped and SPI_MOSI re-routed on another layer in the same batch. Longer per-route budget (180 s); In4 cost lowered for these nets."""
import math
import r2route
from r2_batch import run
from r2_common import route_entries, open_comps
from r2_rip import rip, reconnect
from r2route import SMALL

SL = (0, 2, 6, 8, 12, 10)
APP = 'R2-46 POFV ball escape on In3/In5/In4 (local), 180 s budget'
BALLS = {'E2': 'MISO_AFE', 'F1': 'AFE_CLK', 'B3': 'PD2_INM'}
_orig = r2route.route


def _route(*a, **k):
    k['time_budget'] = 180.0
    k['max_expand'] = 4_000_000
    return _orig(*a, **k)


import r2_common
r2_common.route = _route
r2route.LAYER_COST[10] = 1.2


def plan(b, ledger, opens):
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    blockers = set()
    for num in BALLS:
        p = u6[num]
        for h in b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8):
            if h['kind'] == 'tracks':
                blockers.add(h['uuid'])
    ripped = {b.items[u]['net'] for u in blockers}
    rip(b, ledger, list(blockers), 'R2-46: blocks a U6 POFV site')
    print('ripped', ripped, flush=True)
    vias = {}
    for num in BALLS:
        p = u6[num]
        if b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8):
            print('site still blocked', num, flush=True)
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num}
        b.add_items([], [v])
        vias[num] = v
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        num = next((n for n, net in BALLS.items() if f'Pad {n} [{net}] of U6' in t), None)
        if num is None or num not in vias:
            continue
        info = open_comps(b, it)
        net, comps, (ka, kb) = info
        a, c = comps[ka], comps[kb]
        src, dst = (a, c) if any(e['kind'] == 'pads' and e.get('ref') == 'U6' for e, _ in a) else (c, a)
        r = route_entries(b, net, src, dst, net + ' R2-46', [(APP + ', margin 8 mm', 8.0, SL), (APP + ', margin 20 mm', 20.0, SL)], ledger['routes'], small_vias=False)
        print('OPEN', num, net, 'ok' if r else 'FAILED', flush=True)
        if not r:
            rip(b, ledger, [vias[num]['uuid']], 'rollback U6.' + num + ' POFV')
    if ripped:
        reconnect(b, ledger, ripped, [24.0, 47.0, 32.0, 56.0], [(APP + ' (ripped blocker, other layer), margin 5 mm', 5.0, SL), (APP + ' (ripped blocker), margin 12 mm', 12.0, SL)])


run('R2-46', 'U6_last_balls', plan, j8={'original_outline': [{'outer': [[27.72, 50.065], [25.18, 50.065], [25.18, 51.335], [27.72, 51.335]], 'holes': []}]}, adopt_if_fewer_opens=True, next_step='PD_A, C59, I2C_SDA')
