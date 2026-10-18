#!/usr/bin/env python3
"""Per-net connectivity audit of routed copper.

Each net with >=2 pads gets a union-find over its pads, tracks, vias and
zone-fill islands; items join when geometry touches (0.05 mm slack) on a
shared copper layer (via/pad layers come from GetLayerSet().Seq(), so a
through via joins everything it spans). Pours count only via *filled*
polygons: an item joins a pour island when a representative point lands
inside a filled outline on a shared layer.

Usage: pcbnew-python verify_connectivity.py <board.kicad_pcb>
Always exits 0. Last line: 'OPEN-NETS: <comma list or NONE>'.
"""
import math
import sys
from collections import defaultdict

import pcbnew

MM = 1_000_000
T = 0.05                     # mm touch slack
CELL = 4.0                   # spatial bucket size
SKIP = ("unconnected",)
PAD_HIT = hasattr(pcbnew.PAD, "HitTest")
ZONE_HIT = hasattr(pcbnew.SHAPE_LINE_CHAIN, "PointInside")


def layers_of(it):
    try:
        return set(it.GetLayerSet().Seq())
    except Exception:
        return {ly for ly in range(64) if it.GetLayerSet().Contains(ly)}


def seg_dist(x0, y0, x1, y1, px, py):
    dx, dy = x1 - x0, y1 - y0
    l2 = dx * dx + dy * dy
    t = 0.0 if not l2 else max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / l2))
    return math.hypot(px - (x0 + t * dx), py - (y0 + t * dy))


class UF:
    def __init__(self):
        self.p = {}

    def find(self, x):
        p = self.p.setdefault(x, x)
        if p != x:
            self.p[x] = p = self.find(p)
        return p

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[ra] = rb


def v2(x, y):
    return pcbnew.VECTOR2I(int(round(x * MM)), int(round(y * MM)))


# Item = (layers, bbox, hitfn, pts). Two items join when they share a
# layer, bboxes meet within T, and a rep point of one is inside the
# other's hitfn (HitTest for pads, distance check for segs/vias,
# PointInside for pour islands).

def pad_item(pad):
    bb = pad.GetBoundingBox()
    x0, y0 = bb.GetX() / MM, bb.GetY() / MM
    x1, y1 = x0 + bb.GetWidth() / MM, y0 + bb.GetHeight() / MM
    pos = pad.GetPosition()
    hit = ((lambda x, y: pad.HitTest(v2(x, y))) if PAD_HIT
           else (lambda x, y: x0 <= x <= x1 and y0 <= y <= y1))
    return (layers_of(pad), (x0, y0, x1, y1), hit,
            [(pos.x / MM, pos.y / MM), (x0, y0), (x1, y0), (x0, y1), (x1, y1)])


def seg_item(t):
    s, e = t.GetStart(), t.GetEnd()
    x0, y0, x1, y1 = s.x / MM, s.y / MM, e.x / MM, e.y / MM
    hw = t.GetWidth() / 2 / MM
    bb = (min(x0, x1) - hw, min(y0, y1) - hw, max(x0, x1) + hw, max(y0, y1) + hw)
    return ({t.GetLayer()}, bb, lambda x, y: seg_dist(x0, y0, x1, y1, x, y) <= hw + T,
            [(x0, y0), (x1, y1), ((x0 + x1) / 2, (y0 + y1) / 2)])


def via_item(t):
    p = t.GetPosition()
    x, y, r = p.x / MM, p.y / MM, t.GetWidth(pcbnew.F_Cu) / 2 / MM
    return (layers_of(t), (x - r, y - r, x + r, y + r),
            lambda px, py: math.hypot(px - x, py - y) <= r + T, [(x, y)])


def zone_items(z):
    """One item per filled outline island, per copper layer."""
    out = []
    for ly in layers_of(z) if ZONE_HIT else ():
        try:
            fp = z.GetFilledPolysList(ly)
        except Exception:
            break
        for i in range(fp.OutlineCount() if fp else 0):
            ch = fp.Outline(i)
            if ch.PointCount() < 3:
                continue
            pts = [(ch.GetPoint(k).x / MM, ch.GetPoint(k).y / MM)
                   for k in range(0, ch.PointCount(), max(1, ch.PointCount() // 32))]
            xs, ys = [q[0] for q in pts], [q[1] for q in pts]
            out.append(({ly}, (min(xs), min(ys), max(xs), max(ys)),
                        lambda x, y, ch=ch: ch.PointInside(v2(x, y)), pts))
    return out


def connected(a, b):
    if not (a[0] & b[0]):                      # shared copper layer?
        return False
    if (a[1][0] > b[1][2] + T or b[1][0] > a[1][2] + T
            or a[1][1] > b[1][3] + T or b[1][1] > a[1][3] + T):
        return False
    return any(b[2](x, y) for x, y in a[3]) or any(a[2](x, y) for x, y in b[3])


def islands_of(items, npads):
    uf = UF()
    buckets = defaultdict(list)
    for i in range(npads):
        uf.find(i)
    for i, it in enumerate(items):
        for cx in range(int(it[1][0] // CELL), int(it[1][2] // CELL) + 1):
            for cy in range(int(it[1][1] // CELL), int(it[1][3] // CELL) + 1):
                lst = buckets[(cx, cy)]
                for j in lst:
                    if connected(items[j], it):
                        uf.union(j, i)
                lst.append(i)
    return len({uf.find(i) for i in range(npads)})


def main():
    board = pcbnew.LoadBoard(sys.argv[1])
    pads, copper = defaultdict(list), defaultdict(list)
    for f in board.GetFootprints():
        for p in f.Pads():
            n = p.GetNetname() or ""
            if n and not n.startswith(SKIP):
                pads[n].append(p)
    for t in board.GetTracks():
        n = t.GetNetname() or ""
        if n and not n.startswith(SKIP):
            copper[n].append(via_item(t) if t.Type() == pcbnew.PCB_VIA_T
                             else seg_item(t))
    zones = unfilled = 0
    for z in board.Zones():
        n = z.GetNetname() or ""
        if n and not n.startswith(SKIP):
            zones += 1
            zi = zone_items(z)
            unfilled += not zi
            copper[n] += zi

    open_nets, checked = {}, 0
    for n, pl in sorted(pads.items()):
        if len(pl) < 2:
            continue
        checked += 1
        isl = islands_of([pad_item(p) for p in pl] + copper.get(n, []), len(pl))
        if isl > 1:
            open_nets[n] = isl

    print(f"Nets checked (>=2 pads): {checked}")
    print(f"Fully connected: {checked - len(open_nets)}")
    print(f"OPEN: {len(open_nets)}")
    for n, k in sorted(open_nets.items()):
        print(f"  {n}: {k} islands")
    print(f"Net zones: {zones} ({unfilled} with no filled polys)")
    print("OPEN-NETS: " + (",".join(sorted(open_nets)) or "NONE"))


if __name__ == "__main__":
    main()
