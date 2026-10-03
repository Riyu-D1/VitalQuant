#!/usr/bin/env python3
"""Finalize the 4-way routed board:
  merge -> rip+re-route conflicts -> retry open pads -> fill pours -> save.

Usage: route_finalize.py <base.kicad_pcb> <final.kicad_pcb> <routed1..4>
"""
import os
import subprocess
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable


def sh(*args):
    print("+", " ".join(os.path.basename(a) if a.endswith(".py") else a
                       for a in args), flush=True)
    r = subprocess.run(args, cwd=HERE)
    if r.returncode != 0:
        sys.exit(r.returncode)


def main():
    base, final = sys.argv[1], sys.argv[2]
    workers = sys.argv[3:]
    merged = "/tmp/hwv2_merged.kicad_pcb"

    # 1. merge + conflict rip (writes build/conflict_nets.txt)
    sh(PY, "route_merge.py", base, merged, *workers)

    # 2. arbitration pass: re-route conflict nets against surviving copper
    if os.path.getsize("build/conflict_nets.txt") > 0:
        sh(PY, "route_all.py", merged, "--only", "build/conflict_nets.txt",
           "--out", merged)

    # 3. retry pass over every net that still has open pads
    r = subprocess.run([PY, "unrouted_report.py", merged], cwd=HERE,
                       capture_output=True, text=True)
    # lines look like:  NETNAME                      N open pad(s): REF.P, ...
    open_nets = sorted({ln.split()[0] for ln in r.stdout.splitlines()
                        if "open pad(s):" in ln})
    if open_nets:
        with open("build/retry_nets.txt", "w") as f:
            f.write("\n".join(open_nets))
        sh(PY, "route_all.py", merged, "--only", "build/retry_nets.txt",
           "--out", merged)

    # 4. fill all pours
    board = pcbnew.LoadBoard(merged)
    pcbnew.ZoneFiller(board).Fill(board.Zones())
    pcbnew.SaveBoard(final, board)
    print("wrote", final, flush=True)


if __name__ == "__main__":
    main()
