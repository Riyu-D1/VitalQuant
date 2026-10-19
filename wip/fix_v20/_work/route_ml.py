#!/usr/bin/env python3
"""Multi-layer A* router: hops between In2/In3/In5 via through-vias.
Obstacles = real copper (segs/vias/pads). Zone fills on In1/In4/In6 are
ignored for vias (refill clears them); route layers themselves have no fills.
Edge slots & board outline enforced via mask passed in.
"""
import sys, math, heapq
sys.path.insert(0, '_work')
import numpy as np
from check import Chk, CLR
from shapely.geometry import Point
from shapely.strtree import STRtree

STEP = 0.05
RLAYERS = ['In2.Cu', 'In3.Cu', 'In5.Cu']
VIAR = 0.2  # new via radius (d0.4)

class ML:
    def __init__(self, chk, net, w, x0, y0, x1, y1, margin=0.02,
                 extra_vias=(), edge_mask=None, slot_boxes=()):
        self.c = chk; self.net = net; self.w = w
        self.x0, self.y0 = x0, y0
        self.nx = int((x1-x0)/STEP)+1; self.ny = int((y1-y0)/STEP)+1
        self.need = w/2 + CLR + margin
        self.vneed = VIAR + CLR + margin
        # refillable fills (net,layer) set -> excluded from obstacle model
        self.fills = set()
        for z in chk.p.zones:
            for lay, pts in z['fills']:
                if len(pts) >= 3:
                    self.fills.add((lay, z['net']))
        # hard obstacle geometries per layer (skip fills)
        self.layer_items = {}
        for L in set(list(chk.layers.keys())+RLAYERS):
            items = [(g, n) for g, n in chk.layers.get(L, []) if (L, n) not in self.fills]
            self.layer_items[L] = items
        # rasterize blocked cells per route layer
        self.blk = {}
        xs = x0 + np.arange(self.nx)*STEP
        ys = y0 + np.arange(self.ny)*STEP
        self.X, self.Y = np.meshgrid(xs, ys)
        for L in RLAYERS:
            self.blk[L] = self._rasterize(L)
        # board mask (valid = inside outline/tab & outside slots)
        valid = edge_mask(self.X, self.Y)
        for bx0, by0, bx1, by1 in slot_boxes:
            valid &= ~((self.X > bx0-0.45) & (self.X < bx1+0.45) &
                       (self.Y > by0-0.45) & (self.Y < by1+0.45))
        # extra vias as obstacles on all layers
        rr = VIAR + w/2 + CLR + 0.01
        for vx, vy, vn in extra_vias:
            valid &= ~((self.X-vx)**2 + (self.Y-vy)**2 < rr*rr)
        for L in RLAYERS:
            self.blk[L] |= ~valid
        # extra vias as via-site obstacles too (wider spacing)
        self.viablk = np.zeros((self.ny, self.nx), dtype=bool)
        rr2 = VIAR + VIAR + CLR + 0.01
        for vx, vy, vn in extra_vias:
            self.viablk |= (self.X-vx)**2 + (self.Y-vy)**2 < rr2*rr2
        self.viablk |= ~edge_mask(self.X, self.Y)  # via must be inside board
        # via candidates memo
        self._viacache = {}
        # pre-collect all-layer geometry for via checks
        self.allg = []
        for L, items in self.layer_items.items():
            for g, n in items:
                if n == net: continue
                self.allg.append((g, n, L))
        self.tree = STRtree([g for g, n, L in self.allg])

    def _rasterize(self, layer):
        # per-obstacle approach: mark cells inside each obstacle's inflated bbox,
        # exact distance check only in the bbox margin band.
        items = self.layer_items.get(layer, [])
        need = self.need
        blk = np.zeros((self.ny, self.nx), dtype=bool)
        for g, n in items:
            if n == self.net: continue
            bx0, by0, bx1, by1 = g.bounds
            ix0 = max(0, int((bx0-need-self.x0)/STEP)-1)
            ix1 = min(self.nx-1, int(math.ceil((bx1+need-self.x0)/STEP))+1)
            iy0 = max(0, int((by0-need-self.y0)/STEP)-1)
            iy1 = min(self.ny-1, int(math.ceil((by1+need-self.y0)/STEP))+1)
            if ix1 < ix0 or iy1 < iy0: continue
            for iy in range(iy0, iy1+1):
                y = self.y0 + iy*STEP
                for ix in range(ix0, ix1+1):
                    if blk[iy, ix]: continue
                    p = Point(self.x0+ix*STEP, y)
                    if g.contains(p) or g.distance(p) < need:
                        blk[iy, ix] = True
        return blk

    def via_ok(self, ix, iy):
        """through-via at cell: all-layer clearance vs hard copper (fills OK)."""
        key = (ix, iy)
        v = self._viacache.get(key)
        if v is not None: return v
        if self.viablk[iy, ix]:
            self._viacache[key] = False; return False
        x, y = self.x0+ix*STEP, self.y0+iy*STEP
        disk = Point(x, y).buffer(VIAR)
        ok = True
        for gi in self.tree.query(disk.buffer(self.vneed+0.3)):
            g = self.allg[int(gi)][0]
            if disk.distance(g) < self.vneed - 1e-9:
                ok = False; break
        self._viacache[key] = ok
        return ok

    def astar(self, start, target):
        sx, sy = start; tx, ty = target
        six = int(round((sx-self.x0)/STEP)); siy = int(round((sy-self.y0)/STEP))
        tix = int(round((tx-self.x0)/STEP)); tiy = int(round((ty-self.y0)/STEP))
        NL = len(RLAYERS)
        DIRS = [(1,0,1.0),(-1,0,1.0),(0,1,1.0),(0,-1,1.0),
                (1,1,1.4142),(-1,1,1.4142),(1,-1,1.4142),(-1,-1,1.4142)]
        HOPCOST = 40.0  # ~2mm equivalent
        dist = {}; prev = {}
        def h(ix, iy): return math.hypot(ix-tix, iy-tiy)
        # start reachable from the tap via on ANY route layer whose cell is free
        pq = []
        for li, L in enumerate(RLAYERS):
            if not self.blk[L][siy, six]:
                key = (six, siy, li)
                dist[key] = 0.0
                heapq.heappush(pq, (h(six, siy), 0.0, six, siy, li))
        if not pq: return None, ('start blocked all layers',)
        seen = set(); it = 0
        while pq:
            f, d, ix, iy, li = heapq.heappop(pq)
            key = (ix, iy, li)
            if key in seen: continue
            seen.add(key)
            if ix == tix and iy == tiy:
                path = [(ix, iy, li)]
                while key in prev:
                    ix, iy, li = prev[key]
                    path.append((ix, iy, li)); key = (ix, iy, li)
                path.reverse()
                return path, None
            it += 1
            if it > 4000000: break
            for dx, dy, cost in DIRS:
                jx, jy = ix+dx, iy+dy
                if not (0 <= jx < self.nx and 0 <= jy < self.ny): continue
                L = RLAYERS[li]
                if self.blk[L][jy, jx]: continue
                if dx and dy:
                    # no corner cutting: both ortho neighbors must be free
                    if self.blk[L][iy, ix+dx] or self.blk[L][jy, ix]:
                        continue
                nk = (jx, jy, li)
                nd = d + cost
                if nd < dist.get(nk, 1e18):
                    dist[nk] = nd; prev[nk] = (ix, iy, li)
                    heapq.heappush(pq, (nd + h(jx, jy), nd, jx, jy, li))
            # layer hops
            if self.via_ok(ix, iy):
                for lj in range(NL):
                    if lj == li or self.blk[RLAYERS[lj]][iy, ix]: continue
                    nk = (ix, iy, lj)
                    nd = d + HOPCOST
                    if nd < dist.get(nk, 1e18):
                        dist[nk] = nd; prev[nk] = (ix, iy, li)
                        heapq.heappush(pq, (nd + h(ix, iy), nd, ix, iy, lj))
        return None, ('no path', it)
