#!/usr/bin/env python3
"""Diagnostic: replicate worker_route per-net steps for given nets,
catching exceptions before Py_FinalizeEx can swallow the traceback."""
import sys, os, time, traceback, faulthandler
faulthandler.enable()
sys.path.insert(0, '.')
import pcbnew
import route_all as ra
import route_hv as rh
import route_hv_all as rha
from route_flood import bfs

infile = sys.argv[1]
nets = sys.argv[2].split(',')
b = pcbnew.LoadBoard(infile)
obs = ra.Obstacles(b)
guard = rh.SlotGuard(obs, b)
guard.wrap(obs)
GRID = ra.GRID

def net_pad_groups(net):
    groups = []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname() != net:
                continue
            bb = p.GetBoundingBox()
            rects = (bb.GetLeft()/1e6, bb.GetTop()/1e6,
                     bb.GetRight()/1e6, bb.GetBottom()/1e6)
            cells = set()
            for l in p.GetLayerSet().Seq():
                if l not in obs.buckets:
                    continue
                for gx in range(int(rects[0]/GRID), int(rects[2]/GRID)+1):
                    for gy in range(int(rects[1]/GRID), int(rects[3]/GRID)+1):
                        cells.add((gx, gy, l))
            if cells:
                groups.append((rects, cells))
    return groups

def own_cells(net):
    out = set()
    for t in b.GetTracks():
        if str(t.GetNetname()) != net:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            r = t.GetWidth(pcbnew.F_Cu)/2/1e6
            cx, cy = p.x/1e6/GRID, p.y/1e6/GRID
            rr = int(r/GRID)+1
            for dx in range(-rr, rr+1):
                for dy in range(-rr, rr+1):
                    for ly in ra.ROUTE_LAYERS:
                        out.add((int(cx)+dx, int(cy)+dy, ly))
        else:
            s, e = t.GetStart(), t.GetEnd()
            path = ra.simplify([(s.x/1e6, s.y/1e6, t.GetLayer()),
                                (e.x/1e6, e.y/1e6, t.GetLayer())])
            for c in ra.raster_cells(path):
                out.add(c)
    return out

for net in nets:
    print(f'=== {net} ===', flush=True)
    try:
        cls = ra.class_of(b, net)
        print(f'  cls={cls}', flush=True)
        hw = 0.125
        groups = net_pad_groups(net)
        print(f'  pad_groups={len(groups)}', flush=True)
        if len(groups) < 2:
            print(f'{net}: <2 pads, skip', flush=True)
            continue
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
            print(f'  hop{hop}: seed={len(seed)} goals={len(goalset)}', flush=True)
            path, n = bfs(obs, seed, goalset, net, cls, hw, cap=800000)
            hw2 = hw
            if not path:
                hw2 = 0.05
                path, n = bfs(obs, seed, goalset, net, cls, hw2, cap=800000)
            print(f'  hop{hop}: explored={n} path={"None" if not path else len(path)} ({time.time()-t0:.0f}s)', flush=True)
            if not path:
                break
            sp = ra.simplify(path)
            pc = ra.path_clear(obs, sp, net, cls, hw2)
            wv = rh.path_wall_violation(guard, sp, hw2)
            print(f'  hop{hop}: clear={pc} wall={wv}', flush=True)
            if pc and not wv:
                ra.emit(b, obs, sp, net, hw2*2)
                hops += 1
            else:
                break
        oc = own_cells(net)
        left = sum(1 for r, c in groups if not (c & oc))
        print(f'{net}: {"DONE" if left == 0 else f"OPEN-{left}pads"} hops={hops} ({time.time()-t0:.0f}s)', flush=True)
        pcbnew.SaveBoard(infile, b)
        print('  saved', flush=True)
    except BaseException:
        traceback.print_exc()
        sys.stdout.flush(); sys.stderr.flush()
        os._exit(2)
print('DIAG-DONE', flush=True)
sys.stdout.flush(); sys.stderr.flush()
os._exit(0)
