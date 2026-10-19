#!/usr/bin/env python3
import sys, math
sys.path.insert(0,'_work')
import numpy as np
from check import Chk
from route_ml import ML, STEP, RLAYERS
from shapely.geometry import LineString
from smooth import smooth, seg_ok

c = Chk('../fix_v16/vitalq_v2.kicad_pcb')
EXTRA_V=[(52.4,25.4,'VBAT'),(48.9,26.0,'VBAT_SYS'),(49.6,29.0,'GND'),
         (46.25,28.75,'GND'),(45.7,22.53,'VBAT'),(46.5,26.75,'VBAT_SYS')]
# existing new stubs (fixed): VBUS stubs near tab are same-net, ignore;
# VBAT In3 stub & VBAT_SYS/GND In5 stubs are real obstacles for other layers
NEWSEGS=[('In3.Cu','VBAT',(45.7,22.53),(52.4,25.4),0.3),
         ('In5.Cu','VBAT_SYS',(46.5,26.75),(48.9,26.0),0.3),
         ('In5.Cu','GND',(46.25,28.75),(49.6,29.0),0.3),
         ('F.Cu','VBAT',(50.5,23.8375),(52.4,25.4),0.3),
         ('F.Cu','VBAT_SYS',(49.55,21.9625),(48.9,26.0),0.3),
         ('F.Cu','VBAT_SYS',(49.25,26.7),(48.9,26.0),0.3),
         ('F.Cu','GND',(49.99,28.9),(49.6,29.0),0.3)]
# register new segs into checker layers
for L,n,a,b,w in NEWSEGS:
    g=LineString([a,b]).buffer(w/2,cap_style='flat')
    c.layers.setdefault(L,[]).append((g,n))
# new SMD pads as obstacles too (pads live on F.Cu)
for ref in ('Q6','D33','R123'):
    fp=[f for f in c.p.fps if f.ref==ref]
    if not fp: continue
    fp=fp[0]
    for pd in fp.pads:
        gp=c.p.pad_global(fp,pd)
        if gp is None: continue
        hw,hh=pd.size[0]/2,pd.size[1]/2
        import math as m
        r=m.radians(fp.rot+pd.rot); cx,cy=gp
        pts=[(cx+dx*m.cos(r)+dy*m.sin(r), cy-dx*m.sin(r)+dy*m.cos(r))
             for dx,dy in ((-hw,-hh),(hw,-hh),(hw,hh),(-hw,hh))]
        from shapely.geometry import Polygon
        c.layers.setdefault('F.Cu',[]).append((Polygon(pts),pd.net))

slots=[(6.825,24.8,7.825,29.2),(13.275,24.8,14.275,29.2),(19.725,24.8,20.725,29.2),
       (26.175,24.8,27.175,29.2),(6.825,40.5,7.825,44.9),(13.275,40.5,14.275,44.9),
       (20.1,40.5,21.1,44.9),(9.3,47.6,11.5,48.6),(11.9,49.5,12.9,52.3),
       (7.3,51.55,8.3,52.6),(22.0,53.5,23.0,54.5)]

def edge_mask(X,Y):
    r=0.17
    in_old=(X-r>=-2)&(X+r<=48)&(Y-r>=-10.5)&(Y+r<=64.5)
    in_tab=(X+r<=53.6)&(Y-r>=20)&(Y+r<=29.9)
    return in_old|in_tab

start=(float(sys.argv[2]),float(sys.argv[3])) if len(sys.argv)>3 else (26.99,-2.07)
target=(53.0,26.8)
ml=ML(c,'VBUS',0.3, start[0]-6, -7.5, 53.9, 30.0,
      extra_vias=EXTRA_V, edge_mask=edge_mask, slot_boxes=slots)
print("grid",ml.nx,ml.ny,"x",len(RLAYERS))
for L in RLAYERS: print(' ',L,'blocked',int(ml.blk[L].sum()))
path,err=ml.astar(start,target)
if err:
    print('ERR',err); sys.exit(1)
print("path cells:",len(path))
# convert: split into per-layer polylines at hop points
segs=[]; hops=[]; cur=[path[0]]
for i in range(1,len(path)):
    if path[i][2]!=path[i-1][2]:
        hops.append((path[i-1],path[i]))
        cur=[]
    cur.append(path[i])
# verify each consecutive hop: via_ok at hop cell
def bxy(c_): return (ml.x0+c_[0]*STEP, ml.y0+c_[1]*STEP)
print("hops:",[(RLAYERS[a[2]],RLAYERS[b[2]],bxy(a)) for a,b in hops])
# emit per-layer raw path file
with open('_work/ml_route.txt','w') as f:
    for ix,iy,li in path:
        f.write(f"{RLAYERS[li]} {ml.x0+ix*STEP:.3f} {ml.y0+iy*STEP:.3f}\n")
print("wrote _work/ml_route.txt")
