import pcbnew,math,re
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
board=pcbnew.LoadBoard(BASE+'vitalq_v2.kicad_pcb')
MM=1e6
TOP,BOT=0,2
def add_seg(x1,y1,x2,y2,net,layer,w=0.127):
    if abs(x2-x1)<1e-9 and abs(y2-y1)<1e-9: return
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(round(x1*MM)),int(round(y1*MM))))
    t.SetEnd(pcbnew.VECTOR2I(int(round(x2*MM)),int(round(y2*MM))))
    t.SetWidth(int(w*MM));t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(round(x*MM)),int(round(y*MM))))
    v.SetWidth(int(0.4*MM));v.SetDrill(int(0.2*MM));v.SetNet(board.FindNet(net));v.SetLayerPair(0,2);board.Add(v)

# --- 1. via at inner-layer track end adjacent to opposite-side pad ---
VIA_AT=[('CS_AFE4900',27.25,51.5),('ESP_EN',18.1263,40.8394),('CS_AD5940',36.4859,10.2308),
        ('MAX86178_INT',26.4,47.3),('SPI_MOSI',36.4859,10.6308),('AD5940_GPIO0',36.4859,10.1808),
        ('SPI_SCK',26.5859,46.8808)]
# GND pair: vias at both ends tie to pours
for net,x,y in VIA_AT: add_via(x,y,net)
add_via(12.4313,11.6119,'GND');add_via(12.4,5.0031,'GND')

# --- 2. cap respacing: clear all tracks/vias in the cap column then rewire ---
clr=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();x,y=p.x/MM,p.y/MM
        if -1.0<x<3.0 and -15.5<y<-9.5: clr.append(t)
    else:
        s,e=t.GetStart(),t.GetEnd()
        mx,my=(s.x+e.x)/2/MM,(s.y+e.y)/2/MM
        if -1.0<mx<3.0 and -15.5<my<-9.5: clr.append(t)
for t in clr: board.Remove(t)
print('cleared',len(clr))
for m in board.GetFootprints():
    r=m.GetReference()
    if r=='C7': m.SetPosition(pcbnew.VECTOR2I(int(0.6*MM),int(-14.0*MM)));m.SetOrientationDegrees(90)
    if r=='C8': m.SetPosition(pcbnew.VECTOR2I(int(0.6*MM),int(-12.1*MM)));m.SetOrientationDegrees(90)
    if r=='C64': m.SetPosition(pcbnew.VECTOR2I(int(1.6*MM),int(-11.3*MM)));m.SetOrientationDegrees(90)
# rewire: for each moved cap, connect pads by net to targets
U1P={'p1':(2.75,-14.76),'p2':(2.75,-13.49),'p3':(2.75,-12.22)}
def cap_pads(ref):
    for m in board.GetFootprints():
        if m.GetReference()==ref:
            return [(pd.GetNetname(),pd.GetPosition().x/MM,pd.GetPosition().y/MM) for pd in m.Pads()]
# via-free-spot search around a point (avoid pads, vias, edge)
PAD_BB=[]
for m in board.GetFootprints():
    for pd in m.Pads():
        bb=pd.GetBoundingBox()
        PAD_BB.append((bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM,pd.GetNetname()))
def free_via(net,x,y):
    for r in (0.55,0.75,0.95,1.2,1.6,2.0):
        for a in range(0,360,20):
            vx,vy=x+r*math.cos(math.radians(a)),y+r*math.sin(math.radians(a))
            if vx<-1.5 or vx>22.3 or vy<-16.3 or vy>9.5: continue
            ok=True
            for bx1,by1,bx2,by2,pn in PAD_BB:
                if pn==net: continue
                # via circle vs pad bbox: gap >0.15
                dx=max(bx1-vx,0,vx-bx2);dy=max(by1-vy,0,vy-by2)
                if math.hypot(dx,dy)<0.35: ok=False;break
            if ok: return vx,vy
    return None
for ref in ('C7','C8','C64'):
    for nn,px,py in cap_pads(ref):
        if nn=='+3V3_ESP': add_seg(px,py,*U1P['p2'],nn,TOP,0.3)
        elif nn=='ESP_EN': add_seg(px,py,*U1P['p3'],nn,TOP,0.3)
        elif nn=='GND':
            if ref=='C7': add_seg(px,py,*U1P['p1'],nn,TOP,0.3)
            else:
                v=free_via('GND',px,py)
                if v: add_seg(px,py,v[0],v[1],nn,TOP,0.3);add_via(*v,'GND')
board.Save(BASE+'vitalq_v2.kicad_pcb')
print('saved p3')
