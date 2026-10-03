#!/usr/bin/env python3
"""Split the board's routable nets into 4 agent buckets:
  A: HV + ELV + USB (critical/constrained corridors)
  B/C/D: misc nets partitioned by pad-centroid region (minimize cross-agent
  track overlap in the merge step).
Writes build/nets_{A,B,C,D}.txt
"""
import sys
from collections import defaultdict

import pcbnew

MM = 1e6


def main():
    board = pcbnew.LoadBoard(sys.argv[1])
    nets = defaultdict(list)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if not n or n.startswith("unconnected"):
                continue
            pos = p.GetPosition()
            nets[n].append((pos.x / MM, pos.y / MM))
    nets = {n: v for n, v in nets.items() if len(v) > 1}
    pours = {"GND", "+3V3", "+3V3_ANA", "+3V3_ESP", "VBAT", "VBAT_SYS",
             "VBUS", "VDD_CP2102", "TX_5V", "TX_5V_RAW", "+1V8", "+1V8_LDO"}
    a, b, c, d = [], [], [], []
    for n, pts in nets.items():
        if n in pours:
            continue
        no = board.FindNet(n)
        cls = (no.GetNetClassName() or "Default") if no else "Default"
        if cls in ("HV_ELECTRODE", "ELECTRODE_LV", "USB_90R"):
            a.append(n)
            continue
        cx = sum(p[0] for p in pts) / len(pts)
        cy = sum(p[1] for p in pts) / len(pts)
        # quadrants, ~balanced: left / right-top / right-bottom
        if cx < 14:
            b.append(n)
        elif cy < 31:
            c.append(n)
        else:
            d.append(n)
    for name, lst in (("A", a), ("B", b), ("C", c), ("D", d)):
        with open(f"build/nets_{name}.txt", "w") as f:
            f.write("\n".join(sorted(lst)))
        print(name, len(lst), "nets")


if __name__ == "__main__":
    main()
