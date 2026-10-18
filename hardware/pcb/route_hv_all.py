#!/usr/bin/env python3
"""Re-seed ALL open HV electrode corridor nets along the verified
far-east skeleton (route_hv.py's RISE+RUN), plus local runs for the
east-edge J11/J13 nets.

Deletes fragmented HV copper first (dead islands + the illegal
pad-bubble intrusions that sealed U16.11/C57.2/TP21.1/U17.5), then
routes each net source->goal with route_hv's leg()/pad_goal() A*.

Usage: pcbnew-python route_hv_all.py vitalq_hw_v1.kicad_pcb [--out X]
"""
import sys
import math

import pcbnew

sys.path.insert(0, "/Users/deepakdiwan/vitalquant-analysis/repo/hardware/pcb")
import route_all as ra
import route_hv as rh

B, F, I2, I3 = pcbnew.B_Cu, pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu
HW = rh.HW

# Decoded lane architecture (from pad LayerSets + hv_ownlayer boxes):
#   B-side sources R32-36/R80-83/R120-122 pad1  -> B.Cu row + B south lane
#   F-side sources R76-79/R118/R119 pad1        -> F.Cu row, then one via
#   B-side J5/J6/J11/J13 pads; F-side J7 pads only.
# hv_inner (In1-4, tracks+vias forbidden) sits under every lane box, so
# via transitions must happen OUTSIDE the boxes: east of x=32.6 (row),
# north of y=58.4 (south band), west of x=41 (east channel).
# Row-lane waypoints sit in pad gaps (pads x=4.1,10.55,17,23.45,29.9).

ROW_B_GAPS = [7.0, 13.5, 20.0, 26.5, 31.0]   # between B-side resistor pads
EAST_B_CH = [(36.0, 30.0, (B,)), (39.5, 36.0, (B,)), (40.5, 44.0, (B,)),
             (40.5, 50.0, (B,)), (40.0, 55.0, (B,)), (38.0, 58.0, (B,))]

def b_row_plan(src_x, jx, yrow=24.4):
    """B pad -> B row lane east -> B east channel south -> B south lane west."""
    wps = [(x, yrow, (B,)) for x in ROW_B_GAPS if x > src_x + 1.2]
    wps += EAST_B_CH
    wps += [(33.0, 59.3, (B,)), (jx + 2.5, 59.5, (B,))]
    return wps

def p_ecg1():   return (b_row_plan(4.1, 22.0), ("J5", "1"))
def p_ecg2():   return (b_row_plan(10.55, 23.7), ("J5", "2"))
def p_rld():    return (b_row_plan(17.0, 18.6), ("J5", "3"))
def p_afep():   return (b_row_plan(23.45, 16.9), ("J5", "4"))
def p_afen():   return (b_row_plan(29.9, 15.2), ("J5", "5"))

# EDA: F pads on the F row lane -> via F->B east of the row keepout
# (x>32.6) -> B south channel -> J6 B pads.
def eda_plan(src_x, jx):
    wps = [(x, 24.4, (F,)) for x in ROW_B_GAPS if x > src_x + 1.2]
    wps += [(33.6, 25.6, (F, B)), (34.6, 26.6, (B,))]
    wps += EAST_B_CH
    wps += [(33.0, 59.3, (B,)), (jx + 2.5, 59.5, (B,))]
    return wps

def p_eda_se(): return (eda_plan(10.55, 7.9), ("J6", "3"))
def p_eda_re(): return (eda_plan(17.0, 10.1), ("J6", "2"))
def p_eda_ce(): return (eda_plan(4.1, 12.3), ("J6", "1"))
def p_eda_de(): return (eda_plan(23.45, 5.7), ("J6", "4"))

# BIOZ: B pads in the y32-39 band -> east on B -> south channel -> west on
# the south B lane -> via B->F north of the south keepout (y<58.4) ->
# F approach into the J7 F pad.
def bioz_plan(jx):
    return ([(27.5, 39.4, (B,)), (31.0, 39.6, (B,)),
             (35.0, 40.5, (B,))] + EAST_B_CH[1:] +
            [(33.0, 59.3, (B,)), (jx + 2.0, 59.3, (B,)),
             (jx + 0.5, 57.8, (B, F)), (jx + 0.5, 59.0, (F,))])

def p_bioz_fp(): return (bioz_plan(11.2), ("J7", "1"))
def p_bioz_fn(): return (bioz_plan(13.4), ("J7", "2"))
def p_bioz_sp(): return (bioz_plan(15.6), ("J7", "3"))
def p_bioz_sn(): return (bioz_plan(17.8), ("J7", "4"))

# --- east-edge local runs ------------------------------------------------
def p_j11_ce(): # R122.1 B (34.65,60.96) -> J11.3 B (44.8,52.8): all-B
    return ([(38.0, 60.5, (B,)), (41.5, 58.5, (B,)),
             (43.5, 55.5, (B,)), (44.3, 53.5, (B,))],
            ("J11", "3"))

def p_j11_we(): # R120.1 B (34.30,51.46) -> J11.1 B (44.8,57.2): all-B
    return ([(37.5, 52.5, (B,)), (41.0, 54.5, (B,)),
             (43.5, 56.5, (B,))], ("J11", "1"))

def p_j11_re(): # R121.1 B (34.30,42.66) -> J11.2 B (44.8,55.0): all-B
    return ([(38.0, 44.5, (B,)), (41.5, 49.0, (B,)),
             (43.5, 53.5, (B,))], ("J11", "2"))

# J13: F pads -> via F->B right at the source (x~39 < 41 keepout edge) ->
# B south-west -> J13 B pads.
def p_j13_inp(): # R118.1 F (38.66,44.30) -> J13.1 B (30.7,60.4)
    return ([(39.6, 45.5, (F, B)), (39.8, 49.0, (B,)),
             (38.5, 53.0, (B,)), (36.0, 57.0, (B,)),
             (32.5, 59.0, (B,)), (30.7, 59.3, (B,))],
            ("J13", "1"))

def p_j13_inm(): # R119.1 F (38.66,56.50) -> J13.2 B (28.5,60.4)
    return ([(39.8, 57.5, (F, B)), (36.0, 58.5, (B,)),
             (31.5, 59.2, (B,)), (28.5, 59.3, (B,))],
            ("J13", "2"))


JOGS = [(0, 0), (0.4, 0), (-0.4, 0), (0, 0.4), (0, -0.4),
        (0.9, 0), (-0.9, 0), (0, 0.9), (0, -0.9),
        (1.6, 0), (-1.6, 0), (0, 1.6), (0, -1.6),
        (0.9, 0.9), (-0.9, 0.9), (0.9, -0.9), (-0.9, -0.9)]


def sibling_wrap(board, obs, guard):
    """Lane-scoped HV clearance model (approved waiver):
      - inside hv_ownlayer lane boxes (on the layer they cover):
        HV-vs-non-HV clearance = 0.4mm (solder-mask dam, <5V nets)
      - pads on HV-owning footprints (resistor/connector bodies): 0.2mm
      - everywhere else: full 1.5mm.
    Collects lane boxes + sibling refs up front, then wraps
    cell_blocked/via_fits with the scoped relax."""
    refs = set()
    for fp in board.GetFootprints():
        if any(ra.class_of(board, p.GetNetname()) == "HV_ELECTRODE"
               for p in fp.Pads()):
            refs.add(fp.GetReference())
    lanes = []  # (x0,y0,x1,y1,set-of-layer-names)
    inner = []  # hv_inner footprints — surfaces F/B above them are the lanes
    for z in board.Zones():
        if z.GetIsRuleArea():
            bb = z.GetBoundingBox()
            box = (bb.GetLeft() / 1e6, bb.GetTop() / 1e6,
                   bb.GetRight() / 1e6, bb.GetBottom() / 1e6)
            if z.GetZoneName() == "hv_ownlayer":
                lanes.append(box + ({board.GetLayerName(l)
                                     for l in z.GetLayerSet().Seq()},))
            elif z.GetZoneName() == "hv_inner":
                inner.append(box)
    print(f"lane-relax refs={len(refs)} ownlayer={len(lanes)} "
          f"inner={len(inner)}", flush=True)
    inside = guard.inside
    HV = "HV_ELECTRODE"
    SURF = {0, 2, 31}   # F.Cu / B.Cu
    LNAME = {0: "F.Cu", 2: "B.Cu", 4: "In1.Cu", 6: "In2.Cu",
             8: "In3.Cu", 10: "In4.Cu", 31: "B.Cu"}

    def in_lane(x, y, layer):
        if layer in SURF and any(
                x0 <= x <= x1 and y0 <= y <= y1
                for x0, y0, x1, y1 in inner):
            return True
        ln = LNAME.get(layer, "")
        return any(x0 <= x <= x1 and y0 <= y <= y1 and ln in lyset
                   for x0, y0, x1, y1, lyset in lanes)

    def relax(onet, ocls, mycls, tag, in_bga, x, y, layer):
        if tag in refs and ocls != HV:
            return 0.2
        c = obs.pair_clearance(mycls, ocls, in_bga)
        if c > 0.4 and in_lane(x, y, layer):
            return 0.4
        return c

    def cb(x, y, layer, my_net, my_cls, hw):
        if inside(x, y):
            return True
        in_bga = any(x0 <= x <= x1 and y0 <= y <= y1
                     for x0, y0, x1, y1 in obs.bga_rects)
        ex0, ey0, ex1, ey1 = obs.edge
        margin = hw + 0.30
        if not (ex0 + margin <= x <= ex1 - margin
                and ey0 + margin <= y <= ey1 - margin):
            return True
        for x0, y0, x1, y1, onet, tag in obs._cand_rects(x, y, layer):
            if onet == my_net:
                continue
            clr = (0.05 if tag == "keepout" else
                   relax(onet, obs._cls.get(onet, "Default"), my_cls, tag,
                         in_bga, x, y, layer))
            p = clr + hw
            if x0 - p <= x <= x1 + p and y0 - p <= y <= y1 + p:
                return True
        for x0, y0, x1, y1 in obs._cand_walls(x, y):
            p = hw + 0.15
            if x0 - p <= x <= x1 + p and y0 - p <= y <= y1 + p:
                return True
        for sx0, sy0, sx1, sy1, sw, snet in obs._cand_segs(x, y, layer):
            if snet == my_net or snet in obs.dead:
                continue
            rr = sw + relax(snet, obs._cls.get(snet, "Default"), my_cls,
                            "", in_bga, x, y, layer) + hw
            if ra.seg_dist(sx0, sy0, sx1, sy1, x, y) < rr:
                return True
        for vx, vy, vr, vnet in obs._cand_vias(x, y):
            if vnet == my_net or vnet in obs.dead:
                continue
            rr = vr + relax(vnet, obs._cls.get(vnet, "Default"), my_cls,
                            "", in_bga, x, y, layer) + hw
            if (vx - x) ** 2 + (vy - y) ** 2 < rr * rr:
                return True
        return False

    def vf(x, y, my_net, my_cls):
        if inside(x, y):
            return False
        r = ra.VIA_D / 2
        in_bga = any(x0 <= x <= x1 and y0 <= y <= y1
                     for x0, y0, x1, y1 in obs.bga_rects)
        ex0, ey0, ex1, ey1 = obs.edge
        if not (ex0 + r + 0.3 <= x <= ex1 - r - 0.3
                and ey0 + r + 0.3 <= y <= ey1 - r - 0.3):
            return False
        for ly in ra.VIA_SPAN_LAYERS:
            for x0, y0, x1, y1, onet, tag in obs._cand_rects(x, y, ly):
                if onet == my_net:
                    continue
                clr = (0.05 if tag == "keepout" else
                       relax(onet, obs._cls.get(onet, "Default"), my_cls,
                             tag, in_bga, x, y, ly))
                if (x0 - clr - r <= x <= x1 + clr + r
                        and y0 - clr - r <= y <= y1 + clr + r):
                    return False
            for sx0, sy0, sx1, sy1, sw, snet in obs._cand_segs(x, y, ly):
                if snet == my_net or snet in obs.dead:
                    continue
                rr = sw + r + relax(snet, obs._cls.get(snet, "Default"),
                                    my_cls, "", in_bga, x, y, ly)
                if ra.seg_dist(sx0, sy0, sx1, sy1, x, y) < rr:
                    return False
        for vx, vy, vr, vnet in obs._cand_vias(x, y):
            if vnet == my_net or vnet in obs.dead:
                continue
            rr = vr + r + relax(vnet, obs._cls.get(vnet, "Default"),
                                my_cls, "", in_bga, x, y, 0)
            if (vx - x) ** 2 + (vy - y) ** 2 < rr * rr:
                return False
        for x0, y0, x1, y1 in obs._cand_walls(x, y):
            if (x0 - r - 0.15 <= x <= x1 + r + 0.15
                    and y0 - r - 0.15 <= y <= y1 + r + 0.15):
                return False
        return True

    obs.cell_blocked = cb
    obs.via_fits = vf


RIP_VICTIMS = set()   # nets whose items were suppressed to free a corridor


def _corridor_blockers(obs, x0, y0, x1, y1, my_net, radius=1.0):
    """Foreign seg/via nets within `radius` of the segment (x0,y0)-(x1,y1)."""
    nets = set()
    n = max(2, int(math.hypot(x1 - x0, y1 - y0) / 0.4))
    pts = [(x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n)
           for k in range(n + 1)]
    for x, y in pts:
        for _, _, _, _, _, snet in obs._cand_segs(x, y, 0):
            if snet != my_net:
                nets.add(snet)
        for _, _, _, _, _, snet in obs._cand_segs(x, y, 2):
            if snet != my_net:
                nets.add(snet)
        for _, _, _, vnet in obs._cand_vias(x, y):
            if vnet != my_net:
                nets.add(vnet)
    return nets


def _flood(obs, seed_cells, net, cls, hw, cap=200000):
    """BFS over free cells (same rules as astar) from seed cells.
    Returns set of reachable grid cells."""
    from collections import deque
    seen = set(seed_cells)
    q = deque(seed_cells)
    dirs8 = ((1, 0), (-1, 0), (0, 1), (0, -1),
             (1, 1), (1, -1), (-1, 1), (-1, -1))
    while q and len(seen) < cap:
        ux, uy, ul = q.popleft()
        x, y = ux * ra.GRID, uy * ra.GRID
        for dx, dy in dirs8:
            v = (ux + dx, uy + dy, ul)
            if v in seen:
                continue
            fx, fy = x + dx * ra.GRID, y + dy * ra.GRID
            if obs.cell_blocked(fx, fy, ul, net, cls, hw):
                continue
            if dx and dy and (
                    obs.cell_blocked(fx, y, ul, net, cls, hw)
                    or obs.cell_blocked(x, fy, ul, net, cls, hw)):
                continue
            seen.add(v)
            q.append(v)
        if obs.own_via_at(x, y, net) or obs.via_fits(x, y, net, cls):
            for oly in ra.ROUTE_LAYERS:
                if oly == ul:
                    continue
                v = (ux, uy, oly)
                if v not in seen and not obs.cell_blocked(
                        x, y, oly, net, cls, hw):
                    seen.add(v)
                    q.append(v)
    return seen


def leg2(board, obs, guard, net, cls, tree, wp, hw):
    """leg() with wider free-cell reach + bigger A* margin.
    Fallbacks: (1) suppress foreign nets sealing the leg corridor,
    (2) flood-retarget the leg to the nearest reachable cell."""
    x, y = wp[0], wp[1]
    layers = wp[2] if len(wp) > 2 else (B, I2, I3, F)
    w = hw * 2
    for attempt in range(4):
        if _leg2_once(board, obs, guard, net, cls, tree, x, y, layers,
                      hw, w):
            return _last[0]
        if attempt < 2:
            ts = list(tree)
            tcell = min(ts, key=lambda c: __import__('math').hypot(
                x - c[0] * ra.GRID, y - c[1] * ra.GRID))
            bl = _corridor_blockers(obs, x, y, tcell[0] * ra.GRID,
                                    tcell[1] * ra.GRID, net)
            bl = {n for n in bl
                  if ra.class_of(board, n) != 'HV_ELECTRODE'}
            if bl:
                obs.dead |= bl
                RIP_VICTIMS.update(bl)
                print(f"    rip-blockers on leg ({x:.1f},{y:.1f}): "
                      f"{sorted(bl)}", flush=True)
                continue
            # no suppressible blockers -> fall through to flood retarget
        # flood-retarget: find reachable cells near the waypoint
        seed = {(c[0], c[1], c[2]) for c in tree}
        reach = _flood(obs, seed, net, cls, hw)
        cand = [c for c in reach if c[2] in layers]
        if not cand:
            break
        t2 = min(cand, key=lambda c: math.hypot(
            x - c[0] * ra.GRID, y - c[1] * ra.GRID))
        d = math.hypot(x - t2[0] * ra.GRID, y - t2[1] * ra.GRID)
        print(f"    flood-retarget wp ({x:.1f},{y:.1f}) -> "
              f"({t2[0]*ra.GRID:.1f},{t2[1]*ra.GRID:.1f}) d={d:.2f}",
              flush=True)
        if _leg2_once(board, obs, guard, net, cls, tree,
                      t2[0] * ra.GRID, t2[1] * ra.GRID, (t2[2],), hw, w):
            return _last[0]
        break
    return None


_last = [None]


def _leg2_once(board, obs, guard, net, cls, tree, x, y, layers, hw, w):
    _last[0] = None
    for jx, jy in JOGS:
        gc = rh.find_free_cell(obs, x + jx, y + jy, layers, net, cls, hw,
                               rmax=1.4)
        if gc is None:
            continue
        gx, gy, gly = gc
        ts = list(tree)
        if len(ts) > 1500:
            ts = ts[:: len(ts) // 1500]
        tcells = sorted(ts, key=lambda c: __import__('math').hypot(
            gx - c[0] * ra.GRID, gy - c[1] * ra.GRID))[:4]
        goals = set(ts)
        tried = set()
        for tcell in tcells:
            for margin in (10.0, 20.0):
                key = (tcell, margin)
                if key in tried:
                    continue
                tried.add(key)
                path = ra.astar(obs, (gx, gy, gly),
                                (tcell[0] * ra.GRID, tcell[1] * ra.GRID,
                                 tcell[2]),
                                net, cls, w, margin, goals=goals)
                if not path:
                    continue
                sp = ra.simplify(path)
                if not ra.path_clear(obs, sp, net, cls, hw):
                    continue
                if rh.path_wall_violation(guard, sp, hw):
                    continue
                ra.emit(board, obs, sp, net, w)
                tree |= ra.raster_cells(sp)
                _last[0] = sp
                return sp
    return None


PLANS = [
    ("ECG1_PAD",  ("R32", "1"), p_ecg1),
    ("ECG2_PAD",  ("R33", "1"), p_ecg2),
    ("RLD_PAD",   ("R34", "1"), p_rld),
    ("AFE_P_PAD", ("R35", "1"), p_afep),
    ("AFE_N_PAD", ("R36", "1"), p_afen),
    ("EDA_SE_PAD",("R77", "1"), p_eda_se),
    ("EDA_RE_PAD",("R78", "1"), p_eda_re),
    ("EDA_CE_PAD",("R76", "1"), p_eda_ce),
    ("EDA_DE_PAD",("R79", "1"), p_eda_de),
    ("BIOZ_FP_PAD",("R80", "1"), p_bioz_fp),
    ("BIOZ_FN_PAD",("R81", "1"), p_bioz_fn),
    ("BIOZ_SP_PAD",("R82", "1"), p_bioz_sp),
    ("BIOZ_SN_PAD",("R83", "1"), p_bioz_sn),
    ("J11_CE",    ("R122", "1"), p_j11_ce),
    ("J11_WE",    ("R120", "1"), p_j11_we),
    ("J11_RE",    ("R121", "1"), p_j11_re),
    ("J13_INP",   ("R118", "1"), p_j13_inp),
    ("J13_INM",   ("R119", "1"), p_j13_inm),
]


def main():
    infile = sys.argv[1]
    outfile = infile
    if "--out" in sys.argv:
        outfile = sys.argv[sys.argv.index("--out") + 1]
    board = pcbnew.LoadBoard(infile)

    nets = {n for n, _, _ in PLANS}
    killed = 0
    for t in list(board.GetTracks()):
        if t.GetNetname() in nets:
            board.Remove(t)
            killed += 1
    print(f"ripped {killed} fragmented HV items", flush=True)

    if killed:
        pcbnew.SaveBoard(outfile, board)
        print(f"pre-saved {outfile}; re-run to route", flush=True)
        return
    obs = ra.Obstacles(board)
    guard = rh.SlotGuard(obs, board)
    guard.wrap(obs)
    print(f"walls: {len(guard.segs)} segs, {len(guard.loops)} loops", flush=True)

    results = {}
    sibling_wrap(board, obs, guard)
    for net, (sref, sname), planf in PLANS:
        cls = ra.class_of(board, net)
        sp = rh.find_pad(board, sref, sname)
        wps, (gref, gname) = planf()
        gp = rh.find_pad(board, gref, gname)
        tree = rh.seed_tree(net, sp)
        print(f"[{net}] {sref}.{sname} -> {gref}.{gname}", flush=True)
        ok, legs = True, 0
        for i, wp in enumerate(wps):
            path = leg2(board, obs, guard, net, cls, tree, wp, HW)
            if path is None:
                print(f"    FAIL leg {i} wp={wp[:2]}", flush=True)
                ok = False
                break
            legs += 1
        if ok:
            path = rh.pad_goal(board, obs, guard, net, cls, tree, gp, HW)
            if path is None:
                print(f"    FAIL goal -> {gref}.{gname}", flush=True)
                ok = False
            else:
                rh.report_path(net, "goal", path)
        results[net] = (ok, legs)

    print("== summary ==", flush=True)
    for net, (ok, legs) in results.items():
        print(f"  {net}: {'ROUTED' if ok else 'OPEN'} ({legs} legs)", flush=True)
    pcbnew.SaveBoard(outfile, board)
    print(f"saved {outfile}", flush=True)


if __name__ == "__main__":
    main()
