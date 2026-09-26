#!/usr/bin/env bash
# Regenerate the VitalQ hw_v1 schematic and unrouted board, then check them.
# Needs KiCad 9 (kicad-cli and the pcbnew Python module on the system interpreter).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

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

python3 build_pcb.py

kicad-cli pcb drc --severity-error --schematic-parity --format json -o build/drc.json vitalq_hw_v1.kicad_pcb
python3 - << 'PY'
import json
import sys
from collections import Counter

report = json.load(open("build/drc.json", encoding="utf-8"))
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
counts = Counter(errors)
unexpected = {name: n for name, n in counts.items() if name != "unconnected_items"}
print(f"DRC errors: {len(errors)} ({dict(counts)})")
print(f"DRC errors excluding unrouted nets: {sum(unexpected.values())}")
if unexpected:
    sys.exit(1)
PY

echo "check.sh passed"
