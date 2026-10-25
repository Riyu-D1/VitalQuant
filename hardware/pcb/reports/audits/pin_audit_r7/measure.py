#!/usr/bin/env python3
"""R6 audit: per-pin pad positions + nearest same-net capacitor distance.
Run with KiCad bundled python:
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 measure.py
"""
import pcbnew, json, csv, math
from pathlib import Path

ROOT = Path('/Users/deepakdiwan/Downloads/VitalQuant_Hand/fix_v15')
ICS = ["U1","U2","U3","U4","U7","U8","U9","U15","U22"]

board = pcbnew.LoadBoard(str(ROOT/'vitalquant_v2.kicad_pcb'))
nm = pcbnew.FromMM

# BOM values
bom = {}
with open(ROOT/'fab'/'BOM.csv') as f:
    for row in csv.DictReader(f):
        bom[row['Refs']] = (row['Value'], row['Footprint'].split(':')[-1], row.get('DNP',''))

# footprint -> fp object, ref -> pads
fps = {fp.GetReference(): fp for fp in board.GetFootprints()}

def pads_of(ref):
    fp = fps.get(ref)
    if not fp: return {}
    out = {}
    for p in fp.Pads():
        net = p.GetNetname() or ''
        c = p.GetPosition()
        out[p.GetNumber()] = {'net': net, 'x': c.x/1e6, 'y': c.y/1e6}
    return out

def pad_center_mm(ref, num):
    fp = fps.get(ref)
    if not fp: return None
    for p in fp.Pads():
        if p.GetNumber() == num:
            c = p.GetPosition(); return (c.x/1e6, c.y/1e6)
    return None

# net -> [(ref, padnum, x, y)] for ALL pads
net_pads = {}
for ref, fp in fps.items():
    for p in fp.Pads():
        net_pads.setdefault(p.GetNetname() or '', []).append(
            (ref, p.GetNumber(), p.GetPosition().x/1e6, p.GetPosition().y/1e6))

def cap_pads_on_net(net):
    """capacitor pads on a net: [(ref,num,x,y)]"""
    return [t for t in net_pads.get(net, []) if t[0].startswith('C')]

result = {}
pinmap = json.load(open(ROOT/'pinmap.json'))['pins']
for ic in ICS:
    pads = pads_of(ic)
    rows = []
    for num, pname, net, ptype in pinmap[ic]:
        pos = pad_center_mm(ic, num)
        # all refs on net
        refs = sorted({t[0] for t in net_pads.get(net, [])})
        refvals = []
        for r in refs:
            v = bom.get(r, ('?','?',''))
            refvals.append(f"{r}={v[0]}" + ("(DNP)" if v[2] else ""))
        # nearest capacitor pad distance (component pads sharing net)
        near = None
        if pos:
            for cref, cnum, cx, cy in cap_pads_on_net(net):
                # skip pads that are the same component pin
                d = math.hypot(cx-pos[0], cy-pos[1])
                if near is None or d < near[1]:
                    near = (f"{cref}.{cnum}", round(d,2), bom.get(cref,('?','?',''))[0])
        rows.append({'pin': num, 'name': pname, 'net': net, 'type': ptype,
                     'x': round(pos[0],2) if pos else None,
                     'y': round(pos[1],2) if pos else None,
                     'refs': refvals, 'near_cap': near})
    result[ic] = rows

out = ROOT/'pin_audit_r7'/'measure.json'
out.write_text(json.dumps(result, indent=1))
print('wrote', out)
for ic in ICS:
    print(ic, len(result[ic]), 'pins')
