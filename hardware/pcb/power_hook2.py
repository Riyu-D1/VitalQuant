#!/usr/bin/env python3
"""Power-rail hookup pass for the VitalQ hw_v1 board.

For every pad on an In2 island net (plus GND) that its pour cannot reach
without a layer change, drop a 0.6/0.3 via:

  * pad centre inside its net's In2 island -> via-in-pad at the pad centre
    (a through via spans F..B, so it reaches In2 from either side; the
    "F<->In2 / B<->In2 span" requirement is satisfied by the full barrel)
  * pad outside the island                 -> 0.2 mm stub on the pad's own
    outer layer to a via seated EDGE_INSET inside the island boundary,
    max stub length 4 mm; if the edge is farther, or the corridor or the
    via spot fails clearance, the pad is deferred as 'needs track route'
  * GND pad                                -> via at the pad centre into the
    board-wide In1/In4 GND planes (every GND pad gets a drop)

Clearance probe: route_all.Obstacles is the router's own index of pads,
tracks, vias and rule-area keepouts. A point is clear for a 0.6 mm via iff
cell_blocked() is false on every VIA_SPAN_LAYERS layer with half-width
VIA_R — that is via_fits() re-run at our diameter. On top of that, a via
centre must NEVER land inside a foreign-net In2 island rect: the bounded
islands are carved out of the board-wide +3V3 pour, so a foreign via there
would sit inside another net's fill. (A foreign pour on a DIFFERENT layer
is fine — fills clear around the barrel — so only In2 rects are tested.)

Usage: power_hook2.py <board.kicad_pcb> <out.kicad_pcb>
"""
import math
import sys
from collections import defaultdict

import pcbnew
import route_all as R

MM = 1_000_000
IN2 = pcbnew.In2_Cu
F_Cu, B_Cu = pcbnew.F_Cu, pcbnew.B_Cu

# Nets that get an island/plane drop. TX_5V_RAW has no island (its pads are
# routed on signal layers) so it can only defer; "+3V3" owns the board-wide
# In2 pour, i.e. every pad is "inside" its island.
HOOK_NETS = {"VBUS", "VDD_CP2102", "VBAT_SYS", "+3V3_ANA", "VBAT",
             "+1V8", "TX_5V", "TX_5V_RAW", "+3V3"}
# In2 pours that span (nearly) the whole board: fills clear around foreign
# vias, so these are never island-rect obstacles for other nets.
BACKGROUND_POURS = {"+3V3"}

VIA_D, VIA_H = 0.6, 0.3
VIA_R = VIA_D / 2
STUB_W = 0.2
STUB_MAX = 4.0
EDGE_INSET = 0.5      # via seats this far inside the island boundary


def mm(v):
    return int(round(v * MM))


def island_rects(board):
    """In2 island rect per net, read back from the zone outlines that are
    actually in the file (zones are unfilled, so bboxes are authoritative)."""
    rects = {}
    for z in board.Zones():
        if z.GetIsRuleArea() or z.GetLayer() != IN2:
            continue
        n = z.GetNetname()
        bb = z.GetBoundingBox()
        r = (bb.GetX() / MM, bb.GetY() / MM,
             (bb.GetX() + bb.GetWidth()) / MM, (bb.GetY() + bb.GetHeight()) / MM)
        if n in rects:
            a = rects[n]
            r = (min(a[0], r[0]), min(a[1], r[1]),
                 max(a[2], r[2]), max(a[3], r[3]))
        rects[n] = r
    return rects


def inside(x, y, r):
    return r[0] <= x <= r[2] and r[1] <= y <= r[3]


def foreign_island(x, y, net, rects):
    """Name of a bounded In2 island of a DIFFERENT net containing (x,y)."""
    for n, (x0, y0, x1, y1) in rects.items():
        if n == net or n in BACKGROUND_POURS:
            continue
        if x0 <= x <= x1 and y0 <= y <= y1:
            return n
    return None


def via_clear(obs, x, y, net, cls):
    """True if a 0.6/0.3 through via at (x,y) clears all foreign copper,
    pads, vias and keepout zones on every copper layer — via_fits() from
    route_all with r = VIA_R (cell_blocked at half-width VIA_R is the same
    test, incl. edge margin, walls and BGA clearance)."""
    for ly in R.VIA_SPAN_LAYERS:
        if obs.cell_blocked(x, y, ly, net, cls, VIA_R):
            return False
    return True


def corridor_clear(obs, x0, y0, x1, y1, layer, net, cls):
    """Sample the stub corridor every GRID for foreign copper on `layer`."""
    n = max(1, int(math.hypot(x1 - x0, y1 - y0) / R.GRID))
    for k in range(n + 1):
        if obs.cell_blocked(x0 + (x1 - x0) * k / n,
                            y0 + (y1 - y0) * k / n,
                            layer, net, cls, STUB_W / 2):
            return False
    return True


def pour_covered(zones, p, net, fp):
    """Pad already reached by its own net's filled pour (no-op while the
    board's zones are unfilled; kept so a filled board re-runs cleanly)."""
    pos = p.GetPosition()
    if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
        lays = list(range(6))
    else:
        lays = [B_Cu if fp.IsFlipped() else F_Cu]
    for z in zones:
        if z.GetNetname() != net or not z.IsFilled():
            continue
        for L in lays:
            if z.HasFilledPolysForLayer(L) and z.HitTestFilledArea(L, pos):
                return True
    return False


def drop_via(board, obs, x, y, netobj):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    v.SetWidth(mm(VIA_D))
    v.SetDrill(mm(VIA_H))
    v.SetNet(netobj)
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    board.Add(v)
    obs._put_via((x, y, VIA_R, netobj.GetNetname()))


def main():
    infile, outfile = sys.argv[1], sys.argv[2]
    board = pcbnew.LoadBoard(infile)
    obs = R.Obstacles(board)
    rects = island_rects(board)
    zones = list(board.Zones())

    vias = defaultdict(int)      # net -> vias placed
    stubs = defaultdict(int)     # net -> vias reached through a stub
    hooked = defaultdict(int)    # net -> pads already connected, skipped
    deferred = defaultdict(list)  # net -> [(pad, reason)]

    for fp in board.GetFootprints():
        side = B_Cu if fp.IsFlipped() else F_Cu
        ref = fp.GetReference()
        for p in fp.Pads():
            net = p.GetNetname()
            if net != "GND" and net not in HOOK_NETS:
                continue
            attr = p.GetAttribute()
            if attr == pcbnew.PAD_ATTRIB_NPTH:
                continue
            name = f"{ref}.{p.GetName()}"
            pos = p.GetPosition()
            x, y = pos.x / MM, pos.y / MM
            r = rects.get(net)          # None for GND and TX_5V_RAW
            pth = attr == pcbnew.PAD_ATTRIB_PTH

            # --- already hooked? --------------------------------------
            if obs.own_via_at(x, y, net):
                hooked[net] += 1
                continue
            if pth and (net == "GND" or (r is not None and inside(x, y, r))):
                hooked[net] += 1        # barrel reaches the plane/island
                continue
            # NOTE: GetConnectedPads() is deliberately NOT used — same-XY
            # pad pairs (e.g. J1.A9/J1.B4) form a cluster that touches
            # nothing but itself, so "connected" there does not mean the
            # island/plane is reached.
            if pour_covered(zones, p, net, fp):
                hooked[net] += 1
                continue

            netobj = p.GetNet() or board.FindNet(net)
            cls = R.class_of(board, net)

            # --- case 1/3: pad inside its island (or GND) -> via-in-pad -
            if net == "GND" or (r is not None and inside(x, y, r)):
                f = foreign_island(x, y, net, rects)
                if f is not None:
                    deferred[net].append(
                        (name, f"pad centre inside {f} In2 island"))
                elif not via_clear(obs, x, y, net, cls):
                    deferred[net].append((name, "via blocked at pad centre"))
                else:
                    drop_via(board, obs, x, y, netobj)
                    vias[net] += 1
                continue

            # --- case 2: outside the island -> stub to edge + via -------
            if r is None:
                deferred[net].append((name, "no island pour; needs track route"))
                continue
            bx = min(max(x, r[0]), r[2])   # nearest point on island rect
            by = min(max(y, r[1]), r[3])
            cx, cy = (r[0] + r[2]) / 2, (r[1] + r[3]) / 2
            dx, dy = cx - bx, cy - by
            d = math.hypot(dx, dy)
            if d == 0:
                deferred[net].append((name, "degenerate island rect"))
                continue
            vx = bx + EDGE_INSET * dx / d  # seat the via inside the island
            vy = by + EDGE_INSET * dy / d
            stub_len = math.hypot(vx - x, vy - y)
            if stub_len > STUB_MAX:
                deferred[net].append(
                    (name, f"island edge {stub_len:.2f} mm away; needs track route"))
                continue
            f = foreign_island(vx, vy, net, rects)
            if f is not None:
                deferred[net].append(
                    (name, f"edge via inside {f} In2 island; needs track route"))
                continue
            if not via_clear(obs, vx, vy, net, cls):
                deferred[net].append(
                    (name, "edge via spot blocked; needs track route"))
                continue
            if not corridor_clear(obs, x, y, vx, vy, side, net, cls):
                deferred[net].append(
                    (name, "stub corridor blocked; needs track route"))
                continue
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(pcbnew.VECTOR2I(mm(x), mm(y)))
            t.SetEnd(pcbnew.VECTOR2I(mm(vx), mm(vy)))
            t.SetWidth(mm(STUB_W))
            t.SetLayer(side)
            t.SetNet(netobj)
            board.Add(t)
            obs._put_seg(side, (x, y, vx, vy, STUB_W / 2, net))
            drop_via(board, obs, vx, vy, netobj)
            vias[net] += 1
            stubs[net] += 1

    pcbnew.SaveBoard(outfile, board)

    print("== power_hook2 summary ==")
    total_v = total_d = 0
    for net in sorted(set(list(vias) + list(hooked) + list(deferred))):
        nd = len(deferred[net])
        total_v += vias[net]
        total_d += nd
        print(f"  {net:12s} vias {vias[net]:3d} (stub {stubs[net]:3d})  "
              f"already-hooked {hooked[net]:3d}  deferred {nd}")
    print(f"  total: {total_v} vias placed, {total_d} pads deferred")
    if total_d:
        print("== deferred pads ==")
        for net in sorted(deferred):
            for name, why in deferred[net]:
                print(f"  {net:12s} {name:10s} {why}")


if __name__ == "__main__":
    main()
