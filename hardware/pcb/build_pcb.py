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
# The module antenna keep-out is the left 6.5 mm. USB-C is rotated so its
# opening is the right edge; its centre sits 4.2 mm in from that edge.
# Height clears the USB shell (centre 5.17 mm down from the top edge) and a
# row of parts above the module. 4 layers. The board is rectangular.
BOARD_W = 40.0
BOARD_H = 34.0

# Internal edge-cut slots (x0, y0, x1, y1), 0.8 mm wide.
# Thermal island for the two TMP117s, plus the AS7341 / LED barrier.
SLOTS = (
    (29.15, 0.70, 29.95, 5.40),
    (31.95, 5.90, 39.40, 6.70),
    (34.35, 1.15, 35.15, 6.05),
    (16.60, 8.06, 25.60, 8.86),
)

# (x, y, rotation_deg, bottom)
# Rotation is applied before a bottom-side flip.
PLACE = {
    "C1": (29.50, 26.00, 90, False),
    "C2": (32.00, 12.00, 90, False),
    "C3": (33.50, 10.50, 90, False),
    "C4": (27.00, 12.50, 90, False),
    "C6": (30.50, 12.00, 90, False),
    "C7": (18.00, 27.20, 90, True),
    "C8": (16.50, 25.70, 90, True),
    "C9": (27.00, 10.00, 90, False),
    "C10": (28.00, 26.00, 90, False),
    "C11": (24.50, 20.50, 90, True),
    "C13": (26.00, 22.00, 90, True),
    "C14": (25.50, 24.00, 90, True),
    "C15": (23.50, 15.00, 90, True),
    "C16": (19.50, 29.00, 90, True),
    "C18": (16.50, 28.00, 90, True),
    "C19": (18.00, 29.50, 90, True),
    "C20": (18.00, 25.00, 90, True),
    "C21": (15.00, 24.50, 90, True),
    "C22": (21.00, 30.50, 90, True),
    "C23": (15.00, 26.50, 90, True),
    "C24": (21.00, 28.50, 90, True),
    "C25": (19.50, 31.00, 90, True),
    "C26": (16.50, 23.50, 90, True),
    "C27": (17.50, 31.50, 90, True),
    "C28": (16.00, 21.00, 90, True),
    "C29": (14.50, 22.50, 90, True),
    "C30": (14.50, 20.00, 90, True),
    "C32": (17.00, 19.00, 90, True),
    "C33": (18.00, 23.00, 90, True),
    "C34": (13.00, 21.00, 90, True),
    "C35": (13.50, 24.50, 90, True),
    "C36": (15.50, 7.50, 90, True),
    "C37": (29.00, 6.70, 90, False),
    "C38": (27.50, 27.50, 0, False),
    "C39": (17.00, 7.00, 90, True),
    "C40": (32.20, 1.45, 0, True),
    "C41": (14.00, 6.00, 90, True),
    "C42": (22.50, 32.00, 90, True),
    "C43": (11.50, 21.00, 90, True),
    "C44": (30.10, 14.15, 0, False),
    "C45": (34.40, 14.15, 0, False),
    "C46": (32.50, 8.50, 90, False),
    "C47": (38.30, 14.15, 0, False),
    "C48": (11.00, 26.00, 90, True),
    "C49": (12.50, 26.50, 90, True),
    "C50": (17.00, 15.00, 90, True),
    "C51": (10.00, 21.00, 90, True),
    "C52": (9.50, 26.00, 90, True),
    "C53": (22.50, 28.50, 90, True),
    "C54": (21.00, 32.50, 90, True),
    "C55": (24.00, 28.50, 90, True),
    "C56": (25.00, 15.00, 90, True),
    "C57": (25.50, 26.00, 90, True),
    "C58": (27.50, 7.50, 90, False),
    "C59": (24.00, 30.70, 90, True),
    "C60": (24.00, 32.70, 90, True),
    "C61": (37.70, 1.45, 0, True),
    "D1": (13.10, 3.85, 0, True),
    "D2": (14.60, 3.85, 0, True),
    "D3": (16.10, 3.85, 0, True),
    "D4": (17.60, 3.85, 0, True),
    "D5": (19.10, 3.85, 0, True),
    "D6": (9.20, 28.10, 0, True),
    "D7": (10.80, 28.10, 0, True),
    "D8": (12.40, 28.10, 0, True),
    "D9": (14.00, 28.10, 0, True),
    "D10": (20.00, 6.70, 0, True),
    "D11": (23.30, 6.80, 90, True),
    "J1": (35.80, 28.83, 90, False),
    "J2": (36.40, 10.60, 0, False),
    "J3": (25.80, 3.15, 0, False),
    "J5": (16.40, 1.70, 0, True),
    "J6": (12.40, 32.20, 0, True),
    "L1": (38.55, 21.50, 0, False),
    "L2": (37.30, 17.30, 0, False),
    "Q1": (24.90, 6.80, 0, True),
    "R1": (29.50, 23.50, 0, False),
    "R2": (29.50, 27.50, 0, False),
    "R3": (33.50, 15.50, 0, False),
    "R5": (25.50, 28.20, 90, True),
    "R6": (25.50, 30.20, 90, True),
    "R7": (25.50, 32.20, 90, True),
    "R8": (32.50, 5.00, 0, False),
    "R9": (27.00, 26.00, 90, True),
    "R10": (28.50, 26.00, 90, True),
    "R11": (27.00, 28.00, 90, True),
    "R12": (8.50, 21.00, 90, True),
    "R13": (8.00, 26.00, 90, True),
    "R14": (16.00, 13.00, 90, True),
    "R15": (17.50, 13.00, 90, True),
    "R16": (24.00, 13.00, 90, True),
    "R17": (7.50, 14.50, 90, True),
    "R18": (16.00, 10.00, 90, True),
    "R19": (17.50, 10.00, 90, True),
    "R20": (15.50, 5.50, 90, True),
    "R21": (12.50, 5.50, 90, True),
    "R22": (12.50, 7.50, 90, True),
    "R23": (33.50, 19.00, 0, False),
    "R24": (35.50, 19.00, 0, False),
    "R25": (35.50, 15.50, 0, False),
    "R26": (7.50, 16.50, 90, True),
    "R27": (7.50, 18.50, 90, True),
    "R28": (7.50, 23.00, 90, True),
    "R29": (7.50, 28.00, 90, True),
    "R30": (7.00, 30.00, 90, True),
    "R31": (7.00, 20.50, 90, True),
    "R32": (25.50, 11.50, 90, True),
    "R33": (24.00, 11.00, 90, True),
    "R34": (7.00, 32.00, 90, True),
    "R35": (30.00, 26.00, 90, True),
    "R36": (28.50, 28.00, 90, True),
    "R39": (30.00, 28.00, 90, True),
    "R40": (27.00, 12.50, 90, True),
    "R41": (28.50, 12.50, 90, True),
    "R42": (30.00, 12.50, 90, True),
    "R43": (31.50, 12.50, 90, True),
    "R44": (33.00, 12.00, 90, True),
    "R45": (27.00, 10.50, 90, True),
    "R46": (28.50, 10.50, 90, True),
    "R47": (11.00, 3.00, 90, True),
    "R48": (11.00, 5.00, 90, True),
    "R49": (27.50, 14.00, 0, False),
    "R50": (30.50, 7.00, 0, False),
    "R51": (37.50, 15.50, 0, False),
    "R52": (34.50, 10.70, 90, True),
    "R53": (30.00, 10.20, 90, True),
    "U1": (12.75, 15.50, 90, False),
    "U2": (30.00, 21.40, 0, False),
    "U3": (35.00, 21.50, 0, False),
    "U4": (30.00, 17.10, 0, False),
    "U5": (10.30, 30.30, 0, False),
    "U6": (24.70, 17.60, 0, True),
    "U7": (21.80, 24.60, 0, True),
    "U8": (20.30, 18.90, 0, True),
    "U9": (10.60, 23.60, 0, True),
    "U10": (20.40, 12.40, 0, True),
    "U11": (32.20, 3.55, 0, True),
    "U12": (11.00, 17.30, 0, True),
    "U13": (30.20, 9.40, 0, False),
    "U14": (27.40, 30.50, 0, False),
    "U15": (34.30, 17.30, 0, False),
    "U16": (11.20, 11.00, 0, True),
    "U17": (28.90, 12.40, 0, False),
    "U18": (18.70, 30.10, 0, False),
    "U19": (28.80, 31.40, 0, True),
    "U20": (37.70, 3.55, 0, True),
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
    # This KiCad install has no Connector_USB 3D shapes. The official STEP
    # ships with the royalblue demo; keep a copy next to the other models.
    if name.startswith("USB_C_Receptacle_HRO"):
        step = ROOT / "lib" / "vitalq.3d" / "USB_C_Receptacle_HRO_TYPE-C-31-M-12.step"
        models = fp.Models()
        if len(models):
            models[0].m_Filename = str(step)
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


def add_slot(board, x0, y0, x1, y1):
    add_edge(board, x0, y0, x1, y0)
    add_edge(board, x1, y0, x1, y1)
    add_edge(board, x1, y1, x0, y1)
    add_edge(board, x0, y1, x0, y0)


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
    for slot in SLOTS:
        add_slot(board, *slot)
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
            for sx0, sy0, sx1, sy1 in SLOTS:
                # Expand the slot by the required copper clearance and test a hit.
                if not (
                    x1 <= sx0 - 0.3
                    or x0 >= sx1 + 0.3
                    or y1 <= sy0 - 0.3
                    or y0 >= sy1 + 0.3
                ):
                    edge_hits.append(f"{ref}.{pad.GetNumber()} slot")
                    break
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


# Exact orderable numbers. 0402 passives are Yageo RC0402FR (1%) and Murata GRM155.
# 0603 bulk capacitors are the Murata parts named in the regulator tables.
MPN = {
    "U1": "ESP32-WROOM-32E-N8R2",
    "U2": "MCP73831T-2ACI/OT",
    "U3": "TPS63802DLAR",
    "U4": "TPS7A2018PDBVR",
    "U5": "CP2102N-A02-GQFN28R",
    "U6": "AFE4900YZR",
    "U7": "AD5940BCBZ-RL7",
    "U8": "ADS1292RIRSMT",
    "U9": "PCA9306DCUR",
    "U10": "AS7341-DLGM",
    "U11": "TMP117AIDRVR",
    "U12": "MLX90632SLD-DCB-100-SP",
    "U13": "BME280",
    "U14": "LSM6DSV80XTR",
    "U15": "TPS61240YFFR",
    "U16": "SFH 7072",
    "U17": "MAX17048G+T10",
    "U18": "W25Q512JVEIQ",
    "U19": "TCA6408ARSVR",
    "U20": "TMP117AIDRVR",
    "Q1": "CSD13380F3",
    "D10": "NF2W757G-F1",
    "D11": "SFH 4053",
    "L1": "DFE201612E-R47M",
    "L2": "DFE201612E-1R0M",
    "J1": "TYPE-C-31-M-12",
}
for _ref in ("D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9"):
    MPN[_ref] = "TPD1E10B06DPYR"

R_MPN = {
    "10": "RC0402FR-0710RL",
    "100": "RC0402FR-07100RL",
    "200": "RC0402FR-07200RL",
    "1k": "RC0402FR-071KL",
    "4.7k": "RC0402FR-074K7L",
    "5.1k": "RC0402FR-075K1L",
    "10k": "RC0402FR-0710KL",
    "40.2k": "RC0402FR-0740K2L",
    "49.9k": "RC0402FR-0749K9L",
    "100k": "RC0402FR-07100KL",
    "200k": "RC0402FR-07200KL",
    "560k": "RC0402FR-07560KL",
    "1M": "RC0402FR-071ML",
    "10M": "RC0402FR-0710ML",
}
# 0402 unless the footprint is 0603.
C_MPN_0402 = {
    "2.2n": "GRM1555C1H222JA01",
    "4.7n": "GRM1555C1H472JA01",
    "15n": "GRM155R71H153KA12",
    "100n": "GRM155R71C104KA88",
    "470n": "GRM155R61A474KE15",
    "1u": "GRM155R61A105KE15",
    "2.2u": "GRM155R60J225ME15",
    "4.7u": "GRM155R60J475ME87",
    "10u": "GRM155R60J106ME05",
}
C_MPN_0603 = {
    "4.7u": "GRM188R61A475KE15",
    "10u": "GRM188R61A106ME69",
    "22u": "GRM188R61A226ME15",
}


def mpn_for(ref, value, footprint):
    if ref in MPN:
        return MPN[ref]
    if ref.startswith("R"):
        return R_MPN[value]
    if ref.startswith("C"):
        table = C_MPN_0603 if "0603" in footprint else C_MPN_0402
        return table[value]
    if ref.startswith("J"):
        return "solder pads"
    return ""


def write_bom(comps):
    rows = []
    key = lambda kv: (kv[0][0], int("".join(ch for ch in kv[0] if ch.isdigit()) or "0"))
    for ref, meta in sorted(comps.items(), key=key):
        rows.append(
            (
                ref,
                meta["value"],
                mpn_for(ref, meta["value"], meta["footprint"]),
                meta["footprint"],
                "1",
            )
        )
    with BOM.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Ref", "Value", "MPN", "Footprint", "Qty"])
        w.writerows(rows)


if __name__ == "__main__":
    sys.exit(main())
