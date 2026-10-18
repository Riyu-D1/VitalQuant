#!/usr/bin/env python3
"""Deterministic scripted routing for hw_v2.

Runs AFTER build_pcb.py (which always regenerates the placed-but-unrouted
board). Routes are rebuilt from pad positions each run, so the pipeline
stays reproducible.

Strategy (per ROUTING_PLAN.md):
  1. HV_ELECTRODE corridor first — dedicated lanes, 1.5 mm to foreign
     copper; HV-to-HV needs only 0.127 mm so the lanes bundle.
  2. Everything else: A* on F.Cu/B.Cu with pairwise clearance inflation.
  3. Power/GND pads reach their In1/In2 pours via the router's vias or
     by pour contact at fill time (B-side pads touch the B.Cu GND pour
     directly once zones fill).

All emitted tracks/vias are LOCKED so a subsequent Specctra DSN export
marks them (type fix) and Freerouting preserves them.

Usage: pcbnew-python route_all.py <board.kicad_pcb>
"""
import heapq
import math
import sys
from collections import defaultdict

import pcbnew

MM = 1_000_000
F_CU, B_CU = pcbnew.F_Cu, pcbnew.B_Cu
IN1, IN2, IN3, IN4 = (pcbnew.In1_Cu, pcbnew.In2_Cu,
                     pcbnew.In3_Cu, pcbnew.In4_Cu)
# 6-layer: F + B + In2 (shared w/ power pours) + In3 for signals.
# In1 and In4 stay solid GND return planes.
ROUTE_LAYERS = (F_CU, B_CU, IN2, IN3)
VIA_SPAN_LAYERS = (F_CU, B_CU, IN1, IN2, IN3, IN4)  # through vias clear all six

GRID = 0.10          # mm
BUCKET = 2.0         # spatial hash cell size (>= max obstacle influence)
MAX_INFL = 2.0       # max obstacle influence = 1.5 clearance + 0.5 slack
VIA_D, VIA_H = 0.60, 0.30   # through via at board min-drill (JLC legal)
VIA_PEN = 6.0              # mm-equivalent cost for a layer change
STEP_ORTHO = GRID
STEP_DIAG = GRID * math.sqrt(2)

CLASS_RULE = {
    "HV_ELECTRODE": (1.5, 0.25),
    "ELECTRODE_LV": (0.20, 0.15),
    "USB_90R": (0.15, 0.25),
}
DEFAULT_CLR, DEFAULT_W = 0.127, 0.20
BGA_CLR = 0.09

SKIP_NET_PREFIXES = ("unconnected",)


def mm(v):
    return int(round(v * MM))


class Obstacles:
    def __init__(self, board):
        self.buckets = {ly: defaultdict(list) for ly in VIA_SPAN_LAYERS}
        # Segments are stored on every copper layer, not just ROUTE_LAYERS:
        # imported (e.g. Freerouting) copper may sit on In1/In4 and through
        # vias must still clear it.
        self.seg_buckets = {ly: defaultdict(list) for ly in VIA_SPAN_LAYERS}
        self.via_buckets = defaultdict(list)   # vias exist on all layers
        self.wall_buckets = defaultdict(list)
        self.bga_rects = []
        self.edge = None
        self._cls = {}
        self.dead = set()          # ripped nets — their segs/vias/pads-as-copper ignored
        self._build(board)

    # ---------- index helpers -----------------------------------------
    def _put_rect(self, layer, rect):
        x0, y0, x1, y1 = rect[0] - MAX_INFL, rect[1] - MAX_INFL, rect[2] + MAX_INFL, rect[3] + MAX_INFL
        bx0, by0, bx1, by1 = (int(x0 // BUCKET), int(y0 // BUCKET), int(x1 // BUCKET), int(y1 // BUCKET))
        for bx in range(bx0, bx1 + 1):
            for by in range(by0, by1 + 1):
                self.buckets[layer][(bx, by)].append(rect)

    def _put_via(self, v):
        x, y = v[0], v[1]
        r = v[2] + MAX_INFL
        bx0, by0, bx1, by1 = (int((x - r) // BUCKET), int((y - r) // BUCKET),
                              int((x + r) // BUCKET), int((y + r) // BUCKET))
        for bx in range(bx0, bx1 + 1):
            for by in range(by0, by1 + 1):
                self.via_buckets[(bx, by)].append(v)

    def _put_wall(self, wseg):
        x0, y0, x1, y1 = wseg
        x0, x1 = sorted((x0 - MAX_INFL, x1 + MAX_INFL))
        y0, y1 = sorted((y0 - MAX_INFL, y1 + MAX_INFL))
        for bx in range(int(x0 // BUCKET), int(x1 // BUCKET) + 1):
            for by in range(int(y0 // BUCKET), int(y1 // BUCKET) + 1):
                self.wall_buckets[(bx, by)].append(wseg)

    def _put_seg(self, layer, seg):
        x0, y0, x1, y1 = seg[0], seg[1], seg[2], seg[3]
        r = seg[4] + MAX_INFL
        bx0, by0 = int((min(x0, x1) - r) // BUCKET), int((min(y0, y1) - r) // BUCKET)
        bx1, by1 = int((max(x0, x1) + r) // BUCKET), int((max(y0, y1) + r) // BUCKET)
        for bx in range(bx0, bx1 + 1):
            for by in range(by0, by1 + 1):
                self.seg_buckets[layer][(bx, by)].append(seg)

    def _cand_segs(self, x, y, layer):
        return self.seg_buckets.get(layer, {}).get((int(x // BUCKET), int(y // BUCKET)), ())

    def _cand_rects(self, x, y, layer):
        return self.buckets[layer].get((int(x // BUCKET), int(y // BUCKET)), ())

    def _cand_vias(self, x, y):
        return self.via_buckets.get((int(x // BUCKET), int(y // BUCKET)), ())

    def _cand_walls(self, x, y):
        return self.wall_buckets.get((int(x // BUCKET), int(y // BUCKET)), ())

    # ---------- build ---------------------------------------------------
    def _build(self, board):
        for n in board.GetNetsByName().values():
            self._cls[n.GetNetname()] = n.GetNetClassName() or "Default"
        for fp in board.GetFootprints():
            ref = fp.GetReference()
            for pad in fp.Pads():
                net = pad.GetNetname() or ""
                bb = pad.GetBoundingBox()
                x0, y0 = bb.GetX() / MM, bb.GetY() / MM
                x1, y1 = (bb.GetX() + bb.GetWidth()) / MM, (bb.GetY() + bb.GetHeight()) / MM
                ls = pad.GetLayerSet()
                thru = ls.Contains(IN1)
                for ly in VIA_SPAN_LAYERS:
                    if thru or ls.Contains(ly):
                        self._put_rect(ly, (x0, y0, x1, y1, net, ref))
        for t in board.GetTracks():
            net = t.GetNetname() or ""
            if t.Type() == pcbnew.PCB_VIA_T:
                p = t.GetPosition()
                self._put_via((p.x / MM, p.y / MM, t.GetWidth(F_CU) / 2 / MM, net))
            else:
                s, e = t.GetStart(), t.GetEnd()
                ly = t.GetLayer()
                if ly in self.buckets:
                    self._put_seg(ly, (s.x / MM, s.y / MM, e.x / MM, e.y / MM,
                                       t.GetWidth() / 2 / MM, net))
        for z in board.Zones():
            if not z.GetIsRuleArea():
                continue
            name = z.GetZoneName() or ""
            bb = z.GetBoundingBox()
            x0, y0 = bb.GetX() / MM, bb.GetY() / MM
            x1, y1 = (bb.GetX() + bb.GetWidth()) / MM, (bb.GetY() + bb.GetHeight()) / MM
            if name == "bga_fanout":
                self.bga_rects.append((x0, y0, x1, y1))
                continue
            if not z.GetDoNotAllowTracks():
                continue
            for ly in VIA_SPAN_LAYERS:
                if z.GetLayerSet().Contains(ly):
                    self._put_rect(ly, (x0, y0, x1, y1, None, "keepout"))
        drawings = [d for d in board.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts]
        xs, ys, segs = [], [], []
        for d in drawings:
            if isinstance(d, pcbnew.PCB_SHAPE) and d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
                s, e = d.GetStart(), d.GetEnd()
                segs.append((s.x / MM, s.y / MM, e.x / MM, e.y / MM))
                xs += [s.x / MM, e.x / MM]
                ys += [s.y / MM, e.y / MM]
        self.edge = (min(xs), min(ys), max(xs), max(ys))
        ex0, ey0, ex1, ey1 = self.edge
        for s in segs:
            inside = (ex0 + 0.05 < s[0] < ex1 - 0.05 and ex0 + 0.05 < s[2] < ex1 - 0.05
                      and ey0 + 0.05 < s[1] < ey1 - 0.05 and ey0 + 0.05 < s[3] < ey1 - 0.05)
            if inside:
                self._put_wall(s)

    # ---------- clearance model -----------------------------------------
    def pair_clearance(self, cls_a, cls_b, in_bga):
        if cls_a == "HV_ELECTRODE" and cls_b != "HV_ELECTRODE":
            return 1.5
        if cls_b == "HV_ELECTRODE" and cls_a != "HV_ELECTRODE":
            return 1.5
        if in_bga:
            return BGA_CLR
        if cls_a == "ELECTRODE_LV" or cls_b == "ELECTRODE_LV":
            return 0.20
        if cls_a == "USB_90R" or cls_b == "USB_90R":
            return 0.15
        return DEFAULT_CLR

    def cell_blocked(self, x, y, layer, my_net, my_cls, hw):
        in_bga = any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in self.bga_rects)
        ex0, ey0, ex1, ey1 = self.edge
        margin = hw + 0.30
        if not (ex0 + margin <= x <= ex1 - margin and ey0 + margin <= y <= ey1 - margin):
            return True
        for x0, y0, x1, y1, onet, tag in self._cand_rects(x, y, layer):
            if onet == my_net:
                continue
            clr = 0.05 if tag == "keepout" else self.pair_clearance(
                my_cls, self._cls.get(onet, "Default"), in_bga)
            p = clr + hw
            if x0 - p <= x <= x1 + p and y0 - p <= y <= y1 + p:
                return True
        for x0, y0, x1, y1 in self._cand_walls(x, y):
            p = hw + 0.15
            if x0 - p <= x <= x1 + p and y0 - p <= y <= y1 + p:
                return True
        for sx0, sy0, sx1, sy1, sw, snet in self._cand_segs(x, y, layer):
            if snet == my_net or snet in self.dead:
                continue
            rr = sw + self.pair_clearance(my_cls, self._cls.get(snet, "Default"), in_bga) + hw
            if seg_dist(sx0, sy0, sx1, sy1, x, y) < rr:
                return True
        for vx, vy, vr, vnet in self._cand_vias(x, y):
            if vnet == my_net or vnet in self.dead:
                continue
            rr = vr + self.pair_clearance(my_cls, self._cls.get(vnet, "Default"), in_bga) + hw
            if (vx - x) ** 2 + (vy - y) ** 2 < rr * rr:
                return True
        return False

    def own_via_at(self, x, y, my_net):
        """True if an existing via of my_net covers (x,y) — layer change is
        free there, no new via needed."""
        for vx, vy, vr, vnet in self._cand_vias(x, y):
            if (vnet == my_net and vnet not in self.dead
                    and (vx - x) ** 2 + (vy - y) ** 2 <= (vr + 0.06) ** 2):
                return True
        return False

    def via_fits(self, x, y, my_net, my_cls):
        r = VIA_D / 2
        in_bga = any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in self.bga_rects)
        ex0, ey0, ex1, ey1 = self.edge
        if not (ex0 + r + 0.3 <= x <= ex1 - r - 0.3 and ey0 + r + 0.3 <= y <= ey1 - r - 0.3):
            return False
        for ly in VIA_SPAN_LAYERS:
            for x0, y0, x1, y1, onet, tag in self._cand_rects(x, y, ly):
                if onet == my_net:
                    continue
                clr = (0.05 if tag == "keepout" else
                       self.pair_clearance(my_cls, self._cls.get(onet, "Default"), in_bga))
                if x0 - clr - r <= x <= x1 + clr + r and y0 - clr - r <= y <= y1 + clr + r:
                    return False
            for sx0, sy0, sx1, sy1, sw, snet in self._cand_segs(x, y, ly):
                if snet == my_net or snet in self.dead:
                    continue
                rr = sw + r + self.pair_clearance(my_cls, self._cls.get(snet, "Default"), in_bga)
                if seg_dist(sx0, sy0, sx1, sy1, x, y) < rr:
                    return False
        for vx, vy, vr, vnet in self._cand_vias(x, y):
                if vnet == my_net or vnet in self.dead:
                    continue
                rr = vr + r + self.pair_clearance(my_cls, self._cls.get(vnet, "Default"), in_bga)
                if (vx - x) ** 2 + (vy - y) ** 2 < rr * rr:
                    return False
        for x0, y0, x1, y1 in self._cand_walls(x, y):
            if x0 - r - 0.15 <= x <= x1 + r + 0.15 and y0 - r - 0.15 <= y <= y1 + r + 0.15:
                return False
        return True


def seg_dist(x0, y0, x1, y1, px, py):
    """Distance from point (px,py) to segment (x0,y0)-(x1,y1)."""
    dx, dy = x1 - x0, y1 - y0
    l2 = dx * dx + dy * dy
    if l2 == 0:
        return math.hypot(px - x0, py - y0)
    t = max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / l2))
    return math.hypot(px - (x0 + t * dx), py - (y0 + t * dy))


def class_of(board, netname):
    n = board.FindNet(netname)
    return (n.GetNetClassName() or "Default") if n else "Default"


def pads_of_net(board, netname):
    out = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname() == netname:
                pos = p.GetPosition()
                ls = p.GetLayerSet()
                if ls.Contains(IN1):
                    layers = ROUTE_LAYERS
                elif ls.Contains(F_CU):
                    layers = (F_CU,)
                else:
                    layers = (B_CU,)
                out.append((fp.GetReference(), p.GetName(), pos.x / MM, pos.y / MM, layers))
    return out


def astar(obs, start, goal, my_net, my_cls, w, margin=9.0, goals=None):
    """Grid A* on {F,B}, windowed to link bbox + margin. Returns [(x,y,layer)...].

    `goals` is an optional set of grid cells (same net copper) — reaching ANY
    of them completes the link. `goal` (nearest one) steers the heuristic."""
    hw = w / 2
    cells = {}
    sx, sy, sly = start
    gx, gy, gly = goal
    wx0 = min(sx, gx) - margin
    wx1 = max(sx, gx) + margin
    wy0 = min(sy, gy) - margin
    wy1 = max(sy, gy) + margin

    def free(x, y, ly):
        if not (wx0 <= x <= wx1 and wy0 <= y <= wy1):
            return False
        k = (round(x / GRID), round(y / GRID), ly)
        v = cells.get(k)
        if v is None:
            v = not obs.cell_blocked(x, y, ly, my_net, my_cls, hw)
            cells[k] = v
        return v

    s = (round(sx / GRID), round(sy / GRID), sly)
    g = (round(gx / GRID), round(gy / GRID), gly)
    if goals:
        goals = frozenset(goals)
        goals = goals | {g}
    hit = None

    dirs8 = ((1, 0, STEP_ORTHO), (-1, 0, STEP_ORTHO), (0, 1, STEP_ORTHO), (0, -1, STEP_ORTHO),
             (1, 1, STEP_DIAG), (1, -1, STEP_DIAG), (-1, 1, STEP_DIAG), (-1, -1, STEP_DIAG))

    def h(x, y, ly):
        return math.hypot(x * GRID - gx, y * GRID - gy) * 1.003 + (VIA_PEN if ly != gly else 0)

    pq = [(h(s[0], s[1], s[2]), 0.0, s)]
    came, dist, gdone = {}, {s: 0.0}, set()
    pops = 0
    while pq:
        pops += 1
        if pops > 300_000:        # hard bound — bail rather than churn forever
            return None
        _, d, cur = heapq.heappop(pq)
        if cur == g or (goals and cur in goals):
            hit = cur
            break
        if cur in gdone or d > dist.get(cur, 1e9):
            continue
        gdone.add(cur)
        cx, cy, cly = cur
        x, y = cx * GRID, cy * GRID
        for dx, dy, cost in dirs8:
            nx, ny = cx + dx, cy + dy
            fx, fy = nx * GRID, ny * GRID
            if not free(fx, fy, cly):
                continue
            if dx and dy and (not free(x + dx * GRID, y, cly) or not free(x, y + dy * GRID, cly)):
                continue    # no corner cutting — the chord would clip the blocked cell
            nd = d + cost
            nkey = (nx, ny, cly)
            if nd < dist.get(nkey, 1e9):
                dist[nkey] = nd
                came[nkey] = cur
                heapq.heappush(pq, (nd + h(fx, fy, cly), nd, nkey))
        own_via = obs.own_via_at(x, y, my_net)
        if own_via or obs.via_fits(x, y, my_net, my_cls):
            for oly in ROUTE_LAYERS:              # through via reaches all route layers
                if oly == cly or not free(x, y, oly):
                    continue
                nkey = (cx, cy, oly)
                nd = d + (0.0 if own_via else VIA_PEN)
                if nd < dist.get(nkey, 1e9):
                    dist[nkey] = nd
                    came[nkey] = (cur,)
                    heapq.heappush(pq, (nd + h(x, y, oly), nd, nkey))
    if hit is None:
        return None
    path = []
    cur = hit
    while cur != s:
        path.append(cur)
        c = came[cur]
        cur = c[0] if isinstance(c, tuple) and len(c) == 1 else c
    path.append(s)
    path.reverse()
    out = [(cx * GRID, cy * GRID, ly) for cx, cy, ly in path]
    out[0] = (sx, sy, sly)
    return out


def simplify(path):
    if len(path) <= 2:
        return path
    out = [path[0]]
    for i in range(1, len(path) - 1):
        a, b, c = out[-1], path[i], path[i + 1]
        if a[2] == b[2] == c[2]:
            v1 = (round(b[0] - a[0], 3), round(b[1] - a[1], 3))
            v2 = (round(c[0] - b[0], 3), round(c[1] - b[1], 3))
            if v1[0] * v2[1] - v1[1] * v2[0] == 0 and (v1[0] * v2[0] + v1[1] * v2[1]) > 0:
                continue
        out.append(b)
    out.append(path[-1])
    return out


EMITTED = defaultdict(list)   # net -> [board items], for rip-up


def path_clear(obs, path, net, cls, hw):
    """Final safety check: every emitted segment sampled along its length
    must satisfy exact clearance vs all foreign copper/pads/keepouts.
    Catches anything the grid-center cell_blocked missed (corner clips,
    stale caches). Returns True if the whole path is safe to emit."""
    for i in range(len(path) - 1):
        ax, ay, al = path[i]
        bx, by, bl = path[i + 1]
        if al != bl:
            continue                       # via — validated by via_fits at search time
        n = max(1, int(math.hypot(bx - ax, by - ay) / GRID))
        for k in range(n + 1):
            px = ax + (bx - ax) * k / n
            py = ay + (by - ay) * k / n
            if obs.cell_blocked(px, py, al, net, cls, hw):
                return False
    return True


def emit(board, obs, path, net, w):
    netobj = board.FindNet(net)
    obs.dead.discard(net)   # fresh copper — stale entries may over-block slightly
    for i in range(len(path) - 1):
        ax, ay, al = path[i]
        bx, by, bl = path[i + 1]
        if al != bl:
            if obs.own_via_at(ax, ay, net):
                continue
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(mm(ax), mm(ay)))
            v.SetWidth(mm(VIA_D))
            v.SetDrill(mm(VIA_H))
            v.SetNet(netobj)
            v.SetViaType(pcbnew.VIATYPE_THROUGH)
            v.SetLocked(True)
            board.Add(v)
            EMITTED[net].append(v)
            obs._put_via((ax, ay, VIA_D / 2, net))
            continue
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(mm(ax), mm(ay)))
        t.SetEnd(pcbnew.VECTOR2I(mm(bx), mm(by)))
        t.SetWidth(mm(w))
        t.SetLayer(al)
        t.SetNet(netobj)
        t.SetLocked(True)
        board.Add(t)
        EMITTED[net].append(t)
        obs._put_seg(al, (ax, ay, bx, by, w / 2, net))


def raster_cells(path):
    """Grid cells a path occupies — these become connection targets for the
    remaining pads of the same net (tree growth)."""
    out = set()
    for i in range(len(path) - 1):
        ax, ay, al = path[i]
        bx, by, bl = path[i + 1]
        if al != bl:
            out.add((round(ax / GRID), round(ay / GRID), al))
            out.add((round(ax / GRID), round(ay / GRID), bl))
            continue
        n = max(1, int(math.hypot(bx - ax, by - ay) / GRID))
        for k in range(n + 1):
            out.add((round((ax + (bx - ax) * k / n) / GRID),
                     round((ay + (by - ay) * k / n) / GRID), al))
    return out


def route_net(board, obs, netname):
    pads = pads_of_net(board, netname)
    if len(pads) < 2:
        return 0, 0, []
    cls = class_of(board, netname)
    _, w = CLASS_RULE.get(cls, (DEFAULT_CLR, DEFAULT_W))
    # Seed the net tree with existing copper (partial routes survive a re-run)
    # plus pad 0's cells; every other pad routes to the nearest existing
    # copper of this net (Steiner-style growth).
    tree = net_tree_cells(board, netname)
    for la in pads[0][4]:
        tree.add((round(pads[0][2] / GRID), round(pads[0][3] / GRID), la))
    remaining = pads[1:]
    ok = fail = 0
    failed = []
    while remaining:
        ts = list(tree)
        if len(ts) > 1200:
            ts = ts[:: len(ts) // 1200]

        def dtree(p):
            return min(math.hypot(p[2] - c[0] * GRID, p[3] - c[1] * GRID) for c in ts)

        p = min(remaining, key=dtree)
        # already connected: copper already runs through this pad's cells
        if any((round(p[2] / GRID), round(p[3] / GRID), la) in tree for la in p[4]):
            ok += 1
            remaining.remove(p)
            continue
        tcell = min(tree, key=lambda c: math.hypot(p[2] - c[0] * GRID, p[3] - c[1] * GRID))
        done = False
        for margin in (9.0, 25.0, 45.0):
            for la in p[4]:
                path = astar(obs, (p[2], p[3], la),
                             (tcell[0] * GRID, tcell[1] * GRID, tcell[2]),
                             netname, cls, w, margin, goals=tree)
                if path:
                    sp = simplify(path)
                    if not path_clear(obs, sp, netname, cls, w / 2):
                        continue    # segment-level check failed — try next layer/margin
                    emit(board, obs, sp, netname, w)
                    tree |= raster_cells(sp)
                    ok += 1
                    done = True
                    break
            if done:
                break
        if not done:
            fail += 1
            failed.append(p)
            print(f"    FAIL {netname}: {p[0]}.{p[1]}", flush=True)
        remaining.remove(p)
    return ok, fail, failed


def net_tree_cells(board, net):
    """Grid cells of a net's existing on-board copper (tracks + vias) plus
    its seed pad — the valid connection targets for tree growth."""
    cells = set()
    for t in board.GetTracks():
        if t.GetNetname() != net:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            for ly in ROUTE_LAYERS:
                cells.add((round(p.x / MM / GRID), round(p.y / MM / GRID), ly))
        else:
            s, e = t.GetStart(), t.GetEnd()
            ax, ay, bx, by = s.x / MM, s.y / MM, e.x / MM, e.y / MM
            n = max(1, int(math.hypot(bx - ax, by - ay) / GRID))
            for k in range(n + 1):
                cells.add((round((ax + (bx - ax) * k / n) / GRID),
                           round((ay + (by - ay) * k / n) / GRID), t.GetLayer()))
    return cells


def route_pad(board, obs, netname, pad):
    """Connect a single pad to its net's existing copper."""
    cls = class_of(board, netname)
    _, w = CLASS_RULE.get(cls, (DEFAULT_CLR, DEFAULT_W))
    tree = net_tree_cells(board, netname)
    if not tree:
        p0 = pads_of_net(board, netname)[0]
        for la in p0[4]:
            tree.add((round(p0[2] / GRID), round(p0[3] / GRID), la))
    if any((round(pad[2] / GRID), round(pad[3] / GRID), la) in tree for la in pad[4]):
        return True    # copper already reaches this pad
    tcell = min(tree, key=lambda c: math.hypot(pad[2] - c[0] * GRID, pad[3] - c[1] * GRID))
    for margin in (9.0, 25.0, 45.0):
        for la in pad[4]:
            path = astar(obs, (pad[2], pad[3], la),
                         (tcell[0] * GRID, tcell[1] * GRID, tcell[2]),
                         netname, cls, w, margin, goals=tree)
            if path:
                sp = simplify(path)
                if not path_clear(obs, sp, netname, cls, w / 2):
                    continue
                emit(board, obs, sp, netname, w)
                return True
    return False


def main():
    infile = sys.argv[1]
    outfile = infile
    only = None
    i = 2
    while i < len(sys.argv):
        if sys.argv[i] == "--only":
            only = set(open(sys.argv[i + 1]).read().split())
            i += 2
        elif sys.argv[i] == "--out":
            outfile = sys.argv[i + 1]
            i += 2
        else:
            i += 1
    board = pcbnew.LoadBoard(infile)
    obs = Obstacles(board)

    nets = defaultdict(int)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if n and not n.startswith(SKIP_NET_PREFIXES):
                nets[n] += 1
    nets = {n: c for n, c in nets.items() if c > 1}
    if only is not None:
        nets = {n: c for n, c in nets.items() if n in only}

    def span(n):
        ps = pads_of_net(board, n)
        xs = [p[2] for p in ps]
        ys = [p[3] for p in ps]
        return math.hypot(max(xs) - min(xs), max(ys) - min(ys))

    hv = [n for n in nets if class_of(board, n) == "HV_ELECTRODE"]
    elv = [n for n in nets if class_of(board, n) == "ELECTRODE_LV"]
    usb = [n for n in nets if class_of(board, n) == "USB_90R"]
    pours = {"GND", "+3V3", "+3V3_ANA", "+3V3_ESP", "VBAT", "VBAT_SYS",
             "VBUS", "VDD_CP2102", "TX_5V", "TX_5V_RAW", "+1V8", "+1V8_LDO"}
    handled = set(hv) | set(elv) | set(usb) | pours
    # short, local nets first — long buses route around them later.
    # An explicit --only list forces pour nets to route as well (pour-net
    # stubs to a pour edge/plane via are how the tail-end opens close).
    rest = sorted(
        (n for n in nets if n not in handled or (only is not None and n in pours)),
        key=lambda n: (nets[n], span(n)))

    print(f"== routing: {len(hv)} HV, {len(elv)} ELV, {len(usb)} USB, {len(rest)} misc ==", flush=True)
    ok = fail = 0
    open_pads = []   # (netname, pad) still unrouted
    label_of = {}
    for label, group in (("HV", hv), ("ELV", elv), ("USB", usb), ("misc", rest)):
        for n in group:
            label_of[n] = label
            o, f, fp = route_net(board, obs, n)
            ok, fail = ok + o, fail + f
            open_pads += [(n, p) for p in fp]
            if f or label != "misc":
                print(f"  [{label}] {n}: {o} ok {f} fail", flush=True)

    # ---- rip-up rounds: retry open pads; if still sealed, tear up the
    # misc-net copper blocking the pad's escape ring and re-route it ----
    NEVER_RIP = set(hv) | set(elv) | set(usb)
    ripped_ever = set()

    def sealers_of(pad):
        """Foreign nets whose tracks/vias sit within 1.4 mm of the pad."""
        px, py = pad[2], pad[3]
        out = set()
        for ly in ROUTE_LAYERS:
            for bx in range(int((px - 3.4) // BUCKET), int((px + 3.4) // BUCKET) + 1):
                for by in range(int((py - 3.4) // BUCKET), int((py + 3.4) // BUCKET) + 1):
                    for sx0, sy0, sx1, sy1, sw, snet in obs.seg_buckets[ly].get((bx, by), ()):
                        if snet in EMITTED and snet not in NEVER_RIP and snet not in obs.dead \
                                and seg_dist(sx0, sy0, sx1, sy1, px, py) < 1.4:
                            out.add(snet)
        for bx in range(int((px - 3.4) // BUCKET), int((px + 3.4) // BUCKET) + 1):
            for by in range(int((py - 3.4) // BUCKET), int((py + 3.4) // BUCKET) + 1):
                for vx, vy, vr, vnet in obs.via_buckets.get((bx, by), ()):
                    if vnet in EMITTED and vnet not in NEVER_RIP and vnet not in obs.dead \
                            and math.hypot(vx - px, vy - py) < 1.4:
                        out.add(vnet)
        return out

    def rip(net):
        for it in EMITTED.pop(net, []):
            board.Remove(it)
        obs.dead.add(net)

    for rnd in range(0 if "--no-ripup" in sys.argv else 4):
        if not open_pads:
            break
        print(f"== rip-up round {rnd + 1}: {len(open_pads)} open ==", flush=True)
        still = []
        ripped_now = set()
        for n, p in open_pads:
            if route_pad(board, obs, n, p):
                ok += 1
                continue
            se = sealers_of(p) - ripped_ever
            if not se:
                still.append((n, p))
                continue
            for s in se:
                ripped_ever.add(s)
                ripped_now.add(s)
                rip(s)
            print(f"    rip {sorted(se)} for {n}: {p[0]}.{p[1]}", flush=True)
            if route_pad(board, obs, n, p):
                ok += 1
            else:
                still.append((n, p))
        # re-route everything ripped this round AFTER the sweep — stops the
        # rip/re-fail cascade
        for s in sorted(ripped_now, key=lambda n: len(pads_of_net(board, n))):
            o2, f2, fp2 = route_net(board, obs, s)
            ok += o2
            still += [(s, q) for q in fp2]
        open_pads = still
        # checkpoint after every round — a kill never loses committed copper
        pcbnew.SaveBoard(outfile, board)

    pcbnew.SaveBoard(outfile, board)
    print(f"== done: {ok} links routed, {len(open_pads)} open ==", flush=True)


if __name__ == "__main__":
    main()
