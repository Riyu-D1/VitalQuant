#!/usr/bin/env python3
"""Unrouted wearable placement for VitalQ hw_v1 → hw_v2.

The schematic netlist is the connectivity source. This script loads every
footprint, assigns pad nets from that netlist, and parks the parts in
functional clusters. It does not route.

hw_v2: every PLACE coordinate below is on the 40.0 × 62.0 mm outline
(FLOORPLAN_V2.md §6 retarget). The skin sensor cluster stays co-registered
on the bottom at y~44-56, the defib ladder rows run at y~28/36 with the
creepage slots between columns, and the tail pads dress the top edge.
board_finish.py still carries the old 36.5 × 70 constants, so main() retargets
its board geometry + keep-outs via module attribute patches before
finish_board() runs.
"""

from __future__ import annotations

import csv
import os
import subprocess
import sys
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent
SCH = ROOT / "vitalq_hw_v1.kicad_sch"
PCB = ROOT / "vitalq_hw_v1.kicad_pcb"
BOM = ROOT / "vitalq_hw_v1_bom.csv"
KICAD_FP = Path(os.environ.get("KICAD_FOOTPRINT_DIR", "")) if os.environ.get("KICAD_FOOTPRINT_DIR") else Path("/usr/share/kicad/footprints")
if not KICAD_FP.is_dir():
    for _cand in (
        Path("/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"),
        Path("/usr/share/kicad/footprints"),
        Path("/usr/local/share/kicad/footprints"),
    ):
        if _cand.is_dir():
            KICAD_FP = _cand
            break

# Millimetres. Origin is the lower-left corner, Y up (KiCad board coordinates).
# hw_v2 outline is 40.0 x 62.0 (FLOORPLAN_V2.md). 4 layers. Skin sensors share
# one bottom cluster; the second TMP117 sits on the top at the same XY.
# USB-C (J1) is on the top edge at y~2.7; the ESP32-S3-MINI-1 antenna tab
# overhangs the top edge above U1's pad field (x 4.7..20.1).
BOARD_W = 40.0
# Patient-facing defib ladder runs as two 2512 rows at y~28.4 (bottom:
# R32-R36, top: R76-R79) and y~36.3 (bottom: R80-R83), with the creepage
# slots cut between the columns. Test points and fiducials scatter through
# the skin-cluster band; the connector tails dress the top edge (y~60.4).
BOARD_H = 62.0

# 1.0 mm slots (JLC NPTH slot minimum) in the gaps between the electrode
# pad columns of both ladder rows. The path through the slot is the
# creepage path; each slot spans the electrode copper and clears the
# surrounding parts by >=0.3 mm.
CREEP_SLOTS = (
    (6.825, 22.3, 7.825, 26.7),
    (13.275, 22.3, 14.275, 26.7),
    (19.725, 22.3, 20.725, 26.7),
    (26.175, 22.3, 27.175, 26.7),
    (6.825, 38.0, 7.825, 42.4),
    (13.275, 38.0, 14.275, 42.4),
    (20.1, 38.0, 21.1, 42.4),
)

# Internal edge-cut slots (x0, y0, x1, y1). SLOTS[0] is the AS7341/LED
# barrier under the optical cluster (drawn via add_slot); SLOTS[1:] are
# check-rects mirroring the drawn ISLAND moat channel — they keep the
# pad-to-cut clearance gates honest around the U11/U20 TMP117 pair at
# (20.2, 47.9). MLX90632 (U12 at 10.3, 48.3)
# has three sides slotted with the high-Y side open as a trace bridge.
SLOTS = (
    (22.0, 51.0, 23.0, 52.0),
    # TMP117 moat check-rects — they mirror the ISLAND channel below (the
    # polygon is what actually gets drawn; these let the clearance gates
    # test pads against the real cut edges, including the neck).
    (17.2, 43.69, 18.53, 50.2),
    (18.53, 43.69, 21.87, 45.4),
    (21.87, 43.69, 23.1, 50.2),
    (20.7, 49.2, 21.87, 50.2),
)
MLX_SLOTS = (
    (7.3, 49.05, 8.3, 50.1),
    (11.9, 47.0, 12.9, 49.8),
    (9.3, 45.1, 11.5, 46.1),
)

# (x, y, rotation_deg, bottom)
# Rotation is applied before a bottom-side flip.
# hw_v2 40 x 62 layout: U1 + comms at the bottom edge (y<20), the defib
# ladder rows run across y~28.4/36.3 with creepage slots between columns,
# the skin sensor cluster co-registers on the bottom at y~44-56, and the
# tail pads + debug connectors dress the top edge (y~60).
PLACE = {
    "U1": (12.40, 10.00, 0, False),
    "J1": (26.00, 2.70, 180, False),
    "U5": (24.50, 13.40, 0, False),
    "Q3": (22.80, 18.50, 0, False),
    "C9": (26.20, 17.50, 0, False),
    "C64": (19.90, 9.00, 0, True),
    "SW1": (2.20, 17.60, 90, False),
    "SW2": (2.20, 11.30, 90, False),
    "R5": (22.40, 9.40, 0, False),
    "R6": (24.50, 8.70, 0, False),
    "R64": (26.60, 9.40, 0, False),
    "R62": (29.50, 9.30, 0, False),
    "R65": (28.80, 10.60, 0, False),
    "C62": (30.20, 12.30, 90, False),
    "C63": (18.79, 11.93, 90, True),
    "R10": (30.90, 14.30, 90, False),
    "R32": (4.10, 28.40, 270, True),
    "R33": (10.55, 28.40, 270, True),
    "R34": (17.00, 28.40, 270, True),
    "R35": (23.45, 28.40, 270, True),
    "R36": (29.90, 28.40, 270, True),
    "R76": (4.10, 28.40, 270, False),
    "R77": (10.55, 28.40, 270, False),
    "R78": (17.00, 28.40, 270, False),
    "R79": (23.45, 28.40, 270, False),
    "R80": (4.10, 36.30, 90, True),
    "R81": (10.55, 36.30, 90, True),
    "R82": (17.00, 36.30, 90, True),
    "R83": (23.45, 36.30, 90, True),
    "U3": (29.50, 32.00, 0, False),
    "L1": (30.10, 28.80, 0, False),
    "U4": (29.50, 19.30, 0, False),
    "U15": (26.00, 19.90, 0, False),
    "L2": (29.00, 16.40, 0, False),
    "U17": (6.00, 34.00, 0, False),
    "U13": (10.00, 34.20, 0, False),
    "U14": (14.00, 34.50, 0, False),
    "U18": (4.60, 46.10, 0, False),
    "U19": (30.10, 46.20, 0, False),
    "U20": (20.20, 47.90, 0, False),
    "U25": (10.82, 41.30, 90, False),
    "D25": (4.00, 40.40, 0, False),
    "R49": (26.30, 31.00, 90, False),
    "R15": (30.30, 25.90, 0, False),
    "R1": (30.50, 20.05, 90, True),
    "D1": (4.90, 20.00, 0, False),
    "D2": (6.35, 20.00, 0, False),
    "D3": (7.80, 20.00, 0, False),
    "D6": (12.15, 20.00, 0, False),
    "D7": (13.60, 20.00, 0, False),
    "D8": (15.05, 20.00, 0, False),
    "D9": (16.50, 20.00, 0, False),
    "D21": (17.95, 20.00, 0, False),
    "D22": (19.45, 20.00, 0, False),
    "D23": (20.90, 20.20, 0, False),
    "D24": (23.40, 20.40, 0, False),
    "R39": (5.10, 18.85, 0, False),
    "R40": (7.10, 18.85, 0, False),
    "C21": (13.10, 18.85, 0, False),
    "C22": (15.10, 18.85, 0, False),
    "C42": (13.53, 14.38, 0, True),
    "R88": (19.30, 18.85, 0, False),
    "R93": (30.60, 35.70, 90, False),
    "R94": (11.75, 6.00, 90, True),
    "R95": (5.80, 50.00, 0, False),
    "R96": (5.80, 51.80, 0, False),
    "J8": (26.45, 50.70, 0, False),
    "J3": (26.50, 60.35, 0, False),
    "J7": (14.50, 60.35, 0, False),
    "J5": (18.60, 60.40, 0, True),
    "J6": (9.00, 60.40, 0, True),
    "J13": (29.60, 60.40, 0, True),
    "J9": (38.80, 15.00, 90, True),
    "J10": (38.80, 43.40, 90, True),
    "J11": (38.80, 55.00, 90, True),
    "J12": (3.30, 50.00, 90, True),
    "J2": (12.40, 11.90, 0, True),
    "U7": (9.50, 19.20, 90, True),
    "U8": (24.50, 16.50, 0, True),
    "U9": (18.90, 19.50, 0, True),
    "U2": (16.00, 14.80, 0, True),
    "U21": (23.20, 10.80, 0, True),
    "U24": (29.40, 17.00, 0, True),
    "Q2": (18.70, 14.30, 0, True),
    "Q1": (14.90, 17.20, 0, True),
    "R59": (13.60, 18.40, 0, True),
    "R60": (16.60, 17.60, 0, True),
    "R61": (18.70, 17.30, 0, True),
    "R66": (22.50, 20.50, 0, True),
    "R8": (30.30, 14.60, 0, True),
    "R25": (27.00, 10.40, 0, True),
    "C69": (26.40, 12.90, 0, True),
    "C68": (20.40, 13.40, 0, True),
    "R102": (23.90, 8.00, 0, True),
    "R103": (26.40, 6.90, 0, True),
    "R104": (28.50, 7.60, 0, True),
    "R105": (24.00, 6.50, 0, True),
    "R100": (24.60, 20.30, 0, True),
    "R101": (15.50, 20.40, 0, True),
    "R42": (28.60, 43.40, 0, True),
    "C1": (30.50, 35.40, 90, True),
    "U16": (13.40, 53.50, 0, True),
    "U10": (24.30, 54.00, 0, True),
    "U12": (10.30, 48.30, 0, True),
    "U11": (20.20, 47.90, 0, True),
    "U6": (25.70, 48.30, 0, True),
    "U22": (26.10, 45.05, 0, True),
    "U23": (7.60, 44.50, 0, True),
    "D10": (19.70, 52.60, 90, True),
    "D11": (14.00, 46.30, 0, True),
    "D12": (15.90, 48.30, 0, True),
    "Q4": (12.30, 44.40, 0, True),
    "R84": (10.60, 44.30, 0, True),
    "R85": (14.00, 45.10, 0, True),
    "R86": (16.10, 45.70, 0, True),
    "R87": (14.80, 49.50, 0, True),
    "C80": (30.50, 39.50, 0, True),
    "C81": (30.16, 60.00, 90, False),
    "C82": (10.40, 44.20, 0, False),
    "C83": (24.61, 44.85, 0, False),
    "C78": (21.00, 54.05, 0, False),
    "C79": (20.80, 55.55, 0, False),
    "C8": (6.33, 5.18, 0, True),
    "C67": (5.21, 54.55, 0, False),
    "C70": (1.60, 56.05, 0, False),
    "C76": (3.61, 56.05, 0, False),
    "C77": (19.00, 54.05, 0, False),
    "C66": (30.79, 12.54, 90, True),
    "C61": (10.00, 54.05, 0, False),
    "C65": (12.00, 54.05, 0, False),
    "C60": (5.61, 53.05, 0, False),
    "C59": (7.81, 52.05, 0, False),
    "FID1": (24.90, 55.00, 0, False),
    "FID2": (2.00, 58.60, 0, False),
    "FID3": (5.50, 58.60, 0, False),
    "FID5": (30.30, 56.20, 0, False),
    "FID6": (28.20, 56.50, 0, True),
    "H1": (2.60, 6.00, 0, True),
    "H2": (1.85, 1.90, 0, False),
    "TP1": (16.40, 42.50, 0, False),
    "TP2": (18.60, 42.50, 0, False),
    "TP3": (23.00, 42.50, 0, False),
    "TP4": (25.20, 42.50, 0, False),
    "TP5": (18.20, 55.75, 0, False),
    "TP6": (18.20, 51.75, 0, False),
    "TP7": (14.95, 56.25, 0, False),
    "TP8": (25.20, 46.50, 0, False),
    "TP9": (27.40, 46.50, 0, False),
    "TP10": (5.70, 56.25, 0, False),
    "TP11": (1.00, 50.50, 0, False),
    "TP12": (3.20, 50.50, 0, False),
    "TP13": (9.20, 37.00, 0, False),
    "TP14": (16.40, 54.50, 0, False),
    "TP15": (27.40, 54.50, 0, False),
    "TP16": (22.00, 57.50, 0, False),
    "TP17": (7.70, 53.75, 0, False),
    "TP18": (7.20, 28.25, 0, False),
    "TP19": (13.70, 28.25, 0, False),
    "TP20": (20.20, 28.25, 0, False),
    "TP21": (26.70, 28.25, 0, False),
    "TP22": (9.45, 55.75, 0, False),
    "TP23": (12.20, 55.75, 0, False),
    "TP24": (27.40, 38.50, 0, False),
    "TP25": (1.00, 36.50, 0, False),
    "TP26": (20.30, 36.40, 0, False),
    "TP27": (20.90, 51.70, 0, False),
    "FID4": (6.55, 6.95, 0, True),
    "C10": (9.38, 6.42, 0, True),
    "C44": (18.80, 5.20, 0, True),
    "C45": (15.77, 6.42, 0, True),
    "C47": (29.90, 21.90, 0, False),
    "C7": (2.17, 10.33, 0, True),
    "R106": (8.83, 8.16, 0, True),
    "R107": (10.82, 8.16, 0, True),
    "R108": (12.82, 8.16, 0, True),
    "R109": (16.10, 48.70, 0, False),
    "R11": (16.82, 8.16, 0, True),
    "R110": (14.20, 44.00, 0, True),
    "R112": (4.83, 10.07, 0, True),
    "R113": (6.83, 10.07, 0, True),
    "R114": (12.40, 44.40, 0, False),
    "R12": (6.83, 11.57, 0, True),
    "R13": (1.62, 12.07, 0, True),
    "R14": (20.22, 14.67, 0, True),
    "R16": (18.63, 16.16, 0, True),
    "R17": (20.62, 16.16, 0, True),
    "R18": (28.23, 14.47, 0, True),
    "R19": (28.23, 18.97, 0, True),
    "R2": (1.62, 14.07, 0, True),
    "R20": (3.63, 14.07, 0, True),
    "R21": (1.62, 15.57, 0, True),
    "R22": (3.63, 15.57, 0, True),
    "R23": (1.62, 17.07, 0, True),
    "R24": (3.63, 17.07, 0, True),
    "R26": (1.62, 18.57, 0, True),
    "R27": (3.63, 18.57, 0, True),
    "R28": (1.62, 20.07, 0, True),
    "R29": (3.63, 20.07, 0, True),
    "R3": (5.17, 14.53, 90, True),
    "R30": (5.17, 16.52, 90, True),
    "R31": (5.17, 18.52, 90, True),
    "R41": (13.53, 19.46, 0, True),
    "R43": (7.43, 51.77, 0, True),
    "R44": (7.43, 53.27, 0, True),
    "R45": (7.43, 54.77, 0, True),
    "R46": (6.97, 46.73, 90, True),
    "R47": (25.42, 50.77, 0, True),
    "R48": (27.42, 50.77, 0, True),
    "R50": (23.76, 45.23, 90, True),
    "R51": (23.76, 47.23, 90, True),
    "R52": (27.56, 47.73, 90, True),
    "R53": (28.66, 45.92, 90, True),
    "R63": (28.66, 47.92, 90, True),
    "R67": (1.17, 31.53, 90, True),
    "R68": (13.72, 34.06, 0, True),
    "R69": (26.73, 33.56, 0, True),
    "R7": (16.40, 23.00, 0, True),
    "R70": (30.50, 22.60, 0, True),
    "R71": (34.00, 23.00, 0, True),
    "R72": (24.50, 22.60, 0, True),
    "R73": (23.22, 0.92, 0, True),
    "R74": (25.22, 0.92, 0, True),
    "R75": (27.22, 0.92, 0, True),
    "R89": (7.82, 14.07, 0, True),
    "R9": (9.82, 14.07, 0, True),
    "R90": (11.36, 14.53, 90, True),
    "R91": (13.90, 37.00, 0, False),
    "R92": (29.82, 23.46, 0, False),
    "R97": (1.62, 32.96, 0, False),
    "R98": (1.62, 34.46, 0, False),
    "R99": (3.17, 33.42, 90, False),
    "C11": (3.16, 12.51, 90, True),
    "C13": (29.94, 43.79, 0, False),
    "C14": (23.75, 49.20, 90, True),
    "C15": (20.20, 34.05, 0, True),
    "C16": (26.60, 20.16, 0, True),
    "C18": (3.16, 35.40, 90, False),
    "C19": (8.83, 15.08, 0, True),
    "C2": (20.00, 11.50, 90, True),
    "C20": (13.10, 15.50, 0, True),
    "C23": (5.00, 21.80, 0, True),
    "C24": (5.70, 20.30, 0, True),
    "C25": (6.70, 15.30, 0, True),
    "C26": (24.00, 32.86, 0, False),
    "C27": (26.00, 32.86, 0, False),
    "C28": (19.20, 33.86, 0, False),
    "C29": (17.20, 34.36, 0, False),
    "C3": (21.20, 34.36, 0, False),
    "C30": (29.34, 12.48, 0, True),
    "C32": (27.89, 12.54, 90, True),
    "C33": (26.30, 11.50, 0, True),
    "C34": (24.40, 13.20, 0, True),
    "C35": (30.80, 10.90, 0, True),
    "C36": (15.40, 40.45, 0, False),
    "C37": (17.40, 40.45, 0, False),
    "C38": (22.20, 40.45, 0, False),
    "C39": (24.20, 40.45, 0, False),
    "C4": (26.20, 40.45, 0, False),
    "C40": (28.20, 40.45, 0, False),
    "C41": (3.01, 41.95, 0, False),
    "C43": (5.01, 41.95, 0, False),
    "C46": (23.00, 21.60, 0, False),
    "C48": (12.60, 46.05, 0, False),
    "C49": (10.00, 47.05, 0, False),
    "C50": (10.00, 48.55, 0, False),
    "C51": (9.40, 50.05, 0, False),
    "C52": (14.00, 50.05, 0, False),
    "C53": (11.40, 50.55, 0, False),
    "C54": (13.40, 51.55, 0, False),
    "C55": (1.60, 52.05, 0, False),
    "C56": (3.61, 52.05, 0, False),
    "C57": (9.80, 52.05, 0, False),
    "C58": (27.34, 44.19, 0, False),
    "C6": (25.54, 21.59, 0, False),
    "C84": (28.50, 20.30, 0, True),
    "R115": (28.00, 21.30, 0, True),
    "R116": (30.80, 56.50, 0, True),
    "R117": (28.40, 41.90, 90, True),
    "R118": (35.70, 44.30, 180, False),
    "R119": (35.70, 56.50, 180, False),
    "R120": (34.30, 48.50, 90, True),
    "R121": (34.30, 39.70, 90, True),
    "R122": (34.65, 58.00, 90, True),
    "D26": (17.50, 37.00, 0, False),
    "D27": (23.70, 36.40, 0, False),
    "D28": (25.40, 36.40, 0, False),
    "D29": (33.50, 46.80, 0, False),
    "D30": (19.30, 38.50, 0, False),
    "TP28": (5.02, 37.50, 0, False),
    "D4": (22.14, 7.64, 0, True),
    "D5": (28.74, 6.34, 0, True),
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
        # kicad-cli exports schematic DNP as a valueless property
        # (property (name "dnp")) — not a (dnp "yes") atom.
        dnp_prop = any(
            isinstance(c, list) and c and c[0] == "property"
            and any(isinstance(p, list) and p[:2] == ["name", "dnp"] for p in c[1:])
            for c in comp[1:]
        )
        comps[ref] = {
            "value": atom(comp, "value", ""),
            "footprint": atom(comp, "footprint", ""),
            "dnp": dnp_prop or dnp in ("yes", "true", "1") or ref in DNP_REFS,
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


# Thermal moat around the U11/U20 TMP117 pair at (20.2, 47.9). One polygon
# draws the entire milled channel: west leg x17.2-18.53, top bar
# y43.69-45.4, east leg x21.87-23.1, and the two south wings x17.2-18.3 /
# x20.7-23.1 at y49.2-50.2 — every leg >=1.0 mm so it mills at JLC's
# non-plated-slot floor. The paddle is the notched material
# x18.53-21.87, y45.4-49.2 plus its south neck through the x18.3-20.7 gap —
# all 14 U11/U20 pads sit on it with ~0.32 mm margin. A closed Edge.Cuts
# loop removes its interior, so the paddle outline itself must never be
# the loop.
ISLAND = (
    (17.2, 43.69),
    (23.1, 43.69),
    (23.1, 50.2),
    (20.7, 50.2),
    (20.7, 49.2),
    (21.87, 49.2),
    (21.87, 45.4),
    (18.53, 45.4),
    (18.53, 49.2),
    (18.3, 49.2),
    (18.3, 50.2),
    (17.2, 50.2),
)


def add_keepout(board):
    """hw_v2: U1 is ESP32-S3-MINI-1 at (12.4, 10.0) rot 0 — the on-board
    PCB antenna is the pad-free tab on the module's -Y end, overhanging the
    top board edge by ~2.8 mm (Espressif-preferred mounting). The keep-out
    covers the tab's inboard strip (y 0..2.45, x 4.55..20.25): no tracks,
    vias, pads or pours on any layer. J9's tail pads moved out of the zone."""
    zone = pcbnew.ZONE(board)
    zone.SetIsRuleArea(True)
    zone.SetDoNotAllowTracks(True)
    zone.SetDoNotAllowVias(True)
    zone.SetDoNotAllowPads(True)
    zone.SetDoNotAllowZoneFills(True)  # KiCad 10 renamed SetDoNotAllowCopperPour
    zone.SetDoNotAllowFootprints(False)
    zone.SetZoneName("antenna_keepout")
    zone.SetLayerSet(pcbnew.LSET.AllCuMask())
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in ((4.55, 0.0), (20.25, 0.0), (20.25, 2.45), (4.55, 2.45)):
        outline.Append(mm(x), mm(y))
    board.Add(zone)


def apply_rules(board):
    """JLC 4-layer rules. 0.09 mm is the BGA-fanout floor; 0.127 mm is the custom-rule default."""
    ds = board.GetDesignSettings()
    ds.m_MinClearance = mm(0.09)
    ds.m_TrackMinWidth = mm(0.09)
    ds.m_ViasMinSize = mm(0.30)
    ds.m_ViasMinDrill = mm(0.2)
    ds.m_ViasMinAnnularWidth = mm(0.05)
    ds.m_CopperEdgeClearance = mm(0.3)
    # Floor is the BGA via-in-pad case. The custom rules raise this outside bga_fanout.
    ds.m_HoleClearance = mm(0.15)
    ds.m_HoleToHoleMin = mm(0.2)
    ds.m_MinThroughDrill = mm(0.2)
    ds.m_MinSilkTextHeight = mm(1.0)
    ds.m_MinSilkTextThickness = mm(0.15)
    ds.m_SilkClearance = mm(0.15)
    # Zero global mask expansion keeps a web between 0.4 mm BGA balls.
    # Via-in-pad balls set their own NSMD margin.
    ds.m_SolderMaskExpansion = mm(0)
    ds.m_SolderMaskMinWidth = mm(0.1)
    ds.m_SolderMaskToCopperClearance = mm(0.05)
    net = ds.m_NetSettings.GetDefaultNetclass()
    # 0.09 mm floor so the BGA fan-out rule can apply. Outside that area the
    # custom rule requires 0.127 mm.
    net.SetClearance(mm(0.09))
    net.SetTrackWidth(mm(0.2))
    net.SetViaDiameter(mm(0.6))
    net.SetViaDrill(mm(0.3))


def courtyard(fp, bottom):
    fp.BuildCourtyardCaches()
    layer = pcbnew.B_CrtYd if bottom else pcbnew.F_CrtYd
    return fp.GetCachedCourtyard(layer)


def overlaps(a, b):
    if a is None or b is None or a.OutlineCount() == 0 or b.OutlineCount() == 0:
        return False
    return a.Collide(b)


# ---------------------------------------------------------------------------
# board_finish retarget shims. board_finish.py still carries the hw_v1
# 36.5 x 70 constants plus absolute keep-out/pour/silk coordinates (another
# workstream owns that file), so main() patches the module before calling
# finish_board. The pad-driven pieces (HV keep-outs, hole keep-outs, BGA
# fan-out areas, via-in-pad, pin-1 dots) already follow the placed copper.


def _retarget_board_finish(bf):
    """Install the 40 x 62 versions of board_finish's absolute geometry."""
    bf.BOARD_W = BOARD_W
    bf.BOARD_H = BOARD_H

    def _pour_keepouts(board):
        """No L3 power copper under the temperature sensors (moved islands)."""
        for x0, y0, x1, y1 in (
            (18.1, 44.3, 22.2, 50.3),  # TMP117 island around U11/U20
            (7.2, 44.9, 13.0, 50.3),   # MLX90632 moat around U12
        ):
            bf._rect_zone(
                board, (pcbnew.In2_Cu,),
                ((x0, y0), (x1, y0), (x1, y1), (x0, y1)),
                "sensor_nopower", pours=True,
            )

    def _blocked(x, y, rects, pad_boxes):
        if x < 0.9 or x > BOARD_W - 0.9 or y < 0.9 or y > BOARD_H - 0.9:
            return True
        # Antenna keep-out under the module's overhanging tab (see add_keepout).
        if 4.55 <= x <= 20.25 and y <= 2.45:
            return True
        # HV keep-outs already carry a 1.0 mm inflate; the extra 0.9 puts
        # the 0.6 mm stitch barrel >=1.9 mm from HV pad copper, clearing the
        # 1.5 mm 'HV electrode to other nets' rule with margin.
        for x0, y0, x1, y1 in rects:
            if x0 - 0.9 <= x <= x1 + 0.9 and y0 - 0.9 <= y <= y1 + 0.9:
                return True
        # 0.55 clears via annulus (0.3) + hole/copper clearance (0.2) and
        # covers NPTH pads, which carry no plated barrel to measure against.
        for x0, y0, x1, y1 in pad_boxes:
            if x0 - 0.55 <= x <= x1 + 0.55 and y0 - 0.55 <= y <= y1 + 0.55:
                return True
        return False

    def _stitch(board, hv_rects):
        gnd = board.FindNet("GND")
        if gnd is None:
            return 0
        pad_boxes = []
        for fp in board.GetFootprints():
            for pad in fp.Pads():
                pad_boxes.append(bf._box(pad, 0))
        for fp in board.GetFootprints():
            if fp.GetReference().startswith("H"):
                c = fp.GetPosition()
                cx, cy = c.x / 1e6, c.y / 1e6
                hv_rects.append((cx - 2.2, cy - 2.2, cx + 2.2, cy + 2.2))
        points = []
        step = 2.8
        y = 1.2
        while y < BOARD_H - 1.0:
            points.append((1.2, y))
            points.append((BOARD_W - 1.2, y))
            y += step
        x = 1.2
        while x < BOARD_W - 1.0:
            points.append((x, 1.2))
            points.append((x, BOARD_H - 1.2))
            x += step
        # Switcher neighbourhoods: U3/L1 buck-boost, U15/U4 rails, L2 filter.
        for cx, cy in ((29.5, 32.0), (26.0, 19.9), (29.0, 16.4)):
            for dx in (-1.6, 0, 1.6):
                for dy in (-1.6, 0, 1.6):
                    points.append((cx + dx, cy + dy))
        placed = []
        n = 0
        for x, y in points:
            if bf._blocked(x, y, hv_rects, pad_boxes):
                continue
            if any((x - px) ** 2 + (y - py) ** 2 < 1.3 ** 2 for px, py in placed):
                continue
            bf._add_via(board, pcbnew.VECTOR2I(mm(x), mm(y)), gnd, 0.6, 0.3, tent=True)
            placed.append((x, y))
            n += 1
        return n

    def _pours(board):
        board_pts = (
            (0.3, 0.3), (BOARD_W - 0.3, 0.3),
            (BOARD_W - 0.3, BOARD_H - 0.3), (0.3, BOARD_H - 0.3),
        )
        bf._zone(board, "GND", pcbnew.In1_Cu, board_pts, 0)
        bf._zone(board, "GND", pcbnew.B_Cu, board_pts, 0)
        bf._zone(board, "+3V3", pcbnew.In2_Cu, board_pts, 0)
        # L3 islands follow the moved power blocks: VBAT under U2/J2,
        # TX_5V under the SFH7072 anode cluster, +1V8 under the U9 LDO output.
        bf._zone(board, "VBAT", pcbnew.In2_Cu,
                 ((11.0, 10.0), (20.5, 10.0), (20.5, 17.0), (11.0, 17.0)), 2)
        bf._zone(board, "TX_5V", pcbnew.In2_Cu,
                 ((11.0, 52.0), (19.8, 52.0), (19.8, 56.8), (11.0, 56.8)), 3)
        bf._zone(board, "+1V8", pcbnew.In2_Cu,
                 ((18.5, 18.3), (24.5, 18.3), (24.5, 21.5), (18.5, 21.5)), 3)

    def _silk(board):
        boxes = bf._pad_boxes(board)
        candidates = (
            ("VitalQ hw_v1 rev A 2026-09", 8.3, 24.0, pcbnew.F_SilkS, 22.0, 1.2),
            ("JLCJLCJLCJLC", 15.0, 42.2, pcbnew.B_SilkS, 12.0, 1.2),
            ("J8 1 +3V3  2 GND  3 TX  4 RX  5 EN  6 IO0", 2.0, 47.9, pcbnew.F_SilkS, 26.0, 1.2),
        )
        for text, x, y, layer, width, height in candidates:
            if bf._hits(x, y - height, x + width, y, boxes):
                continue
            bf._silk_text(board, text, x, y, layer)
        labels = {
            "J3": "J3 FSR",
            "J5": "J5 ECG1 ECG2 RLD PPG+ PPG-",
            "J6": "J6 CE RE SE DE",
            "J7": "J7 F+ F- S+ S-",
            "J2": "J2 BAT+ BAT- NTC",
        }
        for ref, text in labels.items():
            fp = board.FindFootprintByReference(ref)
            if fp is None:
                continue
            # Labels must clear silk rings (e.g. test-point pin-1 marks) too,
            # not just pads — add each footprint's silk circles to the hit set.
            label_boxes = list(boxes)
            for other in board.GetFootprints():
                for item in other.GraphicalItems():
                    if not isinstance(item, pcbnew.PCB_SHAPE):
                        continue
                    if item.GetShape() != pcbnew.SHAPE_T_CIRCLE:
                        continue
                    if item.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                        continue
                    bb = item.GetBoundingBox()
                    label_boxes.append((
                        bb.GetX() / 1e6 - 0.1, bb.GetY() / 1e6 - 0.1,
                        (bb.GetX() + bb.GetWidth()) / 1e6 + 0.1,
                        (bb.GetY() + bb.GetHeight()) / 1e6 + 0.1,
                    ))
            pos = fp.GetPosition()
            layer = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
            for dy in (3.2, 4.4, -3.6):
                for dx in (0.0, 1.0, 2.0):
                    x, y = pos.x / 1e6 - 4.0 + dx, pos.y / 1e6 + dy
                    if x < 0.8:
                        x = 0.8
                    if bf._hits(x, y - 1.1, x + min(len(text) * 0.7, 24), y, label_boxes):
                        continue
                    if y < 1.2 or y > BOARD_H - 0.4:
                        continue
                    bf._silk_text(board, text, x, y, layer)
                    break
                else:
                    continue
                break
        bf._pin1_dots(board, boxes)

    bf._pour_keepouts = _pour_keepouts
    bf._blocked = _blocked
    bf._stitch = _stitch
    bf._pours = _pours
    bf._silk = _silk


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
        if ref.startswith("TP"):
            for pad in fp.Pads():
                pad.SetLayerSet(pad.GetLayerSet().RemoveLayer(pcbnew.F_Paste))
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
        # NC pins generate per-pin "unconnected-" nets; a padless NC ball
        # (U22 rows B-E dropped for fanout escape) is intentional, not a miss.
        if name.startswith("unconnected-"):
            continue
        unresolved.append(f"{ref} pin {pin} ({name}) has no footprint pad")

    add_edge(board, 0, 0, BOARD_W, 0)
    add_edge(board, BOARD_W, 0, BOARD_W, BOARD_H)
    add_edge(board, BOARD_W, BOARD_H, 0, BOARD_H)
    add_edge(board, 0, BOARD_H, 0, 0)
    add_slot(board, *SLOTS[0])  # AS7341 / LED barrier
    for _slot in CREEP_SLOTS:
        add_slot(board, *_slot)
    for _slot in MLX_SLOTS:
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
    # Inner BGA balls of U6/U7 get a 0.30/0.20 VIP barrel in board_finish:
    # those vias pierce both copper layers, so every other pad must clear
    # a 0.30 mm box around the ball center regardless of layer.
    def _inner_balls(fp):
        pads = list(fp.Pads())
        for pad in pads:
            px, py = pad.GetPosition().x, pad.GetPosition().y

            def has(sx, sy):
                for o in pads:
                    if o == pad:
                        continue
                    dx = o.GetPosition().x - px
                    dy = o.GetPosition().y - py
                    along = dx * sx + dy * sy
                    if abs(dx * sy - dy * sx) < mm(0.12) and mm(0.30) < along < mm(0.55):
                        return True
                return False

            if has(1, 0) and has(-1, 0) and has(0, 1) and has(0, -1):
                yield pad

    vip_boxes = []
    for _ref in ("U6", "U7"):
        fp, _ = placed[_ref]
        for pad in _inner_balls(fp):
            pos = pad.GetPosition()
            vip_boxes.append(
                (
                    _ref,
                    (
                        pos.x / 1e6 - 0.30,
                        pos.y / 1e6 - 0.30,
                        pos.x / 1e6 + 0.30,
                        pos.y / 1e6 + 0.30,
                    ),
                )
            )

    def _pbox(pad):
        box = pad.GetBoundingBox()
        return (
            box.GetX() / 1e6,
            box.GetY() / 1e6,
            (box.GetX() + box.GetWidth()) / 1e6,
            (box.GetY() + box.GetHeight()) / 1e6,
        )

    for ref, (fp, bottom) in placed.items():
        for pad in fp.Pads():
            pb = _pbox(pad)
            attr = pad.GetAttribute()
            if attr == pcbnew.PAD_ATTRIB_NPTH:
                # Mounting drills pierce every layer: edge, slot and a 0.2 mm
                # hole-to-copper gap against every other pad/hole.
                if min(pb[0], pb[1], BOARD_W - pb[2], BOARD_H - pb[3]) < 0.3:
                    edge_hits.append(f"{ref}.{pad.GetNumber()} edge")
                for s in SLOTS + CREEP_SLOTS + MLX_SLOTS:
                    if not (
                        pb[2] <= s[0] - 0.3
                        or pb[0] >= s[2] + 0.3
                        or pb[3] <= s[1] - 0.3
                        or pb[1] >= s[3] + 0.3
                    ):
                        edge_hits.append(f"{ref}.{pad.GetNumber()} slot")
                        break
                for other, (ofp, _ob) in placed.items():
                    if other == ref:
                        continue
                    for op in ofp.Pads():
                        if _gap(pb, _pbox(op)) < 0.2:
                            pth_hits.append(
                                f"{ref}.{pad.GetNumber()} hole vs {other}.{op.GetNumber()}"
                            )
                            break
                    else:
                        continue
                    break
                continue
            for oref, vb in vip_boxes:
                if oref == ref:
                    continue
                if pb[0] < vb[2] and pb[2] > vb[0] and pb[1] < vb[3] and pb[3] > vb[1]:
                    pth_hits.append(f"{ref}.{pad.GetNumber()} overlaps {oref} VIP via")
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
            for sx0, sy0, sx1, sy1 in SLOTS + CREEP_SLOTS + MLX_SLOTS:
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

    import board_finish as bf

    _retarget_board_finish(bf)
    bf.finish_board(board)
    board.SetFileName(str(PCB))
    pcbnew.SaveBoard(str(PCB), board)
    write_bom(comps)
    write_jlc(board, comps)
    print(f"wrote {PCB.name}  {BOARD_W:.1f} x {BOARD_H:.1f} mm")
    if clashes or edge_hits or pth_hits or unresolved:
        return 1
    return 0


# Exact orderable numbers. 0402 passives are Yageo RC0402FR (1%) and Murata GRM155.
# 0603 bulk capacitors are the Murata parts named in the regulator tables.
MPN = {
    "U1": "ESP32-S3-MINI-1-N4R2",
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
    "Q1": "CSD13380F3T",
    "Q2": "CSD13380F3T",
    "Q4": "CSD13380F3T",
    "U21": "USBLC6-2SC6",
    "D25": "KT-0603G",
    "SW1": "TS-1088-AR02016",
    "SW2": "TS-1088-AR02016",
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
    "C1": "CL10A475KA8NQNC",
    "C7": "CL10A226MQ8NRNC",
    "C10": "CL10A105KA8NNNC",
    "R93": "0603WAF0000T5E",
    "R94": "0603WAF0000T5E",
}
for _ref in ("D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D21", "D22", "D23", "D24",
             "D26", "D27", "D28", "D29", "D30"):
    MPN[_ref] = "TPD1E10B06DPYR"
DNP_REFS = {"R61"}

R_MPN = {
    "10": "RC0402FR-0710RL",
    "100": "RC0402FR-07100RL",
    "200": "RC0402FR-07200RL",
    "1k": "RC0402FR-071KL",
    "4.7k": "RC0402FR-074K7L",
    "5.1k": "RC0402FR-075K1L",
    "0": "25121WJ0000T4E",
    "1.5k": "0402WGF1501TCE",
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
    "10M": "0402WGF1005TCE",
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
    # Connector-side tails upstream of the R118-R122 cut-points —
    # patient-facing when J11/J13 are populated.
    "J11_WE", "J11_RE", "J11_CE", "J13_INP", "J13_INM",
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
        if value == "0":
            return "25121WJ0000T4E" if "2512" in footprint else "0402WGF0000TCE"
        return R_MPN[value]
    if ref.startswith("C"):
        table = C_MPN_0603 if "0603" in footprint else C_MPN_0402
        return table[value]
    if ref.startswith("J"):
        return "solder pads"
    return ""


# LCSC numbers confirmed against the prefab review. Anything else stays blank
# rather than guessing a catalogue code.
LCSC_BY_MPN = {
    "ESP32-S3-MINI-1-N4R2": "C3013941",
    "USBLC6-2SC6": "C7519",
    "KT-0603G": "C12624",
    "TS-1088-AR02016": "C720477",
    "CL10A475KA8NQNC": "C69335",
    "CL10A226MQ8NRNC": "C59461",
    "CSD13380F3T": "C2871092",
    "MAX17048G+T10": "C2682616",
    "AD5940BCBZ-RL7": "C650308",
    "ADS1292RIRSMT": "C882777",
    "TCA6408ARSVR": "C2649390",
    "SFH 7072": "C2655172",
    "0402WGF0000TCE": "C17168",
    "25121WJ0000T4E": "C2908946",
    "0603WAF0000T5E": "C21189",
    "0402WGF1501TCE": "C25867",
    "0402WGF1005TCE": "C26082",
}
LCSC_BY_VALUE = {
    "10k": "C25744",
    "100k": "C25741",
    "1k": "C11702",
    "0": "C2908946",
}
CONSIGN_MPNS = {
    "DPCR2512-51KJT18",
    "NF2W757G-F1",
    "MLX90632SLD-DCB-100-SP",
    "LSM6DSV80XTR",
    "W25Q512JVEIQ",
}
# KiCad footprint name -> extra degrees after the bottom-side mirror.
# SOT-23 is the Bouni kicad-jlcpcb-tools convention (pin 1 at the top of the tape).
ROTATION_CORRECTIONS = (
    ("SOT-23", -90),
    ("SOT-23-5", -90),
    ("SOT-23-6", -90),
)


def lcsc_for(ref, value, mpn):
    if mpn in CONSIGN_MPNS or mpn == "RC0402FR-07301KL":
        return ""
    if mpn in LCSC_BY_MPN:
        return LCSC_BY_MPN[mpn]
    if ref.startswith("R") and value in LCSC_BY_VALUE:
        if value == "0" and mpn == "0603WAF0000T5E":
            return "C21189"
        return LCSC_BY_VALUE[value]
    return ""


def consign_for(mpn):
    if mpn in CONSIGN_MPNS:
        return "consign/global sourcing"
    if mpn == "RC0402FR-07301KL":
        return "extended library; value stays 301k"
    return ""


def _jlc_rotation(fp):
    name = str(fp.GetFPID().GetLibItemName())
    rotation = fp.GetOrientation().AsDegrees()
    # Bouni fabrication.py: bottom angles are mirrored on the Y axis.
    if fp.GetLayer() != 0:
        rotation = (180 - rotation) % 360
    correction = 0
    for token, delta in ROTATION_CORRECTIONS:
        if token in name:
            correction = delta
            break
    return (rotation + correction) % 360, correction, name


def write_jlc(board, comps):
    """JLCPCB BOM and CPL. DNP, fiducials, holes, test pads and the bare pad footprints stay off both."""
    skip_prefix = ("FID", "TP", "H")
    bare = {"J2", "J3", "J5", "J6", "J7", "J8", "J9", "J10", "J11"}
    bom_rows = []
    cpl_rows = []
    corrections = []
    by_ref = {fp.GetReference(): fp for fp in board.GetFootprints()}
    for ref, meta in sorted(comps.items(), key=lambda kv: kv[0]):
        if meta.get("dnp") or ref.startswith(skip_prefix) or ref in bare:
            continue
        mpn = mpn_for(ref, meta["value"], meta["footprint"])
        lcsc = lcsc_for(ref, meta["value"], mpn)
        note = consign_for(mpn)
        bom_rows.append((meta["value"], ref, meta["footprint"].split(":")[-1], lcsc, note))
        fp = by_ref.get(ref)
        if fp is None:
            continue
        pos = fp.GetPosition()
        rot, correction, name = _jlc_rotation(fp)
        if correction:
            corrections.append((ref, name, correction))
        side = "bottom" if fp.IsFlipped() else "top"
        cpl_rows.append((ref, f"{pos.x/1e6:.4f}", f"{pos.y/1e6:.4f}", side, f"{rot:.1f}"))
    jlc_bom = ROOT / "jlc_bom.csv"
    jlc_cpl = ROOT / "jlc_cpl.csv"
    with jlc_bom.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Consign/Global sourcing"])
        # Group identical parts the way JLC expects: one row, designators joined.
        groups = {}
        for value, ref, fp, lcsc, note in bom_rows:
            groups.setdefault((value, fp, lcsc, note), []).append(ref)
        for (value, fp, lcsc, note), refs in groups.items():
            w.writerow([value, ",".join(refs), fp, lcsc, note])
    with jlc_cpl.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        w.writerows(cpl_rows)
    note_path = ROOT / "build" / "jlc_rotation_corrections.txt"
    note_path.parent.mkdir(exist_ok=True)
    lines = [
        "Bottom parts use Bouni fabrication.py: rotation = (180 - KiCad) mod 360.",
        "Additional corrections (degrees) applied on top of that:",
    ]
    if corrections:
        for ref, name, delta in corrections:
            lines.append(f"  {ref} {name} {delta}")
    else:
        lines.append("  (none matched)")
    lines.append("Every other footprint: 0 extra degrees. Confirm tape orientation at order time for the BGAs, WSON, and USB-C.")
    note_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_bom(comps):
    rows = []
    key = lambda kv: (kv[0][0], int("".join(ch for ch in kv[0] if ch.isdigit()) or "0"))
    for ref, meta in sorted(comps.items(), key=key):
        mpn = mpn_for(ref, meta["value"], meta["footprint"])
        rows.append(
            (
                ref,
                meta["value"],
                mpn,
                meta["footprint"],
                "1",
                "DNP" if meta.get("dnp") else "",
                lcsc_for(ref, meta["value"], mpn),
                consign_for(mpn),
            )
        )
    with BOM.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Ref", "Value", "MPN", "Footprint", "Qty", "DNP", "LCSC", "Consign/Global sourcing"])
        w.writerows(rows)


if __name__ == "__main__":
    sys.exit(main())
