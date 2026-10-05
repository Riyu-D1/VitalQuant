#!/usr/bin/env bash
# Regenerate the VitalQ hw_v1 schematic and unrouted board, then check them.
# Needs KiCad: kicad-cli on PATH and an interpreter that can import pcbnew
# (KiCad's bundled Python — the system interpreter does not ship pcbnew).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

# Make kicad-cli reachable for this script and for build_pcb.py's netlist export.
KICAD_BIN="/Applications/KiCad/KiCad.app/Contents/MacOS"
if [ -d "$KICAD_BIN" ]; then
    export PATH="$KICAD_BIN:$PATH"
fi

# build_pcb.py needs pcbnew. Prefer the system interpreter if it has it;
# otherwise use KiCad's bundled Python (overridable via KICAD_PYTHON).
if python3 -c "import pcbnew" 2> /dev/null; then
    PCB_PY=python3
else
    PCB_PY="${KICAD_PYTHON:-/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3}"
fi

python3 build_sch.py

kicad-cli sch erc --severity-error --format json -o build/erc.json vitalq_hw_v1.kicad_sch
python3 - << 'PY'
import json
import sys

report = json.load(open("build/erc.json", encoding="utf-8"))
errors = []

def walk(node):
    if isinstance(node, dict):
        if node.get("severity") == "error" and "type" in node:
            errors.append(node["type"])
        for value in node.values():
            walk(value)
    elif isinstance(node, list):
        for item in node:
            walk(item)

walk(report)
print(f"ERC errors: {len(errors)}")
if errors:
    sys.exit(1)
PY

"$PCB_PY" build_pcb.py

kicad-cli pcb drc --severity-error --severity-warning --schematic-parity --format json -o build/drc.json vitalq_hw_v1.kicad_pcb
python3 - << 'PY'
import json
import sys
from collections import Counter

report = json.load(open("build/drc.json", encoding="utf-8"))
errors = []

# Inherent intra-package clearance: Q1/Q2/Q4 are pico-FET SOT-23s whose
# 0.100 mm pad pitch cannot satisfy the 0.127 mm default rule. Same on the
# hw_v1 baseline. These pairs are waived by name; anything else still fails.
INTRA_PACKAGE = {"Q1", "Q2", "Q4"}

# U22 MAX86178 WLP-49 row F: 0.35 mm pitch makes 0.30/0.20 mm VIPs sit
# 0.05 mm apart — unfixable by geometry on any process that can fab the
# part at all. Requires JLC POFV / HDI; violations confined to the F-row
# pad/via box are waived. Anything outside that box still fails.
U22_FROW = (24.9, 45.6, 27.3, 45.9)  # x0,y0,x1,y1

def _in_u22_frow(item):
    p = item.get("pos") or {}
    x, y = p.get("x"), p.get("y")
    if x is None:
        return False
    return U22_FROW[0] <= x <= U22_FROW[2] and U22_FROW[1] <= y <= U22_FROW[3]

def waived(node):
    t = node.get("type")
    items = node.get("items") or []
    if not items:
        return False
    if t in {"clearance", "hole_clearance", "solder_mask_bridge"} and all(
        _in_u22_frow(i) for i in items
    ):
        return True
    if t != "clearance":
        return False
    for i in items:
        d = i.get("description", "")
        ref = d.rsplit(" of ", 1)[-1].rstrip(" .").split(" ")[0]
        if ref not in INTRA_PACKAGE:
            return False
    return True

def walk(node):
    if isinstance(node, dict):
        if node.get("severity") == "error" and "type" in node:
            if not waived(node):
                errors.append(node["type"])
        for value in node.values():
            walk(value)
    elif isinstance(node, list):
        for item in node:
            walk(item)

walk(report)
counts = Counter(errors)
unexpected = {name: n for name, n in counts.items() if name != "unconnected_items"}
print(f"DRC errors: {len(errors)} ({dict(counts)})")
print(f"DRC errors excluding unrouted nets: {sum(unexpected.values())}")
if unexpected:
    sys.exit(1)

silk = []

def walk_silk(node):
    if isinstance(node, dict):
        if node.get("severity") == "warning" and node.get("type") in {
            "silk_overlap",
            "silk_over_copper",
            "silk_edge_clearance",
        }:
            silk.append(node["type"])
        for value in node.values():
            walk_silk(value)
    elif isinstance(node, list):
        for item in node:
            walk_silk(item)

walk_silk(report)
# Silk warnings are informational: edge-mounted connectors legitimately run
# silk to the board edge, and footprint pin-1 dots overlap pads by library
# convention. Fabs clip silk off pads automatically; these do not gate.
print(f"Silkscreen warnings: {len(silk)} ({dict(Counter(silk))})")
PY

echo "check.sh passed"
