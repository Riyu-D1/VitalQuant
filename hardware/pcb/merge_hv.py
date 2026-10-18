#!/usr/bin/env python3
"""Copy all items of the listed HV nets from src board into dst board.

usage: merge_hv.py <dst> <src> <NET,NET,...>
"""
import sys
sys.path.insert(0, '.')
import pcbnew
from merge_workers import trackkey

dst_f, src_f = sys.argv[1], sys.argv[2]
nets = set(sys.argv[3].split(','))
d = pcbnew.LoadBoard(dst_f)
s = pcbnew.LoadBoard(src_f)
have = {trackkey(t) for t in d.GetTracks()}
netmap = {n.GetNetname(): n for n in d.GetNetsByName().values()}
ins = 0
for t in s.GetTracks():
    n = str(t.GetNetname() or '')
    if n not in nets:
        continue
    k = trackkey(t)
    if k in have:
        continue
    if t.Type() == pcbnew.PCB_VIA_T:
        nt = pcbnew.PCB_VIA(d)
        nt.SetPosition(t.GetPosition())
        nt.SetWidth(t.GetWidth(pcbnew.F_Cu))
        nt.SetDrill(t.GetDrill())
    else:
        nt = pcbnew.PCB_TRACK(d)
        nt.SetStart(t.GetStart())
        nt.SetEnd(t.GetEnd())
        nt.SetWidth(t.GetWidth())
        nt.SetLayer(t.GetLayer())
    if n in netmap:
        nt.SetNet(netmap[n])
    d.Add(nt)
    have.add(k)
    ins += 1
pcbnew.SaveBoard(dst_f, d)
print(f'merged {ins} HV items from {src_f} -> {dst_f}')
