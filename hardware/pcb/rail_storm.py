#!/usr/bin/env python3
"""Storm pass for pour nets: for every open pad on a pour net, spiral-search
a legal via site near the pad that lands inside the pad's own island/plane,
then emit a short stub + via.

GND pads: any legal via site works (In1/In4 are full-board planes).
In2-island nets: the via XY must fall inside the net's own In2 island.
Pads already connected (biggest connectivity island covers them) are skipped.

Usage: rail_storm.py <board> <out> [--drc <drc.json>]
"""
import math
import sys

import pcbnew

MM = int(1e6)
POURS = {"GND", "+3V3", "+3V3_ANA", "+1V8", "+1V8_LDO", "VBAT", "VBAT_SYS",
         "VBUS", "VDD_CP2102", "TX_5V", "TX_5V_RAW", "+3V3_ESP", "VBIAS0"}
GND_LIKE = {"GND"}
VIA_D, VIA_DRILL, STUB_W = 0.6, 0.3, 0.25
CLR = 0.13           # default class clearance pad margin
SEARCH_R = 2.2       # mm


def mm(v):
    if isinstance(v, tuple):
        return pcbnew.VECTOR2I(int(v[0] * 1e6), int(v[1] * 1e6))
    return int(v * 1e6)


def islands(board):
    """net -> list of (x0,y0,x1,y1) In2 pour rects (unfilled outlines)."""
    out = {}
    for z in board.Zones():
        if z.GetIsRuleArea():
            continue
        if not z.GetLayerSet().Contains(pcbnew.In2_Cu):
            continue
        bb = z.GetBoundingBox()
        out.setdefault(z.GetNetname(), []).append(
            (bb.GetX() / 1e6, bb.GetY() / 1e6,
             (bb.GetX() + bb.GetWidth()) / 1e6, (bb.GetY() + bb.GetHeight()) / 1e6))
    return out


def copper_rects(board):
    """All pad/via/track bboxes per layer-set for clearance probing."""
    rects = []  # (x0,y0,x1,y1,net,span_layers_tuple)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            rects.append((
                bb.GetX() / 1e6, bb.GetY() / 1e6,
                (bb.GetX() + bb.GetWidth()) / 1e6, (bb.GetY() + bb.GetHeight()) / 1e6,
                p.GetNetname(), "pad"))
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            r = t.GetWidth() / 2 / 1e6
            x, y = p.x / 1e6, p.y / 1e6
            rects.append((x - r, y - r, x + r, y + r, t.GetNetname(), "via"))
        else:
            s, e = t.GetStart(), t.GetEnd()
            hw = t.GetWidth() / 2 / 1e6 + 0.01
            ly = t.GetLayerName()
            rects.append((
                min(s.x, e.x) / 1e6 - hw, min(s.y, e.y) / 1e6 - hw,
                max(s.x, e.x) / 1e6 + hw, max(s.y, e.y) / 1e6 + hw,
                t.GetNetname(), ly))
    return rects


def main():
    src, out = sys.argv[1], sys.argv[2]
    board = pcbnew.LoadBoard(src)
    con = board.GetConnectivity()
    isl = islands(board)
    rects = copper_rects(board)

    # which pads are open? Prefer the exact DRC unconnected-pairs report
    # (--drc <json>), else fall back to connectivity-island analysis.
    open_set = set()
    if "--drc" in sys.argv:
        import json, re
        d = json.load(open(sys.argv[sys.argv.index("--drc") + 1]))
        for u in d.get("unconnected_items", []):
            for it in u.get("items", []):
                m = re.match(r"Pad (\S+) \[([^\]]+)\] of (\S+)", it.get("description", ""))
                if m:
                    open_set.add((m.group(3), m.group(1), m.group(2)))
    open_pads = []   # (fp_ref, pad, net, x, y, layer_is_front)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if n not in POURS:
                continue
            if open_set:
                if (fp.GetReference(), p.GetNumber(), n) not in open_set:
                    continue
            else:
                others = 0
                for it in con.GetConnectedItems(p, pcbnew.EXCLUDE_ZONES):
                    if isinstance(it, pcbnew.PAD) and it is not p:
                        others += 1
                if others:
                    continue
            pos = p.GetPosition()
            open_pads.append((fp.GetReference(), p, n,
                              pos.x / 1e6, pos.y / 1e6,
                              p.GetLayerSet().Contains(pcbnew.F_Cu)))
    print(f"open pour-net pads: {len(open_pads)}")

    def own_island(n, x, y):
        if n in GND_LIKE:
            return True
        for x0, y0, x1, y1 in isl.get(n, ()):
            if x0 - 0.3 <= x <= x1 + 0.3 and y0 - 0.3 <= y <= y1 + 0.3:
                return True
        return False

    def foreign_island(n, x, y):
        for net, rr in isl.items():
            if net == n:
                continue
            for x0, y0, x1, y1 in rr:
                if x0 - 0.35 <= x <= x1 + 0.35 and y0 - 0.35 <= y <= y1 + 0.35:
                    return True
        return False

    def via_ok(n, x, y):
        r = VIA_D / 2 + CLR
        if not own_island(n, x, y):
            return False
        if foreign_island(n, x, y):
            return False
        for x0, y0, x1, y1, net, tag in rects:
            if net == n or not net:
                continue
            if x0 - r <= x <= x1 + r and y0 - r <= y <= y1 + r:
                return False
        return True

    placed = deferred = 0
    for ref, pad, n, px, py, front in open_pads:
        net = board.FindNet(n)
        site = None
        for rr in range(0, int(SEARCH_R / 0.15) + 1):
            r = rr * 0.15
            for a in range(16):
                x = px + r * math.cos(a * math.pi / 8)
                y = py + r * math.sin(a * math.pi / 8)
                if via_ok(n, x, y):
                    site = (x, y)
                    break
            if site:
                break
        if site is None:
            deferred += 1
            continue
        vx, vy = site
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(mm((vx, vy)))
        via.SetWidth(mm(VIA_D))
        via.SetDrill(mm(VIA_DRILL))
        via.SetNet(net)
        via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        board.Add(via)
        if (vx, vy) != (px, py):
            tr = pcbnew.PCB_TRACK(board)
            tr.SetStart(mm((px, py)))
            tr.SetEnd(mm((vx, vy)))
            tr.SetWidth(mm(STUB_W))
            tr.SetLayer(pcbnew.F_Cu if front else pcbnew.B_Cu)
            tr.SetNet(net)
            board.Add(tr)
        placed += 1
    pcbnew.SaveBoard(out, board)
    print(f"storm: {placed} via+stub placed, {deferred} deferred")


if __name__ == "__main__":
    main()
