#!/usr/bin/env python3
"""Merge liberate-worker boards into a base board.

Order matters because this pcbnew build's SWIG container proxies die
unpredictably after board mutation: replay the rip logs FIRST (they only
ever target pre-existing base copper), then insert new worker copper.

1. replays each worker's .rips file: deletes the matching seg/via from base
   (kind + geometry + net, 0.02 mm tolerance)
2. inserts every track/via present in a worker board but absent from base
   (geometry-keyed dedupe, same as merge_workers.py)
3. writes <out>.heals.txt listing nets whose copper was ripped — those need
   a re-heal pass (their islands may be split)

usage: merge_delta.py <base.kicad_pcb> <out.kicad_pcb> <worker_dir>
"""
import sys
import glob
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
from merge_workers import trackkey

base_path, out_path, wdir = sys.argv[1], sys.argv[2], sys.argv[3]
b = pcbnew.LoadBoard(base_path)
ALL_BASE = list(b.GetTracks())   # snapshot pre-mutation — container API is flaky
have = {trackkey(t) for t in ALL_BASE}
netmap = {n.GetNetname(): n for n in b.GetNetsByName().values()}

# ---- replay rip logs -----------------------------------------------------
harmed = set()
ripped = 0
for rf in sorted(glob.glob(os.path.join(wdir, '*.rips'))):
    for line in open(rf):
        parts = line.split()
        if len(parts) < 3:
            continue
        kind, net = parts[0], parts[1]
        geom = [float(v) for v in parts[2:]]
        hit = None
        for t in ALL_BASE:
            try:
                if kind == 'via' and t.Type() == pcbnew.PCB_VIA_T:
                    p = t.GetPosition()
                    if abs(p.x / 1e6 - geom[0]) < 0.02 and \
                       abs(p.y / 1e6 - geom[1]) < 0.02 and \
                       str(t.GetNetname()) == net:
                        hit = t
                        break
                elif kind == 'seg' and t.Type() != pcbnew.PCB_VIA_T:
                    s, e = t.GetStart(), t.GetEnd()
                    if (abs(s.x / 1e6 - geom[0]) < 0.02
                            and abs(s.y / 1e6 - geom[1]) < 0.02
                            and abs(e.x / 1e6 - geom[2]) < 0.02
                            and abs(e.y / 1e6 - geom[3]) < 0.02
                            and str(t.GetNetname()) == net):
                        hit = t
                        break
            except Exception:
                continue
        if hit is not None:
            b.Remove(hit)
            harmed.add(net)
            ripped += 1

# ---- insert worker copper -------------------------------------------------
ins = 0
for wf in sorted(glob.glob(os.path.join(wdir, 'b*.kicad_pcb'))):
    w = pcbnew.LoadBoard(wf)
    n = 0
    for t in w.GetTracks():
        k = trackkey(t)
        if k in have:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            nt = pcbnew.PCB_VIA(b)
            nt.SetPosition(t.GetPosition())
            nt.SetWidth(t.GetWidth(pcbnew.F_Cu))
            nt.SetDrill(t.GetDrill())
        else:
            nt = pcbnew.PCB_TRACK(b)
            nt.SetStart(t.GetStart())
            nt.SetEnd(t.GetEnd())
            nt.SetWidth(t.GetWidth())
            nt.SetLayer(t.GetLayer())
        nm = str(t.GetNetname() or '')
        if nm in netmap:
            nt.SetNet(netmap[nm])
        b.Add(nt)
        have.add(k)
        ins += 1
        n += 1
    if n:
        print(os.path.basename(wf), n)

with open(out_path + '.heals.txt', 'w') as f:
    f.write('\n'.join(sorted(harmed)) + '\n')
pcbnew.SaveBoard(out_path, b)
print(f'inserted {ins}, ripped {ripped}, harmed-nets -> {out_path}.heals.txt')
