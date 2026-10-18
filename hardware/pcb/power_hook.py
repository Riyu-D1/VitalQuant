#!/usr/bin/env python3
"""Close remaining power-net opens on a routed board:
for every pad on a pour net that is still isolated (no track connection and
not covered by its own net's filled pour), drop a 0.45/0.20 through-via at
the pad centre, or at a nearby offset joined by a short stub track.
The via barrel lands in the net's inner-layer pour / plane, closing the
connection. All placements are clearance-checked with the router's own
obstacle model (via_fits + sampled cell_blocked for stubs).

Usage: power_hook.py <board.kicad_pcb>
"""
import math
import sys
from collections import defaultdict

import pcbnew
import route_all as R

POURS = {"GND", "+3V3", "+3V3_ANA", "+3V3_ESP", "VBAT", "VBAT_SYS", "VBUS",
         "VDD_CP2102", "TX_5V", "TX_5V_RAW", "+1V8", "+1V8_LDO"}


def pour_covered(zones, p, net, fp):
    pos = p.GetPosition()
    if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
        lays = list(range(6))
    else:
        lays = [pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu]
    for z in zones:
        if z.GetNetname() != net or not z.IsFilled():
            continue
        for L in lays:
            if z.HasFilledPolysForLayer(L) and z.HitTestFilledArea(L, pos):
                return True
    return False


def stub_clear(obs, x0, y0, x1, y1, layer, net, cls, hw):
    n = max(1, int(math.hypot(x1 - x0, y1 - y0) / R.GRID))
    for k in range(n + 1):
        px = x0 + (x1 - x0) * k / n
        py = y0 + (y1 - y0) * k / n
        if obs.cell_blocked(px, py, layer, net, cls, hw):
            return False
    return True


def main():
    board = pcbnew.LoadBoard(sys.argv[1])
    con = board.GetConnectivity()
    zones = list(board.Zones())
    obs = R.Obstacles(board)
    pads = defaultdict(list)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if n in POURS:
                pads[n].append((fp.GetReference() + "." + p.GetName(), p, fp))
    placed = 0
    skipped = []
    for net, pl in pads.items():
        if len(pl) < 2:
            continue
        netobj = board.FindNet(net)
        cls = R.class_of(board, net)
        for name, p, fp in pl:
            if any(q is not p for q in con.GetConnectedPads(p)):
                continue                       # already joined
            if pour_covered(zones, p, net, fp):
                continue                       # pour reaches it
            pos = p.GetPosition()
            cx, cy = pos.x / 1e6, pos.y / 1e6
            side = pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu
            spot = (cx, cy, False) if obs.via_fits(cx, cy, net, cls) else None
            if spot is None:
                # spiral outward to ~1.2 mm; stub back to pad centre
                for rad in (0.35, 0.5, 0.7, 0.9, 1.2):
                    for ang in range(0, 360, 30):
                        vx = cx + rad * math.cos(math.radians(ang))
                        vy = cy + rad * math.sin(math.radians(ang))
                        if obs.via_fits(vx, vy, net, cls) and stub_clear(
                                obs, cx, cy, vx, vy, side, net, cls, 0.1):
                            spot = (vx, vy, True)
                            break
                    if spot:
                        break
            if spot is None:
                skipped.append(name)
                continue
            vx, vy, need_stub = spot
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(int(vx * 1e6), int(vy * 1e6)))
            v.SetWidth(int(R.VIA_D * 1e6))
            v.SetDrill(int(R.VIA_H * 1e6))
            v.SetNet(netobj)
            v.SetViaType(pcbnew.VIATYPE_THROUGH)
            board.Add(v)
            obs._put_via((vx, vy, R.VIA_D / 2, net))
            if need_stub:
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pcbnew.VECTOR2I(int(cx * 1e6), int(cy * 1e6)))
                t.SetEnd(pcbnew.VECTOR2I(int(vx * 1e6), int(vy * 1e6)))
                t.SetWidth(int(0.2 * 1e6))
                t.SetLayer(side)
                t.SetNet(netobj)
                board.Add(t)
                obs._put_seg(side, (cx, cy, vx, vy, 0.1, net))
            placed += 1
    pcbnew.SaveBoard(sys.argv[1], board)
    print("placed", placed, "power vias; skipped", len(skipped), skipped[:20])


if __name__ == "__main__":
    main()
