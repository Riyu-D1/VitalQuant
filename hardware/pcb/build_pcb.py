#!/usr/bin/env python3
"""Unrouted wearable placement for VitalQ hw_v1.

The schematic netlist is the connectivity source. This script loads every
footprint, assigns pad nets from that netlist, and parks the parts in
functional clusters. It does not route.
"""

from __future__ import annotations

import csv
import subprocess
import sys
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent
SCH = ROOT / "vitalq_hw_v1.kicad_sch"
PCB = ROOT / "vitalq_hw_v1.kicad_pcb"
BOM = ROOT / "vitalq_hw_v1_bom.csv"
KICAD_FP = Path("/usr/share/kicad/footprints")

# Millimetres. Origin is the lower-left corner, Y up (KiCad board coordinates).
# ESP32 body courtyard, antenna on the left, ends near x=26.3. USB-C rotated
# so its opening is the right edge needs 9.5 mm. Those two set the width.
# 36.2 mm still clears both. 35 mm does not.
# Height is the USB-C shell: its copper ends at y=27.45, and 0.3 mm copper-to-edge
# means the outline cannot sit below 27.75. 27.8 mm is the clearance that holds.
BOARD_W = 36.2
BOARD_H = 27.8

# (x, y, rotation_deg, bottom)
# Rotation is applied before a bottom-side flip.
PLACE = {
    "C1": (35.30, 1.85, 90, False),
    "C2": (29.93, 2.67, 90, False),
    "C3": (35.24, 5.84, 90, False),
    "C4": (29.93, 6.11, 90, False),
    "C6": (35.30, 8.93, 90, False),
    "C7": (9.20, 23.10, 0, False),
    "C8": (11.30, 23.10, 0, False),
    "C9": (29.47, 7.52, 0, False),
    "C10": (29.52, 8.73, 0, False),
    "C11": (18.34, 16.66, 90, True),
    "C13": (17.31, 16.85, 90, True),
    "C14": (20.56, 16.85, 90, True),
    "C15": (23.85, 8.40, 90, True),
    "C16": (15.10, 9.90, 90, True),
    "C18": (9.97, 13.16, 0, True),
    "C19": (7.93, 10.35, 90, True),
    "C20": (7.82, 8.34, 90, True),
    "C21": (10.27, 14.12, 0, True),
    "C22": (7.79, 12.26, 90, True),
    "C23": (6.98, 10.43, 90, True),
    "C24": (12.28, 14.64, 0, True),
    "C25": (6.86, 8.34, 90, True),
    "C26": (8.52, 14.16, 90, True),
    "C27": (10.38, 15.39, 0, True),
    "C28": (14.35, 3.35, 90, True),
    "C29": (14.14, 6.58, 0, True),
    "C30": (8.08, 4.25, 90, True),
    "C32": (9.62, 6.61, 0, True),
    "C33": (7.80, 2.02, 90, True),
    "C34": (6.87, 1.79, 90, True),
    "C35": (14.74, 5.11, 0, True),
    "C36": (30.70, 15.05, 90, True),
    "C37": (22.28, 10.93, 0, True),
    "C38": (30.45, 9.40, 90, True),
    "C39": (8.70, 23.35, 0, True),
    "C40": (14.40, 23.35, 0, True),
    "C41": (33.72, 2.07, 90, True),
    "C42": (15.57, 7.86, 90, True),
    "C43": (12.05, 6.42, 0, True),
    "J1": (32.00, 22.63, 90, False),
    "J2": (17.55, 25.70, 0, False),
    "J3": (22.55, 25.70, 0, False),
    "J4": (21.15, 19.35, 180, True),
    "J5": (19.35, 10.30, 270, True),
    "J6": (25.55, 5.15, 0, True),
    "R1": (26.11, 24.00, 90, False),
    "R2": (29.26, 10.28, 0, False),
    "R3": (35.12, 3.92, 0, False),
    "R5": (7.51, 22.92, 90, False),
    "R6": (16.80, 24.40, 90, True),
    "R7": (18.90, 24.40, 90, True),
    "R8": (25.55, 26.15, 90, False),
    "R9": (19.41, 16.65, 90, True),
    "R10": (20.21, 15.17, 0, True),
    "R11": (13.18, 15.66, 0, True),
    "R12": (7.30, 14.18, 90, True),
    "R13": (7.38, 6.65, 0, True),
    "R14": (14.44, 1.35, 90, True),
    "R15": (15.55, 3.35, 90, True),
    "R16": (17.20, 3.40, 0, True),
    "R17": (7.44, 5.71, 0, True),
    "R18": (33.94, 6.24, 0, True),
    "R19": (32.15, 2.67, 0, True),
    "R20": (32.37, 6.67, 90, True),
    "R21": (30.90, 16.85, 0, True),
    "R22": (30.55, 13.05, 90, True),
    "U1": (12.75, 12.20, 90, False),
    "U2": (32.55, 1.85, 0, False),
    "U3": (32.55, 5.29, 0, False),
    "U4": (32.55, 8.93, 0, False),
    "U5": (32.55, 13.97, 0, False),
    "U6": (22.50, 16.05, 0, True),
    "U7": (11.50, 9.90, 0, True),
    "U8": (11.15, 3.35, 0, True),
    "U9": (33.15, 4.40, 0, True),
    "U10": (26.90, 15.05, 0, True),
    "U11": (22.85, 12.85, 0, True),
    "U12": (27.25, 9.40, 0, True),
    "U13": (8.70, 25.85, 0, False),
    "U14": (12.70, 25.55, 0, False),
}


def parse_sexp(text: str):
    s = text.strip()
    n = len(s)
    i = 0

    def skip(i):
        while i < n and s[i] in " \t\r\n":
            i += 1
        return i

    def read(i):
        i = skip(i)
        if i >= n:
            raise SystemExit("truncated netlist")
        if s[i] == "(":
            i += 1
            items = []
            while True:
                i = skip(i)
                if s[i] == ")":
                    return items, i + 1
                item, i = read(i)
                items.append(item)
        if s[i] == '"':
            i += 1
            buf = []
            while s[i] != '"':
                if s[i] == "\\":
                    i += 1
                buf.append(s[i])
                i += 1
            return "".join(buf), i + 1
        j = i
        while j < n and s[j] not in " \t\r\n()":
            j += 1
        return s[i:j], j

    tree, _ = read(0)
    return tree


def child(node, name):
    return [c for c in node if isinstance(c, list) and c and c[0] == name]


def atom(node, name, default=None):
    for c in node:
        if isinstance(c, list) and c and c[0] == name and len(c) >= 2:
            return c[1]
    return default


def load_netlist(path: Path):
    tree = parse_sexp(path.read_text(encoding="utf-8"))
    comps = {}
    for comp in child(tree, "components")[0][1:]:
        if not isinstance(comp, list) or not comp or comp[0] != "comp":
            continue
        ref = atom(comp, "ref")
        comps[ref] = {
            "value": atom(comp, "value", ""),
            "footprint": atom(comp, "footprint", ""),
        }
    nets = []
    pin_net = {}
    for net in child(tree, "nets")[0][1:]:
        if not isinstance(net, list) or not net or net[0] != "net":
            continue
        name = atom(net, "name")
        code = int(atom(net, "code"))
        nodes = []
        for node in net:
            if isinstance(node, list) and node and node[0] == "node":
                ref = atom(node, "ref")
                pin = str(atom(node, "pin"))
                nodes.append((ref, pin))
                pin_net[(ref, pin)] = name
        nets.append((code, name, nodes))
    return comps, nets, pin_net


def export_netlist(dest: Path):
    subprocess.check_call(
        ["kicad-cli", "sch", "export", "netlist", "-o", str(dest), str(SCH)],
        cwd=ROOT,
    )


def load_footprint(fpname: str):
    nick, name = fpname.split(":", 1)
    if nick == "vitalq":
        lib = ROOT / "lib" / "vitalq.pretty"
    elif nick == "snap":
        lib = ROOT / "lib" / "snap.pretty"
    else:
        lib = KICAD_FP / f"{nick}.pretty"
    fp = pcbnew.FootprintLoad(str(lib), name)
    if fp is None:
        raise SystemExit(f"footprint not found: {fpname}")
    return fp


def mm(v):
    return pcbnew.FromMM(v)


def vec(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def shrink_fab_reference(fp):
    """Vendor fab reference text is about 1 mm and covers the body in the 2D plot."""
    size = pcbnew.VECTOR2I(mm(0.35), mm(0.35))
    for item in fp.GraphicalItems():
        if not hasattr(item, "GetText"):
            continue
        if item.GetLayer() not in (pcbnew.F_Fab, pcbnew.B_Fab):
            continue
        text = item.GetText()
        if text not in (fp.GetReference(), "${REFERENCE}", "REF**") and "REFERENCE" not in text:
            continue
        item.SetTextSize(size)
        item.SetTextThickness(mm(0.07))


def add_edge(board, x1, y1, x2, y2):
    shape = pcbnew.PCB_SHAPE(board)
    shape.SetShape(pcbnew.SHAPE_T_SEGMENT)
    shape.SetLayer(pcbnew.Edge_Cuts)
    shape.SetStart(vec(x1, y1))
    shape.SetEnd(vec(x2, y2))
    shape.SetWidth(mm(0.1))
    board.Add(shape)


def add_keepout(board):
    """No copper under the module antenna. The rest of the keep-out is off the board."""
    zone = pcbnew.ZONE(board)
    zone.SetIsRuleArea(True)
    zone.SetDoNotAllowTracks(True)
    zone.SetDoNotAllowVias(True)
    zone.SetDoNotAllowPads(True)
    zone.SetDoNotAllowCopperPour(True)
    zone.SetDoNotAllowFootprints(False)
    zone.SetLayerSet(pcbnew.LSET.AllCuMask())
    outline = zone.Outline()
    outline.NewOutline()
    # On-board part of the module antenna keep-out. The rest hangs off the left edge.
    for x, y in ((0.0, 0.0), (6.5, 0.0), (6.5, BOARD_H), (0.0, BOARD_H)):
        outline.Append(mm(x), mm(y))
    board.Add(zone)


def apply_rules(board):
    """JLCPCB 4-layer defaults. The project file carries the same numbers."""
    ds = board.GetDesignSettings()
    ds.m_MinClearance = mm(0.09)
    ds.m_TrackMinWidth = mm(0.09)
    ds.m_ViasMinSize = mm(0.45)
    ds.m_ViasMinDrill = mm(0.2)
    ds.m_CopperEdgeClearance = mm(0.3)
    ds.m_HoleClearance = mm(0.2)
    ds.m_HoleToHoleMin = mm(0.5)
    ds.m_MinThroughDrill = mm(0.2)
    ds.m_MinSilkTextHeight = mm(0.45)
    ds.m_MinSilkTextThickness = mm(0.08)
    # Fine-pitch BGAs (0.17 mm copper gap). Zero mask expansion keeps a
    # solder-mask web; 0.1 mm is the JLCPCB minimum bridge.
    ds.m_SolderMaskExpansion = mm(0)
    ds.m_SolderMaskMinWidth = mm(0.1)
    ds.m_SolderMaskToCopperClearance = mm(0.05)
    net = ds.m_NetSettings.GetDefaultNetclass()
    net.SetClearance(mm(0.09))
    net.SetTrackWidth(mm(0.2))
    net.SetViaDiameter(mm(0.45))
    net.SetViaDrill(mm(0.2))


def courtyard(fp, bottom):
    fp.BuildCourtyardCaches()
    layer = pcbnew.B_CrtYd if bottom else pcbnew.F_CrtYd
    return fp.GetCachedCourtyard(layer)


def overlaps(a, b):
    if a is None or b is None or a.OutlineCount() == 0 or b.OutlineCount() == 0:
        return False
    return a.Collide(b)


def main():
    net_path = ROOT / "build" / "vitalq_hw_v1.net"
    net_path.parent.mkdir(exist_ok=True)
    export_netlist(net_path)
    comps, nets, pin_net = load_netlist(net_path)

    missing = sorted(set(comps) - set(PLACE))
    extra = sorted(set(PLACE) - set(comps))
    if missing or extra:
        raise SystemExit(f"placement list mismatch missing={missing} extra={extra}")

    board = pcbnew.CreateEmptyBoard()
    board.SetCopperLayerCount(4)
    apply_rules(board)

    net_items = {}
    for code, name, _nodes in nets:
        item = pcbnew.NETINFO_ITEM(board, name, code)
        board.Add(item)
        net_items[name] = item

    placed = {}
    unresolved = []
    for ref, meta in sorted(comps.items()):
        fp = load_footprint(meta["footprint"])
        x, y, rot, bottom = PLACE[ref]
        nick, name = meta["footprint"].split(":", 1)
        fp.SetFPID(pcbnew.LIB_ID(nick, name))
        fp.SetReference(ref)
        fp.SetValue(meta["value"])
        fp.SetPosition(vec(x, y))
        fp.SetOrientation(pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T))
        # 0402 references cannot sit clear of the next part. Hide silk
        # designators. Fab ${REFERENCE} text is shrunk so the 2D plot stays readable.
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
        shrink_fab_reference(fp)
        # Flip before the footprint is on a board segfaults in this KiCad build.
        board.Add(fp)
        if bottom:
            fp.Flip(fp.GetPosition(), False)
        for pad in fp.Pads():
            number = pad.GetNumber()
            # Official M2 footprints leave the NPTH pad number empty. The symbol pin is "1".
            lookup = "1" if number == "" and ref.startswith("H") else number
            net_name = pin_net.get((ref, lookup))
            if net_name:
                pad.SetNet(net_items[net_name])
            elif number not in ("", "MP"):
                unresolved.append(f"{ref} pad {number} has no netlist pin")
        placed[ref] = (fp, bottom)

    # Pads whose netlist pin does not exist on the footprint.
    for (ref, pin), name in pin_net.items():
        fp, _bottom = placed[ref]
        numbers = {pad.GetNumber() for pad in fp.Pads()}
        if pin in numbers:
            continue
        if pin == "1" and "" in numbers and ref.startswith("H"):
            continue
        unresolved.append(f"{ref} pin {pin} ({name}) has no footprint pad")

    add_edge(board, 0, 0, BOARD_W, 0)
    add_edge(board, BOARD_W, 0, BOARD_W, BOARD_H)
    add_edge(board, BOARD_W, BOARD_H, 0, BOARD_H)
    add_edge(board, 0, BOARD_H, 0, 0)
    add_keepout(board)

    # Courtyard clashes on the same side, and copper too close to the outline.
    clashes = []
    refs = list(placed)
    yards = {}
    for ref, (fp, bottom) in placed.items():
        yards[ref] = courtyard(fp, bottom)
    print("yards", len(yards), flush=True)
    for i, a in enumerate(refs):
        for b in refs[i + 1 :]:
            if placed[a][1] != placed[b][1]:
                continue
            if overlaps(yards[a], yards[b]):
                clashes.append(f"{a} x {b}")
    edge_hits = []
    pth_hits = []
    for ref, (fp, bottom) in placed.items():
        for pad in fp.Pads():
            if pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                continue
            box = pad.GetBoundingBox()
            for other, (ofp, obottom) in placed.items():
                if other == ref or obottom == bottom:
                    continue
                for op in ofp.Pads():
                    if op.GetAttribute() not in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                        continue
                    src = op.GetBoundingBox()
                    ob = pcbnew.BOX2I(src.GetOrigin(), src.GetSize())
                    ob.Inflate(mm(0.2))
                    if box.Intersects(ob):
                        pth_hits.append(f"{ref} overlaps {other} through-hole")
                        break
                else:
                    continue
                break
    for ref, (fp, _bottom) in placed.items():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            box = pad.GetBoundingBox()
            x0 = box.GetX() / 1e6
            y0 = box.GetY() / 1e6
            x1 = (box.GetX() + box.GetWidth()) / 1e6
            y1 = (box.GetY() + box.GetHeight()) / 1e6
            gap = min(x0 - 0, y0 - 0, BOARD_W - x1, BOARD_H - y1)
            if gap < 0.3:
                edge_hits.append(f"{ref}.{pad.GetNumber()} edge {gap:.2f} mm")

    u1, _ = placed["U1"]
    u1.BuildCourtyardCaches()
    bb = yards["U1"].BBox()
    print(
        f"U1 courtyard x {bb.GetX()/1e6:.1f}..{(bb.GetX()+bb.GetWidth())/1e6:.1f}"
        f" y {bb.GetY()/1e6:.1f}..{(bb.GetY()+bb.GetHeight())/1e6:.1f}"
    )
    print(
        f"parts {len(placed)}  clashes {len(clashes)}  edge {len(edge_hits)}"
        f"  pth {len(pth_hits)}  pad-miss {len(unresolved)}"
    )
    for line in clashes:
        print("  clash", line)
    for line in edge_hits:
        print("  edge", line)
    for line in pth_hits:
        print("  pth", line)
    for line in unresolved:
        print("  pad", line)

    board.SetFileName(str(PCB))
    pcbnew.SaveBoard(str(PCB), board)
    write_bom(comps)
    print(f"wrote {PCB.name}  {BOARD_W:.1f} x {BOARD_H:.1f} mm")
    if clashes or edge_hits or pth_hits or unresolved:
        return 1
    return 0


def write_bom(comps):
    rows = []
    for ref, meta in sorted(comps.items(), key=lambda kv: (kv[0][0], int("".join(ch for ch in kv[0] if ch.isdigit()) or "0"))):
        rows.append((ref, meta["value"], meta["footprint"], "1"))
    with BOM.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Ref", "Value", "Footprint", "Qty"])
        w.writerows(rows)


if __name__ == "__main__":
    sys.exit(main())
