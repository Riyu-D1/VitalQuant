#!/usr/bin/env python3
"""Merge routed board copies (each from a disjoint net subset) into one board.

Priority merge: sources are imported IN ORDER (A first — critical HV/ELV/USB
copper always wins). Each incoming track/via is validated against already-
committed copper with the router's exact clearance model; violating items are
dropped and their whole net is queued for the sequential arbitration pass
(a partial route is worse than none — drop the whole net's merged items).

Usage: route_merge.py <base.kicad_pcb> <out.kicad_pcb> <routedA> [routedB ...]
"""
import math
import sys
from collections import defaultdict

import pcbnew
import route_all
from route_all import F_CU, ROUTE_LAYERS

MM = 1_000_000


def key_of_track(t):
    if t.Type() == pcbnew.PCB_VIA_T:
        p = t.GetPosition()
        return ("via", round(p.x / 1000), round(p.y / 1000), t.GetNetname() or "")
    s, e = t.GetStart(), t.GetEnd()
    return ("seg", t.GetLayer(), round(s.x / 1000), round(s.y / 1000),
            round(e.x / 1000), round(e.y / 1000), t.GetNetname() or "")


def seg_clear(obs, x0, y0, x1, y1, layer, net, cls, hw):
    """Exact clearance of a candidate segment vs all committed copper —
    same math as route_all.cell_blocked, sampled along the segment."""
    n = max(1, int(math.hypot(x1 - x0, y1 - y0) / route_all.GRID))
    for k in range(n + 1):
        px = x0 + (x1 - x0) * k / n
        py = y0 + (y1 - y0) * k / n
        if obs.cell_blocked(px, py, layer, net, cls, hw):
            return False
    return True


def via_clear(obs, x, y, net, cls):
    r = route_all.VIA_D / 2
    in_bga = any(x0 <= x <= x1 and y0 <= y <= y1
                 for x0, y0, x1, y1 in obs.bga_rects)
    for ly in route_all.VIA_SPAN_LAYERS:
        for x0, y0, x1, y1, onet, tag in obs._cand_rects(x, y, ly):
            if onet == net:
                continue
            clr = (0.05 if tag == "keepout" else
                   obs.pair_clearance(cls, obs._cls.get(onet, "Default"), in_bga))
            if x0 - clr - r <= x <= x1 + clr + r and y0 - clr - r <= y <= y1 + clr + r:
                return False
        for sx0, sy0, sx1, sy1, sw, snet in obs._cand_segs(x, y, ly):
            if snet == net or snet in obs.dead:
                continue
            rr = sw + r + obs.pair_clearance(cls, obs._cls.get(snet, "Default"), in_bga)
            if route_all.seg_dist(sx0, sy0, sx1, sy1, x, y) < rr:
                return False
    for vx, vy, vr, vnet in obs._cand_vias(x, y):
        if vnet == net or vnet in obs.dead:
            continue
        rr = vr + r + obs.pair_clearance(cls, obs._cls.get(vnet, "Default"), in_bga)
        if (vx - x) ** 2 + (vy - y) ** 2 < rr * rr:
            return False
    return True


def main():
    base_path, out_path = sys.argv[1], sys.argv[2]
    base = pcbnew.LoadBoard(base_path)
    obs = route_all.Obstacles(base)
    have = {key_of_track(t) for t in base.GetTracks()}
    added = defaultdict(int)
    bad_nets = set()

    def drop_net(net):
        """Remove all merged items of net (board + dedup keys) and mark it
        dead so its stale spatial-index entries stop blocking later imports."""
        obs.dead.add(net)
        n = 0
        for t in list(base.GetTracks()):
            if t.GetNetname() == net and key_of_track(t) not in HAVE_PRE:
                base.Remove(t)
                have.discard(key_of_track(t))
                n += 1
        return n

    # snapshot of copper that existed before the merge — never removable
    HAVE_PRE = {key_of_track(t) for t in base.GetTracks()}

    for src in sys.argv[3:]:
        b = pcbnew.LoadBoard(src)
        for t in b.GetTracks():
            net = t.GetNetname() or ""
            if not net or net in bad_nets:
                continue
            k = key_of_track(t)
            if k in have:
                continue
            cls = obs._cls.get(net, "Default")
            if t.Type() == pcbnew.PCB_VIA_T:
                p = t.GetPosition()
                x, y = p.x / MM, p.y / MM
                if not via_clear(obs, x, y, net, cls):
                    bad_nets.add(net)
                    drop_net(net)
                    continue
                v = pcbnew.PCB_VIA(base)
                v.SetPosition(p)
                v.SetWidth(t.GetWidth(F_CU))
                v.SetDrill(t.GetDrillValue())
                v.SetNet(base.FindNet(net))
                v.SetViaType(t.GetViaType())
                v.SetLocked(True)
                base.Add(v)
                have.add(k)
                obs._put_via((x, y, t.GetWidth(F_CU) / 2 / MM, net))
                added[net] += 1
            else:
                ly = t.GetLayer()
                if ly not in ROUTE_LAYERS:
                    continue
                s, e = t.GetStart(), t.GetEnd()
                x0, y0, x1, y1 = s.x / MM, s.y / MM, e.x / MM, e.y / MM
                hw = t.GetWidth() / 2 / MM
                if not seg_clear(obs, x0, y0, x1, y1, ly, net, cls, hw):
                    bad_nets.add(net)
                    drop_net(net)
                    continue
                nt = pcbnew.PCB_TRACK(base)
                nt.SetStart(s)
                nt.SetEnd(e)
                nt.SetWidth(t.GetWidth())
                nt.SetLayer(ly)
                nt.SetNet(base.FindNet(net))
                nt.SetLocked(True)
                base.Add(nt)
                have.add(k)
                obs._put_seg(ly, (x0, y0, x1, y1, hw, net))
                added[net] += 1

    print("merged items per net:", dict(added))
    print("dropped-for-arbitration nets:", sorted(bad_nets))
    with open("build/conflict_nets.txt", "w") as f:
        f.write("\n".join(sorted(bad_nets)))
    pcbnew.SaveBoard(out_path, base)
    print("wrote", out_path)


if __name__ == "__main__":
    main()
