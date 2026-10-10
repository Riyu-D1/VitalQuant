"""R2-17e: clear the inner-layer east wall of U6 (own AFE_INP/AFE_INM/LED3_K/PD_INM routes hugging x 26.8-27.7), place all J8-notched POFVs,
route the U6 escapes through their #04 corridors first (C-channel on 3 layers: B3 In3, D4 In2, D2 In4), then re-route the ripped nets
with a band keepout so they stay east of x 27.85."""
import json, math
from shapely.geometry import box, LineString
from r2 import ROOT, STATE, save_json, state
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2route import SIGNAL_LAYERS, SMALL
import r2_u6 as U

BAND = box(26.75, 49.95, 27.85, 53.0)
WALL_NETS = {'AFE_INP', 'AFE_INM', 'LED3_K', 'PD_INM'}
BALLS = ['B3', 'D4', 'D2', 'B2', 'E2', 'E3', 'E4', 'C4']
AUTH = {'ESP_TX', 'ESP_RX', 'AFE_INP', 'MISO_FL', 'CS_FLASH', '+3V3_ANA', 'TX_5V', 'TX4', '+1V8', 'PD_K', 'PD_INM', 'AFE_INM', 'LED3_K'}
APP = 'R2-17e east-wall cleared (own inner routes ripped), corridor escape first, band keepout for re-routes, 60 s budget'

# counters reset: the east wall was my own routing (R2-12/14/16); clearing it changes the geometry (precedent SUPERVISOR_R2_01)
s = state()
reset = {k: v for k, v in s['attempts'].items() if k.split(':')[0] in ('PD2_INP', 'MISO_AFE', 'AFE_BG')}
s.setdefault('attempts_reset_R2_17e', {}).update(reset)
for k in reset:
    s['attempts'][k] = []
save_json(STATE, s)


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    U.add_guard(b)
    seeds = {}
    for t in b.raw['tracks']:
        if t['net'] in WALL_NETS and t['layer'] in (6, 8, 10) and LineString([t['a'], t['b']]).intersects(BAND.union(U.FIELD)):
            seeds[t['uuid']] = 'R2-17e: inner-layer wall east of/through U6 array (' + t['net'] + ')'
    for num in BALLS:
        p = u6[num]
        for h in b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8, ignore=tuple(seeds)):
            if h['kind'] == 'tracks' and h['net'] in AUTH:
                seeds[h['uuid']] = f"blocks U6.{num} POFV ({h['net']})"
    for ball in BALLS:
        cor = U.corridor(ball)
        for l in (6, 8, 10):
            for idx in b.trees[l].query(cor):
                it, sh = b.solids[l][idx]
                if it['kind'] == 'tracks' and it['net'] in AUTH and it['net'] != u6[ball]['net'] and sh.intersects(cor):
                    seeds[it['uuid']] = f"crosses U6.{ball} corridor ({it['net']})"
    rip(b, ledger, list(seeds), 'R2-17e local rip')
    ripped = {r['net'] for r in ledger['removed']}
    print('ripped', len(ledger['removed']), sorted(ripped), flush=True)
    placed = {}
    for num in BALLS:
        p = u6[num]
        if U.ball_via(b, u6, num):
            placed[num] = U.ball_via(b, u6, num)
            continue
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8)
        if hits:
            print('SITE BLOCKED', num, [(h['kind'], h.get('net', h.get('name'))) for h in hits], flush=True)
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num}
        b.add_items([], [v])
        placed[num] = v
    print('placed', list(placed), flush=True)
    for ball in BALLS:
        v = placed.get(ball)
        if not v:
            continue
        lay = U.CH[ball][2]
        r = U.route_ball(b, ledger, ball, v, [(APP + f' ({ball}, {lay}), margin 6 mm', 6.0, lay)])
        print(ball, v['net'], 'ok' if r else 'FAILED', flush=True)
        if not r:
            comps, ids = b.components(v['net'])
            comp = comps[ids[v['uuid']]]
            if not any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp):
                rip(b, ledger, [v['uuid']] + [e['uuid'] for e, _ in comp if str(e['uuid']).startswith('new-') and e['kind'] != 'pads' and e['uuid'] != v['uuid']], 'rollback U6.' + ball)
    # re-route ripped nets, kept out of the band so the U6 east exits stay free
    tmp = {'uuid': 'band', 'name': 'R2-17e band', 'parent': None, 'layers': [6, 8, 10], 'tracks': True, 'vias': True, 'fills': False}
    for l in (6, 8, 10):
        b.keepouts[l].append((tmp, BAND))
    b.cache = {}
    nets = ripped
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, [21.0, 44.0, 32.0, 58.0], [(APP + ' (ripped nets east of band), margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ' (ripped nets east of band), margin 9 mm', 9.0, SIGNAL_LAYERS)])
    for l in (6, 8, 10):
        b.keepouts[l] = [(k, s_) for k, s_ in b.keepouts[l] if k['uuid'] != 'band']
    b.cache = {}
    U.drop_guard(b)


run('R2-17e', 'U6_east_wall_channels', plan, j8={'original_outline': U.J8_RECT}, next_step='R2-18 SPI_SCK F3, U22 C1/D1 (LED3_K re-route + U18 move), leftovers')
