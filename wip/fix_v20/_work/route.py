#!/usr/bin/env python3
"""Grid router (A*) for add-only tracks on vitalq board.
Rasterizes per-layer obstacles (tracks, vias, pads, fills-excluded) at 0.05mm,
runs A* with 8-way moves, simplifies path to 45-deg polyline, verifies with shapely.
"""
import sys, math, heapq
sys.path.insert(0, '_work')
from check import Chk, CLR
from pcb import PCB
from shapely.geometry import Point, LineString
from shapely.strtree import STRtree

STEP = 0.05
FILL_LAYERS = {'In1.Cu', 'In4.Cu', 'In6.Cu'}   # zone-fill layers (refill handles)

class Router:
    def __init__(self, chk):
        self.c = chk
        self.trees = {}
        for L, items in chk.layers.items():
            geoms = [g for g, n in items]
            self.trees[L] = (STRtree(geoms) if geoms else None, geoms, [n for g, n in items])

    def layer_blocked(self, layer, net, w, margin=0.0):
        """Return set of blocked grid cells (ix,iy) for a track of width w on layer.
        Cell (ix,iy) maps to board (x0+ix*STEP, y0+iy*STEP). We check the buffered
        track center clearance: obstacle distance >= w/2+CLR+margin."""
        pass

class Grid:
    def __init__(self, chk, layer, net, w, x0, y0, x1, y1, margin=0.02):
        self.c = chk; self.layer = layer; self.net = net; self.w = w
        self.x0, self.y0 = x0, y0
        self.nx = int((x1-x0)/STEP)+1; self.ny = int((y1-y0)/STEP)+1
        self.need = w/2 + CLR + margin
        self.blocked = bytearray(self.nx*self.ny)
        self._rasterize()

    def _rasterize(self):
        c = self.c
        items = c.layers.get(self.layer, [])
        if not items:
            geoms, nets = [], []
        else:
            geoms = [g for g, n in items]; nets = [n for g, n in items]
        tree = STRtree(geoms) if geoms else None
        # cells to check: step grid
        need = self.need
        for iy in range(self.ny):
            y = self.y0 + iy*STEP
            row = iy*self.nx
            for ix in range(self.nx):
                x = self.x0 + ix*STEP
                p = Point(x, y)
                hit = False
                if tree is not None:
                    for i in tree.query(p.buffer(need)):
                        if nets[i] == self.net: continue
                        if geoms[i].distance(p) < need:
                            hit = True; break
                if not hit:
                    for ko in c.keepouts.get(self.layer, []):
                        if ko.contains(p) or ko.boundary.distance(p) < need:
                            hit = True; break
                if hit: self.blocked[row+ix] = 1

    def free(self, ix, iy):
        return 0 <= ix < self.nx and 0 <= iy < self.ny and not self.blocked[iy*self.nx+ix]

    def astar(self, s, t, maxiter=2000000):
        sx, sy = s; tx, ty = t
        six, siy = int(round((sx-self.x0)/STEP)), int(round((sy-self.y0)/STEP))
        tix, tiy = int(round((tx-self.x0)/STEP)), int(round((ty-self.y0)/STEP))
        if not self.free(six, siy): return None, ('start blocked', sx, sy)
        if not self.free(tix, tiy): return None, ('target blocked', tx, ty)
        DIRS = [(1,0,1.0),(-1,0,1.0),(0,1,1.0),(0,-1,1.0),(1,1,1.4142),(-1,1,1.4142),(1,-1,1.4142),(-1,-1,1.4142)]
        dist = {six+self.nx*siy: 0.0}
        prev = {}
        h = lambda ix,iy: math.hypot(ix-tix, iy-tiy)
        pq = [(h(six,siy), 0.0, six, siy)]
        seen = set()
        it = 0
        while pq:
            f, d, ix, iy = heapq.heappop(pq)
            key = ix + self.nx*iy
            if key in seen: continue
            seen.add(key)
            if ix == tix and iy == tiy:
                # reconstruct
                path = [(ix,iy)]
                while key in prev:
                    ix, iy = prev[key]
                    path.append((ix,iy)); key = ix+self.nx*iy
                path.reverse()
                return [(self.x0+i*STEP, self.y0+j*STEP) for i,j in path], None
            if it > maxiter: break
            it += 1
            for dx,dy,cost in DIRS:
                jx,jy = ix+dx, iy+dy
                if not self.free(jx,jy): continue
                # for diagonal: require both ortho neighbors free (no corner cutting)
                if dx and dy and not (self.free(ix+dx,iy) and self.free(ix,jy+dy)): continue
                nk = jx + self.nx*jy
                nd = d + cost
                if nd < dist.get(nk, 1e18):
                    dist[nk] = nd; prev[nk] = (ix,iy)
                    heapq.heappush(pq,(nd + h(jx,jy), nd, jx, jy))
        return None, ('no path',)

def simplify(path):
    """RDP then snap to 45-deg-ish breakpoints."""
    if not path: return path
    pts = path
    def rdp(pts, eps):
        if len(pts) < 3: return pts
        (x1,y1),(x2,y2) = pts[0], pts[-1]
        d = math.hypot(x2-x1,y2-y1) or 1e-9
        worst, wi = 0, -1
        for i,(x,y) in enumerate(pts[1:-1],1):
            t = ((x-x1)*(x2-x1)+(y-y1)*(y2-y1))/d/d
            px,py = x1+t*(x2-x1), y1+t*(y2-y1)
            dd = math.hypot(x-px,y-py)
            if dd>worst: worst,wi=dd,i
        if worst <= eps: return [pts[0],pts[-1]]
        return rdp(pts[:wi+1],eps)[:-1] + rdp(pts[wi:],eps)
    return rdp(pts, STEP*0.9)

if __name__ == '__main__':
    c = Chk('vitalq_v2.kicad_pcb')
    layer = 'In3.Cu'; net = 'VBUS'; w = 0.3
    g = Grid(c, layer, net, w, 18.0, 9.5, 53.2, 25.0)
    nb = sum(g.blocked)
    print(f"grid {g.nx}x{g.ny}, blocked {nb}")
    path, err = g.astar((18.8083,20.1522), (52.9,23.0))
    if err: print("ERR", err); sys.exit(1)
    sp = simplify(path)
    print(f"path {len(path)} cells, {len(sp)} verts")
    for p in sp: print("   (%.2f,%.2f)" % p)
