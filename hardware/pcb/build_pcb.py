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
# The official ESP32-WROOM-32E courtyard keeps the antenna on the left edge and
# the shield out to about x=26.3. USB-C, opening to the right, needs the rest.
# 37 x 30 is the smallest outline that still clears both courtyards.
BOARD_W = 37.0
BOARD_H = 30.0

# (x, y, rotation_deg, bottom)
# Rotation is applied before a bottom-side flip.
PLACE = {
    # MCU. Rotation 90 puts the antenna at the left edge; the shield ends near x=26.
    "U1": (12.75, 15.0, 90, False),
    "C7": (7.6, 26.3, 0, False),
    "C8": (12.55, 26.3, 0, False),
    "R5": (19.3, 25.85, 0, False),
    "R6": (22.9, 25.85, 0, False),
    "R7": (25.15, 25.4, 0, False),
    "TP1": (21.2, 27.4, 0, False),
    "TP2": (24.6, 27.4, 0, False),
    # USB-C opening faces the right edge. CP2102 sits under it.
    "J1": (32.8, 24.0, 90, False),
    "U5": (31.9, 14.8, 0, False),
    "C9": (27.65, 17.75, 90, False),
    "C10": (27.65, 15.5, 90, False),
    "R4": (27.65, 13.25, 90, False),
    "R1": (7.75, 29.2, 0, False),
    "R2": (12.7, 29.2, 0, False),
    # Chargers and regulators in the right-hand column, clear of the antenna.
    "U4": (32.2, 9.4, 0, False),
    "U3": (32.2, 5.4, 0, False),
    "U2": (32.2, 1.95, 0, False),
    "C1": (35.15, 1.25, 90, False),
    "C2": (35.15, 3.5, 90, False),
    "C3": (35.15, 5.75, 90, False),
    "C4": (35.15, 8.0, 90, False),
    "C5": (35.15, 10.25, 90, False),
    "C6": (27.65, 1.3, 90, False),
    "R3": (29.0, 1.3, 90, False),
    # LiPo and FSR solder pads on the bottom edge.
    "J2": (9.8, 3.15, 0, False),
    "J3": (16.2, 3.15, 0, False),
    "R8": (19.45, 1.0, 0, False),
    # Air sensor and IMU on top, above the module, not against skin.
    "U13": (10.2, 27.4, 0, False),
    "U14": (16.0, 27.25, 0, False),
    # Skin side. Optical parts in one group; PPG above them; ECG and EDA below.
    # The cluster avoids the module's through-hole ground pads and the antenna.
    "U6": (9.0, 21.6, 0, True),
    "U10": (13.4, 21.6, 0, True),
    "U12": (18.6, 21.6, 0, True),
    "U11": (23.6, 21.6, 0, True),
    "J4": (16.5, 27.8, 0, True),
    "J5": (10.5, 12.2, 0, True),
    "J6": (21.5, 12.2, 0, True),
    "C11": (15.05, 9.2, 0, True),
    "C12": (27.2, 9.2, 0, True),
    "C13": (7.4, 8.75, 0, True),
    "C14": (9.65, 8.75, 0, True),
    "C15": (11.9, 8.75, 0, True),
    "R9": (17.3, 8.75, 0, True),
    "R10": (19.55, 8.75, 0, True),
    # 1.8 V translator and its passives, next to the skin group.
    "U9": (29.2, 15.2, 0, True),
    "R21": (26.6, 19.6, 0, True),
    "R22": (32.0, 19.6, 0, True),
    "R18": (26.6, 18.25, 0, True),
    "R19": (28.85, 18.25, 0, True),
    "R20": (31.1, 18.25, 0, True),
    "C36": (33.35, 18.25, 0, True),
    "C37": (7.4, 19.5, 0, True),
    "C38": (9.65, 19.5, 0, True),
    "C39": (22.25, 19.5, 0, True),
    "C40": (24.5, 19.5, 0, True),
    "C41": (17.3, 18.6, 0, True),
    # ECG and EDA analog, lower right, away from the antenna and the USB plug.
    "U8": (32.4, 4.0, 0, True),
    "U7": (32.4, 10.2, 0, True),
    "C28": (27.2, 8.15, 0, True),
    "C29": (27.2, 6.8, 0, True),
    "C30": (27.2, 5.45, 0, True),
    "C31": (27.2, 4.1, 0, True),
    "C32": (27.2, 2.75, 0, True),
    "C33": (27.2, 1.4, 0, True),
    "C34": (21.65, 8.35, 0, True),
    "C35": (23.9, 8.35, 0, True),
    "R14": (18.5, 7.45, 0, True),
    "R15": (20.75, 7.0, 0, True),
    "R16": (23.0, 7.0, 0, True),
    "R17": (25.25, 7.0, 0, True),
    "C16": (35.15, 14.2, 0, True),
    "C17": (27.15, 13.25, 0, True),
    "C18": (27.15, 11.9, 0, True),
    "C19": (27.15, 10.55, 0, True),
    "C20": (35.7, 19.3, 0, True),
    "C21": (35.25, 17.95, 0, True),
    "C22": (26.7, 17.05, 0, True),
    "C23": (28.95, 17.05, 0, True),
    "C24": (31.2, 17.05, 0, True),
    "C25": (33.45, 16.6, 0, True),
    "C26": (35.7, 16.6, 0, True),
    "C27": (32.55, 15.25, 0, True),
    "C42": (34.8, 15.25, 0, True),
    "R11": (32.55, 13.9, 0, True),
    "R12": (14.15, 8.15, 0, True),
    "R13": (7.4, 7.7, 0, True),
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
        # designators; the fab layer still carries ${REFERENCE}.
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
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
    print(f"wrote {PCB.name}  {BOARD_W:.0f} x {BOARD_H:.0f} mm")
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
