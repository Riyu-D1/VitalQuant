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
# 4 layers. Skin sensors share one bottom cluster; the second TMP117 sits
# on the top at the same XY, outside the module body.
BOARD_W = 36.5
# ECG DPCRs on the bottom, EDA DPCRs on the top at the same X. The charge
# pads sit just above that row. Four more 2512s for the chest bioZ leads
# need their own band past the charger row: a 2512 courtyard is 7.66 mm
# tall once it is turned to face the edge, and it has to clear J2.
BOARD_H = 53.4

# 0.8 mm slots in all four gaps between the ECG/EDA electrode pads.
# Each slot starts across the electrode copper and ends 0.3 mm clear of
# the parts above it. The path through the slot is the creepage path.
CREEP_SLOTS = (
    (11.525, 37.05, 12.325, 41.40),
    (17.975, 37.05, 18.775, 42.60),
    (24.425, 37.05, 25.225, 42.60),
    (30.875, 37.05, 31.675, 42.60),
    # Gaps between the four chest-bioZ 2512 electrode pads (R80-R83).
    (11.525, 50.75, 12.325, 53.05),
    (17.975, 50.75, 18.775, 53.05),
    (24.425, 50.75, 25.225, 53.05),
)

# Internal edge-cut slots (x0, y0, x1, y1), 0.8 mm wide.
# AS7341 / LED barrier, then the thermal island around the stacked TMP117s.
# The top side of the island is open for a 2 mm neck toward the cluster.
SLOTS = (
    (16.40, 15.15, 21.60, 15.95),
    (16.10, 5.54, 21.10, 6.34),
    (16.10, 10.35, 17.60, 11.15),
    (19.60, 10.35, 21.10, 11.15),
    (16.10, 5.54, 16.90, 11.15),
    (20.30, 5.54, 21.10, 11.15),
)

# (x, y, rotation_deg, bottom)
# Rotation is applied before a bottom-side flip.
PLACE = {
    "C1": (31.20, 18.50, 0, True),
    "C2": (30.00, 12.00, 0, True),
    "C3": (31.40, 12.00, 90, True),
    "C4": (32.70, 11.50, 90, True),
    "C6": (32.65, 9.50, 90, True),
    "C7": (14.92, 5.17, 0, True),
    "C8": (12.01, 7.20, 90, True),
    "C9": (17.30, 4.51, 0, False),
    "C10": (31.32, 19.63, 0, True),
    "C11": (22.22, 17.67, 90, True),
    "C13": (22.00, 19.80, 90, True),
    "C14": (22.00, 21.90, 90, True),
    "C15": (20.49, 21.66, 0, True),
    "C16": (29.50, 24.00, 0, True),
    "C18": (30.90, 24.00, 90, True),
    "C19": (25.60, 29.90, 0, True),
    "C20": (29.07, 22.67, 0, True),
    "C21": (32.30, 24.00, 0, True),
    "C22": (31.15, 26.27, 90, True),
    "C23": (27.23, 25.65, 90, True),
    "C24": (29.50, 21.20, 0, True),
    "C25": (31.15, 21.73, 90, True),
    "C26": (32.83, 25.08, 0, True),
    "C27": (32.33, 21.94, 90, True),
    "C28": (15.73, 15.15, 90, True),
    "C29": (22.20, 13.50, 90, True),
    "C30": (8.00, 1.00, 0, True),
    "C32": (21.96, 10.62, 90, True),
    "C33": (12.67, 11.77, 0, False),
    "C34": (14.71, 8.97, 0, True),
    "C35": (23.33, 11.77, 0, True),
    "C36": (15.33, 21.22, 0, True),
    "C37": (28.00, 5.00, 0, True),
    "C38": (29.40, 5.00, 90, True),
    "C39": (12.00, 1.00, 0, True),
    "C40": (18.60, 7.00, 0, True),
    "C41": (18.00, 21.65, 0, True),
    "C42": (30.80, 27.99, 0, True),
    "C43": (23.33, 11.77, 0, False),
    "C44": (34.10, 11.50, 90, True),
    "C45": (30.30, 10.27, 0, True),
    "C46": (30.00, 13.40, 0, True),
    "C47": (33.65, 13.77, 0, True),
    "C48": (23.10, 9.80, 90, True),
    "C49": (13.89, 7.84, 0, True),
    "C50": (22.11, 7.84, 90, True),
    "C51": (13.47, 19.73, 90, True),
    "C52": (11.77, 8.97, 0, True),
    "C53": (24.23, 8.97, 90, True),
    "C54": (30.80, 27.99, 0, False),
    "C55": (28.20, 27.99, 0, True),
    "C56": (26.53, 13.71, 90, True),
    "C57": (27.33, 15.27, 0, True),
    "C58": (28.60, 12.00, 90, True),
    "C59": (14.77, 6.80, 0, True),
    "C60": (13.17, 6.44, 90, True),
    "C61": (18.60, 7.00, 0, False),
    "D1": (8.30, 10.10, 0, True),
    "D2": (10.00, 10.10, 0, True),
    "D3": (11.70, 10.10, 0, True),
    "D4": (13.40, 10.10, 0, True),
    "D5": (15.10, 10.10, 0, True),
    "D6": (14.90, 26.15, 0, True),
    "D7": (16.45, 26.15, 0, True),
    "D8": (18.00, 26.15, 0, True),
    "D9": (19.55, 26.15, 0, True),
    "D10": (18.70, 13.50, 0, True),
    "D11": (21.10, 13.70, 90, True),
    "J1": (32.30, 22.20, 90, False),
    "J2": (10.30, 43.80, 0, True),
    "J3": (23.80, 8.70, 0, False),
    "J5": (11.40, 12.30, 0, True),
    "J6": (17.40, 29.70, 0, True),
    "L1": (27.55, 2.40, 0, False),
    "L2": (28.05, 6.15, 0, False),
    "Q1": (22.30, 14.90, 0, True),
    "Q2": (35.50, 43.80, 0, True),
    "R1": (30.40, 16.05, 0, False),
    "R2": (31.32, 17.37, 0, True),
    "R3": (30.00, 14.80, 0, True),
    "R5": (12.71, 3.97, 0, True),
    "R6": (19.29, 3.97, 90, False),
    "R7": (10.01, 6.55, 0, True),
    "R8": (28.00, 6.40, 0, True),
    "R9": (25.46, 6.35, 0, True),
    "R10": (27.33, 15.27, 0, False),
    "R11": (28.20, 27.99, 0, False),
    "R12": (26.10, 26.47, 90, True),
    "R13": (24.84, 25.51, 90, True),
    "R14": (25.32, 11.12, 0, True),
    "R15": (25.32, 11.12, 0, False),
    "R16": (18.00, 22.60, 0, True),
    "R17": (18.00, 4.40, 0, True),
    "R18": (10.00, 1.00, 0, True),
    "R19": (18.06, 23.83, 0, True),
    "R20": (19.96, 23.88, 0, True),
    "R21": (18.88, 24.96, 0, True),
    "R22": (16.00, 1.00, 0, True),
    "R23": (31.13, 8.84, 0, True),
    "R24": (35.50, 11.50, 90, True),
    "R25": (32.00, 15.00, 0, True),
    "R26": (20.81, 4.85, 0, True),
    "R27": (23.35, 6.14, 0, True),
    "R28": (25.36, 8.15, 90, True),
    "R29": (10.07, 7.74, 0, True),
    "R30": (14.97, 4.18, 0, True),
    "R31": (21.03, 4.18, 90, False),
    "R32": (8.70, 35.25, 90, True),
    "R33": (15.15, 35.25, 90, True),
    "R34": (21.60, 35.25, 90, True),
    "R35": (28.05, 35.25, 90, True),
    "R36": (34.50, 35.25, 90, True),
    "R39": (33.46, 21.12, 90, True),
    "R40": (32.79, 28.53, 0, True),
    "R41": (32.79, 28.53, 0, False),
    "R42": (29.50, 29.60, 0, True),
    "R43": (22.00, 25.40, 0, True),
    "R44": (14.64, 11.65, 0, False),
    "R45": (14.00, 1.00, 0, True),
    "R46": (27.76, 9.07, 0, True),
    "R47": (6.90, 21.00, 90, True),
    "R48": (23.50, 27.40, 90, True),
    "R49": (34.90, 16.15, 0, False),
    "R59": (8.0, 43.80, 0, False),
    "R60": (10.2, 43.80, 0, False),
    "R61": (12.4, 43.80, 0, False),
    "R62": (20.40, 1.15, 0, False),
    "R63": (22.70, 1.15, 0, False),
    "R64": (25.00, 1.15, 0, False),
    "R65": (20.40, 2.55, 0, False),
    "C62": (22.70, 2.55, 0, False),
    "C63": (25.00, 2.55, 0, False),
    "R66": (18.80, 43.80, 0, True),
    "R67": (20.95, 43.80, 0, True),
    "R68": (23.10, 43.80, 0, True),
    "R69": (25.25, 43.80, 0, True),
    "R70": (27.40, 43.80, 0, True),
    "R71": (29.55, 43.80, 0, True),
    "R72": (31.70, 43.80, 0, True),
    "R73": (33.85, 43.80, 0, True),
    "R74": (14.6, 43.80, 0, False),
    "R75": (16.8, 43.80, 0, False),
    "C64": (19.0, 43.80, 0, False),
    "C65": (21.2, 43.80, 0, False),
    "C66": (23.4, 43.80, 0, False),
    "Q3": (33.2, 44.15, 0, False),
    "R76": (8.70, 35.70, 90, False),
    "R77": (15.15, 35.70, 90, False),
    "R78": (21.60, 35.70, 90, False),
    "R79": (28.05, 35.70, 90, False),
    "R80": (8.70, 49.40, 90, True),
    "R81": (15.15, 49.40, 90, True),
    "R82": (21.60, 49.40, 90, True),
    "R83": (28.05, 49.40, 90, True),
    "R84": (7.60, 46.55, 0, False),
    "R85": (9.65, 46.55, 0, False),
    "R86": (11.70, 46.55, 0, False),
    "R87": (13.75, 46.55, 0, False),
    "C67": (15.80, 46.55, 0, False),
    "C68": (17.85, 46.55, 0, False),
    "C69": (19.90, 46.55, 0, False),
    "C70": (21.95, 46.55, 0, False),
    "D21": (24.00, 46.55, 0, False),
    "D22": (26.05, 46.55, 0, False),
    "D23": (28.10, 46.55, 0, False),
    "D24": (30.15, 46.55, 0, False),
    "J7": (32.00, 51.90, 0, False),
    "R50": (31.30, 15.99, 0, True),
    "R51": (28.70, 8.01, 0, True),
    "R52": (16.00, 2.90, 0, True),
    "R53": (17.73, 3.17, 0, False),
    "U1": (12.85, 22.00, 90, False),
    "U2": (16.00, 43.80, 0, True),
    "U3": (32.60, 9.80, 0, False),
    "U4": (34.10, 13.95, 0, False),
    "U5": (32.80, 4.10, 0, False),
    "U6": (24.60, 13.80, 0, True),
    "U7": (9.55, 27.35, 90, True),
    "U8": (25.40, 21.60, 0, True),
    "U9": (25.00, 17.20, 0, True),
    "U10": (18.70, 18.50, 0, True),
    "U11": (18.60, 9.15, 0, True),
    "U12": (10.15, 21.40, 0, True),
    "U13": (14.50, 9.55, 0, False),
    "U14": (28.80, 9.60, 0, False),
    "U15": (35.50, 8.90, 0, False),
    "U16": (10.70, 16.50, 0, True),
    "U17": (11.70, 9.65, 0, False),
    "U18": (11.40, 4.20, 0, False),
    "U19": (8.70, 9.60, 0, False),
    "U20": (18.60, 9.15, 0, False),
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
        dnp = atom(comp, "dnp", None)
        comps[ref] = {
            "value": atom(comp, "value", ""),
            "footprint": atom(comp, "footprint", ""),
            "dnp": dnp in ("yes", "true", "1") or ref in DNP_REFS,
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


def add_poly(board, pts):
    """One simple Edge.Cuts loop. Used for the thermal moat so the cuts do not cross."""
    for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
        add_edge(board, x0, y0, x1, y1)


# Thermal moat around U11/U20. Neck is the missing top span, toward the cluster.
ISLAND = (
    (16.10, 11.15),
    (16.10, 5.54),
    (21.10, 5.54),
    (21.10, 11.15),
    (19.60, 11.15),
    (19.60, 10.35),
    (20.30, 10.35),
    (20.30, 6.34),
    (16.90, 6.34),
    (16.90, 10.35),
    (17.60, 10.35),
    (17.60, 11.15),
)


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
        if meta.get("dnp") and hasattr(fp, "SetDNP"):
            fp.SetDNP(True)
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
    add_slot(board, *SLOTS[0])  # AS7341 / LED barrier
    for _slot in CREEP_SLOTS:
        add_slot(board, *_slot)
    add_poly(board, ISLAND)
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
            for sx0, sy0, sx1, sy1 in SLOTS + CREEP_SLOTS:
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
    creepage_report(placed, pin_net)
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
    "U2": "BQ25170DSGR",
    "Q3": "BC847BS,115",
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
    "Q2": "CSD13380F3",
    "R32": "DPCR2512-51KJT18",
    "R33": "DPCR2512-51KJT18",
    "R34": "DPCR2512-51KJT18",
    "R35": "DPCR2512-51KJT18",
    "R36": "DPCR2512-51KJT18",
    "R76": "DPCR2512-51KJT18",
    "R77": "DPCR2512-51KJT18",
    "R78": "DPCR2512-51KJT18",
    "R79": "DPCR2512-51KJT18",
    "R80": "DPCR2512-51KJT18",
    "R81": "DPCR2512-51KJT18",
    "R82": "DPCR2512-51KJT18",
    "R83": "DPCR2512-51KJT18",
    "R61": "NCU15XH103F6SRC",
    "D10": "NF2W757G-F1",
    "D11": "SFH 4053",
    "L1": "DFE201612E-R47M",
    "L2": "DFE201612E-1R0M",
    "J1": "TYPE-C-31-M-12",
}
for _ref in ("D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D21", "D22", "D23", "D24"):
    MPN[_ref] = "TPD1E10B06DPYR"
DNP_REFS = {"R61"}

R_MPN = {
    "10": "RC0402FR-0710RL",
    "100": "RC0402FR-07100RL",
    "200": "RC0402FR-07200RL",
    "1k": "RC0402FR-071KL",
    "4.7k": "RC0402FR-074K7L",
    "5.1k": "RC0402FR-075K1L",
    "10k": "RC0402FR-0710KL",
    "22.1k": "RC0402FR-0722K1L",
    "40.2k": "RC0402FR-0740K2L",
    "47.5k": "RC0402FR-0747K5L",
    "49.9k": "RC0402FR-0749K9L",
    "100k": "RC0402FR-07100KL",
    "200k": "RC0402FR-07200KL",
    "301k": "RC0402FR-07301KL",
    "560k": "RC0402FR-07560KL",
    "1M": "RC0402FR-071ML",
    "5.11M": "RC0402FR-075M11L",
    "10M": "RC0402FR-0710ML",
    "3.0k": "RC0402FR-073KL",
    "27.0k": "RC0402FR-0727KL",
}
# 0402 unless the footprint is 0603.
C_MPN_0402 = {
    "1.5n": "GRM1555C1H152JA01",
    "2.2n": "GRM1555C1H222JA01",
    "4.7n": "GRM1555C1H472JA01",
    "47n": "GRM155R71H473KA88",
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


ELECTRODE_NETS = {
    "ECG1_PAD", "ECG2_PAD", "RLD_PAD", "AFE_P_PAD", "AFE_N_PAD",
    "EDA_CE_PAD", "EDA_SE_PAD", "EDA_RE_PAD", "EDA_DE_PAD",
    "BIOZ_FP_PAD", "BIOZ_FN_PAD", "BIOZ_SP_PAD", "BIOZ_SN_PAD",
}


def _box_mm(box):
    x0 = box.GetX() / 1e6
    y0 = box.GetY() / 1e6
    x1 = (box.GetX() + box.GetWidth()) / 1e6
    y1 = (box.GetY() + box.GetHeight()) / 1e6
    return x0, y0, x1, y1


def _gap(a, b):
    ax0, ay0, ax1, ay1 = a
    bx0, by0, bx1, by1 = b
    dx = max(0.0, max(ax0, bx0) - min(ax1, bx1))
    # separate if not overlapping in that axis
    if ax1 < bx0:
        dx = bx0 - ax1
    elif bx1 < ax0:
        dx = ax0 - bx1
    else:
        dx = 0.0
    if ay1 < by0:
        dy = by0 - ay1
    elif by1 < ay0:
        dy = ay0 - by1
    else:
        dy = 0.0
    return (dx * dx + dy * dy) ** 0.5


def _pad_layers(pad, fp):
    # Flipped SMD pads still report F.Cu from GetLayerName(); the footprint layer is the copper side.
    if pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
        return {"F.Cu", "B.Cu"}
    return {fp.GetLayerName()}


def creepage_report(placed, pin_net):
    """Same-layer edge-to-edge gap from each electrode pad to other copper."""
    pads = []
    for ref, (fp, _bottom) in placed.items():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            net = pin_net.get((ref, pad.GetNumber()), "")
            pads.append(
                (ref, pad.GetNumber(), net, _box_mm(pad.GetBoundingBox()), _pad_layers(pad, fp))
            )
    worst = []
    for ra, pa, na, ba, la in pads:
        if na not in ELECTRODE_NETS:
            continue
        best = None
        for rb, pb, nb, bb, lb in pads:
            if (ra, pa) == (rb, pb) or nb == na or not (la & lb):
                continue
            gap = _gap(ba, bb)
            if best is None or gap < best[0]:
                best = (gap, f"{ra}.{pa}", na, f"{rb}.{pb}", nb)
        if best:
            worst.append(best)
    worst.sort()
    print("electrode creepage, closest other copper on the same layer:")
    for gap, a, na, b, nb in worst:
        flag = "  SHORT OF 4 mm" if gap < 4.0 else ""
        print(f"  {gap:.2f} mm  {a} ({na}) to {b} ({nb}){flag}")


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
                "DNP" if meta.get("dnp") else "",
            )
        )
    with BOM.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Ref", "Value", "MPN", "Footprint", "Qty", "DNP"])
        w.writerows(rows)


if __name__ == "__main__":
    sys.exit(main())
