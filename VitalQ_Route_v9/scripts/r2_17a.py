"""R2-17a (supervisor sub-batch 1, retry minimal): U6 POFV sites + J8 notches. Rip my own R2-14 inner-layer U6 routes that cross the array and
re-route those same nets through their #04 corridors so nothing opens; place B2 B3 C4 D2 D4 E2 E3 E4 POFV (expected dangling until 17b/17c)."""
import json
from shapely.geometry import LineString
from r2 import ROOT
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2route import SIGNAL_LAYERS, SMALL
import r2_u6 as U

NEW = ['B2', 'B3', 'C4', 'D2', 'D4', 'E2', 'E3', 'E4']
OWN = {'A3': 'PD_INM', 'A2': 'PD_INP', 'A1': 'AFE_INM', 'E5': 'TX2', 'F2': 'SPI_MOSI'}
AUTH = {'ESP_TX', 'ESP_RX', 'AFE_INP', 'MISO_FL', 'CS_FLASH', '+3V3_ANA', 'TX_5V', 'TX4', '+1V8', 'PD_K'}
APP = 'R2-17a minimal: rip only own segments that block POFV sites, corridor re-route, 60 s budget'


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    U.add_guard(b)
    seeds = {}
    for num in NEW:
        p = u6[num]
        for h in b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8):
            if h['kind'] == 'tracks' and h['net'] in AUTH | set(OWN.values()):
                seeds[h['uuid']] = f"blocks U6.{num} POFV ({h['net']})"
            else:
                print('BLOCKER', num, h, flush=True)
    rip(b, ledger, list(seeds), 'R2-17a authorised local rip: blocks a U6 POFV site')
    print('ripped', len(ledger['removed']), sorted({r['net'] for r in ledger['removed']}), flush=True)
    placed = {}
    for num in NEW:
        p = u6[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8)
        if hits:
            print('SITE BLOCKED', num, [(h['kind'], h.get('net', h.get('name'))) for h in hits], flush=True)
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num, 'expected_dangling': True}
        b.add_items([], [v])
        placed[num] = v
    print('placed', list(placed), flush=True)
    ripped_nets = {r['net'] for r in ledger['removed']}
    for ball, net in OWN.items():
        v = U.ball_via(b, u6, ball)
        if v is None or net not in ripped_nets:
            continue
        r = U.route_ball(b, ledger, ball, v, [(APP + f' ({ball}), margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + f' ({ball}), margin 8 mm', 8.0, SIGNAL_LAYERS)])
        print(ball, net, 'ok' if r else 'FAILED', flush=True)
    nets = {r['net'] for r in ledger['removed']} - set(OWN.values())
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, [21.0, 45.0, 31.0, 56.0], [(APP + ' (authorised ripped nets), margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ' (authorised ripped nets), margin 8 mm', 8.0, SIGNAL_LAYERS)])
    U.drop_guard(b)


def allowance(d):
    plan = json.loads((d / 'plan.json').read_text())
    ids = {v['board_uuid'] for v in plan['vias'] if v.get('expected_dangling')}
    rep = json.loads((d / 'drc.json').read_text())
    return sum(1 for v in rep['violations'] if v['type'] == 'via_dangling' and any(i['uuid'] in ids for i in v['items']))


run('R2-17a', 'U6_POFV_sites_notches', plan, j8={'original_outline': U.J8_RECT}, real_allowance_fn=allowance, next_step='R2-17b U6 easy escapes E4/C4/E2/B2 (+F3)')
