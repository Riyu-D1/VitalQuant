import pcbnew, math, heapq, sys
from collections import defaultdict
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
SRC=BASE+'_work/pre_promote_backup.kicad_pcb'
DST=BASE+'vitalq_v2.kicad_pcb'
MM=1e6
GRID=0.05; SIG_W=0.127; PWR_W=0.25
CLR=0.24; DIL=int(math.ceil(CLR/GRID))
VIA_R=0.32; VDIL=int(math.ceil((VIA_R+0.18)/GRID))
LAYERS=(0,8,2)                      # F.Cu, In3, B.Cu
NEWX,NEWY=11.5,-6.5                 # U1 centre, 6mm north of (11.5,-0.5)
RZ=(-2.05,-16.55,23.0,6.0)          # rework zone (x0,y0,x1,y1)
KO=(-2.5,-16.55,25.5,-15.25)         # antenna keepout (under antenna +5mm)
BODY=(11.5-9.0,-10.55,11.5+9.0,6.0) # WROOM body on-board rect

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)
def inrect(x,y,r): return r[0]<=x<=r[2] and r[1]<=y<=r[3]

board=pcbnew.LoadBoard(SRC)
TRK0=[]   # (obj, isvia, x1,y1,x2,y2, net, layer)
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition(); TRK0.append((t,True,p.x/MM,p.y/MM,p.x/MM,p.y/MM,t.GetNetname(),-1))
    else:
        s0,e0=t.GetStart(),t.GetEnd()
        TRK0.append((t,False,s0.x/MM,s0.y/MM,e0.x/MM,e0.y/MM,t.GetNetname(),t.GetLayer()))

# ============ 1. GROW OUTLINE: north tab x[-2.05,22.5] up to -16.55 ============
# existing top edge: (-2,-10.5)->(48,-10.5); left edge (-2,64.5)->(-2,-10.5)
T0,T1=-2.05,22.5; TY=-16.55; OY=-10.5; CR=1.0
# remove the old top edge segment & left top corner piece
del_d=[]
for d in board.GetDrawings():
    if d.GetLayerName()!='Edge.Cuts': continue
    s,e=d.GetStart(),d.GetEnd(); x1,y1,x2,y2=s.x/MM,s.y/MM,e.x/MM,e.y/MM
    if abs(y1-OY)<0.01 and abs(y2-OY)<0.01 and min(x1,x2)<22.5:
        del_d.append(d)
    elif abs(x1+2.0)<0.01 and abs(x2+2.0)<0.01 and max(y1,y2)>OY-0.01:
        del_d.append(d)
for d in del_d: board.Remove(d)
def edge_seg(x1,y1,x2,y2):
    sh=pcbnew.PCB_SHAPE(board);sh.SetShape(pcbnew.SHAPE_T_SEGMENT)
    sh.SetLayer(pcbnew.Edge_Cuts)
    sh.SetStart(pcbnew.VECTOR2I(int(x1*MM),int(y1*MM)))
    sh.SetEnd(pcbnew.VECTOR2I(int(x2*MM),int(y2*MM)))
    sh.SetWidth(int(0.05*MM)); board.Add(sh)
def arc_corner(cx,cy,a0):  # quarter arc r=CR starting at angle a0(deg) CCW, as 3 segs
    import math as m
    pts=[]
    for i in range(4):
        a=m.radians(a0+ i*22.5)
        pts.append((cx+CR*m.cos(a),cy+CR*m.sin(a)))
    for i in range(3): edge_seg(*pts[i],*pts[i+1])
# tab outline path: from (-2.0,-10.5) west to (-2.05,-10.5), up, across top, down at 22.5, then east to 48
# bottom-left junction is concave at (-2.05,-10.5): keep square (concave)
edge_seg(-2.0,-10.5,-2.05,-10.5)
edge_seg(-2.05,-10.5,-2.05,TY+CR)
arc_corner(-2.05+CR,TY+CR,180)          # top-left outer corner
edge_seg(-2.05+CR,TY,22.5-CR,TY)
arc_corner(22.5-CR,TY+CR,270)           # top-right outer corner
edge_seg(22.5,TY+CR,22.5,-10.5)
edge_seg(22.5,-10.5,48.0,-10.5)          # remaining top edge
print('outline done')

# ============ 2. ANTENNA KEEPOUT: clear tracks/vias inside KO on all layers ============
ndel=0
trk=[]
for o,isvia,x1,y1,x2,y2,nn,lay in TRK0:
    if isvia:
        if inrect(x1,y1,KO): board.Remove(o);ndel+=1;continue
    else:
        inKO1,inKO2=inrect(x1,y1,KO),inrect(x2,y2,KO)
        inBD1,inBD2=inrect(x1,y1,BODY),inrect(x2,y2,BODY)
        if inKO1 or inKO2 or (lay==0 and (inBD1 or inBD2)):
            board.Remove(o);ndel+=1;continue
    trk.append((isvia,x1,y1,x2,y2,nn,lay))
print('deleted in KO/body:',ndel)

# ============ 3. move colliding parts inside BODY rect ============
moved=[]
for m in board.GetFootprints():
    if m.GetReference()=='U1': continue
    fp=m.GetPosition(); fx,fy=fp.x/MM,fp.y/MM
    bb=m.GetBoundingBox(False,False)
    cx=(bb.GetX()+bb.GetWidth()/2)/MM; cy=(bb.GetY()+bb.GetHeight()/2)/MM
    hw=bb.GetWidth()/2/MM; hh=bb.GetHeight()/2/MM
    if m.IsFlipped(): continue
    if cx+hw>BODY[0]+0.05 and cx-hw<BODY[2]-0.05 and cy+hh>BODY[1]+0.05 and cy-hh<BODY[3]-0.05:
        moved.append(m.GetReference())
        nx=BODY[0]-hw-0.3   # push west out of the body
        if m.GetReference()=='FID4':
            m.SetPosition(pcbnew.VECTOR2I(int(nx*MM),int(-8.0*MM)))
        else:
            m.SetPosition(pcbnew.VECTOR2I(int(nx*MM),int(fy*MM)))
        print('moved part',m.GetReference(),'->',round(nx,2),round(fy,2))
print('moved:',moved)

# ============ 4. swap U1 -> WROOM ============
u1=None
for m in board.GetFootprints():
    if m.GetReference()=='U1': u1=m
if u1: board.Remove(u1)
fplib=BASE+'vitalq.pretty'
fp=pcbnew.FootprintLoad(fplib,'ESP32_S3_WROOM_1')
fp.SetReference('U1'); fp.SetValue('ESP32-S3-WROOM-1-N16R8')
fp.SetPosition(pcbnew.VECTOR2I(int(NEWX*MM),int(NEWY*MM)))
fp.SetOrientationDegrees(0); fp.SetLocked(False)
PADNET = {'1':'GND','2':'+3V3_ESP','3':'ESP_EN','4':'LSM6_INT1','5':'TMP117_ALERT',
'6':'GAUGE_ALRT','7':'CHG_PG','8':'CS_AD5940','9':'CS_FLASH','10':'STATUS_LED',
'11':'CS_AFE4900','12':'I2C_SDA','13':'USB_DM_S3','14':'USB_DP_S3','15':'EXP_INT',
'16':None,'17':'I2C_SCL','18':'CS_ADS1292','19':'SPI_MOSI','20':'SPI_SCK','21':'SPI_MISO',
'22':'AD5940_GPIO0','23':'AFE4900_ADC_RDY','24':None,'25':None,'26':None,'27':'ESP_IO0',
'28':None,'29':None,'30':None,'31':'CS_MAX86178','32':'MAX86178_INT','33':'MIC_CLK',
'34':'MIC_DAT','35':'SPARE_IN','36':'ESP_RX','37':'ESP_TX','38':'ADS1292_DRDY',
'39':'FSR_ADC','40':'GND','41':'GND'}
by={}
for pd in fp.Pads(): by.setdefault(str(pd.GetNumber()),[]).append(pd)
for num,nn in PADNET.items():
    for pd in by.get(num,[]):
        pd.SetNet(board.FindNet(nn) if nn else None)
board.Add(fp)
print('U1 placed',NEWX,NEWY)

# ============ 5. keepout rule zone on all layers ============
zn=pcbnew.ZONE(board)
zn.SetIsRuleArea(True)
zn.SetDoNotAllowTracks(True); zn.SetDoNotAllowVias(True); zn.SetDoNotAllowPads(True)
zn.SetDoNotAllowZoneFills(True); zn.SetDoNotAllowFootprints(False)
zn.SetLayerSet(pcbnew.LSET(pcbnew.LSET.AllCuMask(8)))
zn.Outline().NewOutline()
zn.Outline().Append(int(KO[0]*MM),int(KO[1]*MM))
zn.Outline().Append(int(KO[2]*MM),int(KO[1]*MM))
zn.Outline().Append(int(KO[2]*MM),int(KO[3]*MM))
zn.Outline().Append(int(KO[0]*MM),int(KO[3]*MM))
board.Add(zn)
# cut inner pours: subtract KO rect from each inner-layer zone outline
rcut=0
for z in board.Zones():
    if z is zn: continue
    try:
        if z.GetIsRuleArea(): continue
        ol=z.Outline()
        if ol is None: continue
        # build KO polyset
        ko=pcbnew.SHAPE_POLY_SET(); ko.NewOutline()
        for px,py in [(KO[0],KO[1]),(KO[2],KO[1]),(KO[2],KO[3]),(KO[0],KO[3])]:
            ko.Append(int(px*MM),int(py*MM))
        if ol.Collide(ko):
            ol.BooleanSubtract(ko)
            z.SetOutline(ol); z.UnFill(); rcut+=1
    except Exception as e: print('zone cut err',e)
print('zones cut:',rcut)

# ============ 6. rasterize ============
occ={};vocc={}
def mark(mp,l,ix,iy,n): mp.setdefault((l,ix,iy),set()).add(n)
def ras_seg(x1,y1,x2,y2,net,layer,dil=DIL):
    L=math.hypot(x2-x1,y2-y1); n=max(1,int(L/GRID)+1)
    for i in range(n+1):
        x=x1+(x2-x1)*i/n; y=y1+(y2-y1)*i/n
        ix,iy=round(x/GRID),round(y/GRID)
        for dx in range(-dil,dil+1):
            for dy in range(-dil,dil+1): mark(occ,layer,ix+dx,iy+dy,net)
def ras_via(x,y,net):
    ix,iy=round(x/GRID),round(y/GRID)
    for dx in range(-VDIL,VDIL+1):
        for dy in range(-VDIL,VDIL+1):
            if dx*dx+dy*dy<=VDIL*VDIL:
                vocc.setdefault((ix+dx,iy+dy),set()).add(net)
                for l in range(16): mark(occ,l,ix+dx,iy+dy,net)
def ras_pad(x,y,sx,sy,net,layer=0):
    hx=sx/2+CLR; hy=sy/2+CLR
    for ix in range(int((x-hx)/GRID),int((x+hx)/GRID)+1):
        for iy in range(int((y-hy)/GRID),int((y+hy)/GRID)+1):
            cx,cy=ix*GRID,iy*GRID
            if abs(cx-x)<hx and abs(cy-y)<hy: mark(occ,layer,ix,iy,net)
    hx2=max(sx,sy)/2+VIA_R+0.18
    for ix in range(int((x-hx2)/GRID),int((x+hx2)/GRID)+1):
        for iy in range(int((y-hx2)/GRID),int((y+hx2)/GRID)+1):
            cx,cy=ix*GRID,iy*GRID
            if math.hypot(cx-x,cy-y)<hx2: vocc.setdefault((ix,iy),set()).add(net)
for isvia,x1,y1,x2,y2,nn,lay in trk:
    if isvia: ras_via(x1,y1,nn)
    else: ras_seg(x1,y1,x2,y2,nn,lay)
u1pads={};u1padcells={}
for m in board.GetFootprints():
    for pd in m.Pads():
        nn=pd.GetNetname()
        if nn in ('','Net-'): nn='__NC_%s_%s'%(m.GetReference(),str(pd.GetName()))
        b=pd.GetPosition(); bb=pd.GetBoundingBox()
        pl=2 if pd.IsFlipped() else 0
        ras_pad((bb.GetX()+bb.GetWidth()/2)/MM,(bb.GetY()+bb.GetHeight()/2)/MM,
                bb.GetWidth()/MM,bb.GetHeight()/MM,nn,pl)
        if m.GetReference()=='U1':
            u1pads[str(pd.GetName())]=(b.x/MM,b.y/MM,nn)
            u1cells=set()
            hx=bb.GetWidth()/2/MM+0.02; hy=bb.GetHeight()/2/MM+0.02
            bcx=(bb.GetX()+bb.GetWidth()/2)/MM; bcy=(bb.GetY()+bb.GetHeight()/2)/MM
            for ix in range(int((bcx-hx)/GRID),int((bcx+hx)/GRID)+1):
                for iy in range(int((bcy-hy)/GRID),int((bcy+hy)/GRID)+1): u1cells.add((ix,iy))
            u1padcells[str(pd.GetName())]=u1cells
# keepout cells
for ix in range(int(KO[0]/GRID),int(KO[2]/GRID)+1):
    for iy in range(int(KO[1]/GRID),int(KO[3]/GRID)+1):
        for l in range(16): mark(occ,l,ix,iy,'__KO__')
# zone fills on routing layers (same-net occupancy for goal cells)
def inside_poly(px,py,pts):
    c=False;j=len(pts)-1
    for i in range(len(pts)):
        xi,yi=pts[i];xj,yj=pts[j]
        if ((yi>py)!=(yj>py)) and (px<(xj-xi)*(py-yi)/(yj-yi)+xi): c=not c
        j=i
    return c
for z in board.Zones():
    if z.GetIsRuleArea(): continue
    znet=z.GetNetname()
    if not znet: continue
    for li in LAYERS:
        try:
            if not z.HasFilledPolysForLayer(li): continue
            fpz=z.GetFilledPolysList(li)
        except Exception: continue
        for pi in range(fpz.OutlineCount()):
            ol=fpz.COutline(pi)
            pts=[(ol.CPoint(i).x/MM,ol.CPoint(i).y/MM) for i in range(ol.PointCount())]
            if len(pts)<3: continue
            xs=[p[0] for p in pts];ys=[p[1] for p in pts]
            if max(xs)<RZ[0] or min(xs)>RZ[2] or max(ys)<RZ[1] or min(ys)>RZ[3]: continue
            x0=max(min(xs),RZ[0]-2);x1=min(max(xs),RZ[2]+2)
            y0=max(min(ys),RZ[1]-2);y1=min(max(ys),RZ[3]+2)
            for ix in range(int(x0/GRID),int(x1/GRID)+1):
                for iy in range(int(y0/GRID),int(y1/GRID)+1):
                    if inside_poly(ix*GRID,iy*GRID,pts): mark(occ,li,ix,iy,znet)
print('occ',len(occ))

def blk(ix,iy,net,layer):
    ns=occ.get((layer,ix,iy)); return bool(ns) and ns!={net}
VD2=6
def vblk(ix,iy,net):
    ns=vocc.get((ix,iy))
    if ns and ns!={net}: return True
    for l2 in range(16):
        for dx in range(-VD2,VD2+1):
            for dy in range(-VD2,VD2+1):
                if dx*dx+dy*dy>VD2*VD2: continue
                ns=occ.get((l2,ix+dx,iy+dy))
                if ns and ns!={net}: return True
    return False
def inrz(ix,iy):
    x,y=ix*GRID,iy*GRID
    return RZ[0]-0.1<=x<=RZ[2]+0.1 and RZ[1]-0.1<=y<=RZ[3]+0.1

def astar3(s0,goalcells,net,rad):
    si=(round(s0[0]/GRID),round(s0[1]/GRID),s0[2])
    openh=[(0.0,0,si)]; came={si:None}; ctr=0
    while openh:
        f,_,c=heapq.heappop(openh)
        if (c[0],c[1]) in goalcells:
            path=[c]
            while came[c]: c=came[c];path.append(c)
            return path[::-1]
        for dx,dy in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)):
            nc=(c[0]+dx,c[1]+dy,c[2])
            if nc in came: continue
            if not inrz(nc[0],nc[1]): continue
            if math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1])>rad: continue
            if blk(nc[0],nc[1],net,nc[2]): continue
            came[nc]=c; ctr+=1
            g=math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1])
            heapq.heappush(openh,(g+0.02,ctr,nc))
        for l2 in LAYERS:
            if l2==c[2]: continue
            if blk(c[0],c[1],net,l2) or vblk(c[0],c[1],net): continue
            nc=(c[0],c[1],l2)
            if nc in came: continue
            came[nc]=c; ctr+=1
            g=math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1])
            heapq.heappush(openh,(g+0.8,ctr,nc))
    return None

def add_seg(x1,y1,x2,y2,net,layer):
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(x1*MM),int(y1*MM)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*MM),int(y2*MM)))
    t.SetWidth(int((PWR_W if net in('+3V3_ESP','GND') else SIG_W)*MM))
    t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t);ras_seg(x1,y1,x2,y2,net,layer)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(x*MM),int(y*MM)))
    v.SetWidth(int(0.6*MM));v.SetDrill(int(0.3*MM));v.SetNet(board.FindNet(net))
    v.SetLayerPair(0,2);board.Add(v);ras_via(x,y,net)
def emit(path,net):
    # via at end cell if net's copper there is on a different layer
    ex,ey,el=path[-1]
    ls=[l for l in range(16) if net in occ.get((l,ex,ey),set())]
    if ls and el not in ls:
        if not vblk(ex,ey,net): add_via(ex*GRID,ey*GRID,net)
    for i in range(len(path)-1):
        if path[i][2]!=path[i+1][2]:
            add_via(path[i][0]*GRID,path[i][1]*GRID,net)
    runs=[[path[0]]]
    for p in path[1:]:
        if p[2]==runs[-1][-1][2]: runs[-1].append(p)
        else: runs.append([p])
    for r in runs:
        pts=[(c[0]*GRID,c[1]*GRID) for c in r]
        simp=[pts[0]]
        for i in range(1,len(pts)-1):
            ax,ay=pts[i-1];bx,by=pts[i];cx,cy=pts[i+1]
            if (bx-ax,by-ay)!=(cx-bx,cy-by): simp.append(pts[i])
        simp.append(pts[-1])
        for i in range(len(simp)-1):
            add_seg(simp[i][0],simp[i][1],simp[i+1][0],simp[i+1][1],net,r[0][2])

# ============ 7. build goals per net = cells of surviving copper inside RZ ============
allu1=set()
for cs in u1padcells.values(): allu1|=cs
net_goals=defaultdict(set)
for (l,ix,iy),ns in occ.items():
    if (ix,iy) in allu1: continue
    for n in ns:
        if n.startswith('__'): continue
        if inrz(ix,iy): net_goals[n].add((ix,iy))
# ============ 8. EPAD 3x3 via array (GND) ============
ex,ey=NEWX,NEWY+0.0
for dx in (-2.0,0.0,2.0):
    for dy in (-2.0,0.0,2.0):
        vx,vy=ex+dx,ey+dy
        ix,iy=round(vx/GRID),round(vy/GRID)
        if not vblk(ix,iy,'GND'): add_via(vx,vy,'GND')
print('epad vias done')
# ============ 9. route each U1 pad ============
ok=0; fails=[]
order=sorted(u1pads.items(), key=lambda kv: kv[1][0])  # left column first
for num,(px,py,nn) in order:
    if nn.startswith('__NC'): continue
    if num in ('41',): continue          # handled by EPAD array + pads40
    gs=net_goals.get(nn)
    if not gs: fails.append((num,nn,px,py,'no copper')); continue
    # exclude U1's own pad cells from goals? pad cell itself is a goal already
    # restrict goals: drop cells inside U1 pad area that belong only to other pads
    gl=list(gs)
    d=max(8.0, 2*math.hypot(px-(sum(g[0] for g in gl)/len(gl))*GRID, py-(sum(g[1] for g in gl)/len(gl))*GRID))
    p=astar3((px,py,0),set(gl),nn,min(d,18.0))
    if p: emit(p,nn); ok+=1
    else: fails.append((num,nn,px,py,'no path'))
print('routed pads',ok,'fails',len(fails))
for f in fails: print(' FAIL',f)
board.Save(DST)
print('saved',DST)
