import sys, math
sys.path.insert(0,'_work')
import numpy as np
from check import Chk, CLR
from route import Grid, STEP
from smooth import smooth, seg_ok
from shapely.geometry import Point, LineString

c = Chk('_work/pcb_backup.kicad_pcb')
EXTRA_V=[(52.4,25.4,'VBAT'),(48.9,26.0,'VBAT_SYS'),(49.6,29.0,'GND'),
         (46.25,28.75,'GND'),(45.7,22.53,'VBAT'),(46.5,26.75,'VBAT_SYS')]
slots=[(6.825,24.8,7.825,29.2),(13.275,24.8,14.275,29.2),(19.725,24.8,20.725,29.2),
       (26.175,24.8,27.175,29.2),(6.825,40.5,7.825,44.9),(13.275,40.5,14.275,44.9),
       (20.1,40.5,21.1,44.9),(9.3,47.6,11.5,48.6),(11.9,49.5,12.9,52.3),(7.3,51.55,8.3,52.6),(22.0,53.5,23.0,54.5)]

def try_layer(L, target=(53.0,26.8)):
    g = Grid(c,L,'VBUS',0.3, 17.5, 9.0, 53.3, 29.4)
    nx,ny=g.nx,g.ny
    xs=g.x0+np.arange(nx)*STEP; ys=g.y0+np.arange(ny)*STEP
    Y,X=np.meshgrid(ys,xs,indexing='ij')
    r=0.17
    in_old=(X-r>=-2)&(X+r<=48)&(Y-r>=-10.5)&(Y+r<=64.5)
    in_tab=(X-r>=48)&(X+r<=53.6)&(Y-r>=20)&(Y+r<=29.9)
    span=(X-r>=-2)&(X+r<=53.6)&(Y-r>=20)&(Y+r<=29.9)
    valid=in_old|in_tab|span
    for x0,y0,x1,y1 in slots:
        valid&=~((X>x0-0.45)&(X<x1+0.45)&(Y>y0-0.45)&(Y<y1+0.45))
    blk=np.frombuffer(g.blocked,dtype=np.uint8).reshape(ny,nx).copy()
    blk|=~valid
    for vx,vy,vn in EXTRA_V:
        rr=0.2+0.15+0.1016+0.01
        blk|=((X-vx)**2+(Y-vy)**2<rr*rr)
    g.blocked=bytearray(blk.astype(np.uint8).tobytes())
    print(L,"grid",nx,ny,"blocked",int(blk.sum()),flush=True)
    path,err=g.astar((18.8083,20.1522),target)
    if err: print(L,"ERR",err); return
    print(L,"raw",len(path))
    sm=smooth(c,L,path,0.3,'VBUS')
    print("smoothed:")
    for p in sm: print("  (%.3f,%.3f)"%p)
    open(f'_work/vbus_route_{L}.txt','w').write('\n'.join(f'{p[0]:.4f} {p[1]:.4f}' for p in sm))

try_layer(sys.argv[1] if len(sys.argv)>1 else 'In2.Cu')
