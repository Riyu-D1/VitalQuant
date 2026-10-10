"""R2-6: U7 via-in-pad fan-out (SUPERVISOR_R2_02 priority 2). Functions reused by R2-7."""
import math
from shapely.geometry import box
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect, trim_stubs
from r2_common import route_entries, open_comps
from r2route import route, SIGNAL_LAYERS, SMALL

BALLS = ['B3', 'B4', 'B5', 'B6', 'B7', 'C7', 'D2', 'D7', 'E7', 'F3', 'F5', 'F7']
FIELD = box(36.186 - 0.35, 9.881 - 0.35, 38.986 + 0.35, 12.281 + 0.35)
WINDOW = [34.5, 8.3, 40.7, 13.9]
APPROACHES = [('R2-6 U7 0.25/0.15 POFV at ball centre (existing U7 vias normalised to 0.25/0.15 at ball centre), local rip of blockers, fan-out In3>In2>B to field exit, then A* margin 3 mm', 3.0, SIGNAL_LAYERS),
              ('R2-6 same fan-out, wide detour margin 8 mm', 8.0, SIGNAL_LAYERS)]


def normalise(b, ledger, u7):
    ledger.setdefault('modify_vias', [])
    ledger.setdefault('modify_tracks', [])
    for v in list(b.raw['vias']):
        ball = next((p for p in u7.values() if math.dist(p['xy'], v['xy']) < 0.06 and p['net'] == v['net']), None)
        if not ball:
            continue
        old = list(v['xy'])
        nv = dict(v, xy=list(ball['xy']), diameter=SMALL[0], drill=SMALL[1], filled=True, capped=True)
        moved = []
        for t in list(b.raw['tracks']):
            if t['net'] != v['net']:
                continue
            nt = dict(t)
            for k in ('a', 'b'):
                if math.dist(t[k], old) < 1e-4:
                    nt[k] = list(ball['xy'])
            if nt != t:
                moved.append(nt)
        b.remove_items([v['uuid']] + [t['uuid'] for t in moved])
        b.add_items(moved, [nv])
        ledger['modify_vias'].append({'uuid': v['uuid'], 'net': v['net'], 'from': {'xy': old, 'diameter': v['diameter'], 'drill': v['drill']}, 'xy': nv['xy'], 'diameter': SMALL[0], 'drill': SMALL[1], 'pad': 'U7.' + ball['number'], 'reason': 'normalise to 0.25/0.15 POFV at ball centre so adjacent ball vias meet 0.2 hole-to-copper'})
        ledger['modify_tracks'] += [{'uuid': t['uuid'], 'a': t['a'], 'b': t['b']} for t in moved]
    print('normalised U7 vias', [m['pad'] for m in ledger['modify_vias']], flush=True)


def blockers(b, ledger, u7):
    out = {}
    for num in BALLS:
        p = u7[num]
        for h in b.via_issues_exact(p['net'], p['xy'], *SMALL):
            if h['kind'] == 'tracks':
                out[h['uuid']] = f"blocks U7.{num} POFV ({h['net']})"
    return out


def place(b, ledger, u7):
    placed = {}
    for num in BALLS:
        p = u7[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL)
        if hits:
            ledger['routes'].append({'connection': 'U7.' + num, 'net': p['net'], 'status': 'failed', 'reason': 'POFV site not legal: ' + str(hits)[:300]})
            print('SITE BLOCKED', num, hits, flush=True)
            continue
        v = {'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U7.' + num}
        b.add_items([], [v])
        placed[num] = v
    return placed


def fanout(b, placed, align):
    exitmask = box(*WINDOW).difference(FIELD)
    fan = {}
    for num, v in placed.items():
        comps, ids = b.components(v['net'])
        src = comps[ids[v['uuid']]]
        bounds = [v['xy'][0] - 1.6, v['xy'][1] - 1.6, v['xy'][0] + 1.6, v['xy'][1] + 1.6]
        res, err = route(b, v['net'], src, None, bounds, layers=(8, 6, 2), align=align, escape_track_mask=exitmask, plane=False)
        fan[num] = [v['uuid']]
        if res:
            b.add_items(res['tracks'], res['vias'])
            fan[num] += [t['uuid'] for t in res['tracks']] + [x['uuid'] for x in res['vias']]
            print('FANOUT', num, v['net'], len(res['tracks']), 'tracks', flush=True)
        else:
            print('FANOUT FAILED', num, v['net'], err, flush=True)
    return fan


def route_opens(b, ledger, opens, align, approaches):
    todo = []
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if '[AD5940_RESET]' in t:
            continue
        if any(35.6 <= e['pos']['x'] <= 39.6 and 9.4 <= e['pos']['y'] <= 12.8 for e in it['items']):
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
        src, dst = (a, c) if any(e['kind'] == 'pads' and e.get('ref') == 'U7' for e, _ in a) else (c, a)
        route_entries(b, net, src, dst, net + ' U7', approaches, ledger['routes'], small_vias=False, align=align)


def rollback(b, ledger, placed, fan):
    for num, uu in fan.items():
        v = placed[num]
        comps, ids = b.components(v['net'])
        comp = comps[ids[v['uuid']]]
        others = [x for x in comps.values() if x is not comp and any(e['kind'] in ('pads', 'zones') for e, _ in x)]
        linked = any(math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp if e['kind'] == 'pads') or any(e['kind'] == 'zones' for e, _ in comp)
        if not linked and others:
            rip(b, ledger, [u for u in uu if u in b.items], 'rollback U7.' + num + ' fan-out (no completed connection)')
            print('ROLLBACK', num, flush=True)


def plan(b, ledger, opens):
    u7 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U7'}
    align = (u7['A1']['xy'][0] % 0.05, u7['A1']['xy'][1] % 0.05)
    normalise(b, ledger, u7)
    for u, why in blockers(b, ledger, u7).items():
        rip(b, ledger, [u], 'PROMPT_R2 R2-3/SUPERVISOR_R2_02: ' + why)
    placed = place(b, ledger, u7)
    fan = fanout(b, placed, align)
    route_opens(b, ledger, opens, align, APPROACHES)
    nets = {r['net'] for r in ledger['removed']}
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, WINDOW, [(APPROACHES[0][0].replace('fan-out', 'ripped-net reconnect'), 3.0, SIGNAL_LAYERS)])
    rollback(b, ledger, placed, fan)
    trim_stubs(b, ledger, nets | {v['net'] for v in placed.values()}, WINDOW)


if __name__ == '__main__':
    run('R2-6', 'U7_via_in_pad', plan, next_step='R2-7 U22 via-in-pad + U18 nudge')
