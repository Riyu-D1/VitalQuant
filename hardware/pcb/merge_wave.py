#!/usr/bin/env python3
"""Merge a whole directory of routed shard boards into one board.

Every *.kicad_pcb in <dir> is merged in filename order through
route_merge.py (priority = filename order — name critical-priority shards
so they sort first, e.g. 00_*.kicad_pcb). Each incoming track/via is
validated against already-committed copper; nets whose copper conflicts
are dropped wholesale for the next arbitration pass.

Prints committed segment/via counts and writes the conflict-dropped net
names (one per line) to build/dropped.txt — feed that file back to
route_all.py --only (optionally via shard_nets.py) for the cleanup wave.

Usage: merge_wave.py <base.kicad_pcb> <out.kicad_pcb> <dir>
"""
import glob
import os
import subprocess
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable


def seg_via_counts(board):
    segs = vias = 0
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            vias += 1
        else:
            segs += 1
    return segs, vias


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    base_p, out_p, src_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    srcs = sorted(glob.glob(os.path.join(src_dir, "*.kicad_pcb")))
    skip = {os.path.abspath(p) for p in (base_p, out_p)}
    srcs = [s for s in srcs if os.path.abspath(s) not in skip]
    if not srcs:
        sys.exit(f"no *.kicad_pcb merge sources in {src_dir}")
    print(f"merging {len(srcs)} boards from {src_dir}:")
    for s in srcs:
        print("  ", os.path.basename(s))

    base_segs, base_vias = seg_via_counts(pcbnew.LoadBoard(base_p))

    # route_merge.py does the priority merge + conflict arbitration; run it
    # from HERE so its build/conflict_nets.txt lands next to dropped.txt.
    r = subprocess.run(
        [PY, os.path.join(HERE, "route_merge.py"), base_p, out_p] + srcs,
        cwd=HERE, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        sys.exit(f"route_merge failed (rc={r.returncode})")

    out_segs, out_vias = seg_via_counts(pcbnew.LoadBoard(out_p))
    print(f"committed: {out_segs - base_segs} segments, "
          f"{out_vias - base_vias} vias "
          f"(board totals: {out_segs} segs / {out_vias} vias)")

    cf = os.path.join(HERE, "build", "conflict_nets.txt")
    dropped = open(cf).read().split() if os.path.exists(cf) else []
    drop_f = os.path.join(HERE, "build", "dropped.txt")
    os.makedirs(os.path.dirname(drop_f), exist_ok=True)
    with open(drop_f, "w") as f:
        f.write("\n".join(dropped))
        if dropped:
            f.write("\n")
    print(f"conflict-dropped nets ({len(dropped)}): "
          f"{', '.join(dropped) if dropped else '(none)'}")
    print("wrote", drop_f)


if __name__ == "__main__":
    main()
