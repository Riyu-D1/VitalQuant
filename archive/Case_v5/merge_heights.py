#!/usr/bin/env python3
"""Merge geometry.json (pcbnew) + vrml_boxes.json (3D bboxes) -> geometry.json v2
Adds per-footprint: side, xy bbox of real 3D body, height above board surface.
"""
import json
import numpy as np

g = json.load(open('geometry.json'))
if 'components' in g and 'footprints' not in g:
    g['footprints'] = g.pop('components')
boxes = json.load(open('vrml_boxes.json'))
CX, CY = 23.0, 27.0
TOP_Z = 0.8  # board surfaces measured from VRML (board body -0.7875..0.7875, copper at +-0.8)

# datasheet heights (mm) for parts without resolvable 3D models
H = {
 'TPD1E10B06_DPY': 0.4, 'QFN_LSM6DSV80XTR_STM': 0.9, 'Texas_DSG0008A': 0.8,
 'AD5940': 0.6, 'ADS1292': 1.0, 'L_DFE201612E': 1.2, 'ESP32_S3_MINI': 2.7,
 'W25Q512': 0.8, 'TPS61240': 0.7, 'TMP117': 1.0, 'TCA6408A': 0.6,
 'SFH7072': 1.6, 'NF2W757G': 1.0, 'AS7341': 1.0, 'MLX90632': 1.0,
 'AFE4900': 0.5, 'TDFN-8': 0.8, 'BGA56': 0.6, 'QFN40P400X400X100': 1.0,
 'SHT45': 0.6, 'IM69D130': 1.2, 'MountingHole': 0.0, 'TestPoint': 0.0,
 'Tag-Connect': 0.3, 'Fiducial': 0.0,
}
def h_for(fpname):
    for k, v in H.items():
        if k in fpname: return v
    if fpname.startswith('Pads_') or '_Pad_' in fpname: return 0.0
    if '0402' in fpname: return 0.5
    if '0603' in fpname or '1608' in fpname: return 0.6
    if '2512' in fpname: return 0.65
    return 1.0  # conservative default

# component subtrees in pcb coords (VRML y-axis is inverted: pcb_y = CY - vrml_y)
comps = []
for b in boxes:
    sx=b['max'][0]-b['min'][0]; sy=b['max'][1]-b['min'][1]; sz=b['max'][2]-b['min'][2]
    if sx>45 or sy>70: continue            # board/layer sheets
    if sz < 0.1: continue                  # zero-thickness layer exports
    if abs(sz-0.6)<0.05 and (abs(abs(b['min'][2])-1.4)<0.05 or abs(abs(b['max'][2])-1.4)<0.05):
        continue  # copper pour slabs
    y0p = CY - b['max'][1]; y1p = CY - b['min'][1]   # y flip
    comps.append({'min':[b['min'][0]+CX,y0p,b['min'][2]],
                  'max':[b['max'][0]+CX,y1p,b['max'][2]]})

def crtyd_bbox(fp):
    p = fp['courtyard']
    if not p: return fp['bbox']['x'],fp['bbox']['y'],fp['bbox']['x']+fp['bbox']['w'],fp['bbox']['y']+fp['bbox']['h']
    xs=[q[0] for q in p]; ys=[q[1] for q in p]
    return min(xs),min(ys),max(xs),max(ys)

# assign each subtree to exactly one fp: nearest fp.pos among fps whose
# courtyard(+1mm) contains the subtree center and whose side matches z
def crtyd_of(fp):
    x0,y0,x1,y1 = crtyd_bbox(fp); return x0,y0,x1,y1
assign = {}
for ci,c in enumerate(comps):
    ccx=(c['min'][0]+c['max'][0])/2; ccy=(c['min'][1]+c['max'][1])/2
    side = 'F' if c['min'][2]>0.3 else ('B' if c['max'][2]<-0.3 else '?')
    best=None;bd=1e9
    for fi,fp in enumerate(g['footprints']):
        if side != '?' and fp['side']!=side: continue
        x0,y0,x1,y1=crtyd_of(fp)
        if x0-1<=ccx<=x1+1 and y0-1<=ccy<=y1+1:
            dx=fp['pos'][0]-ccx; dy=fp['pos'][1]-ccy; d=dx*dx+dy*dy
            if d<bd: bd=d; best=fi
    if best is not None:
        assign.setdefault(best,[]).append(ci)

unmatched_comps = [c for i,c in enumerate(comps) if all(i not in v for v in assign.values())]
for fi,fp in enumerate(g['footprints']):
    cand=[comps[i] for i in assign.get(fi,[])]
    if cand:
        zmin=min(c['min'][2] for c in cand); zmax=max(c['max'][2] for c in cand)
        bx0=min(c['min'][0] for c in cand); bx1=max(c['max'][0] for c in cand)
        by0=min(c['min'][1] for c in cand); by1=max(c['max'][1] for c in cand)
        fp['h3d'] = round((zmax-TOP_Z) if fp['side']=='F' else (-TOP_Z-zmin), 3)
        fp['model3d_bbox'] = [round(bx0,3),round(by0,3),round(bx1,3),round(by1,3)]
        fp['h3d_src']='vrml'
    else:
        fp['h3d'] = h_for(fp['fp'])
        fp['model3d_bbox'] = None
        fp['h3d_src']='datasheet' if any(k in fp['fp'] for k in list(H)+['Pads_','0402','0603','1608','2512','TestPoint','Tag-Connect','MountingHole']) else 'default1.0'

g['components'] = g.pop('footprints')
json.dump(g, open('geometry.json','w'), indent=1)

# report
mx_f = max((f['h3d'],f['ref']) for f in g['components'] if f['side']=='F')
mx_b = max((f['h3d'],f['ref']) for f in g['components'] if f['side']=='B')
print("max height F:", mx_f, " B:", mx_b)
print("unmatched subtrees:", len(unmatched_comps))
for c in unmatched_comps[:10]: print("  ", c)
print("h>1.5 parts:")
for f in sorted(g['components'],key=lambda f:-f['h3d']):
    if f['h3d']>1.5:
        print(f"  {f['ref']:5s} {f['side']} h={f['h3d']:5.2f} src={f['h3d_src']:10s} {f['fp'][:40]}")
