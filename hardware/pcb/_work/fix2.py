#!/usr/bin/env python3
# fix2.py — repair pass 2: remove bad adds/orphans, add clean routes
import re, sys, uuid

FN = 'vitalq_v2.kicad_pcb'
src = open(FN).read()
lines = src.splitlines(True)

def block_lines(start_idx):
    d=0; i=start_idx
    while True:
        d += lines[i].count('(')-lines[i].count(')')
        i+=1
        if d<=0: return i

# ---- collect (segment|via) top-level blocks ----
segs=[]; vias=[]
i=0;n=len(lines)
while i<n:
    ln=lines[i]
    m=re.match(r'^\t\((segment|via)\b',ln)
    if m:
        j=block_lines(i)
        (segs if m.group(1)=='segment' else vias).append((i,j))
        i=j
    else: i+=1

def gk(l,t):
    m=re.search(r'\(%s\s+([^)]*)\)'%t,l); return m.group(1).split() if m else None

def parse(rng):
    net=lay=None;sx=sy=ex=ey=None
    for k in range(*rng):
        for l in lines[k].split(')('):  # safe-enough; each prop on own line anyway
            pass
    for k in range(*rng):
        l=lines[k]
        v=gk(l,'net');
        if v: net=v[0].strip('"')
        v=gk(l,'start')
        if v: sx,sy=float(v[0]),float(v[1])
        v=gk(l,'end')
        if v: ex,ey=float(v[0]),float(v[1])
        v=gk(l,'at')
        if v: sx,sy,ex,ey=float(v[0]),float(v[1]),float(v[0]),float(v[1])
        v=gk(l,'layer')
        if v: lay=v[0].strip('"')
        v=gk(l,'layers')
        if v: lay=v[0].strip('"')
    return net,lay,sx,sy,ex,ey

seginfo=[(*parse((a,b)),a,b) for a,b in segs]
viainfo =[(*parse((a,b)),a,b) for a,b in vias]

EP=0.011
def eq(a,b): return abs(a-b)<EP

rip=[]
def R(net,x1,y1,x2,y2,lay):
    hit=0
    for k,(nt,ly,sx,sy,ex,ey,a,b) in enumerate(seginfo):
        if k in rip: continue
        if nt==net and ly==lay and ((eq(sx,x1) and eq(sy,y1) and eq(ex,x2) and eq(ey,y2)) or (eq(sx,x2) and eq(sy,y2) and eq(ex,x1) and eq(ey,y1))):
            rip.append(k);hit+=1
    if hit!=1: print('RIP MISS %s %s %s %s %s %s'%(net,lay,x1,y1,x2,y2))
vrip=[]
def RV(net,x,y):
    hit=0
    for k,(nt,ly,sx,sy,ex,ey,a,b) in enumerate(viainfo):
        if k in vrip: continue
        if nt==net and eq(sx,x) and eq(sy,y): vrip.append(k);hit+=1
    if hit!=1: print('RIPV MISS %s %s %s'%(net,x,y))

# ============ RIP: my bad pass-1 adds ============
# GND bad
R('GND',26.25,-3.755,26.0,-4.0,'F.Cu'); R('GND',32.75,-3.755,32.75,-4.3,'F.Cu')
R('GND',27.3,-1.91,27.0,-2.3,'F.Cu'); R('GND',27.0,-2.3,26.9,-2.4,'F.Cu')
RV('GND',26.0,-4.0); RV('GND',32.75,-4.3); RV('GND',26.9,-2.4)
RV('GND',24.35,-5.5); RV('GND',24.4,-8.9)
# VBUS bad
R('VBUS',27.05,-3.755,27.05,-4.3,'F.Cu'); R('VBUS',27.05,-4.3,26.4,-4.05,'F.Cu')
R('VBUS',26.4,-4.05,26.0,-3.5,'F.Cu'); R('VBUS',26.0,-3.5,25.6,-2.5,'F.Cu')
R('VBUS',25.6,-2.5,25.6,-1.7,'F.Cu'); R('VBUS',25.6,-1.7,26.4,-1.2,'F.Cu')
R('VBUS',26.4,-1.2,26.847,-1.121,'F.Cu')
R('VBUS',31.95,-3.755,31.95,-4.5,'F.Cu'); R('VBUS',31.95,-4.5,32.9,-4.9,'F.Cu')
R('VBUS',32.9,-4.9,33.2,-5.9,'F.Cu'); R('VBUS',33.2,-5.9,33.2,-6.1,'F.Cu')
R('VBUS',33.2,-6.1,33.4,-6.5,'F.Cu')
R('VBUS',33.2,-6.1,31.5,-6.6,'In3.Cu'); R('VBUS',33.4,-6.5,31.5,-6.6,'In3.Cu')
RV('VBUS',33.2,-6.1); RV('VBUS',33.4,-6.5); RV('VBUS',27.93,-2.23)
# CC2 bad
R('USB_CC2',27.75,-3.755,27.75,-2.8,'F.Cu'); R('USB_CC2',27.75,-2.8,27.9,-1.9,'F.Cu')
R('USB_CC2',27.9,-1.9,27.9,-0.9,'F.Cu'); R('USB_CC2',27.9,-0.9,27.57,-0.89,'F.Cu')
# DP bad
R('USB_DP',28.75,-3.755,28.75,-4.05,'F.Cu'); R('USB_DP',28.75,-4.05,29.75,-4.05,'F.Cu')
R('USB_DP',29.75,-4.05,29.75,-3.755,'F.Cu')
R('USB_DP',29.75,-3.755,29.75,-2.95,'F.Cu'); R('USB_DP',29.75,-2.95,28.0,-2.45,'F.Cu')
R('USB_DP',28.0,-2.45,26.4,-2.2,'F.Cu'); R('USB_DP',26.4,-2.2,26.25,-2.2,'F.Cu')
R('USB_DP',28.75,-3.755,28.5,-4.2,'F.Cu'); R('USB_DP',28.5,-4.2,26.5,-6.0,'F.Cu')
R('USB_DP',26.5,-6.0,24.3,-6.0,'F.Cu'); R('USB_DP',24.3,-6.0,23.793,-6.086,'F.Cu')
# DM bad
R('USB_DM',29.25,-3.755,29.25,-4.35,'F.Cu'); R('USB_DM',29.25,-4.35,30.25,-4.35,'F.Cu')
R('USB_DM',30.25,-4.35,30.25,-3.755,'F.Cu')
R('USB_DM',30.25,-3.755,30.25,-2.95,'F.Cu'); R('USB_DM',30.25,-2.95,28.3,-2.5,'F.Cu')
R('USB_DM',28.3,-2.5,26.0,-1.9,'F.Cu'); R('USB_DM',26.0,-1.9,25.87,-1.9,'F.Cu')
R('USB_DM',29.25,-4.35,28.6,-4.7,'F.Cu'); R('USB_DM',28.6,-4.7,28.0,-5.7,'F.Cu')
R('USB_DM',28.0,-5.7,24.5,-6.3,'F.Cu'); R('USB_DM',24.5,-6.3,23.925,-6.287,'F.Cu')
# ESP_TX bad tail
R('ESP_TX',31.516,-6.78,33.4,-7.3,'F.Cu'); R('ESP_TX',33.4,-7.3,34.9,-7.5,'F.Cu')
R('ESP_TX',34.9,-7.5,35.9,-8.0,'F.Cu'); R('ESP_TX',35.9,-8.0,36.3,-8.474,'F.Cu')
# +3V3_ANA bad
R('+3V3_ANA',32.51,-1.9,33.6,-1.6,'F.Cu'); R('+3V3_ANA',33.6,-1.6,35.0,-1.5,'F.Cu')
R('+3V3_ANA',35.0,-1.5,35.7,-1.7,'F.Cu'); R('+3V3_ANA',35.7,-1.7,36.0,-2.4,'F.Cu')
R('+3V3_ANA',36.0,-2.4,36.0,-3.2,'F.Cu'); R('+3V3_ANA',36.0,-3.2,35.807,-3.612,'F.Cu')
# IN1P bad via/stub
R('IN1P',31.49,-1.9,30.9,-1.6,'F.Cu'); R('IN1P',30.9,-1.6,30.9,-1.45,'F.Cu')
R('IN1P',30.9,-1.45,29.921,-1.339,'In2.Cu'); RV('IN1P',30.9,-1.45)
# IN1P east leg stays? (29.921,-1.339)-(32.3,-1.8)-(34.0,-3.2)-(34.7,-3.5)-(35.2,-4.4)-(35.566,-5.766)-(43.266,-5.766) — keep (no violations)

# ============ RIP: orphans / old-position leftovers ============
for seg in [
    ('VBUS',23.550,-4.481,23.550,-4.481),   # zero-len
    ('VBUS',28.450,-3.029,28.450,-3.029),
    ('VBUS',28.450,-4.481,28.450,-4.481),
    ('GND',29.551,-4.118,29.551,-4.118),
    ('GND',29.250,-3.755,29.400,-3.936),
    ('GND',29.400,-3.936,29.551,-4.118),
    ('GND',29.551,-4.118,29.817,-3.968),
    ('GND',29.817,-3.968,30.068,-4.319),
    ('GND',30.068,-4.319,30.320,-4.670),
    ('ESP_TX',24.800,10.998,24.800,-6.780),   # In5 col -> jog
    ('SPARE_IN',25.200,-7.800,25.200,-0.850), # In6 col -> jog
]:
    lay = 'In5.Cu' if seg[0]=='ESP_TX' else ('In6.Cu' if seg[0]=='SPARE_IN' else 'F.Cu')
    R(seg[0],seg[1],seg[2],seg[3],seg[4],lay)

# any remaining VBUS stubs at old pad cols 23.55/28.45
for k,(nt,ly,sx,sy,ex,ey,a,b) in enumerate(seginfo):
    if k in rip: continue
    if nt=='VBUS' and ly=='F.Cu':
        for (px,py) in ((sx,sy),(ex,ey)):
            if (23.4<px<23.7 and -5.0<py<-3.5) or (28.2<px<28.7 and -5.0<py<-2.7):
                rip.append(k);break

# ============ ADD ============
add=[]
def A(net,pts,lay,w):
    for a,b in zip(pts,pts[1:]): add.append(('seg',net,lay,a[0],a[1],b[0],b[1],w))
def V(x,y,net,size=0.4004,drill=0.2002): add.append(('via',net,'FB',x,y,size,drill))

W=0.1017; WU=0.129; WV=0.5; WI=0.5; WG=0.3
T='F.Cu'; L2='In2.Cu'; L3='In3.Cu'; B='B.Cu'; L5='In5.Cu'; L6='In6.Cu'

# ---- GND pad ties into SH rings ----
A('GND',[(26.25,-3.755),(26.25,-4.3),(25.68,-4.63),(25.6,-4.62)],T,WG)
A('GND',[(32.75,-3.755),(32.95,-4.2),(33.4,-4.55)],T,WG)
# 4 shield stitch vias + stubs
V(23.9,-5.6,'GND');  A('GND',[(23.9,-5.6),(24.68,-5.4)],T,WG)
V(23.8,-8.3,'GND');  A('GND',[(23.8,-8.3),(24.0,-8.9),(24.75,-8.85)],T,WG)
A('GND',[(34.75,-4.5),(34.32,-4.5)],T,WG)      # via already exists from pass1
A('GND',[(34.75,-8.6),(34.32,-8.7)],T,WG)

# ---- VBUS ----
A('VBUS',[(27.05,-3.755),(27.05,-4.35),(27.35,-4.7),(27.55,-4.9)],T,WV); V(27.55,-4.9,'VBUS')
A('VBUS',[(31.95,-3.755),(31.95,-4.5),(31.6,-4.9),(31.5,-5.05)],T,WV); V(31.5,-5.05,'VBUS')
A('VBUS',[(27.55,-4.9),(29.5,-4.2)],L3,WI)
A('VBUS',[(31.5,-5.05),(31.5,-6.6)],L3,WI)
# trunk link via on trunk col
V(26.85,0.8,'VBUS')
A('VBUS',[(26.85,0.8),(28.3,-1.2),(29.74,-1.0)],L3,WI)

# ---- USB_CC2: via-hop ----
A('USB_CC2',[(27.75,-3.755),(27.75,-2.55)],T,W); V(27.75,-2.55,'USB_CC2')
A('USB_CC2',[(27.75,-2.55),(28.4,-1.7)],L2,W); V(28.4,-1.7,'USB_CC2')
A('USB_CC2',[(28.4,-1.7),(28.3,-1.91),(27.57,-1.91)],T,W)

# ---- USB_DP: north merge loop + In2 hop to east trunk x26.25 ----
A('USB_DP',[(28.75,-3.755),(28.75,-2.55),(29.75,-2.55),(29.75,-3.755)],T,WU)
V(29.65,-2.55,'USB_DP')
A('USB_DP',[(29.65,-2.55),(28.2,-3.05),(26.4,-2.85),(26.25,-2.6)],L2,WU)
V(26.25,-2.6,'USB_DP')
# west escape feed (pad28.75 tail -> existing (23.793,-6.086) chain); dodge NPTH(26.61,-5.2)
A('USB_DP',[(28.75,-3.755),(28.75,-4.6),(28.2,-5.0),(27.6,-5.7),(26.0,-6.15),(24.6,-6.05),(23.793,-6.086)],T,WU)
# restore ripped north west trunk (baseline verbatim)
A('USB_DP',[(25.382,-3.03),(25.382,-1.749),(23.957,-0.324),(23.932,-0.286),(23.923,-0.241),(23.923,1.062)],T,WU)

# ---- USB_DM: south merge below pad row + west escape; restore north trunk ----
A('USB_DM',[(29.25,-3.755),(29.25,-4.75),(30.25,-4.75),(30.25,-3.755)],T,WU)
A('USB_DM',[(29.25,-4.75),(28.5,-5.6),(26.8,-6.35),(25.0,-6.3),(23.925,-6.287)],T,WU)
# restore ripped north trunk (baseline verbatim)
A('USB_DM',[(25.617,-3.261),(25.617,-3.03),(25.617,-1.7),(25.608,-1.655),(25.583,-1.617),(24.158,-0.192),(24.158,1.111)],T,WU)

# ---- USB_CC1 (unchanged from pass1, re-add since not ripped) ----
# A('USB_CC1',[(30.75,-3.755),(30.75,-4.5),(30.3,-5.0),(30.0,-5.5),(30.0,-6.426)],T,W)  # already present

# ---- ESP_TX tail via-hop + In5 jog ----
A('ESP_TX',[(31.516,-6.78),(33.4,-7.3),(34.9,-7.5),(35.3,-7.3)],T,W); V(35.3,-7.3,'ESP_TX')
A('ESP_TX',[(35.3,-7.3),(36.35,-8.35)],B,W); V(36.35,-8.35,'ESP_TX')
A('ESP_TX',[(24.8,10.998),(24.8,-3.4),(24.35,-3.85),(24.35,-5.95),(24.8,-6.4),(24.8,-6.78)],L5,W)

# ---- SPARE_IN In6 jog ----
A('SPARE_IN',[(25.2,-7.8),(25.2,-7.2),(24.45,-6.6),(24.35,-6.3),(24.35,-3.5),(25.2,-2.9),(25.2,-0.85)],L6,W)

# ---- IN1P ----
A('IN1P',[(31.49,-1.9),(30.9,-1.5),(30.0,-1.2)],T,W); V(30.0,-1.2,'IN1P')
A('IN1P',[(30.0,-1.2),(29.921,-1.339)],L2,W)

# ---- +3V3_ANA ----
A('+3V3_ANA',[(32.51,-1.9),(33.6,-1.6),(35.0,-1.55),(35.4,-1.95),(35.4,-3.0),(35.807,-3.612)],T,W)

# (DM gap at y8.26 resolves itself once the north trunk island is restored — no bridge needed)

# ---- R2 pad2 GND stub + via ----
A('GND',[(27.3,-0.89),(27.3,-0.15)],T,WG); V(27.3,-0.15,'GND')

# ============ apply ============
# delete ripped blocks (desc order)
kill=sorted(set([seginfo[k][6:] for k in rip]+[viainfo[k][6:] for k in vrip]),key=lambda t:-t[0])
for a,b in kill:
    del lines[a:b]
src=''.join(lines)

# append new items before final closing paren of file
ins='\n'
for it in add:
    if it[0]=='seg':
        _,net,lay,x1,y1,x2,y2,w=it
        ins+='\t(segment\n\t\t(start %.6f %.6f)\n\t\t(end %.6f %.6f)\n\t\t(width %.6f)\n\t\t(layer "%s")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n'%(x1,y1,x2,y2,w,lay,net,uuid.uuid4())
    else:
        _,net,_,x,y,size,drill=it
        ins+='\t(via\n\t\t(at %.6f %.6f)\n\t\t(size %.4f)\n\t\t(drill %.4f)\n\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n'%(x,y,size,drill,net,uuid.uuid4())

idx=src.rfind('\n)')
out=src[:idx+1]+ins+')'
open(FN,'w').write(out)
print('ripped segs',len(rip),'vias',len(vrip),'| added',sum(1 for i in add if i[0]=='seg'),'segs',sum(1 for i in add if i[0]=='via'),'vias')
