import pcbnew, math
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
SRC=BASE+'_work/pre_promote_backup.kicad_pcb'
DST=BASE+'vitalq_v2.kicad_pcb'
MM=1e6
TOP,L3,L4,L6,BOT=0,6,8,12,2
SIG_W,PWR_W=0.127,0.3
VIA_D,VIA_DR=0.4,0.2
NEWX,NEWY=11.5,-6.5
KO=(-2.5,-16.55,25.5,-15.25)
A=(1.5,-15.71,21.5,4.25)          # U1 pad copper +0.5mm

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy
    if L2==0:return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)
def inrect(x,y,r):return r[0]<=x<=r[2] and r[1]<=y<=r[3]

board=pcbnew.LoadBoard(SRC)
TRK0=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();TRK0.append((t,True,p.x/MM,p.y/MM,p.x/MM,p.y/MM,t.GetNetname(),-1))
    else:
        s0,e0=t.GetStart(),t.GetEnd()
        TRK0.append((t,False,s0.x/MM,s0.y/MM,e0.x/MM,e0.y/MM,t.GetNetname(),t.GetLayer()))

def add_seg(x1,y1,x2,y2,net,layer,w=SIG_W):
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(x1*MM),int(y1*MM)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*MM),int(y2*MM)))
    t.SetWidth(int(w*MM));t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(x*MM),int(y*MM)))
    v.SetWidth(int(VIA_D*MM));v.SetDrill(int(VIA_DR*MM));v.SetNet(board.FindNet(net))
    v.SetLayerPair(0,2);board.Add(v)
def edge_seg(x1,y1,x2,y2):
    sh=pcbnew.PCB_SHAPE(board);sh.SetShape(pcbnew.SHAPE_T_SEGMENT);sh.SetLayer(pcbnew.Edge_Cuts)
    sh.SetStart(pcbnew.VECTOR2I(int(x1*MM),int(y1*MM)));sh.SetEnd(pcbnew.VECTOR2I(int(x2*MM),int(y2*MM)))
    sh.SetWidth(int(0.05*MM));board.Add(sh)

# ===== 1. outline tab =====
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
    import math as m
    pts=[(cx+CR*m.cos(m.radians(a0+i*30)),cy+CR*m.sin(m.radians(a0+i*30))) for i in range(4)]
    for i in range(3): edge_seg(*pts[i],*pts[i+1])
edge_seg(-2.0,-10.5,-2.05,-10.5);edge_seg(-2.0,-10.5,-2.0,64.5)
edge_seg(-2.05,-10.5,-2.05,-15.55);arc(-1.05,-15.55,180)
edge_seg(-1.05,-16.55,21.5,-16.55);arc(21.5,-15.55,270)
edge_seg(22.5,-15.55,22.5,-10.5);edge_seg(22.5,-10.5,48.0,-10.5)

# ===== 2. keepout zone =====
zn=pcbnew.ZONE(board);zn.SetIsRuleArea(True)
zn.SetDoNotAllowTracks(True);zn.SetDoNotAllowVias(True);zn.SetDoNotAllowPads(True)
zn.SetDoNotAllowZoneFills(True);zn.SetDoNotAllowFootprints(False)
zn.SetLayerSet(pcbnew.LSET(pcbnew.LSET.AllCuMask(8)))
zn.Outline().NewOutline()
for px,py in [(KO[0],KO[1]),(KO[2],KO[1]),(KO[2],KO[3]),(KO[0],KO[3])]: zn.Outline().Append(int(px*MM),int(py*MM))
board.Add(zn)

# ===== 3. move caps into tab + FID4 =====
mv={'C7':(0.6,-14.0,90),'C8':(0.6,-12.6,90),'C64':(0.6,-11.3,90),'FID4':(0.8,-8.0,None)}
for m in board.GetFootprints():
    r=m.GetReference()
    if r in mv:
        x,y,rot=mv[r];m.SetPosition(pcbnew.VECTOR2I(int(x*MM),int(y*MM)))
        if rot is not None: m.SetOrientationDegrees(rot)
        print('moved',r,'->',x,y)

# ===== 4. U1 swap =====
for m in board.GetFootprints():
    if m.GetReference()=='U1': board.Remove(m)
fp=pcbnew.FootprintLoad(BASE+'vitalq.pretty','ESP32_S3_WROOM_1')
fp.SetReference('U1');fp.SetValue('ESP32-S3-WROOM-1-N16R8')
fp.SetPosition(pcbnew.VECTOR2I(int(NEWX*MM),int(NEWY*MM)));fp.SetOrientationDegrees(0)
PADNET={'1':'GND','2':'+3V3_ESP','3':'ESP_EN','4':'LSM6_INT1','5':'TMP117_ALERT','6':'GAUGE_ALRT','7':'CHG_PG','8':'CS_AD5940','9':'CS_FLASH','10':'STATUS_LED','11':'CS_AFE4900','12':'I2C_SDA','13':'USB_DM_S3','14':'USB_DP_S3','15':'EXP_INT','16':None,'17':'I2C_SCL','18':'CS_ADS1292','19':'SPI_MOSI','20':'SPI_SCK','21':'SPI_MISO','22':'AD5940_GPIO0','23':'AFE4900_ADC_RDY','24':None,'25':None,'26':None,'27':'ESP_IO0','28':None,'29':None,'30':None,'31':'CS_MAX86178','32':'MAX86178_INT','33':'MIC_CLK','34':'MIC_DAT','35':'SPARE_IN','36':'ESP_RX','37':'ESP_TX','38':'ADS1292_DRDY','39':'FSR_ADC','40':'GND','41':'GND'}
byp={}
for pd in fp.Pads(): byp.setdefault(str(pd.GetNumber()),[]).append(pd)
for num,nn in PADNET.items():
    for pd in byp.get(num,[]): pd.SetNet(board.FindNet(nn) if nn else None)
board.Add(fp)
# pad bboxes (abs)
padbb={}
for pd in fp.Pads():
    bb=pd.GetBoundingBox();n=str(pd.GetName())
    r=(bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM)
    if n in padbb:
        o=padbb[n];padbb[n]=(min(o[0],r[0]),min(o[1],r[1]),max(o[2],r[2]),max(o[3],r[3]))
    else: padbb[n]=r
print('U1 placed')

# ===== 5. step 2a: reroute USB/IN1P/ECG parts inside A =====
REROUTE={'USB_DP':(L3,[(22.9,-6.09),(21.26,4.4)]),
         'USB_DM':(L3,[(22.1,-6.32),(21.02,5.0)]),
         'IN1P_AC':(L4,[(21.6,0.52),(21.6,-2.05)]),
         'ECG_P':(L4,[(21.6,-7.10),(15.72,4.4)])}
def clippt(x1,y1,x2,y2):
    # clip segment into A: return (inA_bool, list of outside pieces)
    # simple: param clip vs rect A
    t0,t1=0.0,1.0
    dx,dy=x2-x1,y2-y1
    for p,q in ((-dx,x1-A[0]),(dx,A[2]-x1),(-dy,y1-A[1]),(dy,A[3]-y1)):
        if abs(p)<1e-12:
            if q<0: return False,[]
        else:
            r=q/p
            if p<0: t0=max(t0,r)
            else: t1=min(t1,r)
    if t0>t1: return False,[(x1,y1,x2,y2)]
    pieces=[]
    if t0>0: pieces.append((x1,y1,x1+dx*t0,y1+dy*t0))
    if t1<1: pieces.append((x1+dx*t1,y1+dy*t1,x2,y2))
    return True,pieces

ndel=0
for o,isvia,x1,y1,x2,y2,nn,lay in TRK0:
    if isvia:
        if inrect(x1,y1,KO): board.Remove(o);ndel+=1;continue
    else:
        if nn in REROUTE and lay==TOP:
            hits,pieces=clippt(x1,y1,x2,y2)
            if hits:
                board.Remove(o);ndel+=1
                for a,b,c,d in pieces:
                    add_seg(a,b,c,d,nn,TOP,0.127)
                continue
        if inrect(x1,y1,KO) or inrect(x2,y2,KO):
            board.Remove(o);ndel+=1;continue
        # delete F.Cu stub endpoints under module body? no - keep per plan
print('reroute deleted',ndel)
# add via pairs + inner tracks
for nn,(lay,vs) in REROUTE.items():
    for vx,vy in vs: add_via(vx,vy,nn)
    add_seg(vs[0][0],vs[0][1],vs[1][0],vs[1][1],nn,lay,SIG_W)
print('2a done')

# ===== 6. step 2b: trim stub ends within 0.3mm of any U1 pad =====
PADRECTS=list(padbb.values())
def drect(px,py,r):
    dx=max(r[0]-px,0,px-r[2]);dy=max(r[1]-py,0,py-r[3])
    return math.hypot(dx,dy)
# collect current tracks (objects still valid? TRK0 snapshot + deleted set)
deluid=set()
for o,isvia,x1,y1,x2,y2,nn,lay in TRK0:
    if isvia or nn not in PADNET.values() or nn is None: continue
    for ei,(ex,ey) in enumerate(((x1,y1),(x2,y2))):
        dm=min(drect(ex,ey,r) for r in PADRECTS)
        if dm<0.3:
            # pull this endpoint 0.4mm away from nearest pad rect, along seg dir
            ox,oy=(x2,y2) if ei==0 else (x1,y1)
            ux,uy=ex-ox,ey-oy;L=math.hypot(ux,uy)
            if L<0.05:
                board.Remove(o);deluid.add(id(o));break
            ux,uy=ux/L,uy/L
            need=0.4-dm
            nx_,ny_=ex-ux*need,ey-uy*need
            if ei==0: o.SetStart(pcbnew.VECTOR2I(int(nx_*MM),int(ny_*MM)))
            else: o.SetEnd(pcbnew.VECTOR2I(int(nx_*MM),int(ny_*MM)))
            break
print('2b trim done')

# ===== 7. step 3: explicit routes =====
def stub(net,x,y,w=SIG_W,layer=TOP):
    pass
def top_route(x1,y1,x2,y2,net,w=SIG_W): add_seg(x1,y1,x2,y2,net,TOP,w)
def via_route(px,py,vx,vy,tx,ty,net,layer,endvia=True,w=SIG_W):
    add_seg(px,py,vx,vy,net,TOP,w)
    add_via(vx,vy,net)
    add_seg(vx,vy,tx,ty,net,layer,w)
    if endvia: add_via(tx,ty,net)
def top_pts(pts,net,w=SIG_W):
    for i in range(len(pts)-1): add_seg(pts[i][0],pts[i][1],pts[i+1][0],pts[i+1][1],net,TOP,w)

P=lambda n:( -8.75+NEWX, -8.26+(n-1)*1.27+NEWY ) if n<=14 else ( -6.985+(n-15)*1.27+NEWX, 9.5+NEWY ) if n<=26 else ( 8.75+NEWX, 8.25-(n-27)*1.27+NEWY )
# left column
via_route(*P(2),3.8,-13.49,5.20,-4.75,'+3V3_ESP',L3,True,PWR_W)
via_route(*P(3),3.8,-12.22,16.08,-3.83,'ESP_EN',L4)
top_route(*P(4),4.71,-0.50,'LSM6_INT1')
top_route(*P(5),5.40,0.35,'TMP117_ALERT')
top_route(*P(6),5.40,1.20,'GAUGE_ALRT')
top_route(*P(7),5.40,2.05,'CHG_PG')
via_route(*P(8),3.8,-5.87,7.65,4.01,'CS_AD5940',L3,False)
via_route(*P(9),3.8,-4.60,9.85,6.50,'CS_FLASH',L6)
via_route(*P(10),3.8,-3.33,10.70,6.50,'STATUS_LED',L6)
via_route(*P(11),3.8,-2.06,10.16,7.18,'CS_AFE4900',BOT,False)
via_route(*P(12),3.8,-0.79,5.20,2.90,'I2C_SDA',L3)
top_pts([P(13),(3.1,2.6),(4.18,7.76)],'USB_DM_S3')
top_pts([P(14),(3.4,2.9),(4.58,8.07)],'USB_DP_S3')
# bottom row
via_route(*P(15),4.515,1.95,5.40,-1.35,'EXP_INT',L4)
top_route(*P(17),6.09,3.75,'I2C_SCL')
via_route(*P(18),8.325,1.95,5.78,2.70,'CS_ADS1292',L3,False)
top_route(*P(19),11.40,5.45,'SPI_MOSI')
via_route(*P(20),10.865,1.95,9.06,2.62,'SPI_SCK',BOT,False)
via_route(*P(21),12.135,1.95,11.30,5.89,'SPI_MISO',L3)
via_route(*P(22),13.405,1.95,8.59,6.78,'AD5940_GPIO0',L4,False)
top_route(*P(23),15.08,5.32,'AFE4900_ADC_RDY')
# right column
via_route(*P(27),19.1,1.75,5.98,-0.24,'ESP_IO0',L6)
LANE={31:(18.6,2.90),32:(18.3,2.05),33:(18.0,1.20),34:(17.7,0.35),36:(17.4,-2.20)}
for n,(lx,sy) in LANE.items():
    net=PADNET[str(n)]
    top_pts([P(n),(lx,P(n)[1]),(lx,sy),(19.15,sy)],net)
via_route(*P(35),19.1,-8.41,18.09,-0.79,'SPARE_IN',BOT,False)
top_route(*P(37),21.23,-6.78,'ESP_TX')
via_route(*P(38),19.1,-12.22,23.0,-2.38,'ADS1292_DRDY',BOT,False)
via_route(*P(39),19.1,-13.49,7.43,-3.23,'FSR_ADC',L6)
# GND vias
add_via(2.75,-14.76,'GND');add_via(20.25,-14.76,'GND')
for gx in (8.6,10.0,11.4):
    for gy in (-8.44,-7.04,-5.64): add_via(gx,gy,'GND')
# cap links (0.3mm)
for ref in ('C7','C8','C64'):
    for m in board.GetFootprints():
        if m.GetReference()!=ref: continue
        for pd in m.Pads():
            nn=pd.GetNetname();pp=pd.GetPosition();px,py=pp.x/MM,pp.y/MM
            if nn=='+3V3_ESP': add_seg(px,py,2.0,-13.49,nn,TOP,PWR_W)
            elif nn=='ESP_EN': add_seg(px,py,2.0,-12.22,nn,TOP,PWR_W)
            elif nn=='GND':
                if math.hypot(px-2.0,py+14.76)<1.8:
                    add_seg(px,py,2.0,-14.76,nn,TOP,PWR_W)
                else:
                    add_seg(px,py,px+0.55,py,nn,TOP,PWR_W)
                    add_via(px+0.55,py,'GND')
print('routes done')
board.Save(DST)
print('saved',DST)
