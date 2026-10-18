#!/usr/bin/env python3
"""Hand-seeded routing for the 5 unroutable HV electrode nets on sp_A.

Reuses route_all.py's Obstacles/cell_blocked probe, A*, emit helpers.
Differences:
  * Fixes a wall-indexing bug in Obstacles._put_wall: reversed Edge.Cuts
    segments get a degenerate bucket range (sorted() is applied to
    already-inflated endpoints), so slot walls are partly invisible to
    cell_blocked/via_fits.  We rebuild wall_buckets with normalized,
    correctly-inflated segments.
  * Adds a slot-interior guard: points inside a closed interior
    Edge.Cuts loop are blocked even when farther than the wall margin
    (via/track must never sit inside a routed slot).
  * Routes each net leg-by-leg through forensic waypoints using the
    existing A*; emitted copper becomes the goal tree for the next leg
    (Steiner attach), exactly like route_net's growth.

Clearances: HV->non-HV 1.5mm, HV->HV 0.2mm, track width 0.25mm.

Usage: pcbnew-python route_hv.py /tmp/sp_A.kicad_pcb --out /tmp/spA_corr.kicad_pcb
"""
import heapq
import math
import sys
from collections import defaultdict

import pcbnew

sys.path.insert(0, "/Users/deepakdiwan/vitalquant-analysis/repo/hardware/pcb")
import route_all as ra

MM = 1_000_000
GRID = ra.GRID
B, F, I2, I3 = pcbnew.B_Cu, pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu
LN = {F: 'F.Cu', B: 'B.Cu', I2: 'In2', I3: 'In3'}
W_HV = 0.25
HW = W_HV / 2
CLS = "HV_ELECTRODE"

EMITTED = ra.EMITTED  # share the dict so emit() fills it


# ---------------------------------------------------------------- wall fix --
def norm_seg(x0, y0, x1, y1):
    return (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1))


def fix_walls(obs, board):
    """Rebuild obs.wall_buckets with normalized, correctly-inflated segs,
    and return the list of interior wall segs + closed loops for the
    slot-interior guard."""
    obs.wall_buckets = defaultdict(list)
    ex0, ey0, ex1, ey1 = obs.edge
    segs = []
    for d in board.GetDrawings():
        if d.GetLayer() != pcbnew.Edge_Cuts:
            continue
        if not (isinstance(d, pcbnew.PCB_SHAPE) and d.GetShape() == pcbnew.SHAPE_T_SEGMENT):
            continue
        s, e = d.GetStart(), d.GetEnd()
        x0, y0, x1, y1 = s.x / MM, s.y / MM, e.x / MM, e.y / MM
        inside = (ex0 + 0.05 < x0 < ex1 - 0.05 and ex0 + 0.05 < x1 < ex1 - 0.05
                  and ey0 + 0.05 < y0 < ey1 - 0.05 and ey0 + 0.05 < y1 < ey1 - 0.05)
        if not inside:
            continue
        w = norm_seg(x0, y0, x1, y1)
        segs.append(w)
        bx0, by0 = int((w[0] - ra.MAX_INFL) // ra.BUCKET), int((w[1] - ra.MAX_INFL) // ra.BUCKET)
        bx1, by1 = int((w[2] + ra.MAX_INFL) // ra.BUCKET), int((w[3] + ra.MAX_INFL) // ra.BUCKET)
        for bx in range(bx0, bx1 + 1):
            for by in range(by0, by1 + 1):
                obs.wall_buckets[(bx, by)].append(w)
    return segs


def wall_loops(segs):
    """Chain axis-aligned wall segs into closed loops (slot outlines)."""
    adj = defaultdict(list)
    for i, (x0, y0, x1, y1) in enumerate(segs):
        for p in ((x0, y0), (x1, y1)):
            adj[(round(p[0], 3), round(p[1], 3))].append(i)
    loops = []
    used = set()
    for i, s in enumerate(segs):
        if i in used:
            continue
        loop = [i]
        used.add(i)
        cur = (round(s[2], 3), round(s[3], 3))
        start = (round(s[0], 3), round(s[1], 3))
        while cur != start:
            nxt = [j for j in adj.get(cur, ()) if j not in used]
            if not nxt:
                break
            j = nxt[0]
            used.add(j)
            loop.append(j)
            w = segs[j]
            cur = (w[2], w[3]) if (w[0], w[1]) == cur else (w[0], w[1])
            cur = (round(cur[0], 3), round(cur[1], 3))
        if cur == start and len(loop) >= 4:
            loops.append([segs[j] for j in loop])
    return loops


def loop_bbox(loop):
    xs = [w[0] for w in loop] + [w[2] for w in loop]
    ys = [w[1] for w in loop] + [w[3] for w in loop]
    return min(xs), min(ys), max(xs), max(ys)


def point_in_bbox(x, y, bb):
    return bb[0] < x < bb[2] and bb[1] < y < bb[3]


def seg_seg_dist(ax, ay, bx, by, cx, cy, dx, dy):
    """Min distance between segments (ax,ay)-(bx,by) and (cx,cy)-(dx,dy)."""
    def dot(u, v):
        return u[0] * v[0] + u[1] * v[1]
    r = (bx - ax, by - ay)
    s = (dx - cx, dy - cy)
    d = (cx - ax, cy - ay)
    rr = dot(r, r)
    ss = dot(s, s)
    if rr == 0 and ss == 0:
        return math.hypot(dx - ax, dy - ay)
    if rr == 0:
        t = max(0.0, min(1.0, dot((ax - cx, ay - cy), s) / ss))
        return math.hypot(ax - (cx + t * s[0]), ay - (cy + t * s[1]))
    if ss == 0:
        t = max(0.0, min(1.0, -dot(d, r) / rr))
        return math.hypot(cx - (ax + t * r[0]), cy - (ay + t * r[1]))
    # proper intersection test for crossing segs
    den = r[0] * s[1] - r[1] * s[0]
    if den != 0:
        t = (d[0] * s[1] - d[1] * s[0]) / den
        u = (d[0] * r[1] - d[1] * r[0]) / den
        if 0 <= t <= 1 and 0 <= u <= 1:
            return 0.0
    best = 1e18
    # endpoint-to-seg distances
    for (px, py), (ex0, ey0, ex1, ey1) in (
            ((ax, ay), (cx, cy, dx, dy)), ((bx, by), (cx, cy, dx, dy)),
            ((cx, cy), (ax, ay, bx, by)), ((dx, dy), (ax, ay, bx, by))):
        best = min(best, ra.seg_dist(ex0, ey0, ex1, ey1, px, py))
    return best


class SlotGuard:
    """Interior Edge.Cuts walls (correct index) + closed loop interiors."""

    def __init__(self, obs, board):
        self.segs = fix_walls(obs, board)
        self.loops = [loop_bbox(l) for l in wall_loops(self.segs)]

    def inside(self, x, y):
        return any(point_in_bbox(x, y, bb) for bb in self.loops)

    def near(self, x, y, r):
        for x0, y0, x1, y1 in self.segs:
            if x0 - r <= x <= x1 + r and y0 - r <= y <= y1 + r:
                return True
        return False

    def wrap(self, obs):
        orig_cb = obs.cell_blocked
        orig_vf = obs.via_fits
        inside, near = self.inside, self.near

        def cb(x, y, layer, net, cls, hw):
            return inside(x, y) or orig_cb(x, y, layer, net, cls, hw)

        def vf(x, y, net, cls):
            return False if inside(x, y) else orig_vf(x, y, net, cls)

        obs.cell_blocked = cb
        obs.via_fits = vf


# ------------------------------------------------------------- leg routing --
def cell_free(obs, x, y, ly, net, cls, hw):
    return not obs.cell_blocked(x, y, ly, net, cls, hw)


def find_free_cell(obs, x, y, layers, net, cls, hw, rmax=0.7):
    """Spiral-search a free grid cell near (x,y) on each preferred layer.
    Returns (gx, gy, layer) or None."""
    gx, gy = round(x / GRID), round(y / GRID)
    steps = int(rmax / GRID) + 1
    for ly in layers:
        for rad in range(steps + 1):
            # perimeter cells at radius rad
            cand = []
            for dx in range(-rad, rad + 1):
                cand.append((dx, rad))
                cand.append((dx, -rad))
            for dy in range(-rad + 1, rad):
                cand.append((rad, dy))
                cand.append((-rad, dy))
            for dx, dy in cand:
                cx, cy = gx + dx, gy + dy
                if cell_free(obs, cx * GRID, cy * GRID, ly, net, cls, hw):
                    return (cx * GRID, cy * GRID, ly)
    return None


def path_wall_violation(guard, path, hw):
    """Check each emitted segment vs the real wall segs (exact distance)."""
    for i in range(len(path) - 1):
        ax, ay, al = path[i]
        bx, by, bl = path[i + 1]
        if al != bl:
            if guard.near(ax, ay, ra.VIA_D / 2 + 0.15) or guard.inside(ax, ay):
                return f"via at ({ax:.2f},{ay:.2f}) inside/near slot"
            continue
        for (x0, y0, x1, y1) in guard.segs:
            if seg_seg_dist(ax, ay, bx, by, x0, y0, x1, y1) < hw + 0.15:
                return f"seg ({ax:.2f},{ay:.2f})-({bx:.2f},{by:.2f}) crosses wall ({x0:.2f},{y0:.2f})-({x1:.2f},{y1:.2f})"
    return None


def leg(board, obs, guard, net, cls, tree, wp, hw, margin=8.0, jogs=None):
    """Route one leg: attach the waypoint region to the net tree.
    wp = (x, y, [layers]) — layers is a preference list, default all.
    Returns the path emitted, or None."""
    x, y = wp[0], wp[1]
    layers = wp[2] if len(wp) > 2 else (B, I2, I3, F)
    if jogs is None:
        jogs = [(0, 0), (0.3, 0), (-0.3, 0), (0, 0.3), (0, -0.3),
                (0.6, 0), (-0.6, 0), (0, 0.6), (0, -0.6),
                (0.3, 0.3), (-0.3, 0.3), (0.3, -0.3), (-0.3, -0.3),
                (0.6, 0.6), (-0.6, -0.6)]
    w = hw * 2
    for jx, jy in jogs:
        gc = find_free_cell(obs, x + jx, y + jy, layers, net, cls, hw)
        if gc is None:
            continue
        gx, gy, gly = gc
        ts = list(tree)
        if len(ts) > 1500:
            ts = ts[:: len(ts) // 1500]
        tcell = min(ts, key=lambda c: math.hypot(gx - c[0] * GRID, gy - c[1] * GRID))
        path = ra.astar(obs, (gx, gy, gly),
                        (tcell[0] * GRID, tcell[1] * GRID, tcell[2]),
                        net, cls, w, margin, goals=set(ts))
        if not path:
            continue
        sp = ra.simplify(path)
        if not ra.path_clear(obs, sp, net, cls, hw):
            continue
        wv = path_wall_violation(guard, sp, hw)
        if wv:
            continue
        ra.emit(board, obs, sp, net, w)
        tree |= ra.raster_cells(sp)
        return sp
    return None


def pad_goal(board, obs, guard, net, cls, tree, pad, hw):
    """Final leg: attach connector pad to the tree. pad=(ref,name,x,y,layers)."""
    px, py, layers = pad[2], pad[3], pad[4]
    w = hw * 2
    for margin in (8.0, 14.0):
        for la in layers:
            ts = list(tree)
            if len(ts) > 1500:
                ts = ts[:: len(ts) // 1500]
            tcells = sorted(ts, key=lambda c: math.hypot(
                px - c[0] * GRID, py - c[1] * GRID))[:4]
            goals = set(ts)
            for tcell in tcells:
                path = ra.astar(obs, (px, py, la),
                                (tcell[0] * GRID, tcell[1] * GRID, tcell[2]),
                                net, cls, w, margin, goals=goals)
                if not path:
                    continue
                sp = ra.simplify(path)
                if not ra.path_clear(obs, sp, net, cls, hw):
                    continue
                if path_wall_violation(guard, sp, hw):
                    continue
                ra.emit(board, obs, sp, net, w)
                tree |= ra.raster_cells(sp)
                return sp
    return None


def seed_tree(net, pad):
    t = set()
    for la in pad[4]:
        t.add((round(pad[2] / GRID), round(pad[3] / GRID), la))
    return t


def find_pad(board, ref, name):
    for fp in board.GetFootprints():
        if fp.GetReference() != ref:
            continue
        for p in fp.Pads():
            if str(p.GetName()) == str(name):
                pos = p.GetPosition()
                ls = p.GetLayerSet()
                if ls.Contains(pcbnew.In1_Cu):
                    layers = ra.ROUTE_LAYERS
                elif ls.Contains(F):
                    layers = (F,)
                else:
                    layers = (B,)
                return (ref, str(name), pos.x / MM, pos.y / MM, layers)
    raise KeyError(f"{ref}.{name}")


def report_path(net, tag, sp):
    n_seg = sum(1 for i in range(len(sp) - 1) if sp[i][2] == sp[i + 1][2])
    n_via = sum(1 for i in range(len(sp) - 1) if sp[i][2] != sp[i + 1][2])
    lays = sorted({LN[p[2]] for p in sp})
    print(f"    {net} {tag}: {n_seg} seg {n_via} via layers={lays}", flush=True)


# ------------------------------------------------------------------ routes --
# Waypoints along the verified corridor.  (x, y) or (x, y, (layer prefs)).
# Sources escape their pockets to inner layers (B.Cu row is sealed by the
# LV pad wall + slots), run east, rise at x~38-42, run west along y~57-59,
# then drop into each J-pad's interior-facing edge.
RISE = [(34.5, 29.5, (B, I2, I3)), (38.5, 32.0, (B, I2, I3)),
        (40.0, 37.0, (B,)), (40.5, 42.0, (B,)), (41.0, 47.0, (B,)),
        (41.5, 51.5, (B,)), (42.0, 55.0, (B,)), (39.5, 56.6, (B,))]
RUN = [(34.0, 57.3, (B,)), (29.0, 57.8, (B,)), (26.3, 58.9, (B,))]

def plan_ecg1():   # R32.1 (4.10,24.65) -> J5.1 (22.0,60.4)
    return ([(4.1, 23.2, (B,)), (3.9, 22.7, (B,)),
             (8.0, 23.0, (I2, I3, B)), (14.0, 23.5, (I2, I3, B)),
             (20.0, 24.0, (I2, I3, B)), (27.0, 25.0, (I2, I3, B))]
            + RISE + RUN +
            [(23.5, 58.9, (B,))],
            ("J5", "1"))

def plan_ecg2():   # R33.1 (10.55,23.43) -> J5.2 (20.3,60.4)
    return ([(10.8, 24.5, (B,)), (12.3, 26.3, (B,)),
             (13.0, 24.5, (I2, I3, B)), (18.0, 24.5, (I2, I3, B)),
             (26.0, 25.5, (I2, I3, B))]
            + RISE + RUN +
            [(21.0, 59.0, (B,))],
            ("J5", "2"))

def plan_rld():    # R34.1 (17.0,24.65) -> J5.3 (18.6,60.4)
    return ([(15.8, 25.3, (B,)), (16.0, 26.3, (B,)),
             (18.5, 25.5, (I2, I3, B)), (25.0, 26.0, (I2, I3, B))]
            + RISE + RUN +
            [(19.5, 58.9, (B,))],
            ("J5", "3"))

def plan_eda():    # R77.1 (10.55,23.43,F) -> J6.3 (7.9,60.4)
    return ([(10.9, 22.0, (F,)), (13.5, 21.9, (F,)),
             (15.5, 22.5, (I2, I3)), (22.0, 24.0, (I2, I3)), (29.0, 26.0, (I2, I3))]
            + RISE + RUN +
            [(24.0, 58.9, (B,)), (16.0, 59.0, (B,)), (11.0, 59.1, (B,)),
             (9.4, 59.3, (B,))],
            ("J6", "3"))

def plan_bioz():   # R82.1 (17.0,39.26,B) -> J7.3 (15.6,60.35,F)
    return ([(15.5, 38.3, (B,)), (20.5, 38.3, (B,)), (22.0, 40.4, (B,)),
             (24.5, 40.5, (B,)), (26.5, 37.0, (B,)), (13.6, 40.4, (B,)),
             (8.0, 40.5, (B,)), (4.0, 40.0, (B,)), (2.0, 39.0, (B,))],
            ("J7", "3"))

PLANS = [("ECG1_PAD", ("R32", "1"), plan_ecg1),
         ("ECG2_PAD", ("R33", "1"), plan_ecg2),
         ("RLD_PAD", ("R34", "1"), plan_rld),
         ("EDA_SE_PAD", ("R77", "1"), plan_eda),
         ("BIOZ_SP_PAD", ("R82", "1"), plan_bioz)]


def main():
    infile = sys.argv[1]
    outfile = infile
    if "--out" in sys.argv:
        outfile = sys.argv[sys.argv.index("--out") + 1]
    board = pcbnew.LoadBoard(infile)
    obs = ra.Obstacles(board)
    guard = SlotGuard(obs, board)
    guard.wrap(obs)
    print(f"walls: {len(guard.segs)} segs, {len(guard.loops)} closed loops", flush=True)

    results = {}
    for net, (sref, sname), planf in [ (n, s, pf) for n, s, pf in PLANS ]:
        cls = ra.class_of(board, net)
        if cls != CLS:
            print(f"  !! {net} class {cls}", flush=True)
        sp = find_pad(board, sref, sname)
        wps, (gref, gname) = planf()
        gp = find_pad(board, gref, gname)
        tree = seed_tree(net, sp)
        print(f"[{net}] {sref}.{sname} ({sp[2]:.2f},{sp[3]:.2f}) -> {gref}.{gname} ({gp[2]:.2f},{gp[3]:.2f})", flush=True)
        ok = True
        legs = 0
        for i, wp in enumerate(wps):
            path = leg(board, obs, guard, net, cls, tree, wp, HW, margin=9.0)
            if path is None:
                print(f"    FAIL leg {i} wp={wp[:2]}", flush=True)
                ok = False
                break
            legs += 1
        if ok:
            path = pad_goal(board, obs, guard, net, cls, tree, gp, HW)
            if path is None:
                print(f"    FAIL goal leg -> {gref}.{gname}", flush=True)
                ok = False
            else:
                report_path(net, "goal", path)
        results[net] = (ok, legs)

    print("== summary ==", flush=True)
    for net, (ok, legs) in results.items():
        n_items = len(EMITTED.get(net, ()))
        print(f"  {net}: {'ROUTED' if ok else 'OPEN'} ({legs} legs, {n_items} board items)", flush=True)
    pcbnew.SaveBoard(outfile, board)
    print(f"saved {outfile}", flush=True)


if __name__ == "__main__":
    main()
