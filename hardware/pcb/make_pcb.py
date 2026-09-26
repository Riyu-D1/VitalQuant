#!/usr/bin/env python3
"""Load the SKiDL netlist into an unrouted KiCad board.

Footprints are dropped on a coarse grid so the ratsnest is visible. The
coordinates are an import aid, not a placement. Run with the system Python
that has pcbnew (KiCad), not the SKiDL virtualenv.
"""

import json
import sys
from pathlib import Path

import pcbnew

HERE = Path(__file__).resolve().parent
FP_ROOT = Path("/usr/share/kicad/footprints")
LOCAL = HERE / "lib"


def lib_dir(nickname):
    if nickname == "vitalq":
        return LOCAL / "vitalq.pretty"
    return FP_ROOT / f"{nickname}.pretty"


def load_footprint(footprint):
    if ":" not in footprint:
        raise SystemExit(f"footprint {footprint!r} has no library nickname")
    nick, name = footprint.split(":", 1)
    path = lib_dir(nick)
    if not path.is_dir():
        raise SystemExit(f"footprint library not found: {path}")
    fp = pcbnew.FootprintLoad(str(path), name)
    if fp is None:
        raise SystemExit(f"missing footprint {footprint}")
    return fp


def add_outline(board, width, height):
    pts = [(0, 0), (width, 0), (width, height), (0, height)]
    for i in range(4):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % 4]
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(x1), pcbnew.FromMM(y1)))
        seg.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(x2), pcbnew.FromMM(y2)))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(pcbnew.FromMM(0.15))
        board.Add(seg)


def main():
    data = json.loads((HERE / "build" / "board.json").read_text(encoding="utf-8"))
    board = pcbnew.BOARD()
    nets = {}

    def net_for(name):
        if name not in nets:
            item = pcbnew.NETINFO_ITEM(board, name)
            board.Add(item)
            nets[name] = item
        return nets[name]

    # ref -> list of (pad_number) already checked
    problems = []
    placed = []
    x = 8.0
    y = 12.0
    row_h = 0.0
    max_x = 0.0
    limit = 280.0

    for part in sorted(data["parts"], key=lambda p: p["ref"]):
        fp = load_footprint(part["footprint"])
        fp.SetReference(part["ref"])
        fp.SetValue(part["value"])
        if part.get("dnp"):
            fp.SetDNP(True)
        # Paste-only and NPTH pads are not nets. Unnumbered copper (switch frame)
        # is tied to GND so it is not a floating pad.
        pad_nums = set()
        for pad in fp.Pads():
            number = pad.GetNumber()
            if not pad.IsOnCopperLayer():
                continue
            if number == "":
                pad.SetNet(net_for("GND"))
                continue
            pad_nums.add(number)
        symbol_pins = set(part["pins"])
        if pad_nums != symbol_pins:
            missing = sorted(symbol_pins - pad_nums)
            extra = sorted(pad_nums - symbol_pins)
            problems.append(f"{part['ref']} {part['footprint']}: missing pads {missing} extra pads {extra}")
        by_pin = {}
        for net in data["nets"]:
            for pad in net["pads"]:
                if pad["ref"] == part["ref"]:
                    by_pin.setdefault(pad["pin"], net["name"])
        for pad in fp.Pads():
            number = pad.GetNumber()
            if not number or not pad.IsOnCopperLayer():
                continue
            name = by_pin.get(number)
            if name:
                pad.SetNet(net_for(name))
        bb = fp.GetBoundingBox()
        w = bb.GetWidth() / 1e6 + 4.0
        h = bb.GetHeight() / 1e6 + 4.0
        if x + w > limit:
            x = 8.0
            y += row_h
            row_h = 0.0
        fp.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x + w / 2), pcbnew.FromMM(y + h / 2)))
        board.Add(fp)
        x += w
        row_h = max(row_h, h)
        max_x = max(max_x, x)
        placed.append(part["ref"])

    if problems:
        print("\n".join(problems), file=sys.stderr)
        raise SystemExit(f"{len(problems)} footprint/pad mismatches")

    note = pcbnew.PCB_TEXT(board)
    note.SetText("IMPORT GRID, not a layout. Place and route this board yourself.")
    note.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(8), pcbnew.FromMM(6)))
    note.SetLayer(pcbnew.F_SilkS)
    note.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(1.2), pcbnew.FromMM(1.2)))
    note.SetTextThickness(pcbnew.FromMM(0.15))
    board.Add(note)

    add_outline(board, max(max_x + 8, 40), y + row_h + 8)
    out = HERE / "vitalq_hw_v1.kicad_pcb"
    board.Save(str(out))
    print(f"wrote {out} with {len(placed)} footprints and {len(nets)} nets")


if __name__ == "__main__":
    main()
