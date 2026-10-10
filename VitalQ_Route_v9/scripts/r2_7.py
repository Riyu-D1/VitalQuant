"""R2-7 (SUPERVISOR_R2_03): adopt R2-6 candidate as base; delete 5 dangling In3 stubs; free SWEAT_WE by deleting D5 GND via; reroute MISO_AD."""
import json
import math
from r2 import ROOT
from r2_batch import run
from r2_rip import rip, trim_stubs, _anchored
from r2_common import route_entries
from session import summary

BASE = ROOT / 'candidates' / 'R2-6'
STUBS = {'35a19dc6': ('CS_AD5940', [36.53591, 10.180775]), '3854008f': ('VBIAS0', [36.48591, 11.430775]), '47792255': ('BIOZ_FP', [38.13591, 11.980775]), 'adf22370': ('BIOZ_SP', [37.43591, 11.980775]), 'eec6d8c7': ('BIOZ_FN', [38.68591, 11.030775])}
APP_SW = 'R2-7 (supervisor #03): D5 GND via deleted, SWEAT_WE from C5 via through via-free ball channel D5-D4-D3-C3-C2-B2-A2 or D4-E4-F4-G4 on In2/In3'
APP_MISO = 'R2-7 (supervisor #03): MISO_AD via(34.465,10.447) -> via(43.224,11.147), reuse both vias, B/In2/In3 around U7'


def comp_of(b, net, uuid_prefix):
    comps, ids = b.components(net)
    u = next(i for i in b.items if i.startswith(uuid_prefix))
    return comps[ids[u]], u


def plan(b, ledger, opens):
    u7 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U7'}
    align = (u7['A1']['xy'][0] % 0.05, u7['A1']['xy'][1] % 0.05)
    # 1. dangling In3 stubs
    for pre, (net, end) in STUBS.items():
        u = next(i for i in b.items if i.startswith(pre))
        t = b.items[u]
        assert t['net'] == net and t['layer'] == 8 and (math.dist(t['a'], end) < 1e-3 or math.dist(t['b'], end) < 1e-3), (pre, t)
        anchored = [_anchored(b, net, t['a'], {u}), _anchored(b, net, t['b'], {u})]
        assert sum(anchored) == 1, ('stub not single-ended as described', pre, anchored)
        landed = [o for idx in b.trees[8].query(b.items[u]['shapes'][8]) for o, _ in [b.solids[8][idx]] if o['uuid'] != u and o['net'] == net and o['kind'] == 'tracks' and any(b.items[u]['shapes'][8].distance(__import__('shapely').geometry.Point(e)) < 0.002 for e in (o['a'], o['b']))]
        keep = t['a'] if anchored[0] else t['b']
        rip(b, ledger, [u], 'supervisor #03 §1: dangling In3 fan-out stub' + (' (T: dangling part only)' if landed else ''))
        if landed:
            from shapely.geometry import Point as P, LineString
            land = min((e for o in landed for e in (o['a'], o['b']) if t['shapes'][8].distance(P(e)) < 0.002), key=lambda e: math.dist(e, keep))
            seg = {'net': net, 'layer': 8, 'a': list(keep), 'b': list(land), 'width': t['width']}
            assert b.allowed_local(net, 8, [min(keep[0], land[0]) - 1, min(keep[1], land[1]) - 1, max(keep[0], land[0]) + 1, max(keep[1], land[1]) + 1]).covers(LineString([seg['a'], seg['b']]))
            b.add_items([seg], [])
            print('shortened', pre, seg, flush=True)
    # 2. SWEAT_WE: delete D5 GND via-in-pad, keep D5 pad on GND through F tracks to E5/E6
    d5 = next(v for v in b.raw['vias'] if v['net'] == 'GND' and math.dist(v['xy'], u7['D5']['xy']) < 1e-3)
    rip(b, ledger, [d5['uuid']], 'supervisor #03 §2: D5 GND via-in-pad deleted to open SWEAT_WE channel')
    comps, ids = b.components('GND')
    main = max(comps.values(), key=lambda c: (any(e['kind'] == 'zones' for e, _ in c), len(c)))
    d5comp = comps[ids[u7['D5']['uuid']]]
    if d5comp is not main:
        from shapely.geometry import LineString
        link = {'net': 'GND', 'layer': 0, 'a': list(u7['E5']['xy']), 'b': list(u7['E6']['xy']), 'width': 0.1016}
        if b.allowed_local('GND', 0, [36.6, 10.3, 37.8, 11.1]).covers(LineString([link['a'], link['b']])):
            b.add_items([link], [])
            print('added E5-E6 GND F link', flush=True)
        comps, ids = b.components('GND')
        main = max(comps.values(), key=lambda c: (any(e['kind'] == 'zones' for e, _ in c), len(c)))
        if comps[ids[u7['D5']['uuid']]] is not main:
            ledger['issues'].append('D5/E5 GND lost plane connection after via removal')
            return
    print('D5 GND still on plane via E6/E5', flush=True)
    src, _ = comp_of(b, 'SWEAT_WE', '45c2c533')
    dst, _ = comp_of(b, 'SWEAT_WE', '7491b032')
    res = route_entries(b, 'SWEAT_WE', src, dst, 'SWEAT_WE U7', [(APP_SW + ', margin 3 mm', 3.0, (6, 8)), (APP_SW + ', +B, margin 5 mm', 5.0, (6, 8, 2))], ledger['routes'], small_vias=False, align=align)
    if res:
        u = next(i for i in b.items if i.startswith('8a3dcc2b'))
        if not _anchored(b, 'SWEAT_WE', [36.85, 12.7], {u}):
            rip(b, ledger, [u], 'supervisor #03 §2: orphaned old SWEAT_WE In2 piece', extend=True)
    # 3. MISO_AD
    src, _ = comp_of(b, 'MISO_AD', '74f36b0a')
    dst, _ = comp_of(b, 'MISO_AD', '7c5662bd')
    if src is not dst:
        res = route_entries(b, 'MISO_AD', src, dst, 'MISO_AD R91', [(APP_MISO + ', margin 3 mm', 3.0, (2, 6, 8)), (APP_MISO + ', margin 6 mm', 6.0, (2, 6, 8, 0))], ledger['routes'], small_vias=False, align=align)
    for pre, free in (('d60f7f2f', [34.606705, 10.305329]), ('6287c39a', [43.082096, 11.00578])):
        u = next((i for i in b.items if i.startswith(pre)), None)
        if u and not _anchored(b, 'MISO_AD', free, {u}):
            land = [o for l in (2,) for idx in b.trees[l].query(b.items[u]['shapes'][2]) for o, _ in [b.solids[l][idx]] if o['uuid'] != u and o['net'] == 'MISO_AD' and o['kind'] == 'tracks']
            if not land:
                rip(b, ledger, [u], 'supervisor #03 §3: unused 0.2 mm MISO_AD B stub')
    trim_stubs(b, ledger, {'SWEAT_WE', 'MISO_AD', 'GND', 'CS_AD5940', 'VBIAS0', 'BIOZ_FP', 'BIOZ_SP', 'BIOZ_FN'}, [34.0, 8.0, 44.5, 19.5])


def extra(d):
    s = summary(d / 'drc.json')
    t, v = s['types'].get('track_dangling', 0), s['types'].get('via_dangling', 0)
    ok = s['unconnected'] < 32 and s['real'] <= 245 and t <= 2 and v <= 1
    return {'pass': ok, 'text': f"supervisor #03 gate unconnected {s['unconnected']}<32, real {s['real']}<=245, track_dangling {t}<=2, via_dangling {v}<=1 -> {'PASS' if ok else 'FAIL'}"}


run('R2-7', 'repair_R2-6', plan, base=BASE, extra_check=extra, next_step='R2-8 U22 via-in-pad + U18 nudge')
