#!/usr/bin/env python3
"""Shard routable nets into N balanced worker lists.

Input is either a nets file (one net name per line, as consumed by
route_all.py --only) or a .kicad_pcb — for a board, all >1-pad nets are
sharded (same net selection as route_all.main).

Balancing is by total PAD count per shard, not net count. Nets are sorted
by pad-centroid angle around the board centre first, then greedily packed
as contiguous angular sectors — spatially adjacent nets share a shard, so
the routed shard boards fight over the same corridors less at merge time
(route_merge drops conflict losers wholesale).

Usage:
  shard_nets.py <nets-file-or-board.kicad_pcb> <N> [--board <x.kicad_pcb>]

--board is only needed when arg 1 is a nets file and the reference board
isn't vitalq_hw_v1.kicad_pcb in this directory.

Writes build/shard_00.txt .. build/shard_{N-1}.txt (zero-padded).
"""
import math
import os
import sys
from collections import defaultdict

import pcbnew

MM = 1e6
HERE = os.path.dirname(os.path.abspath(__file__))
SKIP_NET_PREFIXES = ("unconnected",)


def net_pads(board):
    """netname -> [(x, y) mm] for every named pad on the board."""
    nets = defaultdict(list)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if not n or n.startswith(SKIP_NET_PREFIXES):
                continue
            pos = p.GetPosition()
            nets[n].append((pos.x / MM, pos.y / MM))
    return nets


def board_centre(board, fallback_pts):
    """Centre of the Edge_Cuts bounding box; mean pad position as fallback."""
    xs, ys = [], []
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts and isinstance(d, pcbnew.PCB_SHAPE):
            bb = d.GetBoundingBox()
            xs += [bb.GetX() / MM, (bb.GetX() + bb.GetWidth()) / MM]
            ys += [bb.GetY() / MM, (bb.GetY() + bb.GetHeight()) / MM]
    if xs:
        return (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    if fallback_pts:
        return (sum(p[0] for p in fallback_pts) / len(fallback_pts),
                sum(p[1] for p in fallback_pts) / len(fallback_pts))
    return 0.0, 0.0


def angular_order(nets, centre):
    """Net names sorted by pad-centroid angle around `centre`.

    The circular order is cut at its largest angular gap so a dense cluster
    isn't split across the first and last shards. Tiebreak by radius then
    name keeps it deterministic."""
    cx, cy = centre
    info = {}
    for n, pts in nets.items():
        mx = sum(p[0] for p in pts) / len(pts)
        my = sum(p[1] for p in pts) / len(pts)
        info[n] = (math.atan2(my - cy, mx - cx),
                   math.hypot(mx - cx, my - cy),
                   len(pts))
    order = sorted(info, key=lambda n: (info[n][0], info[n][1], n))
    if len(order) < 3:
        return order, info
    gaps = []
    for i in range(len(order)):
        a0 = info[order[i]][0]
        a1 = info[order[(i + 1) % len(order)]][0]
        gap = (a1 - a0) % (2 * math.pi)
        gaps.append((gap, i))
    _, cut = max(gaps)          # start just AFTER the widest empty sector
    order = order[cut + 1:] + order[:cut + 1]
    return order, info


def sector_partition(order, pads_of, k):
    """Greedy contiguous-sector bin-packing of the angular order into k
    shards; each shard takes nets until its pad total is as close as
    possible to its fair share of the pads remaining. Returns
    (shards, shard_pads)."""
    shards = [[] for _ in range(k)]
    shard_pads = [0] * k
    i, m = 0, len(order)
    for s in range(k):
        nets_left = m - i
        slots_left = k - s
        if nets_left <= 0:
            break
        if nets_left <= slots_left:          # one net per remaining shard
            for j in range(i, m):
                shards[s + j - i].append(order[j])
                shard_pads[s + j - i] += pads_of[order[j]]
            break
        pads_left = sum(pads_of[n] for n in order[i:])
        target = pads_left / slots_left
        # keep at least one net for every later shard
        cap = nets_left - (slots_left - 1)
        take, cum = 0, 0
        while take < cap:
            nxt = cum + pads_of[order[i + take]]
            take += 1
            if nxt >= target:
                # stop before or after this net — whichever lands nearer target
                if abs(nxt - target) < abs(cum - target):
                    cum = nxt
                else:
                    take -= 1
                break
            cum = nxt
        take = max(take, 1)
        for n in order[i:i + take]:
            shards[s].append(n)
            shard_pads[s] += pads_of[n]
        i += take
    return shards, shard_pads


def partition(order, pads_of, k):
    """Split `order` (angular-sorted) into k shards balanced by pad count.

    Nets too fat to ever share a shard — pad count >= the running fair
    share (e.g. GND, +3V3) — are pulled out LPT-style into dedicated
    shards first; the angular walk then packs only sharable nets, so a
    giant mid-sector can't leave one half of the board overfed and the
    other starving. Returns (shards, shard_pads)."""
    big = []
    rest = list(order)
    while len(big) < k and rest:
        target = sum(pads_of[n] for n in rest) / (k - len(big))
        giant = max(rest, key=lambda n: pads_of[n])
        if pads_of[giant] < target:
            break
        big.append(giant)
        rest.remove(giant)
    if len(big) >= k:                        # degenerate: pure LPT fallback
        shards = [[] for _ in range(k)]
        shard_pads = [0] * k
        for n in sorted(order, key=lambda n: -pads_of[n]):
            s = min(range(k), key=lambda s: shard_pads[s])
            shards[s].append(n)
            shard_pads[s] += pads_of[n]
        return shards, shard_pads
    shards, shard_pads = sector_partition(rest, pads_of, k - len(big))
    for n in big:                            # giants ride in the last shards
        shards.append([n])
        shard_pads.append(pads_of[n])
    return shards, shard_pads


def main():
    args, board_path = [], None
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "--board":
            board_path = sys.argv[i + 1]
            i += 2
        else:
            args.append(sys.argv[i])
            i += 1
    if len(args) != 2:
        sys.exit(__doc__)
    src, k = args[0], int(args[1])
    if k < 1:
        sys.exit("N must be >= 1")

    if src.endswith(".kicad_pcb"):
        board_path = src
        names = None                       # use all >1-pad nets
    else:
        names = open(src).read().split()
        if board_path is None:
            cand = os.path.join(HERE, "vitalq_hw_v1.kicad_pcb")
            if os.path.exists(cand):
                board_path = cand
            else:
                sys.exit("nets file given but no --board and no "
                         "vitalq_hw_v1.kicad_pcb next to the script")

    board = pcbnew.LoadBoard(board_path)
    nets = net_pads(board)
    all_pts = [p for v in nets.values() for p in v]

    if names is None:
        want = {n for n, pts in nets.items() if len(pts) > 1}
        missing = []
    else:
        want = set(names)
        missing = sorted(want - set(nets))
        want &= set(nets)
        if missing:
            print(f"warning: {len(missing)} net(s) not on board "
                  f"(assigned anyway): {missing[:8]}"
                  f"{'...' if len(missing) > 8 else ''}", file=sys.stderr)

    have = {n: nets[n] for n in want}
    order, _ = angular_order(have, board_centre(board, all_pts))
    pads_of = {n: len(v) for n, v in have.items()}
    shards, shard_pads = partition(order, pads_of, k)

    # names missing from the board cost 0 pads — spread them round-robin
    for j, n in enumerate(missing):
        shards[j % k].append(n)

    outdir = os.path.join(HERE, "build")
    os.makedirs(outdir, exist_ok=True)
    w = max(2, len(str(k - 1)))
    print(f"{len(want)} nets / {sum(shard_pads)} pads over {k} shards "
          f"(target {sum(shard_pads) / k:.1f} pads/shard):")
    for s, lst in enumerate(shards):
        f = os.path.join(outdir, f"shard_{s:0{w}d}.txt")
        with open(f, "w") as fh:
            fh.write("\n".join(sorted(lst)))
            if lst:
                fh.write("\n")
        print(f"  shard_{s:0{w}d}: {len(lst):>3} nets, "
              f"{shard_pads[s]:>4} pads")


if __name__ == "__main__":
    main()
