import pcbnew, math, csv, sys
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
SRC=BASE+'_work/pre_promote_backup.kicad_pcb'
DST=BASE+'_work/base_clean.kicad_pcb'
MM=1e6
TOP,L3,L4,L6,BOT=0,6,8,12,2
SIG_W,PWR_W=0.127,0.3
VIA_D,VIA_DR=0.4,0.2
NEWX,NEWY=11.5,-6.5
KO=(-2.5,-16.55,25.5,-15.25)
# pad copper spans x2.0..21.0 y-15.21..3.75 ; +0.5 => R3
R3=(1.5,-15.71,21.5,4.25)
def inr(x,y,r): return r[0]<=x<=r[2] and r[1]<=y<=r[3]
def drect(px,py,r):
    dx=max(r[0]-px,0,px-r[2]);dy=max(r[1]-py,0,py-r[3]);return math.hypot(dx,dy)
def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy
    if L2==0:return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

board=pcbnew.LoadBoard(SRC)
# snapshot ALL original geometry (tuples) before mutation
ORIG=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();ORIG.append({'o':t,'via':1,'x1':p.x/MM,'y1':p.y/MM,'x2':p.x/MM,'y2':p.y/MM,'net':t.GetNetname(),'lay':-1,'w':t.GetWidth()/MM})
    else:
        s,e=t.GetStart(),t.GetEnd()
        ORIG.append({'o':t,'via':0,'x1':s.x/MM,'y1':s.y/MM,'x2':e.x/MM,'y2':e.y/MM,'net':t.GetNetname(),'lay':t.GetLayer(),'w':t.GetWidth()/MM})

def add_seg(x1,y1,x2,y2,net,layer,w=SIG_W):
    if abs(x2-x1)<1e-9 and abs(y2-y1)<1e-9: return None
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(round(x1*MM)),int(round(y1*MM))))
    t.SetEnd(pcbnew.VECTOR2I(int(round(x2*MM)),int(round(y2*MM))))
    t.SetWidth(int(w*MM));t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t);return t
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(round(x*MM)),int(round(y*MM))))
    v.SetWidth(int(VIA_D*MM));v.SetDrill(int(VIA_DR*MM));v.SetNet(board.FindNet(net))
    v.SetLayerPair(0,2);board.Add(v);return v
def edge_seg(x1,y1,x2,y2):
    sh=pcbnew.PCB_SHAPE(board);sh.SetShape(pcbnew.SHAPE_T_SEGMENT);sh.SetLayer(pcbnew.Edge_Cuts)
    sh.SetStart(pcbnew.VECTOR2I(int(x1*MM),int(y1*MM)));sh.SetEnd(pcbnew.VECTOR2I(int(x2*MM),int(y2*MM)))
    sh.SetWidth(int(0.05*MM));board.Add(sh)

# ---------- outline tab ----------
OY=-10.5
del_d=[]
for d in board.GetDrawings():
    if d.GetLayerName()!='Edge.Cuts':continue
    s,e=d.GetStart(),d.GetEnd();x1,y1,x2,y2=s.x/MM,s.y/MM,e.x/MM,e.y/MM
    if abs(y1-OY)<0.01 and abs(y2-OY)<0.01 and min(x1,x2)<22.5: del_d.append(d)
    elif abs(x1+2.0)<0.01 and abs(x2+2.0)<0.01 and max(y1,y2)>OY-0.01: del_d.append(d)
for d in del_d: board.Remove(d)
CR=1.0
def arc(cx,cy,a0):
    pts=[(cx+CR*math.cos(math.radians(a0+i*30)),cy+CR*math.sin(math.radians(a0+i*30))) for i in range(4)]
    for i in range(3): edge_seg(*pts[i],*pts[i+1])
edge_seg(-2.0,-10.5,-2.05,-10.5);edge_seg(-2.0,-10.5,-2.0,64.5)
edge_seg(-2.05,-10.5,-2.05,-15.55);arc(-1.05,-15.55,180)
edge_seg(-1.05,-16.55,21.5,-16.55);arc(21.5,-15.55,270)
edge_seg(22.5,-15.55,22.5,-10.5);edge_seg(22.5,-10.5,48.0,-10.5)

# ---------- keepout zone ----------
zn=pcbnew.ZONE(board);zn.SetIsRuleArea(True);zn.SetZoneName('antenna_keepout')
zn.SetDoNotAllowTracks(True);zn.SetDoNotAllowVias(True);zn.SetDoNotAllowPads(True)
zn.SetDoNotAllowZoneFills(True);zn.SetDoNotAllowFootprints(False)
zn.SetLayerSet(pcbnew.LSET(pcbnew.LSET.AllCuMask(8)))
zn.Outline().NewOutline()
for px,py in [(KO[0],KO[1]),(KO[2],KO[1]),(KO[2],KO[3]),(KO[0],KO[3])]: zn.Outline().Append(int(px*MM),int(py*MM))
board.Add(zn)

# ---------- footprint moves ----------
mv={'C7':(0.6,-14.0,90),'C8':(0.6,-12.1,90),'C64':(1.6,-11.3,90),'FID4':(-0.4,-10.3,None)}
for m in board.GetFootprints():
    r=m.GetReference()
    if r in mv:
        x,y,rot=mv[r];m.SetPosition(pcbnew.VECTOR2I(int(x*MM),int(y*MM)))
        if rot is not None: m.SetOrientationDegrees(rot)
    if r=='U1': board.Remove(m)

fp=pcbnew.FootprintLoad(BASE+'vitalq.pretty','ESP32_S3_WROOM_1')
fp.SetReference('U1');fp.SetValue('ESP32-S3-WROOM-1-N16R8')
fp.SetPosition(pcbnew.VECTOR2I(int(NEWX*MM),int(NEWY*MM)));fp.SetOrientationDegrees(0)
PADNET={'1':'GND','2':'+3V3_ESP','3':'ESP_EN','4':'LSM6_INT1','5':'TMP117_ALERT','6':'GAUGE_ALRT','7':'CHG_PG','8':'CS_AD5940','9':'CS_FLASH','10':'STATUS_LED','11':'CS_AFE4900','12':'I2C_SDA','13':'USB_DM_S3','14':'USB_DP_S3','15':'EXP_INT','16':None,'17':'I2C_SCL','18':'CS_ADS1292','19':'SPI_MOSI','20':'SPI_SCK','21':'SPI_MISO','22':'AD5940_GPIO0','23':'AFE4900_ADC_RDY','24':None,'25':None,'26':None,'27':'ESP_IO0','28':None,'29':None,'30':None,'31':'CS_MAX86178','32':'MAX86178_INT','33':'MIC_CLK','34':'MIC_DAT','35':'SPARE_IN','36':'ESP_RX','37':'ESP_TX','38':'ADS1292_DRDY','39':'FSR_ADC','40':'GND','41':'GND'}
byp={}
for pd in fp.Pads(): byp.setdefault(str(pd.GetNumber()),[]).append(pd)
for num,nn in PADNET.items():
    for pd in byp.get(num,[]): pd.SetNet(board.FindNet(nn) if nn else None)
board.Add(fp)
padbb={};padc={}
for pd in fp.Pads():
    bb=pd.GetBoundingBox();n=str(pd.GetName())
    r=(bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM)
    p=pd.GetPosition()
    if n in padbb:
        o=padbb[n];padbb[n]=(min(o[0],r[0]),min(o[1],r[1]),max(o[2],r[2]),max(o[3],r[3]))
    else: padbb[n]=r;padc[n]=(p.x/MM,p.y/MM)
PADRECTS=[v for v in padbb.values() if True]
print('base done')

# ---------- U1-net set ----------
U1NETS=set(n for n in PADNET.values() if n)
BLOCK={'USB_DP','USB_DM','IN1P_AC','ECG_P'}
PADSET=set(PADNET.values())-{None}

# ---------- per-net item graph over ORIG ----------
# nodes: index into ORIG ; also component pads (as anchor nodes, not deletable)
items=ORIG
# connection points: endpoint of seg / via pos ; plus comp pads as 'padnodes'
from collections import defaultdict
pt2items=defaultdict(set)
for i,it in enumerate(items):
    if it['via']: pt2items[(round(it['x1'],3),round(it['y1'],3))].add(i)
    else:
        pt2items[(round(it['x1'],3),round(it['y1'],3))].add(i)
        pt2items[(round(it['x2'],3),round(it['y2'],3))].add(i)
# component pads: virtual items (never deleted)
paditems=[]   # (net,x,y,x2,y2)
for m in board.GetFootprints():
    if m.GetReference()=='U1': continue
    for pd in m.Pads():
        nn=pd.GetNetname()
        if not nn: continue
        bb=pd.GetBoundingBox();p=pd.GetPosition()
        paditems.append((nn,p.x/MM,p.y/MM,bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM))
# T-junctions: endpoint ON another seg's interior (same net) - per-net pair scan
tj=defaultdict(set)  # point -> set of item idx
pernet=defaultdict(list)
for i,it in enumerate(items):
    if not it['via']: pernet[it['net']].append(i)
for net,idxs in pernet.items():
    for i in idxs:
        it=items[i]
        for ends in (('x1','y1'),('x2','y2')):
            px,py=it[ends[0]],it[ends[1]]
            for j in idxs:
                if j==i:continue
                jt=items[j]
                if jt['lay']!=it['lay']:continue
                if segdist(px,py,jt['x1'],jt['y1'],jt['x2'],jt['y2'])<0.03 and not (abs(px-jt['x1'])<0.03 and abs(py-jt['y1'])<0.03) and not (abs(px-jt['x2'])<0.03 and abs(py-jt['y2'])<0.03):
                    tj[(round(px,3),round(py,3))].add(j)

def is_anchor_pt(net,x,y):
    """point outside R3 that is via/pad/T-junction"""
    if inr(x,y,R3): return False
    k=(round(x,3),round(y,3))
    inds=pt2items.get(k,set())
    for i in inds:
        if items[i]['net']!=net: continue
        if items[i]['via']: return True
    for pi in paditems:
        if pi[0]==net and pi[3]-0.05<=x<=pi[5]+0.05 and pi[4]-0.05<=y<=pi[6]+0.05: return True
    if len(inds)>=3: return True
    if k in tj: return True
    return False

# ---------- 2a reroute: delete whole segs of blocker nets inside R3, add vias at the boundary crossings ----------
REROUTE={'USB_DP':(L3,[(22.9,-6.09),(21.26,4.4)]),
         'USB_DM':(L3,[(22.1,-6.32),(21.02,5.0)]),
         'IN1P_AC':(L4,[(21.6,0.52),(21.6,-2.05)]),
         'ECG_P':(L4,[(21.6,-7.10),(15.72,4.4)])}
delset=set()
def insideA(it):
    if it['via']: return inr(it['x1'],it['y1'],R3)
    mx,my=(it['x1']+it['x2'])/2,(it['y1']+it['y2'])/2
    return inr(mx,my,R3) or inr(it['x1'],it['y1'],R3) or inr(it['x2'],it['y2'],R3)
for i,it in enumerate(items):
    if it['net'] in BLOCK and it['lay']==TOP and insideA(it): delset.add(i)

# ---------- B. U1 nets: delete whole segs within 0.3mm of pads / courtyard-Top ----------
# then walk back dead-end tips still inside R3 until junction leaves R3. Anchors = boundary junctions.
anchors=defaultdict(list)
R2=[(r[0]-0.3,r[1]-0.3,r[2]+0.3,r[3]+0.3) for r in PADRECTS]
CTY=(1.5,-16.0,21.5,4.25)   # U1 courtyard interior approx
def seg_touches_rects(it,rects):
    if it['via']: return min(drect(it['x1'],it['y1'],r) for r in rects)<0.0
    for r in rects:
        # any endpoint/midpoint inside, or min dist from rect corners to seg <0
        if inr(it['x1'],it['y1'],r) or inr(it['x2'],it['y2'],r) or inr((it['x1']+it['x2'])/2,(it['y1']+it['y2'])/2,r): return True
        # distance between seg and rect: check rect corner-to-seg and seg-endpoint-to-rect
        for cx in (r[0],r[2]):
            for cy in (r[1],r[3]):
                if segdist(cx,cy,it['x1'],it['y1'],it['x2'],it['y2'])<1e-6: pass
        if drect(it['x1'],it['y1'],r)<0.0: return True
    return False
def dist2rects(it,rects):
    m=1e9
    pts=[(it['x1'],it['y1']),(it['x2'],it['y2']),((it['x1']+it['x2'])/2,(it['y1']+it['y2'])/2)]
    for r in rects:
        for px,py in pts:
            m=min(m,drect(px,py,r))
        for cx in (r[0],r[2]):
            for cy in (r[1],r[3]):
                m=min(m,segdist(cx,cy,it['x1'],it['y1'],it['x2'],it['y2']))
    return m
# initial deletion: U1-net Top segs/vias within 0.3mm of pad rects or inside courtyard
for i,it in enumerate(items):
    if it['net'] not in U1NETS: continue
    if it['lay']!=TOP: continue
    if dist2rects(it,R2)<0.3 if not it['via'] else min(drect(it['x1'],it['y1'],r) for r in R2)<0.3:
        delset.add(i);continue
    if inr((it['x1']+it['x2'])/2,(it['y1']+it['y2'])/2,CTY) or (not it['via'] and inr(it['x1'],it['y1'],CTY) and inr(it['x2'],it['y2'],CTY)):
        delset.add(i)
# vias of U1 nets inside R3
for i,it in enumerate(items):
    if it['net'] in U1NETS and it['via'] and inr(it['x1'],it['y1'],R3): delset.add(i)

def degree_at(net,x,y,alive):
    k=(round(x,3),round(y,3))
    n=0
    for j in pt2items.get(k,()):
        if items[j]['net']==net and j in alive: n+=1
    return n,k
padpts=defaultdict(set)
for pi in paditems: padpts[pi[0]].add((round(pi[1],3),round(pi[2],3)))
def near_pad(net,x,y):
    for pi in paditems:
        if pi[0]==net and pi[3]-0.05<=x<=pi[5]+0.05 and pi[4]-0.05<=y<=pi[6]+0.05: return True
    return False

# walk-back: iteratively delete survivors with a dead-end tip inside R3
alive={i for i in range(len(items))}-delset
changed=True
while changed:
    changed=False
    for i in list(alive):
        it=items[i]
        if it['net'] not in U1NETS or it['via']: continue
        deadend=None
        for ex,ey in ((it['x1'],it['y1']),(it['x2'],it['y2'])):
            if not inr(ex,ey,R3): continue
            deg,k=degree_at(it['net'],ex,ey,alive)
            if k in tj or near_pad(it['net'],ex,ey): continue
            if deg==1: deadend=(ex,ey);break
        if deadend:
            delset.add(i);alive.discard(i);changed=True
print('after walkback delset',len(delset))
# anchors: degree-1 endpoints within 1mm of R3 + vias within 6mm + pads near
for i in alive:
    it=items[i]
    if it['net'] not in U1NETS: continue
    if it['via']:
        if drect(it['x1'],it['y1'],R3)<6:
            anchors[it['net']].append((it['x1'],it['y1'],-2))
        continue
    for ex,ey in ((it['x1'],it['y1']),(it['x2'],it['y2'])):
        deg,k=degree_at(it['net'],ex,ey,alive)
        if deg==1 and drect(ex,ey,R3)<1.0:
            anchors[it['net']].append((ex,ey,it['lay']))
# junction anchors: endpoints of deleted items where a survivor still sits (outside-or-boundary)
for i in delset:
    it=items[i]
    if it['net'] not in U1NETS: continue
    for ex,ey in ((it['x1'],it['y1']),(it['x2'],it['y2'])):
        k=(round(ex,3),round(ey,3))
        for j in pt2items.get(k,()):
            if j in alive and items[j]['net']==it['net']:
                anchors[it['net']].append((ex,ey,items[j]['lay']));break
print('delset',len(delset),'anchors:',{n:len(l) for n,l in anchors.items()})

# ---------- per-net islands over survivors ----------
def net_islands(net):
    ids=[i for i in alive if items[i]['net']==net]
    par={i:i for i in ids}
    def find(a):
        while par[a]!=a: par[a]=par[par[a]];a=par[a]
        return a
    def uni(a,b):
        ra,rb=find(a),find(b)
        if ra!=rb:par[ra]=rb
    ptm=defaultdict(list)
    for i in ids:
        it=items[i]
        if it['via']:continue
        ptm[(round(it['x1'],3),round(it['y1'],3),it['lay'])].append(i)
        ptm[(round(it['x2'],3),round(it['y2'],3),it['lay'])].append(i)
    for k,v in ptm.items():
        for j in v[1:]: uni(v[0],j)
    for vi in ids:
        if not items[vi]['via']:continue
        vx,vy=items[vi]['x1'],items[vi]['y1']
        for j in ids:
            if j==vi or items[j]['via']:continue
            jt=items[j]
            for x,y in ((jt['x1'],jt['y1']),(jt['x2'],jt['y2'])):
                if math.hypot(x-vx,y-vy)<0.06: uni(vi,j)
    # component pads bridge tracks ending inside their bbox
    for pi in paditems:
        if pi[0]!=net: continue
        hit=[]
        for j in ids:
            jt=items[j]
            if jt['via']:continue
            for x,y in ((jt['x1'],jt['y1']),(jt['x2'],jt['y2'])):
                if pi[3]-0.05<=x<=pi[5]+0.05 and pi[4]-0.05<=y<=pi[6]+0.05:
                    hit.append(j);break
        for j in hit[1:]: uni(hit[0],j)
    comp=defaultdict(list)
    for i in ids: comp[find(i)].append(i)
    return comp

# delete dead fragments: U1-net survivor islands fully inside R3 (dead copper under module)
anchors=defaultdict(list)
for net in U1NETS:
    if net=='GND': continue
    comp=net_islands(net)
    if not comp: continue
    big=max(comp.values(),key=len)
    for k,v in comp.items():
        if v is big: continue
        if all(inr(items[i]['x1'],items[i]['y1'],R3) and inr(items[i]['x2'],items[i]['y2'],R3) for i in v):
            for i in v: delset.add(i);alive.discard(i)
    # anchors = degree-1 endpoints + vias of the biggest island
    idset=set(big)
    deg=defaultdict(int)
    for i in big:
        it=items[i]
        if it['via']:continue
        deg[(round(it['x1'],3),round(it['y1'],3),it['lay'])]+=1
        deg[(round(it['x2'],3),round(it['y2'],3),it['lay'])]+=1
    for i in big:
        it=items[i]
        if it['via']:
            anchors[net].append((it['x1'],it['y1'],-2));continue
        for ex,ey in ((it['x1'],it['y1']),(it['x2'],it['y2'])):
            if deg[(round(ex,3),round(ey,3),it['lay'])]==1:
                anchors[net].append((ex,ey,it['lay']))
print('island anchors:',{n:len(l) for n,l in anchors.items()})

# ---------- C. GND: delete ALL GND Top segs inside R3 ----------
for i,it in enumerate(items):
    if it['net']=='GND' and not it['via'] and it['lay']==TOP and insideA(it): delset.add(i)
# GND vias inside R3 also deleted (planes reconnect)
for i,it in enumerate(items):
    if it['net']=='GND' and it['via'] and inr(it['x1'],it['y1'],R3): delset.add(i)

# KO: any track/via inside keepout on any layer
for i,it in enumerate(items):
    if it['via']:
        if inr(it['x1'],it['y1'],KO): delset.add(i)
    elif inr(it['x1'],it['y1'],KO) or inr(it['x2'],it['y2'],KO) or inr((it['x1']+it['x2'])/2,(it['y1']+it['y2'])/2,KO): delset.add(i)

for i in delset: board.Remove(items[i]['o'])
print('deleted',len(delset))

# ---------- reroute inner links for blocker nets ----------
for nn,(lay,vs) in REROUTE.items():
    for vx,vy in vs: add_via(vx,vy,nn)
    add_seg(vs[0][0],vs[0][1],vs[1][0],vs[1][1],nn,lay,SIG_W)

# ---------- anchors.csv ----------
with open(BASE+'_work/anchors.csv','w',newline='') as f:
    w=csv.writer(f);w.writerow(['net','x','y','layer'])
    for net,lst in anchors.items():
        for x,y,l in lst: w.writerow([net,round(x,4),round(y,4),l])
# ---------- stale keepout + tab pours + FID4 + blocker bridges ----------
for z in list(board.Zones()):
    if z.GetZoneName()=='antenna_keepout':
        o=z.Outline().Outline(0)
        ys=[o.CPoint(i).y/MM for i in range(o.PointCount())]
        if min(ys)>-15.0: board.Remove(z)
srcgnd=None
for z in board.Zones():
    if not z.GetIsRuleArea() and z.GetNetname()=='GND': srcgnd=z;break
for lay in ('Ground Layer 2','Ground Layer 7'):
    zn=pcbnew.ZONE(board);zn.SetNet(board.FindNet('GND'))
    ls=pcbnew.LSET();ls.addLayer(board.GetLayerID(lay));zn.SetLayerSet(ls)
    zn.Outline().NewOutline()
    for px,py in [(-1.9,-16.4),(22.3,-16.4),(22.3,-9.8),(-1.9,-9.8)]:
        zn.Outline().Append(int(px*MM),int(py*MM))
    if srcgnd:
        zn.SetFillMode(srcgnd.GetFillMode());zn.SetThermalReliefGap(srcgnd.GetThermalReliefGap())
        zn.SetThermalReliefSpokeWidth(srcgnd.GetThermalReliefSpokeWidth());zn.SetPadConnection(srcgnd.GetPadConnection())
        zn.SetLocalClearance(srcgnd.GetLocalClearance());zn.SetMinThickness(srcgnd.GetMinThickness())
    board.Add(zn)
for m in board.GetFootprints():
    if m.GetReference()=='FID4': m.SetPosition(pcbnew.VECTOR2I(int(-0.4*MM),int(-10.3*MM)))
for net,links in {'USB_DM':[(22.1,-6.32,23.841,-6.321),(21.02,5.0,21.022,6.433),(22.9173,13.5533,22.0182,12.8378)],
                  'USB_DP':[(22.9,-6.09,23.793,-6.086),(21.26,4.4,21.258,6.482),(10.4425,12.7799,10.5967,13.3272)],
                  'IN1P_AC':[(21.6,0.52,21.515,0.531),(21.6,-2.05,21.747,-2.053)],
                  'ECG_P':[(21.6,-7.1,21.951,-7.102),(15.72,4.4,15.716,4.893)]}.items():
    for a,b_,c,d in links: add_seg(a,b_,c,d,net,TOP,SIG_W)

# ==================== PHASE B: fragment cleanup ====================
RZ=(-2.05,-16.55,23.5,9.0)          # rework zone
R4=(1.7,-15.51,21.3,4.05)           # U1 pad copper bbox + 0.3
def seg_clip_tinterval(x1,y1,x2,y2,r):
    dx,dy=x2-x1,y2-y1
    t0,t1=0.0,1.0
    for p,q in ((-dx,x1-r[0]),(dx,r[2]-x1),(-dy,y1-r[1]),(dy,r[3]-y1)):
        if abs(p)<1e-12:
            if q<0: return None
        else:
            t=q/p
            if p<0:
                if t>t0: t0=t
            else:
                if t<t1: t1=t
            if t0>t1: return None
    return (t0,t1)

def current():
    out=[]
    for t in board.GetTracks():
        if isinstance(t,pcbnew.PCB_VIA):
            p=t.GetPosition();out.append((t,1,p.x/MM,p.y/MM,p.x/MM,p.y/MM,t.GetNetname(),-1))
        else:
            s,e=t.GetStart(),t.GetEnd()
            out.append((t,0,s.x/MM,s.y/MM,e.x/MM,e.y/MM,t.GetNetname(),t.GetLayer()))
    return out

CUR=current()
# B1: zero-length segments (U1 nets, inside RZ)
ops_del=[];ops_edit=[];ops_add=[]
for t,v,x1,y1,x2,y2,net,lay in CUR:
    if v: continue
    if net in U1NETS and math.hypot(x2-x1,y2-y1)<0.001 and inr(x1,y1,RZ):
        ops_del.append(t)
print('B1 zero-len queued',len(ops_del))

# B3: U1-net segs inside R4 -> cut at boundary / delete / split
n3d=0
for t,v,x1,y1,x2,y2,net,lay in CUR:
    if t in ops_del: continue
    if v:
        if net in U1NETS and inr(x1,y1,R4): ops_del.append(t)
        continue
    if net not in U1NETS: continue
    iv=seg_clip_tinterval(x1,y1,x2,y2,R4)
    if iv is None: continue
    ta,tb=iv
    if ta<=0 and tb>=1:
        ops_del.append(t);n3d+=1
    elif ta<=0:
        nx,ny=x1+tb*(x2-x1),y1+tb*(y2-y1)
        ops_edit.append(('s',t,nx,ny));n3d+=1
    elif tb>=1:
        nx,ny=x1+ta*(x2-x1),y1+ta*(y2-y1)
        ops_edit.append(('e',t,nx,ny));n3d+=1
    else:
        ax,ay=x1+ta*(x2-x1),y1+ta*(y2-y1);bx,by=x1+tb*(x2-x1),y1+tb*(y2-y1)
        ops_edit.append(('e',t,ax,ay))
        ops_add.append((bx,by,x2,y2,net,lay));n3d+=1
print('B3 R4 ops',n3d,'adds',len(ops_add))
for t in ops_del: board.Remove(t)
for k,t,nx,ny in ops_edit:
    if k=='s': t.SetStart(pcbnew.VECTOR2I(int(round(nx*MM)),int(round(ny*MM))))
    else: t.SetEnd(pcbnew.VECTOR2I(int(round(nx*MM)),int(round(ny*MM))))
for bx,by,x2,y2,net,lay in ops_add: add_seg(bx,by,x2,y2,net,lay)
del CUR,ops_del,ops_edit,ops_add
print('B applied')

# B2: padless dead islands of U1 nets fully inside RZ
PADL=[]
for m in board.GetFootprints():
    for pd in m.Pads():
        if not pd.GetNetname(): continue
        bb=pd.GetBoundingBox()
        PADL.append((pd.GetNetname(),bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM))
def island_compute(net):
    items=[it for it in current() if it[6]==net]
    ids=list(range(len(items)))
    par={i:i for i in ids}
    def f(a):
        while par[a]!=a: par[a]=par[par[a]];a=par[a]
        return a
    def u(a,b):
        ra,rb=f(a),f(b)
        if ra!=rb: par[ra]=rb
    pt=defaultdict(list)
    for i,(t,v,x1,y1,x2,y2,n,l) in enumerate(items):
        if v: continue
        pt[(round(x1,3),round(y1,3),l)].append(i);pt[(round(x2,3),round(y2,3),l)].append(i)
    for k,vs in pt.items():
        for j in vs[1:]: u(vs[0],j)
    for i,(t,v,x1,y1,x2,y2,n,l) in enumerate(items):
        if not v: continue
        for j,(t2,v2,a1,b1,a2,b2,n2,l2) in enumerate(items):
            if v2: continue
            for x,y in ((a1,b1),(a2,b2)):
                if math.hypot(x-x1,y-y1)<0.06: u(i,j)
    for pn,px1,py1,px2,py2 in PADL:
        if pn!=net: continue
        hit=[]
        for i,(t,v,x1,y1,x2,y2,n,l) in enumerate(items):
            if v: continue
            for x,y in ((x1,y1),(x2,y2)):
                if px1-0.05<=x<=px2+0.05 and py1-0.05<=y<=py2+0.05: hit.append(i);break
        for j in hit[1:]: u(hit[0],j)
    comp=defaultdict(list)
    for i in ids: comp[f(i)].append(i)
    return comp,items
def island_has_pad(mem,items,net):
    for pn,px1,py1,px2,py2 in PADL:
        if pn!=net: continue
        for i in mem:
            t,v,x1,y1,x2,y2,n,l=items[i]
            if v: continue
            for x,y in ((x1,y1),(x2,y2)):
                if px1-0.05<=x<=px2+0.05 and py1-0.05<=y<=py2+0.05: return True
    return False
dead=[]
for net in U1NETS:
    comp,items=island_compute(net)
    for k,mem in comp.items():
        if island_has_pad(mem,items,net): continue
        xs=[];ys=[]
        for i in mem:
            t,v,x1,y1,x2,y2,n,l=items[i];xs+=[x1,x2];ys+=[y1,y2]
        if inr(min(xs),min(ys),RZ) and inr(max(xs),max(ys),RZ):
            for i in mem: dead.append(items[i][0])
for t in dead: board.Remove(t)
print('B2 dead islands deleted',len(dead))

# ==================== PHASE C: hand routes ====================
add_seg(2.75,-14.76,4.3,-14.76,'GND',TOP,0.3);add_via(4.3,-14.76,'GND')
add_seg(20.25,-14.76,18.4,-14.76,'GND',TOP,0.3);add_via(18.4,-14.76,'GND')
for gx in (8.6,10.0,11.4):
    for gy in (-8.44,-7.04,-5.64): add_via(gx,gy,'GND')
def top_route(pts,net,w=SIG_W):
    for i in range(len(pts)-1): add_seg(pts[i][0],pts[i][1],pts[i+1][0],pts[i+1][1],net,TOP,w)
top_route([(2.75,0.48),(3.05,0.78),(3.05,7.55),(4.1773,7.7636)],'USB_DM_S3')
top_route([(2.75,1.75),(2.75,8.55),(4.5791,8.0729)],'USB_DP_S3')
top_route([(0.6,-13.225),(2.75,-13.49)],'+3V3_ESP',0.3)
top_route([(0.6,-14.775),(2.75,-14.76)],'GND',0.3)
top_route([(0.6,-11.62),(1.4,-13.49),(2.75,-13.49)],'+3V3_ESP',0.3)
top_route([(0.6,-12.58),(0.0,-12.6),(0.0,-14.85),(2.75,-14.85)],'GND',0.3)
top_route([(1.6,-10.82),(2.75,-12.22)],'ESP_EN',0.3)
top_route([(1.6,-11.78),(1.6,-12.9)],'GND',0.3);add_via(1.6,-12.9,'GND')
print('C hand routes done')

pcbnew.ZONE_FILLER(board).Fill(board.Zones())
board.Save(DST)
print('SAVED',DST)
