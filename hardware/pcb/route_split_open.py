#!/usr/bin/env python3
"""Split the still-open nets on a partially routed board into 4 disjoint
worker lists: W1 = HV/ELV/USB + pour nets (critical), W2/W3/W4 = misc nets
partitioned by open-pad centroid. Writes build/open_{1,2,3,4}.txt.

Usage: route_split_open.py <merged.kicad_pcb>
"""
import sys
from collections import defaultdict

import pcbnew

MM = 1e6


def connected_pads(board):
    """net -> set of pad (ref,pad) ids already connected to >=1 other pad."""
    import itertools
    return None


def main():
    board = pcbnew.LoadBoard(sys.argv[1])
    # open pads = pads not reached by any track/via of their net
    conn = defaultdict(set)  # net -> set of pad positions (mm ints) with copper
    for t in board.GetTracks():
        net = t.GetNetname()
        if t.Type() == pcbnew.PCB_VIA_T:
            continue
    # simpler: use connectivity audit
    con = board.GetConnectivity()
    open_pts = defaultdict(list)  # net -> [(x,y)]
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if not n or n.startswith("unconnected"):
                continue
            # pad is open if no other same-net pad shares its cluster
            peers = con.GetConnectedPads(p)
            other = [q for q in peers if q is not p]
            if not other:
                open_pts[n].append(p)
    w1, w2, w3, w4 = [], [], [], []
    for n, pads in open_pts.items():
        no = board.FindNet(n)
        cls = (no.GetNetClassName() or "Default") if no else "Default"
        if cls in ("HV_ELECTRODE", "ELECTRODE_LV", "USB_90R"):
            w1.append(n)
            continue
        cx = sum(p.GetPosition().x / MM for p in pads) / len(pads)
        cy = sum(p.GetPosition().y / MM for p in pads) / len(pads)
        if cx < 14:
            w2.append(n)
        elif cy < 31:
            w3.append(n)
        else:
            w4.append(n)
    for name, lst in (("1", w1), ("2", w2), ("3", w3), ("4", w4)):
        with open(f"build/open_{name}.txt", "w") as f:
            f.write("\n".join(sorted(set(lst))))
        print(name, len(set(lst)), "nets")


if __name__ == "__main__":
    main()
