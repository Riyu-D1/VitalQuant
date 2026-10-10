"""Search a small U11 (B side) transform so that POFV via-in-pad at U11.5 (+3V3) and U11.3 (TMP117_ALERT) are legal (SUPERVISOR (B))."""
import json
import math
import sys
from shapely import affinity
from shapely.geometry import Point
from r2route import Board
from geometry import CLEARANCE

b = Board(sys.argv[1])
fp = next(f for f in b.raw['footprints'] if f['ref'] == 'U11')
pads = [p for p in b.raw['pads'] if p['ref'] == 'U11']
own = {p['uuid'] for p in pads}
attached = set()
for p in pads:
    for l, sh in b.items[p['uuid']]['shapes'].items():
        for idx in b.trees[l].query(sh):
            o, _ = b.solids[l][idx]
            if o['net'] == p['net'] and o['kind'] in ('tracks', 'vias'):
                attached.add(o['uuid'])
inner = b.outline.buffer(-0.3)
ox, oy = fp['position']
res = []
for rot in (0, 90, 180, 270):
    for i in range(-8, 9):
        for j in range(-8, 9):
            dx, dy = i * 0.1, j * 0.1
            ok = True
            moved = {}
            for p in pads:
                sh = b.items[p['uuid']]['shapes'].get(2)
                if sh is None:
                    continue
                sh = affinity.translate(affinity.rotate(sh, -rot, origin=(ox, oy)), dx, dy)
                c = affinity.translate(affinity.rotate(Point(p['xy']), -rot, origin=(ox, oy)), dx, dy)
                if not inner.covers(sh):
                    ok = False
                    break
                for idx in b.trees[2].query(sh.buffer(0.2)):
                    o, osh = b.solids[2][idx]
                    if o['uuid'] in own or o['uuid'] in attached:
                        continue
                    if o['net'] != p['net'] and sh.distance(osh) < (0.15 if o['kind'] == 'pads' else CLEARANCE):
                        ok = False
                        break
                if not ok:
                    break
                moved[p['number']] = (c.x, c.y)
            if not ok:
                continue
            good = True
            for num in ('5', '3'):
                net = next(p['net'] for p in pads if p['number'] == num)
                hits = [h for h in b.via_issues_exact(net, list(moved[num]), 0.4, 0.2, ignore=tuple(own | attached))]
                if hits:
                    good = False
                    break
            if good:
                res.append({'rot': rot, 'dx': round(dx, 3), 'dy': round(dy, 3), 'cost': math.hypot(dx, dy) + (0 if rot == 0 else 0.5), 'via5': moved['5'], 'via3': moved['3']})
res.sort(key=lambda r: r['cost'])
print(json.dumps({'origin': fp['position'], 'rotation': fp['rotation'], 'candidates': res[:10], 'n': len(res)}, indent=1))
