"""SUPERVISOR_R2_02 #7: JLC fab outputs from the saved working board into ./fab/ via kicad-cli, plus verification."""
import csv
import json
import subprocess
import zipfile
from pathlib import Path
from session import ROOT, CLI, summary

FAB = ROOT / 'fab'
G = FAB / 'gerbers'
G.mkdir(parents=True, exist_ok=True)
board = str(ROOT / 'vitalq_v2.kicad_pcb')
layers = 'F.Cu,In1.Cu,In2.Cu,In3.Cu,In4.Cu,In5.Cu,In6.Cu,B.Cu,F.Mask,B.Mask,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,Edge.Cuts'


def run(args):
    r = subprocess.run([CLI] + args, capture_output=True, text=True)
    (FAB / 'kicad_cli.log').open('a').write(' '.join(args) + '\n' + r.stdout + r.stderr + '\n')
    assert r.returncode == 0, r.stderr
    return r


run(['pcb', 'export', 'gerbers', '--output', str(G) + '/', '--layers', layers, '--subtract-soldermask', '--use-drill-file-origin', board])
run(['pcb', 'export', 'drill', '--output', str(G) + '/', '--format', 'excellon', '--excellon-separate-th', '--generate-map', '--map-format', 'gerberx2', board])
run(['pcb', 'export', 'pos', '--output', str(FAB / 'positions_raw.csv'), '--format', 'csv', '--units', 'mm', '--side', 'both', '--use-drill-file-origin', board])
run(['sch', 'export', 'bom', '--output', str(FAB / 'bom_raw.csv'), '--fields', 'Reference,Value,Footprint,LCSC,${QUANTITY},${DNP}', '--labels', 'Designator,Comment,Footprint,LCSC,Qty,DNP', '--group-by', 'Value,Footprint,LCSC', '--ref-range-delimiter', '', str(ROOT / 'vitalq_v2.kicad_sch')])
import os
subprocess.run(['/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3', '-B', str(ROOT / 'scripts' / 'fab_bom_cpl.py'), board, str(FAB)], check=True, capture_output=True)
files = sorted(p for p in G.iterdir() if p.is_file())
with zipfile.ZipFile(FAB / 'vitalq_v2_gerbers.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for p in files:
        z.write(p, p.name)
bom_refs = set()
for r in csv.DictReader(open(FAB / 'vitalq_v2_BOM.csv')):
    bom_refs |= {x.strip() for x in r['Designator'].replace(';', ',').split(',') if x.strip()}
cpl_refs = {r['Designator'] for r in csv.DictReader(open(FAB / 'vitalq_v2_CPL.csv'))}
copper = [p.name for p in files if p.suffix in ('.gtl', '.gbl') or (p.suffix.startswith('.g') and p.suffix[2:].isdigit())]
check = {'gerber_files': [p.name for p in files], 'copper_layers': len(copper), 'drill_files': [p.name for p in files if p.suffix in ('.drl',)],
         'bom_refs': len(bom_refs), 'cpl_refs': len(cpl_refs), 'bom_minus_cpl': sorted(bom_refs - cpl_refs), 'cpl_minus_bom': sorted(cpl_refs - bom_refs),
         'drc_of_board': summary(json.loads((ROOT / 'state_R2.json').read_text())['current_drc'])}
(FAB / 'fab_check.json').write_text(json.dumps(check, indent=1))
print(json.dumps({k: (v if not isinstance(v, list) or len(v) < 12 else len(v)) for k, v in check.items()}, indent=1))
