#!/usr/bin/env python3
"""Report unrouted connectivity on the board: per-net missing links.

Uses the connectivity graph: for every net with >1 pad, check which pads
aren't reachable through existing tracks/vias/zones. Prints a compact
per-net list of the pad refs still open, so hand-routing is targeted.

Usage: pcbnew-python unrouted_report.py <board.kicad_pcb>
"""
import sys
from collections import defaultdict

import pcbnew

board = pcbnew.LoadBoard(sys.argv[1])
conn = board.GetConnectivity()

open_links = defaultdict(list)   # net -> [(ref, padname)]
total = 0

for fp in board.GetFootprints():
    ref = fp.GetReference()
    for pad in fp.Pads():
        net = pad.GetNet()
        if net is None or net.GetNetname() == "":
            continue
        name = net.GetNetname()
        if name.startswith("unconnected"):
            continue
        # pads with no connection to any other pad of same net
        others = [
            (fp2.GetReference(), p2.GetName())
            for fp2 in board.GetFootprints()
            for p2 in fp2.Pads()
            if p2.GetNet() is not None and p2.GetNetname() == name and p2 is not pad
        ]
        if not others:
            continue
        if not any(conn.GetConnectedPads(pad)) or (
            {p.GetName() for p in conn.GetConnectedPads(pad)} == {pad.GetName()}
        ):
            open_links[name].append(f"{ref}.{pad.GetName()}")

for net in sorted(open_links):
    pads = open_links[net]
    print(f"{net:<28} {len(pads):>3} open pad(s): {', '.join(pads[:12])}")
    total += len(pads)

print(f"\ntotal isolated pads: {total}")
