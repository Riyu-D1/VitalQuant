#!/usr/bin/env python3
"""Regenerate fix_v1/fab/BOM.csv + CPL.csv from fix_v1 board+schematic.
Adapted from /tmp/vq/export_assembly.py (self-contained)."""
import csv, json, re, sys
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).resolve().parent

def parse(path):
    stack = []; root = None
    for token in re.finditer(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()]+', path.read_text()):
        v = token.group()
        if v == '(':
            node = []
            if stack:
                stack[-1].append(node)
            else:
                root = node
            stack.append(node)
        elif v == ')':
            stack.pop()
        else:
            stack[-1].append(json.loads(v) if v.startswith('"') else v)
    assert not stack
    return root

def children(n, k): return [v for v in n if isinstance(v, list) and v and v[0] == k]
def field(n, k, d=None):
    vs = children(n, k)
    return vs[0][1:] if vs else d
def reference(n): return next(v[2] for v in children(n, 'property') if v[1] == 'Reference')
def props(n): return {p[1]: p[2] for p in children(n, 'property')}
def natural(r): return [int(t) if t.isdigit() else t for t in re.split(r'(\d+)', r)]

CONSIGNED = {
    'D10': 'NF2W757G-F1',
    'D12': 'XL0603NRC730',
}

b = parse(ROOT / 'vitalq_v2.kicad_pcb')
s = parse(ROOT / 'vitalq_v2.kicad_sch')
lib = {x[1]: props(x) for x in children(children(s, 'lib_symbols')[0], 'symbol')}
symbols = {}
for sym in children(s, 'symbol'):
    p = props(sym)
    r = p.get('Reference')
    if not r:
        continue
    inh = lib.get(field(sym, 'lib_id', [''])[0], {})
    symbols.setdefault(r, {}).update({k: v for k, v in {**inh, **p}.items() if v})

eligible = {}; excluded = defaultdict(list)
for f in children(b, 'footprint'):
    r = reference(f); attrs = field(f, 'attr', []); p = props(f)
    if 'exclude_from_bom' in attrs:
        excluded['board_exclude_from_bom'].append(r); continue
    if 'dnp' in attrs:
        excluded['do_not_populate'].append(r); continue
    if f[1].startswith('vitalq:Pads_'):
        excluded['integral_pcb_contact_pads'].append(r); continue
    if 'smd' not in attrs:
        excluded['not_smd'].append(r); continue
    merged = {**symbols.get(r, {}), **{k: v for k, v in p.items() if v}}
    lcsc = merged.get('LCSC', '').strip()
    assert not lcsc or re.fullmatch('C[0-9]+', lcsc), (r, lcsc)
    eligible[r] = {'comment': p['Value'], 'footprint': f[1].split(':')[-1],
                   'lcsc': lcsc, 'manufacturer': merged.get('Manufacturer_Name', ''),
                   'mpn': merged.get('Manufacturer_Part_Number', ''),
                   'at': list(map(float, field(f, 'at')[:2]))}

groups = defaultdict(list)
for r, v in eligible.items():
    groups[tuple(v[k] for k in ['comment', 'footprint', 'lcsc', 'manufacturer', 'mpn'])].append(r)

with (ROOT / 'fab/assembly/BOM_JLC.csv').open('w', newline='') as st:
    w = csv.writer(st)
    w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #', 'Quantity',
                'Manufacturer', 'Manufacturer Part Number'])
    for key, refs in sorted(groups.items(), key=lambda kv: natural(min(kv[1], key=natural))):
        comment, fp, lcsc, mfr, mpn = key
        cons = [c for c in (CONSIGNED.get(r) for r in refs) if c]
        if not lcsc and not cons:
            continue
        mpn_out = cons[0] if cons else mpn
        w.writerow([comment, ', '.join(sorted(refs, key=natural)), fp, lcsc,
                    len(refs), mfr, mpn_out])

positions = list(csv.DictReader((ROOT / 'fab/assembly/positions_kicad.csv').open()))
selected = [p for p in positions if p['Ref'] in eligible]
missing_pos = sorted(set(eligible) - {p['Ref'] for p in selected}, key=natural)
with (ROOT / 'fab/assembly/CPL_JLC.csv').open('w', newline='') as st:
    w = csv.writer(st)
    w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
    for p in sorted(selected, key=lambda p: natural(p['Ref'])):
        r = p['Ref']; x, y = float(p['PosX']), float(p['PosY'])
        ex = eligible[r]['at']
        assert abs(x - ex[0]) < .000051 and abs(y + ex[1]) < .000051, (r, x, y, ex)
        x, y = ex[0], -ex[1]
        layer = {'top': 'Top', 'bottom': 'Bottom', 'front': 'Top', 'back': 'Bottom'}[p['Side'].lower()]
        w.writerow([r, f'{x:.6f}mm', f'{y:.6f}mm', layer, f'{float(p["Rot"])%360:.6f}'])

summary = {'placed_components': len(eligible), 'bom_groups': len(groups),
           'cpl_rows': len(selected),
           'consigned': sorted(CONSIGNED),
           'missing_lcsc_references': sorted([r for r, v in eligible.items()
                                              if not v['lcsc'] and r not in CONSIGNED], key=natural),
           'missing_positions': missing_pos,
           'excluded': {k: sorted(v, key=natural) for k, v in excluded.items()}}
(ROOT / 'fab/assembly/ASSEMBLY_CHECKS.json').write_text(json.dumps(summary, indent=2))
print(json.dumps({k: (len(v) if isinstance(v, list) else v) for k, v in summary.items()}, indent=1))
print('excluded:', {k: len(v) for k, v in excluded.items()})
