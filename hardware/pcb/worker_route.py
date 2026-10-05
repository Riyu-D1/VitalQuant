#!/usr/bin/env python3
"""Worker: BFS-routes a subset of nets on its own board copy.

Multi-hop: repeatedly grows the net's largest island by BFS to the nearest
other island until connected (up to 12 hops). Optionally forbids new copper
inside the reserved HV lane boxes so victims never block HV corridors.

usage: worker_route.py <board> <outfile> <netlist-file> [--forbid-lanes]"""
import sys
import math
import time
sys.path.insert(0, '.')
import pcbnew
import route_all as ra
import route_hv as rh
import route_hv_all as rha
from route_flood import bfs
from collections import deque

infile, outfile, netfile = sys.argv[1], sys.argv[2], sys.argv[3]
FORBID = '--forbid-lanes' in sys.argv
nets = [l.strip() for l in open(netfile) if l.strip()]
b = pcbnew.LoadBoard(infile)
obs = ra.Obstacles(b)
guard = rh.SlotGuard(obs, b)
guard.wrap(obs)
GRID = ra.GRID

boxes = []
if FORBID:
    for z in b.Zones():
        if z.GetIsRuleArea() and z.GetZoneName() in ('hv_ownlayer', 'hv_inner'):
            bb = z.GetBoundingBox()
            boxes.append((bb.GetLeft() / 1e6, bb.GetTop() / 1e6,
                          bb.GetRight() / 1e6, bb.GetBottom() / 1e6))
    _cb = obs.cell_blocked
    _vf = obs.via_fits
    cur = [None, [], []]      # (net, escape segs, own pad rects)

    def esc_segs(rects):
        """For each own pad inside a lane box, a short escape corridor from
        the pad centre to the nearest box edge (+0.4mm beyond)."""
        segs = []
        for px0, py0, px1, py1 in rects:
            cx, cy = (px0 + px1) / 2, (py0 + py1) / 2
            for x0, y0, x1, y1 in boxes:
                if not (x0 <= cx <= x1 and y0 <= cy <= y1):
                    continue
                cands = [((cx, y0 - 0.4), cy - y0), ((cx, y1 + 0.4), y1 - cy),
                         ((x0 - 0.4, cy), cx - x0), ((x1 + 0.4, cy), x1 - cx)]
                (tx, ty), _ = min(cands, key=lambda c: c[1])
                segs.append((cx, cy, tx, ty))
        return segs

    def cb(x, y, layer, my_net, my_cls, hw):
        if cur[0] is not None and any(
                x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in boxes):
            for sx0, sy0, sx1, sy1 in cur[1]:
                if ra.seg_dist(sx0, sy0, sx1, sy1, x, y) <= hw + 0.45:
                    return _cb(x, y, layer, my_net, my_cls, hw)
            for px0, py0, px1, py1 in cur[2]:
                if px0 - hw <= x <= px1 + hw and py0 - hw <= y <= py1 + hw:
                    return _cb(x, y, layer, my_net, my_cls, hw)
            return True
        return _cb(x, y, layer, my_net, my_cls, hw)

    def vf(x, y, my_net, my_cls):
        if any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in boxes):
            return False
        return _vf(x, y, my_net, my_cls)

    obs.cell_blocked = cb
    obs.via_fits = vf


def net_pad_groups(net):
    groups = []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname() != net:
                continue
            bb = p.GetBoundingBox()
            rects = (bb.GetLeft() / 1e6, bb.GetTop() / 1e6,
                     bb.GetRight() / 1e6, bb.GetBottom() / 1e6)
            cells = set()
            for l in p.GetLayerSet().Seq():
                if l not in obs.buckets:
                    continue
                for gx in range(int(rects[0] / GRID), int(rects[2] / GRID) + 1):
                    for gy in range(int(rects[1] / GRID), int(rects[3] / GRID) + 1):
                        cells.add((gx, gy, l))
            if cells:
                groups.append((rects, cells))
    return groups


def own_cells(net):
    """All grid cells covered by the net's existing copper (tracks+vias)."""
    out = set()
    for t in b.GetTracks():
        if str(t.GetNetname()) != net:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            r = t.GetWidth(pcbnew.F_Cu) / 2 / 1e6
            cx, cy = p.x / 1e6 / GRID, p.y / 1e6 / GRID
            rr = int(r / GRID) + 1
            for dx in range(-rr, rr + 1):
                for dy in range(-rr, rr + 1):
                    for ly in ra.ROUTE_LAYERS:
                        out.add((int(cx) + dx, int(cy) + dy, ly))
        else:
            s, e = t.GetStart(), t.GetEnd()
            path = ra.simplify([(s.x / 1e6, s.y / 1e6, t.GetLayer()),
                                (e.x / 1e6, e.y / 1e6, t.GetLayer())])
            for c in ra.raster_cells(path):
                out.add(c)
    return out


for net in nets:
    cls = ra.class_of(b, net)
    hw = 0.125
    groups = net_pad_groups(net)
    if len(groups) < 2:
        print(f'{net}: <2 pads, skip', flush=True)
        continue
    if FORBID:
        cur[0] = net
        cur[2] = [r for r, _ in groups]
        cur[1] = esc_segs(cur[2])
    t0 = time.time()
    hops = 0
    for hop in range(24):
        oc = own_cells(net)
        un = [(r, c) for r, c in groups if not (c & oc)]
        if not un:
            break
        if oc:
            seed = set(oc)
            for r, c in groups:
                if c & oc:
                    seed |= c
        else:
            seed = set(groups[0][1])
            un = groups[1:]
        goalset = set().union(*(c for _, c in un))
        path, n = bfs(obs, seed, goalset, net, cls, hw, cap=800000)
        hw2 = hw
        if not path:
            hw2 = 0.05
            path, n = bfs(obs, seed, goalset, net, cls, hw2,
                          cap=800000)
        if not path:
            break
        sp = ra.simplify(path)
        pc = ra.path_clear(obs, sp, net, cls, hw2)
        wv = rh.path_wall_violation(guard, sp, hw2)
        if pc and not wv:
            ra.emit(b, obs, sp, net, hw2 * 2)
            hops += 1
        else:
            break
    oc = own_cells(net)
    left = sum(1 for r, c in groups if not (c & oc))
    print(f'{net}: {"DONE" if left == 0 else f"OPEN-{left}pads"} '
          f'hops={hops} ({time.time()-t0:.0f}s)', flush=True)
    pcbnew.SaveBoard(outfile, b)
print('WORKER-DONE', flush=True)
