"""U11 (B) transform search for SUPERVISOR (B): moved pads legal on B; for pins 5 (+3V3) and 3 (TMP117_ALERT) find a 0.4/0.2 via
either in-pad or as a short B dog-bone (<= 0.9 mm) on free opposite-side space. Pins 1/2/4/6/7 must be re-attachable (checked later by routing)."""
import json
import math
import sys
from shapely import affinity
from shapely.geometry import Point, LineString
from shapely.ops import unary_union
from r2route import Board
from geometry import CLEARANCE

b = Board(sys.argv[1])
fp = next(f for f in b.raw['footprints'] if f['ref'] == 'U11')
pads = [p for p in b.raw['pads'] if p['ref'] == 'U11']
own = {p['uuid'] for p in pads}
attached = set()
for p in pads:
    for l, sh in b.items[p['uuid']]['shapes'].items():
        for idx in b.trees[l].query(sh.buffer(0.05)):
            o, _ = b.solids[l][idx]
            if o['net'] == p['net'] and o['kind'] in ('tracks', 'vias'):
                attached.add(o['uuid'])
from shapely.geometry import box as _box
ISL = _box(17.2, 46.19, 23.1, 53.6)
local = {u for u, it in b.items.items() if it['kind'] in ('tracks', 'vias') and it['net'] in {p['net'] for p in pads} and any(sh.intersects(ISL) for sh in it['shapes'].values())}
ign = tuple(own | attached | local)
inner = b.outline.buffer(-0.3)
ox, oy = fp['position']
out = []
for rot in (0, 90, 180, 270):
    for i in range(-8, 9):
        for j in range(-8, 9):
            dx, dy = i * 0.1, j * 0.1
            mp = {}
            ok = True
            for p in pads:
                sh = affinity.translate(affinity.rotate(b.items[p['uuid']]['shapes'][2], -rot, origin=(ox, oy)), dx, dy)
                c = affinity.translate(affinity.rotate(Point(p['xy']), -rot, origin=(ox, oy)), dx, dy)
                if not inner.covers(sh):
                    ok = False
                    break
                for idx in b.trees[2].query(sh.buffer(0.2)):
                    o, osh = b.solids[2][idx]
                    if o['uuid'] in ign:  # own pads, attached copper and island-local copper of U11's own nets (re-routed after the move)
                        continue
                    if o['net'] != p['net'] and sh.distance(osh) < (0.15 if o['kind'] == 'pads' else CLEARANCE):
                        ok = False
                        break
                if not ok:
                    break
                mp[p['number']] = (p['net'], sh, (c.x, c.y))
            if not ok:
                continue
            found = {}
            for num in ('5', '3'):
                net, sh, c = mp[num]
                others = unary_union([s for n, (nn, s, _) in mp.items() if nn != net])
                best = None
                for r in [0] + [0.05 * k for k in range(4, 19)]:
                    for a in range(0, 360, 15 if r else 360):
                        v = (c[0] + r * math.cos(math.radians(a)), c[1] + r * math.sin(math.radians(a)))
                        if Point(v).distance(others) < 0.2 + 0.2:
                            continue
                        if b.via_issues_exact(net, list(v), 0.4, 0.2, ignore=ign):
                            continue
                        if r and LineString([c, v]).buffer(0.0508).distance(others) < CLEARANCE:
                            continue
                        if r and not b.allowed_local(net, 2, [min(c[0], v[0]) - 1, min(c[1], v[1]) - 1, max(c[0], v[0]) + 1, max(c[1], v[1]) + 1], ignore=ign).covers(LineString([c, v])):
                            continue
                        best = (round(v[0], 4), round(v[1], 4), round(r, 3))
                        break
                    if best:
                        break
                if not best:
                    break
                found[num] = best
            if len(found) == 2:
                out.append({'rot_delta': rot, 'dx': round(dx, 2), 'dy': round(dy, 2), 'via5': found['5'], 'via3': found['3'], 'cost': math.hypot(dx, dy) + 0.3 * (rot != 0) + found['5'][2] + found['3'][2]})
out.sort(key=lambda r: r['cost'])
print(json.dumps({'origin': fp['position'], 'rotation': fp['rotation'], 'n': len(out), 'best': out[:8]}, indent=1))
