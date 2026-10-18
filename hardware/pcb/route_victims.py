#!/usr/bin/env python3
"""Post-HV sweep: reconnect every net that lost connectivity during the
corridor surgery (victim nets) plus any other still-open pads.

Strategy per open net:
  1. Build island set (flood over same-net copper+pads).
  2. For each pair of islands holding pads, run A* between their nearest
     cells (multi-goal: reach ANY cell of the bigger island).
  3. Emit straight into the board — no arbitration, no merge loss.

Bounded margins, per-link progress printing, checkpoint save every
N commits so a kill never loses banked copper.

Usage: route_victims.py board.kicad_pcb [--nets NET,NET] [--maxiters 40]
"""
import sys
import math
import time
import pcbnew
import route_all as ra
import route_hv as rh
import route_hv_all as rha

GRID = ra.GRID


def net_islands(board, net):
    """Flood-fill same-net copper+pads -> list of cell-sets."""
    items = []
    for t in board.GetTracks():
        if t.GetNetname() == net:
            items.append(t)
    pads = ra.pads_of_net(board, net)
    # Rasterize items to cells on their layers
    cells = set()
    padcells = set()
    ALL_LY = (0, 2, 4, 6, 8, 10)
    for t in items:
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            gx, gy = round(p.x / 1e6 / GRID), round(p.y / 1e6 / GRID)
            rad = int(t.GetWidth() / 1e6 / 2 / GRID) + 1
            ls = {l for l in t.GetLayerSet().Seq() if l in ALL_LY}
            if not ls:
                ls = set(ALL_LY)
            for la in ls:
                for dx in range(-rad, rad + 1):
                    for dy in range(-rad, rad + 1):
                        cells.add((gx + dx, gy + dy, la))
        else:
            s, e = t.GetStart(), t.GetEnd()
            la = t.GetLayer()
            n = max(1, int(math.hypot(
                (e.x - s.x) / 1e6, (e.y - s.y) / 1e6) / GRID))
            for k in range(n + 1):
                cells.add((round((s.x + (e.x - s.x) * k / n) / 1e6 / GRID),
                           round((s.y + (e.y - s.y) * k / n) / 1e6 / GRID),
                           la))
    for ref, pn, px, py, layers in pads:
        fp = board.FindFootprintByReference(ref)
        for p in fp.Pads():
            if str(p.GetName()) != str(pn):
                continue
            bb = p.GetBoundingBox()
            for la in layers:
                gx0 = int(bb.GetLeft() / 1e6 / GRID)
                gx1 = int(bb.GetRight() / 1e6 / GRID) + 1
                gy0 = int(bb.GetTop() / 1e6 / GRID)
                gy1 = int(bb.GetBottom() / 1e6 / GRID) + 1
                for gx in range(gx0, gx1):
                    for gy in range(gy0, gy1):
                        c = (gx, gy, la)
                        cells.add(c)
                        padcells.add(c)
    # flood on 26-neighbourhood (3D: same layer 8-neigh + vertical via cells)
    seen = set()
    islands = []
    for c in cells:
        if c in seen:
            continue
        q = [c]
        seen.add(c)
        isl = []
        while q:
            u = q.pop()
            isl.append(u)
            ux, uy, ul = u
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dl in (-1, 0, 1):
                        if dx == dy == dl == 0:
                            continue
                        if dl != 0 and (dx != 0 or dy != 0):
                            continue
                        v = (ux + dx, uy + dy, ul + dl)
                        if v in cells and v not in seen:
                            seen.add(v)
                            q.append(v)
        islands.append(isl)
    pad_islands = []
    for isl in islands:
        if any(c in padcells for c in isl):
            pad_islands.append(isl)
    return pad_islands, pads


def link_islands(board, obs, net, cls, isl_a, isl_b, w):
    """A* between two cell islands; returns emitted path or None."""
    ta = list(isl_a)
    tb = set(isl_b)
    if len(ta) > 1500:
        ta = ta[:: len(ta) // 1500]
    if len(tb) > 1500:
        tb = set(list(tb)[:: len(tb) // 1500])
    c0 = ta[0]
    best = min(tb, key=lambda c: math.hypot(
        c[0] - c0[0], c[1] - c0[1], (c[2] - c0[2]) * 40))
    for s in ta[:200]:
        g = min(tb, key=lambda c: math.hypot(
            c[0] - s[0], c[1] - s[1], (c[2] - s[2]) * 40))
        for margin in (6.0, 14.0):
            path = ra.astar(obs,
                            (s[0] * GRID, s[1] * GRID, s[2]),
                            (g[0] * GRID, g[1] * GRID, g[2]),
                            net, cls, w, margin, goals=tb)
            if not path:
                continue
            sp = ra.simplify(path)
            if not ra.path_clear(obs, sp, net, cls, w / 2):
                continue
            ra.emit(board, obs, sp, net, w)
            return sp
    return None


def main():
    infile = sys.argv[1]
    outfile = infile
    only = None
    if '--out' in sys.argv:
        outfile = sys.argv[sys.argv.index('--out') + 1]
    if '--nets' in sys.argv:
        only = set(sys.argv[sys.argv.index('--nets') + 1].split(','))
    board = pcbnew.LoadBoard(infile)
    obs = ra.Obstacles(board)
    guard = rh.SlotGuard(obs, board)
    guard.wrap(obs)
    rha.sibling_wrap(board, obs, guard)

    # collect nets with >1 pad-island
    work = []
    for n in board.GetNetsByName().values():
        net = n.GetNetname()
        if only and net not in only:
            continue
        isls, pads = net_islands(board, net)
        if len(pads) < 2:
            continue
        if len(isls) > 1 or not any(
                True for _ in isls):
            work.append((net, isls))
    print(f"{len(work)} nets with fragmented pads", flush=True)

    commits = 0
    still_open = []
    for net, isls in work:
        cls = ra.class_of(board, net)
        w = 0.25 if cls == 'HV_ELECTRODE' else 0.15
        isls.sort(key=len, reverse=True)
        main_isl = set(isls[0])
        ok = True
        for isl in isls[1:]:
            t0 = time.time()
            sp = link_islands(board, obs, net, cls, isl, main_isl, w)
            if sp is None:
                ok = False
                print(f"  {net}: FAIL link ({len(isl)} cells) "
                      f"{time.time()-t0:.1f}s", flush=True)
            else:
                main_isl |= set(isl)
                main_isl |= ra.raster_cells(sp)
                commits += 1
                print(f"  {net}: link +{len(sp)} pts "
                      f"{time.time()-t0:.1f}s", flush=True)
        if not ok:
            still_open.append(net)
        if commits and commits % 25 == 0:
            pcbnew.SaveBoard(outfile, board)
            print(f"  [checkpoint {commits} links]", flush=True)
    pcbnew.SaveBoard(outfile, board)
    print(f"saved {outfile}; {len(still_open)} nets still fragmented: "
          f"{still_open}", flush=True)


if __name__ == '__main__':
    main()
