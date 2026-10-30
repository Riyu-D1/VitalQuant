#!/usr/bin/env python3
"""Re-run DRC + ERC on the committed fix_v24 snapshot via kicad-cli.

Inputs/ holds the project set exactly as analysed: vitalq_v2.kicad_pro
(fix_v19 rules project), vitalq_v2.kicad_dru (fix_v23 custom rules,
auto-loaded because it shares the basename), vitalq_v2.kicad_pcb /
vitalq_v2.kicad_sch (fix_v24). kicad-cli resolves the project from the
same-basename .kicad_pro, so the commands mirror the original run.

Writes ../results/drc_v24.json + ../results/erc_v24.json and prints the
violation counts.
"""
import os, sys, json, subprocess, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from toolfind import require_tool

HERE = os.path.dirname(os.path.abspath(__file__))
D = lambda *p: os.path.join(HERE, "..", *p)

ap = argparse.ArgumentParser()
ap.add_argument("--proj", default=D("inputs", "vitalq_v2"),
                help="project basename (no extension) inside inputs/")
ap.add_argument("--out", default=D("results"))
args = ap.parse_args()

kicad = require_tool("kicad-cli")

subprocess.run([kicad, "pcb", "drc", "--severity-error", "--schematic-parity",
                "--format", "json", args.proj + ".kicad_pcb",
                "-o", os.path.join(args.out, "drc_v24.json")],
               check=True, capture_output=True, text=True)
subprocess.run([kicad, "sch", "erc", "--severity-error",
                "--format", "json", args.proj + ".kicad_sch",
                "-o", os.path.join(args.out, "erc_v24.json")],
               check=True, capture_output=True, text=True)

d = json.load(open(os.path.join(args.out, "drc_v24.json")))
e = json.load(open(os.path.join(args.out, "erc_v24.json")))
print("DRC:", len(d["violations"]), "violations |",
      len(d["unconnected_items"]), "unconnected |",
      len(d.get("schematic_parity", [])), "parity issues")
sheets = e.get("sheets", [])
print("ERC:", sum(len(s.get("violations", [])) for s in sheets), "violations")
