#!/usr/bin/env python3
"""Exact-verification path smoothing: compress A* grid path to legal 45deg segments."""
import sys, math
sys.path.insert(0, '_work')
from check import Chk, CLR
from shapely.geometry import LineString

def seg_ok(c, layer, a, b, w, net):
    g = LineString([a, b]).buffer(w/2, cap_style='flat')
    for gg, n in c.layers.get(layer, []):
        if n == net: continue
        if g.distance(gg) < CLR - 1e-9:
            return False
    for ko in c.keepouts.get(layer, []):
        if g.intersects(ko): return False
    return True

def dir_ok(a, b, tol=0.02):
    dx, dy = b[0]-a[0], b[1]-a[1]
    if abs(dx) < 1e-9 or abs(dy) < 1e-9: return True
    return abs(abs(dx)-abs(dy)) < tol*max(abs(dx),abs(dy))

def smooth(c, layer, path, w, net):
    """greedy furthest-reach: from i, extend to furthest j with same 45dir + clear"""
    # first: compress collinear grid steps into run waypoints
    runs = [path[0]]
    px, py = path[0]
    d0 = None
    for x, y in path[1:]:
        d = (math.copysign(1,x-px) if x!=px else 0, math.copysign(1,y-py) if y!=py else 0)
        if d != d0:
            runs.append((px,py)); d0 = d
        px, py = x, y
    runs.append(path[-1])
    # dedupe
    wp = [runs[0]]
    for p in runs[1:]:
        if p != wp[-1]: wp.append(p)
    # greedy LOS
    out = [wp[0]]; i = 0
    n = len(wp)
    while i < n-1:
        best = None
        for j in range(n-1, i, -1):
            a, b = wp[i], wp[j]
            if dir_ok(a,b) and seg_ok(c, layer, a, b, w, net):
                best = j; break
        if best is None:
            # fall back: single step
            best = i+1
            if not seg_ok(c, layer, wp[i], wp[best], w, net):
                print("  WARN: bad seg", wp[i], wp[best])
        out.append(wp[best]); i = best
    return out

if __name__ == '__main__':
    c = Chk('vitalq_v2.kicad_pcb')
    from route import Grid
    g = Grid(c, 'In3.Cu', 'VBUS', 0.3, 18.0, 9.5, 53.2, 25.0)
    path, err = g.astar((18.8083,20.1522), (52.9,23.0))
    print("raw cells:", len(path) if path else err)
    sm = smooth(c, 'In3.Cu', path, 0.3, 'VBUS')
    print("smoothed verts:")
    for p in sm: print("   (%.3f,%.3f)" % p)
    ok = all(seg_ok(c,'In3.Cu',a,b,0.3,'VBUS') for a,b in zip(sm,sm[1:]))
    print("all segs clear:", ok)
