#!/usr/bin/env python3
"""Grid router for VitalQ board. Uses geom.json dump from pcbnew.
Coordinate space: mm. Grid pitch: 0.05 mm.
Layers: F, B, G2(In1), L3(In2), P4(In3), G5(In4)."""
import json, math, heapq, sys
import numpy as np

LID = {0: 'Top Layer', 2: 'Bottom Layer', 4: 'Ground Layer 2', 6: 'Layer 3', 8: 'Power Layer 4', 10: 'Ground Layer 5'}
LNAME = {'F': 0, 'B': 2, 'G2': 4, 'L3': 6, 'P4': 8, 'G5': 10}
LI = ['F', 'B', 'G2', 'L3', 'P4', 'G5']
NL = 6

class Router:
    def __init__(self, geomfile='geom.json', res=0.05):
        g = json.load(open(geomfile))
        self.g = g
        self.res = res
        xs = [e[0] for e in g['edge']] + [e[2] for e in g['edge']]
        ys = [e[1] for e in g['edge']] + [e[3] for e in g['edge']]
        self.x0, self.x1 = min(xs) - 0.5, max(xs) + 0.5
        self.y0, self.y1 = min(ys) - 0.5, max(ys) + 0.5
        self.W = int((self.x1 - self.x0) / res) + 1
        self.H = int((self.y1 - self.y0) / res) + 1
        self._build()

    def cell(self, x, y):
        return int(round((x - self.x0) / self.res)), int(round((y - self.y0) / self.res))

    def xy(self, cx, cy):
        return self.x0 + cx * self.res, self.y0 + cy * self.res

    def _disk(self, mask, cx, cy, r):
        res = self.res
        rr = int(r / res) + 1
        for dy in range(-rr, rr + 1):
            for dx in range(-rr, rr + 1):
                if dx * dx + dy * dy <= rr * rr:
                    x, y = cx + dx, cy + dy
                    if 0 <= x < mask.shape[1] and 0 <= y < mask.shape[0]:
                        mask[y, x] = True

    def _rect(self, mask, cx, cy, w, h, rot_deg, dil):
        """Rotated rect centered at GRID (cx,cy), size w x h mm, dilated by dil mm."""
        res = self.res
        rot = math.radians(rot_deg)
        co, si = math.cos(rot), math.sin(rot)
        hw, hh = (w / 2 + dil) / res, (h / 2 + dil) / res
        # corners in grid units
        cs = [(hw, hh), (-hw, hh), (-hw, -hh), (hw, -hh)]
        pts = [(cx + px * co - py * si, cy + px * si + py * co) for px, py in cs]
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        H, W = mask.shape
        xlo, xhi = max(0, int(min(xs)) - 1), min(W, int(max(xs)) + 2)
        ylo, yhi = max(0, int(min(ys)) - 1), min(H, int(max(ys)) + 2)
        if xlo >= xhi or ylo >= yhi:
            return
        for gy in range(ylo, yhi):
            for gx in range(xlo, xhi):
                lx = (gx - cx) * co + (gy - cy) * si
                ly = -(gx - cx) * si + (gy - cy) * co
                if abs(lx) <= hw + 0.5 and abs(ly) <= hh + 0.5:
                    mask[gy, gx] = True

    def _seg(self, mask, x1, y1, x2, y2, w, dil):
        """Capsule segment in mm painted into mask (grid coords)."""
        res = self.res
        hw = w / 2 + dil
        steps = max(2, int(math.hypot(x2 - x1, y2 - y1) / res * 2))
        for i in range(steps + 1):
            t = i / steps
            self._disk(mask, int(round((x1 + (x2 - x1) * t - self.x0) / res)),
                       int(round((y1 + (y2 - y1) * t - self.y0) / res)), hw)

    def _poly(self, mask, pts, dil):
        """Filled polygon (mm pts) painted dilated."""
        res = self.res
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        x0c, x1c = self.cell(min(xs) - dil, 0)[0], self.cell(max(xs) + dil, 0)[0]
        y0c, y1c = self.cell(0, min(ys) - dil)[1], self.cell(0, max(ys) + dil)[1]
        H, W = mask.shape
        x0c, x1c = max(0, x0c), min(W - 1, x1c)
        y0c, y1c = max(0, y0c), min(H - 1, y1c)
        if x0c > x1c or y0c > y1c:
            return
        n = len(pts)
        # even-odd fill
        for gy in range(y0c, y1c + 1):
            y = self.y0 + gy * res
            xs_int = []
            for i in range(n):
                x1p, y1p = pts[i]; x2p, y2p = pts[(i + 1) % n]
                if (y1p <= y < y2p) or (y2p <= y < y1p):
                    xs_int.append(x1p + (y - y1p) / (y2p - y1p) * (x2p - x1p))
            xs_int.sort()
            for k in range(0, len(xs_int) - 1, 2):
                a, b = xs_int[k], xs_int[k + 1]
                ga, gb = self.cell(a, y)[0], self.cell(b, y)[0]
                ga, gb = max(0, ga), min(W - 1, gb)
                mask[gy, ga:gb + 1] = True
        # dilate boundary by drawing edges as segments
        for i in range(n):
            x1p, y1p = pts[i]; x2p, y2p = pts[(i + 1) % n]
            self._seg(mask, x1p, y1p, x2p, y2p, 0.0, dil)

    def _build(self):
        g, res = self.g, self.res
        # allcopper mask per layer (net-agnostic) & per-net copper cells
        self.allmask = [np.zeros((self.H, self.W), dtype=bool) for _ in range(NL)]
        self.solid = [np.zeros((self.H, self.W), dtype=bool) for _ in range(NL)]
        self.zone_all = [np.zeros((self.H, self.W), dtype=bool) for _ in range(NL)]
        self.netcells = {}   # net -> list of (layer, mask)
        # board-edge blocked region: everything outside outline + slot polys
        outer = [[self.x0, self.y0], [self.x1, self.y0], [self.x1, self.y1], [self.x0, self.y1]]
        edge_block = np.zeros((self.H, self.W), dtype=bool)
        # outside outline: paint all then carve rect (outline is a simple rect here)
        edge_block[:] = True
        # outline main rect -2..48 x -10.5..64.5
        bx0, by0 = self.cell(-2.0, -10.5)
        bx1, by1 = self.cell(48.0, 64.5)
        edge_block[by0:by1 + 1, bx0:bx1 + 1] = False
        # slot cutouts (interior edge rects)
        edge_block_slots = np.zeros((self.H, self.W), dtype=bool)
        # group edge segs into rectangles
        segs = [tuple(e) for e in g['edge']]
        # find closed rects among segs
        slots = []
        used = set()
        for i, s in enumerate(segs):
            if i in used: continue
            x0, y0, x1, y1 = s
            if abs(x0 - x1) < 0.001 or abs(y0 - y1) < 0.001:
                pass
        # simpler: use fixed slot list from earlier scan
        self.slot_polys = [(6.825, 24.8, 7.825, 29.2), (6.825, 40.5, 7.825, 44.9), (7.3, 51.55, 8.3, 52.6),
                           (9.3, 47.6, 11.5, 48.6), (11.9, 49.5, 12.9, 52.3), (13.275, 24.8, 14.275, 29.2),
                           (13.275, 40.5, 14.275, 44.9), (19.725, 24.8, 20.725, 29.2), (20.1, 40.5, 21.1, 44.9),
                           (26.175, 24.8, 27.175, 29.2)]
        for s in self.slot_polys:
            a, b = self.cell(s[0], s[1]), self.cell(s[2], s[3])
            edge_block_slots[b[1]:a[1] + 1, a[0]:b[0] + 1] = True
        # sensor island moat: region between outer frame (17.2-23.1,46.19-52.7) and inner island (18.53-21.87,47.9-51.7) minus bridge (x18.3-20.7 y51.7-52.7)
        moat = np.zeros((self.H, self.W), dtype=bool)
        a = self.cell(17.2, 46.19); b = self.cell(23.1, 52.7)
        moat[a[1]:b[1] + 1, a[0]:b[0] + 1] = True
        c = self.cell(18.53, 47.9); d = self.cell(21.87, 52.7)   # island extends to bridge bottom
        moat[c[1]:d[1] + 1, c[0]:d[0] + 1] = False
        br = self.cell(18.3, 51.7); br2 = self.cell(20.7, 52.7)
        moat[br[1]:br2[1] + 1, br[0]:br2[0] + 1] = False
        self.edge_block = edge_block | edge_block_slots | moat
        # keepouts per layer: tr-block and via-block
        self.ko_track = [np.zeros((self.H, self.W), dtype=bool) for _ in range(NL)]
        self.ko_via = [np.zeros((self.H, self.W), dtype=bool) for _ in range(NL)]
        for k in g['keepouts']:
            for li, lname in enumerate(LI):
                if LID[LNAME[lname]] not in k['layers']:
                    continue
                m = np.zeros((self.H, self.W), dtype=bool)
                self._poly(m, k['poly'], 0.0)
                if k['tr']:
                    self.ko_track[li] |= m
                if k['via']:
                    self.ko_via[li] |= m
        # copper items
        padl = {'Top Layer': 0, 'Bottom Layer': 1}
        for p in g['pads']:
            for lname in p['layers']:
                if lname not in padl:  # only outer copper layers get pads
                    continue
                li = padl[lname]
                m = np.zeros((self.H, self.W), dtype=bool)
                w = max(p['w'], p['w']); h = p['h']
                if p['shape'] == 0:  # circle
                    r = max(p['w'], p['h']) / 2
                    c = self.cell(p['x'], p['y'])
                    self._disk(m, c[0], c[1], r)
                else:
                    self._rect(m, (p['x'] - self.x0) / res, (p['y'] - self.y0) / res, p['w'], p['h'], p.get('rot', 0), 0.0)
                self.allmask[li] |= m
                self.solid[li] |= m
                self.netcells.setdefault(p['net'], {}).setdefault(li, np.zeros((self.H, self.W), dtype=bool))
                self.netcells[p['net']][li] |= m
        for t in g['tracks']:
            li = [i for i, k in enumerate(LI) if LNAME[k] == t['l']]
            if not li: continue
            li = li[0]
            m = np.zeros((self.H, self.W), dtype=bool)
            self._seg(m, t['x1'], t['y1'], t['x2'], t['y2'], t['w'], 0.0)
            self.allmask[li] |= m
            self.solid[li] |= m
            self.netcells.setdefault(t['net'], {}).setdefault(li, np.zeros((self.H, self.W), dtype=bool))
            self.netcells[t['net']][li] |= m
        for v in g['vias']:
            c = self.cell(v['x'], v['y'])
            for li in range(NL):
                m = np.zeros((self.H, self.W), dtype=bool)
                self._disk(m, c[0], c[1], v['d'] / 2)
                self.allmask[li] |= m
                self.solid[li] |= m
                self.netcells.setdefault(v['net'], {}).setdefault(li, np.zeros((self.H, self.W), dtype=bool))
                self.netcells[v['net']][li] |= m
        # zones: fill = obstacle for foreign; own-net = goal. Use outline+fill polys.
        self.zone_own = {}   # (net) -> per-layer mask of pour region
        for z in g['zones']:
            lname = z['layer']
            li = None
            for i, k in enumerate(LI):
                if LID[LNAME[k]] == lname: li = i
            if li is None: continue
            polys = z.get('polys') or z.get('outline') or []
            m = np.zeros((self.H, self.W), dtype=bool)
            for poly in polys:
                self._poly(m, poly, 0.0)
            self.zone_own.setdefault(z['net'], {})[li] = \
                self.zone_own.get(z['net'], {}).get(li, np.zeros((self.H, self.W), dtype=bool)) | m
            # zone fill goes into zone_all (per layer, all nets) — not solid copper
            self.zone_all[li] |= m
            self.allmask[li] |= m
            self.netcells.setdefault(z['net'], {}).setdefault(li, np.zeros((self.H, self.W), dtype=bool))
            self.netcells[z['net']][li] |= m

    def masks_for(self, net, clearance=0.1016, twidth=0.1016, vrad=0.2):
        """Return (blocked[6], goal[6], viaok) for given net.
        blocked: foreign copper + edge + ko_track dilated by (clearance+twidth/2).
        goal: same-net copper cells.
        viaok: cells where via (0.4/0.2) fits vs foreign copper on ALL layers."""
        dil = clearance + twidth / 2
        vdil = clearance + vrad + 0.02   # via pad r
        blocked = []
        goal = []
        own = self.netcells.get(net, {})
        for li in range(NL):
            # foreign = allmask minus own cells, dilated
            fm = self.allmask[li].copy()
            if li in own:
                fm &= ~own[li]
            b = self._dilated(fm, dil)
            b |= self.edge_block
            b |= self.ko_track[li]
            blocked.append(b)
            g = np.zeros((self.H, self.W), dtype=bool)
            if li in own:
                g |= own[li]
            if net in self.zone_own and li in self.zone_own[net]:
                g |= self.zone_own[net][li]
            goal.append(g)
        # viaok: no foreign copper within vdil on every copper layer; no ko_via
        viaok = np.ones((self.H, self.W), dtype=bool)
        for li in range(NL):
            fm = self.solid[li].copy()
            if li in own:
                fm &= ~own[li]
            viaok &= ~self._dilated(fm, vdil)
            viaok &= ~self.ko_via[li]
        viaok &= ~self._dilated(self.edge_block, 0.35)
        return blocked, goal, viaok

    def _dilated(self, m, r):
        if r <= 0: return m.copy()
        rr = int(math.ceil(r / self.res))
        out = np.zeros_like(m)
        ys, xs = np.nonzero(m)
        # coarse dilation via shifts is too slow; use disk structuring
        s = 2 * rr + 1
        ker = np.zeros((s, s), dtype=bool)
        for dy in range(-rr, rr + 1):
            for dx in range(-rr, rr + 1):
                if dx * dx + dy * dy <= rr * rr:
                    ker[rr + dy, rr + dx] = True
        # binary dilation via scipy? not avail. do manual convolution on padded array (slow but ok at res .1)
        pad = np.pad(m, rr, constant_values=False)
        for dy in range(s):
            for dx in range(s):
                if ker[dy, dx]:
                    out |= pad[dy:dy + self.H, dx:dx + self.W]
        return out

    def _start_island(self, net, scell, layers):
        """Flood fill on own-net copper across layers from start cell.
        Returns per-layer mask of the fragment the start belongs to."""
        own = self.netcells.get(net, {})
        ownmask = [own.get(li, np.zeros((self.H, self.W), dtype=bool)) for li in range(NL)]
        # zones of own net also count as copper
        if net in self.zone_own:
            for li, zm in self.zone_own[net].items():
                ownmask[li] = ownmask[li] | zm
        island = [np.zeros((self.H, self.W), dtype=bool) for _ in range(NL)]
        sx, sy, sli = scell
        if not ownmask[sli][sy, sx]:
            ownmask[sli] = ownmask[sli].copy(); ownmask[sli][sy, sx] = True
        stack = [(sx, sy, sli)]
        island[sli][sy, sx] = True
        while stack:
            x, y, li = stack.pop()
            for dx, dy in ((-1,0),(1,0),(0,-1),(0,1),(-1,-1),(1,1),(-1,1),(1,-1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < self.W and 0 <= ny < self.H and ownmask[li][ny, nx] and not island[li][ny, nx]:
                    island[li][ny, nx] = True
                    stack.append((nx, ny, li))
            for nli in range(NL):
                if nli != li and ownmask[nli][y, x] and not island[nli][y, x]:
                    island[nli][y, x] = True
                    stack.append((x, y, nli))
        return island

    def route(self, net, start, target=None, goal_exclude=True, layers=None, twidth=0.1016,
              clearance=0.1016, maxcells=None, via_penalty=3.0, trad=0.2):
        """A* from start=(x,y,li) to goal. If target=(x,y) given, goal = cells
        within trad mm of target (any layer in `layers`). Else goal = all net
        copper minus start island.
        Returns (path_cells [(cx,cy,li)...], via_cells [set]) or None."""
        if layers is None: layers = list(range(NL))
        blocked, goal, viaok = self.masks_for(net, clearance, twidth)
        sx, sy = self.cell(start[0], start[1])
        sli = start[2]
        if target is not None:
            tx, ty = self.cell(target[0], target[1])
            goal = [np.zeros((self.H, self.W), dtype=bool) for _ in range(NL)]
            trr = int(trad / self.res) + 1
            for li in layers:
                m = np.zeros((self.H, self.W), dtype=bool)
                self._disk(m, tx, ty, trad)
                goal[li] = m
        elif goal_exclude:
            isl = self._start_island(net, (sx, sy, sli), layers)
            for li in range(NL):
                goal[li] = goal[li] & ~isl[li]
        # ensure start cell free (own pad may be marked blocked by dilation of nearby own? unmark)
        # A* state: (x,y,li); cost g; heuristic = octile dist to nearest goal cell (approx via distance transform? use euclid to goal centroid)
        ys, xs = np.nonzero(np.logical_or.reduce([goal[li] & ~blocked[li] | goal[li] for li in layers]))
        if len(ys) == 0:
            return None
        # precompute goal distance field via multi-source Dijkstra on grid (cheap chamfer)
        dist = self._chamfer(goal, layers)
        INF = 1e18
        bestg = np.full((self.H, self.W, NL), np.inf, dtype=np.float32)
        parent = {}
        h = dist[sy, sx]
        if not np.isfinite(h): return None
        pq = [(h, 0.0, sx, sy, sli)]
        bestg[sy, sx, sli] = 0.0
        res = self.res
        NB = [(-1,0,1.0),(1,0,1.0),(0,-1,1.0),(0,1,1.0),(-1,-1,1.4142),(-1,1,1.4142),(1,-1,1.4142),(1,1,1.4142)]
        expanded = 0
        found = None
        H, W = self.H, self.W
        while pq:
            f, gc, x, y, li = heapq.heappop(pq)
            if gc > bestg[y, x, li] + 1e-6: continue
            expanded += 1
            if maxcells and expanded > maxcells: break
            if goal[li][y, x]:
                found = (x, y, li); break
            for dx, dy, c in NB:
                nx, ny = x + dx, y + dy
                if not (0 <= nx < W and 0 <= ny < H): continue
                if blocked[li][ny, nx]: continue
                ng = gc + c * res
                if ng < bestg[ny, nx, li] - 1e-6:
                    bestg[ny, nx, li] = ng
                    parent[(nx, ny, li)] = (x, y, li)
                    heapq.heappush(pq, (ng + dist[ny, nx], ng, nx, ny, li))
            # via moves
            if viaok[y, x]:
                for nli in layers:
                    if nli == li: continue
                    if blocked[nli][y, x]: continue
                    ng = gc + via_penalty
                    if ng < bestg[y, x, nli] - 1e-6:
                        bestg[y, x, nli] = ng
                        parent[(x, y, nli)] = (x, y, li)
                        heapq.heappush(pq, (ng + dist[y, x], ng, x, y, nli))
        if not found:
            return None
        # reconstruct
        path = []
        cur = found
        while cur != (sx, sy, sli):
            path.append(cur)
            cur = parent.get(cur)
            if cur is None: return None
        path.append((sx, sy, sli))
        path.reverse()
        return path, viaok

    def _chamfer(self, goal, layers):
        """Distance field to nearest goal cell (combined layers)."""
        gm = np.zeros((self.H, self.W), dtype=bool)
        for li in layers: gm |= goal[li]
        INF = 1e9
        d = np.full((self.H, self.W), INF, dtype=np.float32)
        d[gm] = 0.0
        # two-pass chamfer 3-4
        for y in range(self.H):
            for x in range(self.W):
                if d[y, x] == 0: continue
                v = d[y, x]
                if x > 0: v = min(v, d[y, x - 1] + 3)
                if y > 0: v = min(v, d[y - 1, x] + 3)
                if x > 0 and y > 0: v = min(v, d[y - 1, x - 1] + 4)
                if x < self.W - 1 and y > 0: v = min(v, d[y - 1, x + 1] + 4)
                d[y, x] = v
        for y in range(self.H - 1, -1, -1):
            for x in range(self.W - 1, -1, -1):
                if d[y, x] == 0: continue
                v = d[y, x]
                if x < self.W - 1: v = min(v, d[y, x + 1] + 3)
                if y < self.H - 1: v = min(v, d[y + 1, x] + 3)
                if x < self.W - 1 and y < self.H - 1: v = min(v, d[y + 1, x + 1] + 4)
                if x > 0 and y < self.H - 1: v = min(v, d[y + 1, x - 1] + 4)
                d[y, x] = v
        return d / 3.0 * self.res

    def simplify(self, path):
        """Simplify A* cell path to segments + via list (mm coords)."""
        if not path or len(path) < 2: return None
        segs = []   # (li, x1,y1,x2,y2)
        vias = []   # (x,y)
        cur_layer = path[0][2]
        sx, sy = self.xy(path[0][0], path[0][1])
        px, py = sx, sy
        run_dir = None
        pts = [(self.xy(p[0], p[1]), p[2]) for p in path]
        out = []
        i = 0
        n = len(pts)
        while i < n - 1:
            (x1, y1), l1 = pts[i]
            (x2, y2), l2 = pts[i + 1]
            if l2 != l1:
                # via at (x1,y1)
                out.append(('V', l1, l2, x1, y1))
                i += 1
                continue
            # extend run in direction
            dx, dy = x2 - x1, y2 - y1
            dnorm = (round(dx / self.res), round(dy / self.res))
            rx, ry = x2, y2
            j = i + 1
            while j < n - 1:
                (ax, ay), la = pts[j]
                (bx, by), lb = pts[j + 1]
                if lb != la: break
                nd = (round((bx - ax) / self.res), round((by - ay) / self.res))
                if nd != dnorm: break
                rx, ry = bx, by
                j += 1
            out.append(('S', l1, (x1, y1), (rx, ry)))
            i = j
        # merge: convert to segments/vias
        cur = None
        for item in out:
            if item[0] == 'S':
                _, li, a, b = item
                segs.append((li, a[0], a[1], b[0], b[1]))
            else:
                _, l1, l2, x, y = item
                vias.append((x, y))
        return segs, vias


if __name__ == '__main__':
    r = Router()
    print('grid', r.W, 'x', r.H)
