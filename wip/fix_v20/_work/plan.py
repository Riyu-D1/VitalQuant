#!/usr/bin/env python3
"""fix_v18 additive plan: verify all new geometry before editing the board."""
import sys, math
sys.path.insert(0, '_work')
from check import Chk, CLR
from pcb import PCB
from shapely.geometry import Point, LineString, Polygon, box

c = Chk('vitalq_v2.kicad_pcb')
FILL = {'In1.Cu', 'In4.Cu', 'In6.Cu'}

def seg_chk(layer, a, b, w, net):
    g = LineString([a, b]).buffer(w/2, cap_style='flat')
    bad = []
    for gg, n in c.layers.get(layer, []):
        if n == net: continue
        d = g.distance(gg)
        if d < CLR - 1e-9: bad.append((round(d,4), n))
    for ko in c.keepouts.get(layer, []):
        if g.intersects(ko): bad.append(('keepout', ''))
    return bad

def via_chk(x, y, r, net, ignore_fill=True):
    g = Point(x, y).buffer(r)
    bad = []
    for L, items in c.layers.items():
        if ignore_fill and L in FILL: continue
        for gg, n in items:
            if n == net: continue
            d = g.distance(gg)
            if d < CLR - 1e-9: bad.append((L, round(d,4), n))
    for L, kos in c.keepouts.items():
        for ko in kos:
            if g.intersects(ko): bad.append((L, 'keepout', ''))
    return bad

# ---------------- new footprints (F.Cu) ----------------
Q6 = (50.5, 22.9, 90.0)     # SOT-23 rot90: pin1 G NE(51.45,21.96) pin2 S NW(49.55,21.96) pin3 D S(50.5,23.84)
D33 = (50.3, 26.15, 90.0)   # SOD-323 rot90: pad1 K N(50.3,25.10) pad2 A S(50.3,27.20)
R123 = (50.5, 28.3, 0.0)    # 0402: pad1 W(50.02,28.3) pad2 E(50.98,28.3)

def xform(fp, lx, ly):
    x, y, rot = fp; r = math.radians(rot)
    return (x + lx*math.cos(r) - ly*math.sin(r), y + lx*math.sin(r) + ly*math.cos(r))

PADS = {  # (center, halfsize, net)
 'Q6.1': (xform(Q6,-0.9375,-0.95), (0.7375,0.3), 'VBUS'),
 'Q6.2': (xform(Q6,-0.9375, 0.95), (0.7375,0.3), 'VBAT_SYS'),
 'Q6.3': (xform(Q6, 0.9375, 0.0), (0.7375,0.3), 'VBAT'),
 'D33.1':(xform(D33,-1.05,0), (0.3,0.225), 'VBAT_SYS'),
 'D33.2':(xform(D33, 1.05,0), (0.3,0.225), 'VBUS'),
 'R123.1':(xform(R123,-0.51,0), (0.27,0.32), 'GND'),
 'R123.2':(xform(R123, 0.51,0), (0.27,0.32), 'VBUS'),
}
# rotate pad halfsize for 90-deg rotations
for k in ('Q6.1','Q6.2','Q6.3','D33.1','D33.2'):
    (px,py),(hx,hy),n = PADS[k]
    PADS[k] = ((px,py),(hy,hx),n)

VIAS = [  # (x,y,net)
 (53.0, 26.8, 'VBUS'),     # tab via (In3 arrival)
 (52.4, 25.4, 'VBAT'),     # tab via
 (48.9, 26.0, 'VBAT_SYS'), # tab via
 (49.6, 28.5, 'GND'),      # tab via
 (46.5, 26.5, 'GND'),      # tap via into In1/In6 fill
 (45.7, 22.53, 'VBAT'),    # tap via on VBAT F.Cu track
 (46.9, 25.5, 'VBAT_SYS'), # tap via into In4 VBAT_SYS fill
]

NEW_SEGS = [  # (layer,a,b,w,net)
 ('In3.Cu',(46.9,25.5),(48.9,26.0),0.3,'VBAT_SYS'),
 ('In3.Cu',(45.7,22.53),(52.4,25.4),0.3,'VBAT'),
 ('In5.Cu',(46.5,26.5),(49.6,28.5),0.3,'GND'),
 # F.Cu stubs
 ('F.Cu',(51.45,21.96),(53.0,23.0),0.3,'VBUS'),          # gate -> spine
 ('F.Cu',(53.0,23.0),(53.0,27.2),0.3,'VBUS'),            # spine
 ('F.Cu',(50.3,27.2),(53.0,27.2),0.3,'VBUS'),            # anode pad2 -> spine
 ('F.Cu',(50.98,28.3),(53.0,28.3),0.3,'VBUS'),           # R123 pad2 -> spine
 ('F.Cu',(53.0,27.2),(53.0,28.3),0.3,'VBUS'),            # spine ext
 ('F.Cu',(50.5,23.84),(52.4,25.4),0.3,'VBAT'),           # drain pad3 -> VBAT via
 ('F.Cu',(49.55,21.96),(48.9,26.0),0.3,'VBAT_SYS'),      # source pad2 -> via
 ('F.Cu',(50.3,25.10),(48.9,26.0),0.3,'VBAT_SYS'),       # cathode pad1 -> via
 ('F.Cu',(50.02,28.3),(49.6,28.5),0.3,'GND'),            # pad1 -> GND via
]

def pad_geom(c_, h):
    return Point(*c_).buffer(1) and box(c_[0]-h[0], c_[1]-h[1], c_[0]+h[0], c_[1]+h[1])

print("=== via checks ===")
for x,y,net in VIAS:
    bad = via_chk(x, y, 0.2, net)
    # also vs new pads & new stubs
    g = Point(x,y).buffer(0.2)
    for pk,(pc,h,n) in PADS.items():
        if n==net: continue
        if g.distance(pad_geom(pc,h)) < CLR: bad.append(('newpad',pk))
    print(f"  via {net} ({x},{y}):", 'OK' if not bad else bad[:6])

print("=== new pad vs existing F.Cu ===")
for pk,(pc,h,n) in PADS.items():
    g = pad_geom(pc,h)
    bad=[]
    for gg,nn in c.layers.get('F.Cu',[]):
        if nn==n: continue
        if g.distance(gg)<CLR-1e-9: bad.append(nn)
    print(f"  pad {pk} {n} @{pc}:", 'OK' if not bad else set(bad))

print("=== pad-vs-pad ===")
items=list(PADS.items())
for i in range(len(items)):
    for j in range(i+1,len(items)):
        (k1,(c1,h1,n1)),(k2,(c2,h2,n2))=items[i],items[j]
        if n1==n2: continue
        d=pad_geom(c1,h1).distance(pad_geom(c2,h2))
        if d<CLR: print(f"  {k1}({n1}) vs {k2}({n2}): gap {d:.3f}")

print("=== new segs vs existing ===")
for L,a,b,w,net in NEW_SEGS:
    bad=seg_chk(L,a,b,w,net)
    print(f"  {L} {net} {a}->{b}:", 'OK' if not bad else bad[:6])

print("=== new segs vs new pads/vias/other segs ===")
g2=[]
for L,a,b,w,net in NEW_SEGS:
    g2.append((L,LineString([a,b]).buffer(w/2,cap_style='flat'),net,a,b))
for pk,(pc,h,net) in PADS.items():
    pg=pad_geom(pc,h)
    for L,g,net2,a,b in g2:
        if L!='F.Cu' or net2==net: continue
        if g.distance(pg)<CLR-1e-9: print(f"  seg {net2}{a}->{b} hits pad {pk}({net}) gap{g.distance(pg):.3f}")
for vx,vy,vn in VIAS:
    vg=Point(vx,vy).buffer(0.2)
    for L,g,net,a,b in g2:
        if L!='F.Cu' or net==vn: continue
        if g.distance(vg)<CLR-1e-9: print(f"  seg {net} {a}->{b} near via {vn}({vx},{vy}) gap {g.distance(vg):.3f}")
for i in range(len(g2)):
    for j in range(i+1,len(g2)):
        L1,g1,n1,a1,b1=g2[i]; L2,g2_,n2,a2,b2=g2[j]
        if L1!=L2 or n1==n2: continue
        d=g1.distance(g2_)
        if d<CLR-1e-9: print(f"  seg-seg {n1}{a1}->{b1} vs {n2}{a2}->{b2}: gap {d:.3f}")
print("done")
