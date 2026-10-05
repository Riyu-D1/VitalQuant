#!/usr/bin/env python3
"""Sequential HV corridor router — probe-proven sequence.
Routes all PLANS nets on the surface lanes, prints per-leg progress,
checkpoints after every net. Usage: route_hv_seq.py board [--out f]"""
import sys
import time
import pcbnew
import route_all as ra
import route_hv as rh
import route_hv_all as rha


def main():
    infile = sys.argv[1]
    outfile = infile
    if '--out' in sys.argv:
        outfile = sys.argv[sys.argv.index('--out') + 1]
    only = None
    if '--nets' in sys.argv:
        only = set(sys.argv[sys.argv.index('--nets') + 1].split(','))
    b = pcbnew.LoadBoard(infile)
    obs = ra.Obstacles(b)
    guard = rh.SlotGuard(obs, b)
    guard.wrap(obs)
    rha.sibling_wrap(b, obs, guard)
    if '--spy' in sys.argv:
        orig = ra.astar
        calls = [0]

        def spy(obs2, start, goal, n, c, w, margin, goals=None):
            calls[0] += 1
            t = time.time()
            r = orig(obs2, start, goal, n, c, w, margin, goals)
            print(f"    astar#{calls[0]} s={start} g={goal} "
                  f"m={margin} -> {len(r) if r else 'NONE'} "
                  f"{time.time()-t:.1f}s", flush=True)
            return r
        ra.astar = spy
    results = {}
    for net, (sref, sname), planf in rha.PLANS:
        if only and net not in only:
            continue
        cls = ra.class_of(b, net)
        sp = rh.find_pad(b, sref, sname)
        wps, (gref, gname) = planf()
        gp = rh.find_pad(b, gref, gname)
        tree = rh.seed_tree(net, sp)
        print(f"[{net}] {sref}.{sname} -> {gref}.{gname}", flush=True)
        ok, t0 = True, time.time()
        for i, wp in enumerate(wps):
            p = rha.leg2(b, obs, guard, net, cls, tree, wp, rha.HW)
            if p is None:
                print(f"    FAIL leg {i} wp={wp[:2]} "
                      f"{time.time()-t0:.1f}s", flush=True)
                ok = False
                break
        if ok:
            p = rh.pad_goal(b, obs, guard, net, cls, tree, gp, rha.HW)
            if p is None:
                print(f"    FAIL goal {time.time()-t0:.1f}s", flush=True)
                ok = False
        results[net] = ok
        print(f"    {'ROUTED' if ok else 'OPEN'} "
              f"{time.time()-t0:.1f}s", flush=True)
        pcbnew.SaveBoard(outfile, b)   # checkpoint after every net
    print("== summary ==", flush=True)
    for n, ok in results.items():
        print(f"  {n}: {'ROUTED' if ok else 'OPEN'}", flush=True)
    print(f"saved {outfile}", flush=True)


if __name__ == '__main__':
    main()
