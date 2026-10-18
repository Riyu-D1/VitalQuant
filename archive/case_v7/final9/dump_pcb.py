#!/usr/bin/env python3
"""case_v7 final9: dump board footprints/pads/nets to parse/pcb.json (pcbnew)."""
import pcbnew, json, os

PCB = '/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v9/vitalq_v2.kicad_pcb'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'parse', 'pcb.json')

b = pcbnew.LoadBoard(PCB)
lname = {i: pcbnew.LayerName(i) for i in range(pcbnew.PCB_LAYER_ID_COUNT)}
fps = []
for fp in b.GetFootprints():
    pads = []
    for p in fp.Pads():
        pads.append({'num': p.GetNumber(), 'net': p.GetNetname() or None,
                     'layer': p.GetLayerName() if hasattr(p, 'GetLayerName') else None})
    fps.append({'ref': fp.GetReference(), 'value': fp.GetValue(),
                'layer': lname[fp.GetLayer()],
                'at': (pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y)),
                'pads': pads})
segs = vias = 0
for t in b.GetTracks():
    if t.Type() == pcbnew.PCB_VIA_T:
        vias += 1
    else:
        segs += 1
json.dump({'footprints': fps, 'segments': segs, 'vias': vias},
          open(OUT, 'w'), indent=1)
print('footprints', len(fps), 'segments', segs, 'vias', vias)
