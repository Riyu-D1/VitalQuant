#!/usr/bin/env python3
"""Enlarge the VitalQ board outline 46x70 -> 50x75 mm (Quilter v7).

Symmetric growth about the old centre (23,27): x 0..46 -> -2..48,
y -8..62 -> -10.5..64.5. Applies to a kicad_pcb in place:

- 4-segment Edge.Cuts rectangle rewritten (same style, square corners).
- Locked set stays locked; interior skin-side cluster + HV corridor +
  electrode pads shift +2.5 mm in Y as one rigid group (keeps the same
  standoff from the +Y skin edge). Right-edge tail pads J9-J11 also get
  +2 mm X (stay flush to the new edge), left-edge J12 FFC gets -2 mm X.
- -Y end group (J1 USB-C, U1 ESP32 + antenna keepout, J2 LiPo pads)
  shifts -2.5 mm in Y so the connector/antenna stay on the board edge.
- H1/H2 mounting holes move from the parked row back on-board at their
  original corner insets, and are locked.
- Zones/rule areas move with the group they belong to; interior
  Edge.Cuts slots move with the cluster; the stray sliver left of the
  old edge moves -2 mm so it stays dead geometry outside the outline.

No connectivity, footprint, symbol or net changes.

Usage: enlarge_outline.py <file.kicad_pcb>
"""

import re
import sys

DX = 2.0      # each X edge grows outward by this
DY = 2.5      # each Y edge grows outward by this
X0, X1 = -DX, 46.0 + DX          # -2 .. 48
Y0, Y1 = -8.0 - DY, 62.0 + DY    # -10.5 .. 64.5

# footprint -> (dx, dy). All other locked parts get (0, +DY).
MOVES = {
    "J1": (0.0, -DY),     # USB-C receptacle, stays flush on -Y edge
    "U1": (0.0, -DY),     # ESP32, antenna end stays on -Y edge
    "J2": (0.0, -DY),     # LiPo pads move with the -Y end group
    "J9": (DX, DY),       # +X edge tail pads
    "J10": (DX, DY),
    "J11": (DX, DY),
    "J12": (-DX, DY),     # -X edge FFC connector
}
# H1/H2: parked off-board in v6 -> back on-board, same corner insets
# (2.6/1.85 mm from the old left edge, 8.3125/3.3156 mm from the old top).
HOLES = {
    "H1": (X0 + 2.6, Y0 + 8.3125),
    "H2": (X0 + 1.85, Y0 + 3.3156),
}
GROUP_Y_MIN = 12.0   # locked parts below this belong to the -Y end group

def r6(v):
    """Round to 6 decimals, drop float noise and trailing zeros."""
    s = f"{v:.6f}".rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


AT_RE = re.compile(r"\(at ([-\d.]+) ([-\d.]+)((?: [-\d.]+)?)\)")
XY_RE = re.compile(r"\(xy ([-\d.]+) ([-\d.]+)\)")
SE_RE = re.compile(
    r"\(start ([-\d.]+) ([-\d.]+)\)\s*\n\s*\(end ([-\d.]+) ([-\d.]+)\)")


def top_level_blocks(txt):
    """Yield (start, end) spans of depth-1 items: '\\t(foo ... \\n\\t)'."""
    i = 0
    n = len(txt)
    while True:
        i = txt.find("\n\t(", i)
        if i < 0:
            return
        start = i + 1
        end = txt.find("\n\t)", start)
        if end < 0:
            return
        yield start, end + 3
        i = end + 3


def zone_shift(block):
    name = re.search(r'\(name "([^"]*)"\)', block)
    name = name.group(1) if name else ""
    pts = XY_RE.findall(block)
    if not pts:
        return (0.0, 0.0)
    xs = [float(p[0]) for p in pts]
    ys = [float(p[1]) for p in pts]
    cx, cy = sum(xs) / len(xs), sum(ys) / len(ys)
    if name == "antenna_keepout":
        return (0.0, -DY)
    if name == "hole_keepout":
        return (-DX, -DY)           # follows H1/H2 corner insets
    if min(xs) >= 42.0:             # +X-edge tail pad keepouts
        return (DX, DY)
    if cy >= GROUP_Y_MIN:           # corridor + skin-cluster zones
        return (0.0, DY)
    return (0.0, 0.0)


def transform(path):
    txt = open(path).read()
    out = []
    pos = 0
    stats = {"edge": 0, "zone": 0, "fp": 0}

    for start, end in top_level_blocks(txt):
        b = txt[start:end]
        if b.startswith("\t(gr_line") and '"Edge.Cuts"' in b:
            sm, em = re.search(r"\(start ([-\d.]+) ([-\d.]+)\)", b), \
                re.search(r"\(end ([-\d.]+) ([-\d.]+)\)", b)
            x0, y0, x1, y1 = (float(sm.group(1)), float(sm.group(2)),
                              float(em.group(1)), float(em.group(2)))
            if max(abs(x0 - x1), abs(y0 - y1)) > 40:
                # outer outline edge -> remap onto the new rectangle
                def nx(v):
                    return -2.0 if abs(v) < 0.01 else \
                        (48.0 if abs(v - 46) < 0.01 else v)

                def ny(v):
                    return -10.5 if abs(v + 8) < 0.01 else \
                        (64.5 if abs(v - 62) < 0.01 else v)
                nb = SE_RE.sub(
                    f"(start {nx(x0)} {ny(y0)})\n\t\t(end {nx(x1)} {ny(y1)})",
                    b, count=1)
            else:
                # interior cutout slots follow the skin-side cluster;
                # the sliver left of old x=0 shifts out to stay dead
                dx, dy = (0.0, DY) if x0 > -0.5 else (-DX, 0.0)
                nb = SE_RE.sub(lambda m: (
                    f"(start {r6(float(m.group(1)) + dx)} "
                    f"{r6(float(m.group(2)) + dy)})\n"
                    f"\t\t(end {r6(float(m.group(3)) + dx)} "
                    f"{r6(float(m.group(4)) + dy)})"), b, count=1)
            stats["edge"] += 1
        elif b.startswith("\t(zone") or b.startswith("\t(rule_area"):
            dx, dy = zone_shift(b)
            nb = XY_RE.sub(lambda m: (
                f"(xy {r6(float(m.group(1)) + dx)} "
                f"{r6(float(m.group(2)) + dy)})"), b) if (dx or dy) else b
            if dx or dy:
                stats["zone"] += 1
        elif b.startswith("\t(footprint"):
            rm = re.search(r'property "Reference" "([^"]+)"', b)
            ref = rm.group(1) if rm else ""
            nb = b
            if ref in HOLES:
                hx, hy = HOLES[ref]
                nb = AT_RE.sub(f"(at {r6(hx)} {r6(hy)}\\g<3>)", b,
                               count=1)
                if "(locked yes)" not in nb:
                    nb = re.sub(r"(\(footprint [^\n]*\n)(\t+)",
                                r"\1\2(locked yes)\n\2", nb, count=1)
                stats["fp"] += 1
            elif "(locked yes)" in b:
                if ref in MOVES:
                    dx, dy = MOVES[ref]
                else:
                    am = AT_RE.search(b)
                    dy = -DY if float(am.group(2)) < GROUP_Y_MIN else DY
                    dx = 0.0
                nb = AT_RE.sub(lambda m: (
                    f"(at {r6(float(m.group(1)) + dx)} "
                    f"{r6(float(m.group(2)) + dy)}{m.group(3)})"),
                    b, count=1)
                stats["fp"] += 1
        else:
            nb = b
        out.append(txt[pos:start])
        out.append(nb)
        pos = end
    out.append(txt[pos:])
    open(path, "w").write("".join(out))
    print(f"{path}: outline {X0}..{X1} x {Y0}..{Y1}; "
          f"{stats['edge']} edge segs, {stats['zone']} zones, "
          f"{stats['fp']} footprints moved")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        transform(p)
