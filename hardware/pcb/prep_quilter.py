#!/usr/bin/env python3
"""Prep vitalq_hw_v1.kicad_pcb for Quilter AI upload.

- strip tracks/vias/copper zones (keep Edge_Cuts + rule-area keepouts)
- lock skin side + HV corridor + connectors/pads + ESP32 where they are
- move every other footprint off-board in rough rows
- save to vitalq_quilter_input.kicad_pcb (source untouched)

NOTE: fetch all typed objects (zones, footprints, drawings) BEFORE any
board.Remove() — mass track removal breaks the SWIG typemap for later
accessors (returns untyped SwigPyObject) in this KiCad build.
"""
import re
import pcbnew

SRC = "vitalq_hw_v1.kicad_pcb"
DST = "vitalq_quilter_input.kicad_pcb"
MM = 1_000_000

def refnum(r):
    m = re.match(r"([A-Z]+)(\d+)", r)
    return (m.group(1), int(m.group(2))) if m else (r, 0)

def in_range(r, prefix, lo, hi):
    m = re.match(rf"{prefix}(\d+)$", r)
    return bool(m) and lo <= int(m.group(1)) <= hi

def is_hv(r):
    # HV electrode corridor: creepage ladders R32-36/R76-83, J11/J13 0-ohm
    # cut-points R118-122, TVS/clamp rows D1-D3 + D6-D9 + D21-D30.
    # (D4/D5 are USB ESD clamps near J1, not corridor -> unlocked.)
    return (in_range(r, "R", 32, 36) or in_range(r, "R", 76, 83)
            or in_range(r, "R", 118, 122) or in_range(r, "D", 1, 3)
            or in_range(r, "D", 6, 9) or in_range(r, "D", 21, 30))

# Skin-facing sensor cluster, physics-fixed on the bottom side
# (optical/thermal co-registration ~x7-26, y44-56) + U20 TMP117 top twin.
SENSORS = {"U6", "U10", "U11", "U12", "U16", "U20", "U22", "U23",
           "D10", "D11", "D12"}

board = pcbnew.LoadBoard(SRC)

# --- read everything first ---------------------------------------------
zones = [board.GetArea(i) for i in range(board.GetAreaCount())]
keepouts = [z for z in zones if z.GetIsRuleArea()]
copper_zones = [z for z in zones if not z.GetIsRuleArea()]
tracks = list(board.GetTracks())

xs, ys = [], []
for d in board.GetDrawings():
    if d.GetLayer() == pcbnew.Edge_Cuts:
        bb = d.GetBoundingBox()
        xs += [bb.GetLeft(), bb.GetRight()]
        ys += [bb.GetTop(), bb.GetBottom()]

locked, loose = [], []
for fp in board.GetFootprints():
    r = fp.GetReference()
    on_bottom = fp.GetLayer() == pcbnew.B_Cu
    pre = re.match(r"[A-Z]+", r).group(0)
    lock = (r in SENSORS or is_hv(r) or pre == "J" or r == "U1")
    (locked if lock else loose).append(fp)

fp_boxes = {id(fp): fp.GetBoundingBox() for fp in loose}

# --- mutate -------------------------------------------------------------
for t in tracks:
    board.Remove(t)
for z in copper_zones:
    board.Remove(z)
print(f"tracks removed: {len(tracks)}; zones: kept {len(keepouts)} keepouts, "
      f"removed {len(copper_zones)} copper zones")

for fp in locked:
    fp.SetLocked(True)
print(f"locked {len(locked)}  loose {len(loose)}  total {len(locked)+len(loose)}")

# --- move loose parts off-board in rough rows ---------------------------
x0 = max(xs) + 10 * MM
y0 = min(ys)
cur_x, cur_y, row_h = x0, y0, 0
ROW_W, GAP = 170 * MM, int(1.5 * MM)

for fp in sorted(loose, key=lambda f: refnum(f.GetReference())):
    bb = fp_boxes[id(fp)]
    w, h = bb.GetWidth(), bb.GetHeight()
    if cur_x > x0 and cur_x + w > x0 + ROW_W:
        cur_x, cur_y, row_h = x0, cur_y + row_h + GAP, 0
    pos = fp.GetPosition()
    fp.SetPosition(pcbnew.VECTOR2I(
        pos.x + int(cur_x - bb.GetLeft()),
        pos.y + int(cur_y - bb.GetTop())))
    cur_x += w + GAP
    row_h = max(row_h, h)

pcbnew.SaveBoard(DST, board)
print("saved", DST)
print("LOCKED:", " ".join(sorted((fp.GetReference() for fp in locked),
                                 key=refnum)))
print("LOOSE:", " ".join(sorted((fp.GetReference() for fp in loose),
                                key=refnum)))
