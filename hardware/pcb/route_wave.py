#!/usr/bin/env python3
"""One parallel routing wave:
  1. merge any finished worker outputs into the base (priority order)
  2. recount truly-open nets on the merged board
  3. split them into N angular shards
  4. launch N detached route_all workers

Usage: route_wave.py <base.kicad_pcb> <merged_out.kicad_pcb> <nshards>
        <worker1.kicad_pcb> [worker2...]
"""
import math
import os
import subprocess
import sys
from collections import defaultdict

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
MM = 1e6


def open_nets(board):
    con = board.GetConnectivity()
    zones = list(board.Zones())

    def covered(p, fp, net):
        pos = p.GetPosition()
        lays = range(6) if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH else [
            pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu]
        for z in zones:
            if z.GetNetname() == net and z.IsFilled():
                for L in lays:
                    if z.HasFilledPolysForLayer(L) and z.HitTestFilledArea(L, pos):
                        return True
        return False

    nets = defaultdict(list)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if not n or n.startswith("unconnected"):
                continue
            nets[n].append((p, fp))
    out = []
    for n, pl in nets.items():
        if len(pl) < 2:
            continue
        iso = [p for p, fp in pl
               if not any(q is not p for q in con.GetConnectedPads(p))
               and not covered(p, fp, n)]
        if iso:
            out.append(n)
    return out


def shard(board, nets, k, prefix):
    cen = defaultdict(list)
    want = set(nets)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname() in want:
                cen[p.GetNetname()].append(
                    (p.GetPosition().x / MM, p.GetPosition().y / MM))
    c = {n: (sum(x for x, _ in v) / len(v), sum(y for _, y in v) / len(v))
         for n, v in cen.items()}
    ang = sorted(c, key=lambda n: math.atan2(c[n][1] - 31, c[n][0] - 20))
    n = len(ang)
    step = (n + k - 1) // k
    files = []
    for i in range(k):
        lst = sorted(ang[i * step:(i + 1) * step])
        f = f"build/{prefix}_{i}.txt"
        with open(f, "w") as fh:
            fh.write("\n".join(lst))
        files.append(f)
        print(prefix, i, len(lst), "nets")
    return files


def main():
    base, merged, k = sys.argv[1], sys.argv[2], int(sys.argv[3])
    workers = sys.argv[4:]
    have = [w for w in workers if os.path.exists(w)]
    if have:
        subprocess.run([PY, "route_merge.py", base, merged] + have,
                       cwd=HERE, check=True)
        base = merged
    board = pcbnew.LoadBoard(base)
    nets = open_nets(board)
    print(len(nets), "nets still open")
    if not nets:
        print("ALL ROUTED")
        return
    files = shard(board, nets, k, "wave")
    for i, f in enumerate(files):
        for sfx in (".kicad_pcb", ".kicad_pro"):
            src = base if sfx == ".kicad_pcb" else \
                base.rsplit(".kicad_pcb", 1)[0] + ".kicad_pro"
            subprocess.run(["cp", src, f"/tmp/wave_{i}{sfx}"])
        subprocess.run(
            f"nohup {PY} route_all.py /tmp/wave_{i}.kicad_pcb --only {f} "
            f"--out /tmp/waveout_{i}.kicad_pcb > /tmp/waverun_{i}.log 2>&1 &",
            shell=True, cwd=HERE)
        subprocess.run(["cp", os.path.join(HERE, "vitalq_hw_v1.kicad_pro"),
                        f"/tmp/waveout_{i}.kicad_pro"])
    print("launched", len(files), "wave workers")


if __name__ == "__main__":
    main()
