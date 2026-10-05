#!/usr/bin/env python3
"""Deterministic BFS router for the HV corridor nets.

Replaces the A*-waypoint chain: one breadth-first search (same movement
rules as astar — 8-dir + via hops + corner-cut guard) from the source pad
to ANY goal-pad cell. Parent pointers give the exact path; emit direct.
Guaranteed to find a path if one exists — no heuristic pockets.

Usage: route_flood.py board [--nets A,B] [--out f] [--dead NET,NET]
"""
import sys
import math
import time
from collections import deque
import pcbnew
import route_all as ra
import route_hv as rh
import route_hv_all as rha

GRID = ra.GRID
DIRS8 = ((1, 0), (-1, 0), (0, 1), (0, -1),
         (1, 1), (1, -1), (-1, 1), (-1, -1))


def pad_cells(board, ref, num, net, cls, obs, hw):
    fp = board.FindFootprintByReference(ref)
    p = next(p for p in fp.Pads() if str(p.GetNumber()) == str(num))
    bb = p.GetBoundingBox()
    cells = set()
    for l in p.GetLayerSet().Seq():
        nm = board.GetLayerName(l)
        if not nm.endswith('.Cu'):
            continue
        for gx in range(int(bb.GetLeft() / 1e6 / GRID),
                        int(bb.GetRight() / 1e6 / GRID) + 1):
            for gy in range(int(bb.GetTop() / 1e6 / GRID),
                            int(bb.GetBottom() / 1e6 / GRID) + 1):
                cells.add((gx, gy, l))
    return cells


def bfs(obs, seed, goals, net, cls, hw, cap=900000):
    """BFS with parent tracking. Returns cell-path list or None."""
    parent = {c: None for c in seed}
    q = deque(seed)
    hit = None
    while q and len(parent) < cap:
        u = q.popleft()
        if u in goals:
            hit = u
            break
        ux, uy, ul = u
        x, y = ux * GRID, uy * GRID
        for dx, dy in DIRS8:
            v = (ux + dx, uy + dy, ul)
            if v in parent:
                continue
            fx, fy = x + dx * GRID, y + dy * GRID
            if obs.cell_blocked(fx, fy, ul, net, cls, hw):
                continue
            if dx and dy and (
                    obs.cell_blocked(fx, y, ul, net, cls, hw)
                    or obs.cell_blocked(x, fy, ul, net, cls, hw)):
                continue
            parent[v] = u
            q.append(v)
        if obs.own_via_at(x, y, net) or obs.via_fits(x, y, net, cls):
            for oly in ra.ROUTE_LAYERS:
                if oly == ul:
                    continue
                v = (ux, uy, oly)
                if v in parent:
                    continue
                if not obs.cell_blocked(x, y, oly, net, cls, hw):
                    parent[v] = u
                    q.append(v)
    if hit is None:
        return None, len(parent)
    path = []
    c = hit
    while c is not None:
        path.append(c)
        c = parent[c]
    path.reverse()
    return [(cx * GRID, cy * GRID, cl) for cx, cy, cl in path], len(parent)


def main():
    infile = sys.argv[1]
    outfile = infile
    if '--out' in sys.argv:
        outfile = sys.argv[sys.argv.index('--out') + 1]
    only = None
    if '--nets' in sys.argv:
        only = set(sys.argv[sys.argv.index('--nets') + 1].split(','))
    if '--dead' in sys.argv:
        dead_extra = set(
            sys.argv[sys.argv.index('--dead') + 1].split(','))
    else:
        dead_extra = set()
    b = pcbnew.LoadBoard(infile)
    obs = ra.Obstacles(b)
    guard = rh.SlotGuard(obs, b)
    guard.wrap(obs)
    rha.sibling_wrap(b, obs, guard)
    obs.dead |= dead_extra

    results = {}
    for net, (sref, sname), planf in rha.PLANS:
        if only and net not in only:
            continue
        cls = ra.class_of(b, net)
        hw = rha.HW
        _, (gref, gname) = planf()
        seed = pad_cells(b, sref, sname, net, cls, obs, hw)
        goals = pad_cells(b, gref, gname, net, cls, obs, hw)
        t0 = time.time()
        path, explored = bfs(obs, seed, goals, net, cls, hw)
        print(f"[{net}] {sref}.{sname}->{gref}.{gname} "
              f"explored={explored} "
              f"{'path ' + str(len(path)) if path else 'UNREACHABLE'} "
              f"{time.time()-t0:.1f}s", flush=True)
        if path:
            sp = ra.simplify(path)
            wv = rh.path_wall_violation(guard, sp, hw)
            pc = ra.path_clear(obs, sp, net, cls, hw)
            print(f"    simplified={len(sp)} clear={pc} wall={wv}",
                  flush=True)
            if pc and not wv:
                ra.emit(b, obs, sp, net, hw * 2)
                results[net] = 'ROUTED'
            else:
                results[net] = 'OPEN(unsafe-path)'
        else:
            results[net] = 'UNREACHABLE'
        pcbnew.SaveBoard(outfile, b)
    print('== summary ==', flush=True)
    for n, r in results.items():
        print(f'  {n}: {r}', flush=True)
    print(f"rip victims to re-route: {sorted(rha.RIP_VICTIMS | dead_extra)}",
          flush=True)
    print(f'saved {outfile}', flush=True)


if __name__ == '__main__':
    main()
