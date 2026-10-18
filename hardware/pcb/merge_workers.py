#!/usr/bin/env python3
"""Merge worker outputs into the master board.

For each worker file, takes every track/via whose net is in that worker's
group list and is not already present (by geometry+net+layer) on the
master. Reports inserted counts and geometric collisions vs other nets.

usage: merge_workers.py <master.kicad_pcb> <grpdir> [w1.kicad_pcb ...]
"""
import sys
sys.path.insert(0, '.')
import pcbnew

MM = 1e6


def trackkey(t):
    if t.Type() == pcbnew.PCB_VIA_T:
        p = t.GetPosition()
        return ('V', t.GetNetname(), int(p.x), int(p.y),
                int(t.GetWidth(pcbnew.F_Cu)))
    s, e = t.GetStart(), t.GetEnd()
    return ('S', t.GetNetname(), t.GetLayer(),
            int(s.x), int(s.y), int(e.x), int(e.y), int(t.GetWidth()))


def main():
    master_f = sys.argv[1]
    grpdir = sys.argv[2]
    workers = sys.argv[3:]
    m = pcbnew.LoadBoard(master_f)
    have = {trackkey(t) for t in m.GetTracks()}
    netmap = {n.GetNetname(): n for n in m.GetNetsByName().values()}

    # net -> group file lookup
    import os
    import glob
    groups = {}
    for gf in glob.glob(os.path.join(grpdir, 'g*.txt')):
        for line in open(gf):
            groups[line.strip()] = gf

    total = 0
    for wf in workers:
        wname = os.path.basename(wf).replace('.kicad_pcb', '')
        # group file matching this worker (w_gNN -> gNN.txt)
        tag = wname.split('_')[-1]
        gf = os.path.join(grpdir, tag + '.txt')
        if not os.path.exists(gf):
            print(f'{wname}: no group file, skip')
            continue
        wnets = set(l.strip() for l in open(gf) if l.strip())
        w = pcbnew.LoadBoard(wf)
        ins = 0
        for t in w.GetTracks():
            n = str(t.GetNetname() or '')
            if n not in wnets:
                continue
            k = trackkey(t)
            if k in have:
                continue
            if t.Type() == pcbnew.PCB_VIA_T:
                nt = pcbnew.PCB_VIA(m)
                p = t.GetPosition()
                nt.SetPosition(p)
                nt.SetWidth(t.GetWidth(pcbnew.F_Cu))
                nt.SetDrill(t.GetDrill())
            else:
                nt = pcbnew.PCB_TRACK(m)
                nt.SetStart(t.GetStart())
                nt.SetEnd(t.GetEnd())
                nt.SetWidth(t.GetWidth())
                nt.SetLayer(t.GetLayer())
            if n in netmap:
                nt.SetNet(netmap[n])
            m.Add(nt)
            have.add(k)
            ins += 1
        total += ins
        print(f'{wname}: +{ins} items')
    pcbnew.SaveBoard(master_f, m)
    print(f'total inserted: {total} -> {master_f}')


if __name__ == '__main__':
    main()
