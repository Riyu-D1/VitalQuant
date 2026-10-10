"""R2-14: U6 batch A, J8-legal subset (SUPERVISOR_R2_04): A1 AFE_INM, A2 PD_INP, A3 PD_INM, E5 TX2, F2 SPI_MOSI, F3 SPI_SCK.
B2/C4/E2/E4 (and the C-channel group) lie inside the J8 footprint no-via rule area -> not touched (hard rule, reported)."""
import math
from shapely.geometry import box
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2_common import route_entries, open_comps
from r2route import route, SIGNAL_LAYERS, SMALL

BALLS = ['A1', 'A2', 'A3', 'E5', 'F2', 'F3']
RIP_WIN = box(24.0, 49.0, 27.0, 52.2)
WINDOW = [22.0, 46.0, 30.0, 55.0]
FIELD = box(24.9 - 0.35, 49.8 - 0.35, 26.5 + 0.35, 51.8 + 0.35)
APP = 'R2-14 U6 0.25/0.15 POFV at ball centre (J8-legal balls only), D5 GND via normalised, authorised In2 ESP_TX/ESP_RX/AFE_INP + F ESP_RX rip in array window, ripped first, fan-out In3>In2>F, A*'
APPS = [(APP + ', margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ', margin 7 mm', 7.0, SIGNAL_LAYERS)]
AUTH = {'ESP_TX', 'ESP_RX', 'AFE_INP'}


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    # normalise D5 GND via (outside J8) so E5 POFV meets 0.2 hole-to-copper
    ledger['modify_vias'], ledger['modify_tracks'] = [], []
    for v in list(b.raw['vias']):
        if v['net'] == 'GND' and math.dist(v['xy'], u6['D5']['xy']) < 0.01 and v['diameter'] > 0.26:
            nv = dict(v, diameter=SMALL[0], drill=SMALL[1], filled=True, capped=True)
            b.remove_items([v['uuid']])
            b.add_items([], [nv])
            ledger['modify_vias'].append({'uuid': v['uuid'], 'net': 'GND', 'from': {'xy': v['xy'], 'diameter': v['diameter'], 'drill': v['drill']}, 'xy': v['xy'], 'diameter': SMALL[0], 'drill': SMALL[1], 'pad': 'U6.D5', 'reason': 'normalise to 0.25/0.15 POFV so adjacent E5 POFV meets 0.2 hole-to-copper'})
    seeds = {}
    for num in BALLS:
        p = u6[num]
        for h in b.via_issues_exact(p['net'], p['xy'], *SMALL):
            if h['kind'] == 'tracks' and h['net'] in AUTH and b.items[h['uuid']]['shapes'][h['layer']].intersects(RIP_WIN):
                seeds[h['uuid']] = f"blocks U6.{num} POFV ({h['net']}; authorised SUPERVISOR_R2_04 / PROMPT_R2 R2-4)"
            else:
                ledger['routes'].append({'connection': 'U6.' + num, 'net': p['net'], 'status': 'failed', 'reason': 'unauthorised blocker ' + str(h)})
                print('UNAUTHORISED BLOCKER', num, h, flush=True)
    for u, why in seeds.items():
        rip(b, ledger, [u], why)
    print('ripped', sorted({r['net'] for r in ledger['removed']}), flush=True)
    placed = {}
    for num in BALLS:
        p = u6[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL)
        if hits:
            print('SITE BLOCKED', num, [(h['kind'], h.get('net', h.get('name'))) for h in hits], flush=True)
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num}
        b.add_items([], [v])
        placed[num] = v
    nets = {r['net'] for r in ledger['removed']}
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, WINDOW, [(APP + ' (ripped net first, outside the array), margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ' (ripped net first), margin 7 mm', 7.0, SIGNAL_LAYERS)])
    exitmask = box(*WINDOW).difference(FIELD)
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
    todo = []
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if any(f'Pad {n} ' in t and 'of U6' in t for n in placed):
            todo.append(it)
    todo.sort(key=lambda it: math.dist((it['items'][0]['pos']['x'], it['items'][0]['pos']['y']), (it['items'][-1]['pos']['x'], it['items'][-1]['pos']['y'])))
    for it in todo:
        info = open_comps(b, it)
        if info is None or None in info[2]:
            continue
        net, comps, (ka, kb) = info
        if ka == kb:
            continue
        a, c = comps[ka], comps[kb]
        src, dst = (a, c) if any(e['kind'] == 'vias' and e['uuid'] in {v['uuid'] for v in placed.values()} for e, _ in a) else (c, a)
        route_entries(b, net, src, dst, net + ' U6', APPS, ledger['routes'], small_vias=True)
    for num, uu in fan.items():
        v = placed[num]
        comps, ids = b.components(v['net'])
        comp = comps[ids[v['uuid']]]
        if not (any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp) or any(e['kind'] == 'zones' for e, _ in comp)):
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback U6.' + num + ' fan-out (no completed connection)')
            print('ROLLBACK', num, flush=True)


run('R2-14', 'U6_batchA_J8_legal', plan, next_step='R2-15 leftovers outside J8 (U22 C1/D1 via LED3_K re-route, +3V3 stitches, TMP117_ALERT, J12 MP, AFE_N_PAD); J8-covered U6 balls need supervisor decision')
