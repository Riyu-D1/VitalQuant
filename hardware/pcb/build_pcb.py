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
# Wider than 35 mm because the module is 25.5 mm long and USB-C plus the
# JST PH housing need a strip beside it. Antenna keep-out hangs off the left edge.
BOARD_W = 52.0
BOARD_H = 40.0

# (x, y, rotation_deg, bottom)
# Rotation is counterclockwise, applied before a bottom-side flip.
PLACE = {
    # MCU. Rotation 90 sends the antenna keep-out off the left edge.
    "U1": (12.75, 22.0, 90, False),
    "C7": (28.6, 29.6, 0, False),
    "C8": (28.6, 27.8, 0, False),
    "R5": (28.6, 25.6, 0, False),
    "R6": (28.6, 23.8, 0, False),
    "R7": (28.6, 22.0, 0, False),
    "TP1": (28.6, 19.6, 0, False),
    "TP2": (28.6, 16.6, 0, False),
    # USB-UART, top-right, clear of the analog front ends.
    "J1": (46.0, 34.5, 0, False),
    "U5": (35.4, 34.6, 0, False),
    "C9": (30.5, 36.6, 0, False),
    "C10": (30.5, 34.8, 0, False),
    "R4": (30.5, 33.0, 0, False),
    "R1": (47.6, 27.6, 0, False),
    "R2": (50.0, 27.6, 0, False),
    # Power and charging, between the UART and the analog parts.
    "U3": (32.4, 24.6, 0, False),
    "U4": (37.4, 24.6, 0, False),
    "U2": (42.6, 24.6, 0, False),
    "C1": (31.2, 21.5, 0, False),
    "C2": (33.4, 21.5, 0, False),
    "C3": (35.6, 21.5, 0, False),
    "C4": (37.8, 21.5, 0, False),
    "C5": (40.0, 21.5, 0, False),
    "C6": (42.2, 21.5, 0, False),
    "R3": (45.4, 21.5, 0, False),
    # LiPo connector below the module, mating toward the bottom edge.
    "J2": (19.0, 5.6, 0, False),
    # BME280 and the IMU sit above the module so the air sensor is not against skin.
    "U13": (15.2, 36.8, 0, False),
    "U14": (21.6, 36.8, 0, False),
    # ECG and EDA, lower right, away from the module and the USB plug.
    "U8": (34.2, 12.6, 0, False),
    "U7": (45.2, 13.0, 0, False),
    "C28": (31.2, 19.4, 0, False),
    "C29": (33.4, 19.4, 0, False),
    "C30": (35.6, 19.4, 0, False),
    "C31": (37.8, 19.4, 0, False),
    "C32": (40.0, 19.4, 0, False),
    "C33": (42.2, 19.4, 0, False),
    "C34": (31.2, 17.6, 0, False),
    "C35": (33.4, 17.6, 0, False),
    "R14": (35.6, 17.6, 0, False),
    "R15": (37.8, 17.6, 0, False),
    "R16": (40.0, 17.6, 0, False),
    "R17": (42.2, 17.6, 0, False),
    # FSR divider and electrode headers on the bottom edge.
    "J3": (26.2, 3.4, 0, False),
    "R8": (26.2, 8.2, 0, False),
    "J5": (31.6, 2.8, 0, False),
    "J6": (39.6, 2.8, 0, False),
    "H1": (48.6, 2.8, 0, False),
    "H2": (9.2, 5.6, 0, False),
    # Skin side. Kept clear of USB-C and other through-hole pads, and not under the module can.
    "U6": (32.2, 36.2, 0, True),
    "J4": (50.2, 18.0, 0, True),
    "C11": (35.0, 37.8, 0, True),
    "C12": (37.2, 37.8, 0, True),
    "C13": (35.0, 36.0, 0, True),
    "C14": (37.2, 36.0, 0, True),
    "C15": (39.2, 37.8, 0, True),
    "R9": (39.2, 36.0, 0, True),
    "R10": (32.2, 33.8, 0, True),
    "U10": (35.4, 33.2, 0, True),
    "U11": (39.0, 33.6, 0, True),
    "U12": (32.2, 30.4, 0, True),
    "C36": (35.4, 30.2, 0, True),
    "C37": (37.6, 30.2, 0, True),
    "C38": (39.8, 30.2, 0, True),
    "C39": (35.4, 28.4, 0, True),
    "C40": (37.6, 28.4, 0, True),
    "C41": (39.8, 28.4, 0, True),
    "U9": (36.2, 26.0, 0, True),
    "R18": (39.6, 26.0, 0, True),
    "R19": (32.4, 26.0, 0, True),
    "R20": (34.4, 24.2, 0, True),
    "R21": (36.6, 24.2, 0, True),
    "R22": (38.8, 24.2, 0, True),
    # AD5940 passives on the back, directly under the BGA.
    "C16": (39.4, 16.4, 0, True),
    "C17": (41.6, 16.4, 0, True),
    "C18": (43.8, 16.4, 0, True),
    "C42": (46.0, 16.4, 0, True),
    "C19": (48.2, 16.4, 0, True),
    "C20": (39.4, 14.6, 0, True),
    "C21": (41.6, 14.6, 0, True),
    "C22": (43.8, 14.6, 0, True),
    "C23": (46.0, 14.6, 0, True),
    "C24": (48.2, 14.6, 0, True),
    "C25": (39.4, 12.8, 0, True),
    "C26": (41.6, 12.8, 0, True),
    "C27": (43.8, 12.8, 0, True),
    "R12": (46.0, 12.8, 0, True),
    "R11": (48.2, 12.8, 0, True),
    "R13": (50.2, 14.6, 90, True),
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
    # Antenna section of the module, from the left edge to the shield.
    for x, y in ((0.0, 0.0), (6.6, 0.0), (6.6, BOARD_H), (0.0, BOARD_H)):
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
    ds.m_MinSilkTextHeight = mm(0.6)
    ds.m_MinSilkTextThickness = mm(0.08)
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
    print(f"parts {len(placed)}  clashes {len(clashes)}  edge {len(edge_hits)}  pad-miss {len(unresolved)}")
    for line in clashes:
        print("  clash", line)
    for line in edge_hits:
        print("  edge", line)
    for line in unresolved:
        print("  pad", line)

    board.SetFileName(str(PCB))
    pcbnew.SaveBoard(str(PCB), board)
    write_bom(comps)
    print(f"wrote {PCB.name}  {BOARD_W:.0f} x {BOARD_H:.0f} mm")
    if clashes or edge_hits or unresolved:
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
