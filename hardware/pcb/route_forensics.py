#!/usr/bin/env python3
"""Classify unrouted pads: SEALED (local component too small -> placement/
geometry problem) vs FAR (component big but target unreachable -> routing)."""
import math
import sys
from collections import deque

import pcbnew
import route_all
from route_all import F_CU, GRID, ROUTE_LAYERS

# re-implement the flood using Obstacles directly

def flood(obs, x, y, ly0, net, cls, hw=0.1, cap=60000):
    s = (round(x / GRID), round(y / GRID), ly0)
    seen = {s}
    dq = deque([s])
    while dq and len(seen) < cap:
        cx, cy, cly = dq.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nk = (cx + dx, cy + dy, cly)
            if nk not in seen and not obs.cell_blocked(
                    nk[0] * GRID, nk[1] * GRID, cly, net, cls, hw):
                seen.add(nk)
                dq.append(nk)
        if obs.own_via_at(cx * GRID, cy * GRID, net) or obs.via_fits(
                cx * GRID, cy * GRID, net, cls):
            for oly in ROUTE_LAYERS:
                if oly == cly:
                    continue
                nk = (cx, cy, oly)
                if nk not in seen and not obs.cell_blocked(
                        cx * GRID, cy * GRID, oly, net, cls, hw):
                    seen.add(nk)
                    dq.append(nk)
    return seen


def main():
    import route_all
    board = pcbnew.LoadBoard(sys.argv[1])
    obs = route_all.Obstacles(board)
    nets = {}
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if n and not n.startswith("unconnected"):
                nets.setdefault(n, []).append((fp.GetReference(), p.GetName(),
                                               p.GetPosition().x / 1e6,
                                               p.GetPosition().y / 1e6,
                                               p.GetLayerSet()))
    cls_cache = {}
    sealed, big = [], []
    for n, pads in nets.items():
        if len(pads) < 2:
            continue
        cls = cls_cache.get(n)
        if cls is None:
            no = board.FindNet(n)
            cls = (no.GetNetClassName() or "Default") if no else "Default"
            cls_cache[n] = cls
        if cls == "HV_ELECTRODE":
            pass
        for ref, pn, x, y, ls in pads:
            l0 = F_CU if ls.Contains(F_CU) else ROUTE_LAYERS[-1]
            comp = flood(obs, x, y, l0, n, cls)
            if len(comp) < 300:
                sealed.append((n, ref, pn, round(x, 1), round(y, 1), len(comp)))
            elif len(comp) < 2000:
                big.append((n, ref, pn, len(comp)))
    print(f"SEALED pads (comp<300): {len(sealed)}")
    for s in sealed:
        print("   ", s)
    print(f"SMALL comps (<2000): {len(big)}")
    for s in big[:40]:
        print("   ", s)


if __name__ == "__main__":
    main()
