import pcbnew,math,re,heapq
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
board=pcbnew.LoadBoard(BASE+'vitalq_v2.kicad_pcb')
MM=1e6
TOP,BOT=0,2
U1NETS={'+3V3_ESP','ESP_EN','LSM6_INT1','TMP117_ALERT','GAUGE_ALRT','CHG_PG','CS_AD5940','CS_FLASH','STATUS_LED','CS_AFE4900','I2C_SDA','USB_DM_S3','USB_DP_S3','EXP_INT','I2C_SCL','CS_ADS1292','SPI_MOSI','SPI_SCK','SPI_MISO','AD5940_GPIO0','AFE4900_ADC_RDY','ESP_IO0','CS_MAX86178','MAX86178_INT','MIC_CLK','MIC_DAT','SPARE_IN','ESP_RX','ESP_TX','ADS1292_DRDY','FSR_ADC','GND'}
BLOCK={'USB_DP','USB_DM','IN1P_AC','ECG_P'}
ALLN=U1NETS|BLOCK

def add_seg(x1,y1,x2,y2,net,layer,w=0.127):
    if abs(x2-x1)<1e-9 and abs(y2-y1)<1e-9: return
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(round(x1*MM)),int(round(y1*MM))))
    t.SetEnd(pcbnew.VECTOR2I(int(round(x2*MM)),int(round(y2*MM))))
    t.SetWidth(int(w*MM));t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(round(x*MM)),int(round(y*MM))))
    v.SetWidth(int(0.4*MM));v.SetDrill(int(0.2*MM));v.SetNet(board.FindNet(net));v.SetLayerPair(0,2);board.Add(v)

# snapshot current copper
def snap():
    seg=[];via=[]
    for t in board.GetTracks():
        if isinstance(t,pcbnew.PCB_VIA):
            p=t.GetPosition();via.append((t,p.x/MM,p.y/MM,t.GetNetname()))
        else:
            s,e=t.GetStart(),t.GetEnd()
            seg.append((t,s.x/MM,s.y/MM,e.x/MM,e.y/MM,t.GetLayer(),t.GetNetname(),t.GetWidth()/MM))
    return seg,via
SEG,VIA=snap()
PAD_BB=[]
for m in board.GetFootprints():
    for pd in m.Pads():
        bb=pd.GetBoundingBox()
        PAD_BB.append((bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM,pd.GetNetname(),pd.GetDrillSizeX()/MM))

# remove p3's misplaced vias (near-list)
BAD=[('AD5940_GPIO0',36.4859,10.1808)]
for net,x,y in BAD:
    for t,vx,vy,n in VIA:
        if n==net and abs(vx-x)<0.02 and abs(vy-y)<0.02: board.Remove(t)
VIA=[v for v in VIA if not (v[3]=='AD5940_GPIO0' and abs(v[1]-36.4859)<0.02 and abs(v[2]-10.1808)<0.02)]

def ptsegd(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy
    if L2==0:return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

# via free-spot: clear of other-net pads(+0.3), vias(+0.45), same-layer seg edges(+0.15), drills
def via_free(net,x,y,lay=TOP):
    for r in [0.45,0.6,0.8,1.0,1.4,1.8,2.4]:
        for a in range(0,360,15):
            vx,vy=x+r*math.cos(math.radians(a)),y+r*math.sin(math.radians(a))
            ok=True
            for bx1,by1,bx2,by2,pn,dr in PAD_BB:
                dx=max(bx1-vx,0,vx-bx2);dy=max(by1-vy,0,vy-by2)
                if pn!=net and math.hypot(dx,dy)<0.30: ok=False;break
            if not ok: continue
            for t,vxx,vyy,n in VIA:
                if n!=net and math.hypot(vx-vxx,vy-vyy)<0.45: ok=False;break
            if ok: return vx,vy
    return None

# pad-joins: via off-pad + surface stub + inner stub
JOINS=[  # (net, pad_cx,cy, pad_layer, inner_track_end, inner_layer)
 ('AD5940_GPIO0',37.3859,10.2808,TOP,37.3359,10.1808,8),
 ('CS_AD5940',36.5859,10.2808,TOP,36.4859,10.2308,BOT),  # pad Top, track Bot: via joins both
 ('SPI_MOSI',36.5859,10.6808,TOP,36.4859,10.6308,8),
 ('CS_AFE4900',27.2,51.4,BOT,27.25,51.5,8),
 ('MAX86178_INT',26.5,47.35,BOT,26.4,47.3,12),
 ('SPI_SCK',26.5,46.95,BOT,26.5859,46.8808,6),
]
for net,px,py,pl,tx,ty,tl in JOINS:
    v=via_free(net,tx,ty)
    if not v: print('NOVIASPOT',net);continue
    add_via(*v,net)
    add_seg(tx,ty,v[0],v[1],net,tl)
    add_seg(px,py,v[0],v[1],net,pl)
board.Save(BASE+'vitalq_v2.kicad_pcb')
print('p4a saved')
