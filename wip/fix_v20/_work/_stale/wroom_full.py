#!/usr/bin/env python3
# WROOM fix_v20 surgery v4 — full F.Cu rip inside RZ, re-route all affected nets
import pcbnew, math, heapq, time
from collections import defaultdict

PCB = '/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/vitalq_v2.kicad_pcb'
OUT = '/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/_work/stage2.kicad_pcb'
RZ = (1.2, -10.55, 24.2, 12.2)
NEWX, NEWY = 12.75, -0.5
MM = 1000000
GRID = 0.05
CLR = 0.10
SIG_W = 0.127
PWR_W = 0.25
F_CU = 0

def inside(x,y,m=0.0):
    return RZ[0]-m<=x<=RZ[2]+m and RZ[1]-m<=y<=RZ[3]+m

board = pcbnew.LoadBoard(PCB)
u1 = board.FindFootprintByReference('U1')
u1nets = set(p.GetNetname() for p in u1.Pads() if p.GetNetname())

moved = {'C7':(2.2,0.72),'C8':(2.3,2.29),'C50':(15.0,10.7),'C64':(23.4,-3.0),
         'R71':(12.0,10.5),'R73':(16.5,10.7),'R67':(23.14,-0.07)}
for fp_ in board.GetFootprints():
    r=fp_.GetReference()
    if r in moved:
        fp_.SetPosition(pcbnew.VECTOR2I_MM(*moved[r]))
        print("move",r,"->",moved[r])

# ---- rip ALL F.Cu inside RZ (all nets); clip crossers; inner layers untouched ----
del_seg=[]; kept_inner=[]; terminals=defaultdict(list)  # net -> [(x,y)]
def clip(x,y,px,py):
    last=(x,y)
    for i in range(1,400):
        tt=i/399.0; xx,yy=x+(px-x)*tt,y+(py-y)*tt
        if inside(xx,yy,0.002): last=(xx,yy)
        else: break
    return last
for t in board.GetTracks():
    n=t.GetNetname()
    if isinstance(t, pcbnew.PCB_VIA): continue
    s,e=t.GetStart(),t.GetEnd()
    x1,y1,x2,y2=s.x/MM,s.y/MM,e.x/MM,e.y/MM
    i1,i2=inside(x1,y1),inside(x2,y2)
    if not i1 and not i2: continue
    if i1!=i2:
        if i1:
            nx,ny=clip(x1,y1,x2,y2); t.SetStart(pcbnew.VECTOR2I_MM(nx,ny))
            terminals[n].append((nx,ny))
        else:
            nx,ny=clip(x2,y2,x1,y1); t.SetEnd(pcbnew.VECTOR2I_MM(nx,ny))
            terminals[n].append((nx,ny))
        if t.GetLayer()!=F_CU: kept_inner.append(t)
        continue
    if t.GetLayer()==F_CU: del_seg.append(t)
    else: kept_inner.append(t)
print("del F.Cu segs",len(del_seg),"| inner kept",len(kept_inner))
for t in del_seg: board.Remove(t)

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

# vias inside RZ: keep ones anchored to kept inner copper -> they are terminals
del_via=[]; kept_vias=[]
for t in board.GetTracks():
    if not isinstance(t,pcbnew.PCB_VIA): continue
    p=t.GetPosition(); vx,vy=p.x/MM,p.y/MM
    if not inside(vx,vy): continue
    n=t.GetNetname()
    def dist_to_s(s_):
        ss,ee=s_.GetStart(),s_.GetEnd()
        return segdist(vx,vy,ss.x/MM,ss.y/MM,ee.x/MM,ee.y/MM)
    anch = any(dist_to_s(s_)<0.35 for s_ in kept_inner)
    if anch:
        kept_vias.append((vx,vy,n)); terminals[n].append((vx,vy))
    else:
        del_via.append(t)
print("vias kept",len(kept_vias),"del",len(del_via))
for t in del_via: board.Remove(t)

# ---- footprint swap ----
board.Remove(u1)
fp = pcbnew.FootprintLoad('/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/vitalq.pretty','ESP32_S3_WROOM_1')
fp.SetPosition(pcbnew.VECTOR2I_MM(NEWX,NEWY))
fp.SetReference('U1'); fp.SetValue('ESP32-S3-WROOM-1-N16R8'); fp.SetLocked(True)
board.Add(fp)
PADNET = {'1':'GND','2':'+3V3_ESP','3':'ESP_EN','4':'LSM6_INT1','5':'TMP117_ALERT',
'6':'GAUGE_ALRT','7':'CHG_PG','8':'CS_AD5940','9':'CS_FLASH','10':'STATUS_LED',
'11':'CS_AFE4900','12':'I2C_SDA','13':'USB_DM_S3','14':'USB_DP_S3','15':'EXP_INT',
'16':None,'17':'I2C_SCL','18':'CS_ADS1292','19':'SPI_MOSI','20':'SPI_SCK','21':'SPI_MISO',
'22':'AD5940_GPIO0','23':'AFE4900_ADC_RDY','24':None,'25':None,'26':None,'27':'ESP_IO0',
'28':None,'29':None,'30':None,'31':'CS_MAX86178','32':'MAX86178_INT','33':'MIC_CLK',
'34':'MIC_DAT','35':'SPARE_IN','36':'ESP_RX','37':'ESP_TX','38':'ADS1292_DRDY',
'39':'FSR_ADC','40':'GND','41':'GND'}
pads_by_num=defaultdict(list)
for pad in fp.Pads(): pads_by_num[pad.GetNumber()].append(pad)
for pnum,nn in PADNET.items():
    net=board.FindNet(nn) if nn else None
    for pad in pads_by_num.get(pnum,[]): pad.SetNet(net)
# terminals: U1 pads
for pad in fp.Pads():
    n=pad.GetNetname()
    if not n: continue
    p=pad.GetPosition()
    terminals[n].append((p.x/MM,p.y/MM))
# non-U1 pads inside RZ on any net that lost copper -> terminals
for fp_ in board.GetFootprints():
    if fp_.GetReference()=='U1': continue
    for pad in fp_.Pads():
        if not pad.IsOnLayer(F_CU): continue
        p=pad.GetPosition(); n=pad.GetNetname()
        if not n: continue
        if inside(p.x/MM,p.y/MM):
            terminals[n].append((p.x/MM,p.y/MM))

# EPAD GND vias (place now: they are GND terminals)
gndnet=board.FindNet('GND')
for dx in (-2.9,-1.5,-0.1):
    for dy in (-1.94,-0.54,0.86):
        v=pcbnew.PCB_VIA(board); v.SetPosition(pcbnew.VECTOR2I_MM(NEWX+dx,NEWY+dy))
        v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetWidth(int(0.45*MM)); v.SetDrill(int(0.2*MM))
        v.SetNet(gndnet); board.Add(v)
        terminals['GND'].append((NEWX+dx,NEWY+dy))
print("9 EPAD vias placed")

# drop nets that don't need routing (single terminal -> nothing to connect)
todo={n:pts for n,pts in terminals.items() if len(set((round(a,3),round(b,3)) for a,b in pts))>=2}
print("nets to route:",len(todo), sorted(todo))

# ---- geometry / router ----
segs=[];vias=[];pads=[];zones_block=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition(); vias.append((p.x/MM,p.y/MM,t.GetNetname(),t.GetWidth(F_CU)/MM))
    else:
        s,e=t.GetStart(),t.GetEnd()
        segs.append((s.x/MM,s.y/MM,e.x/MM,e.y/MM,t.GetNetname(),t.GetLayer(),t.GetWidth()/MM))
for fp_ in board.GetFootprints():
    if fp_.GetReference()=='U1': continue
    for pad in fp_.Pads():
        if not pad.IsOnLayer(F_CU): continue
        p=pad.GetPosition(); sz=pad.GetSize(); r=int(pad.GetOrientationDegrees())%180
        w,h=(sz.x/MM,sz.y/MM) if r==0 else (sz.y/MM,sz.x/MM)
        pads.append((p.x/MM,p.y/MM,w,h,pad.GetNetname()))
for z in board.Zones():
    if not z.GetIsRuleArea(): continue
    if not z.GetDoNotAllowTracks(): continue
    if not z.IsOnLayer(F_CU): continue
    pts=[]
    try:
        out=z.Outline().Outline(0)
        for i in range(out.PointCount()):
            p=out.CPoint(i); pts.append((p.x/MM,p.y/MM))
    except Exception: pass
    if pts: zones_block.append(pts)
zones_block.append([(NEWX-9,NEWY-9.8),(NEWX+9,NEWY-9.8),(NEWX+9,NEWY-15.75),(NEWX-9,NEWY-15.75)])
print("zones blocking F.Cu:",len(zones_block))
u1_padlist=[]
for pad in fp.Pads():
    p=pad.GetPosition(); sz=pad.GetSize(); rot=int(pad.GetOrientationDegrees())%180
    w,h=(sz.x/MM,sz.y/MM) if rot==0 else (sz.y/MM,sz.x/MM)
    u1_padlist.append((p.x/MM,p.y/MM,w,h,pad.GetNetname(),pad.GetNumber()))

gx0,gy0=RZ[0],RZ[1]
W=int((RZ[2]-RZ[0])/GRID)+2; H=int((RZ[3]-RZ[1])/GRID)+2
def cell(x,y): return (int(round((x-gx0)/GRID)),int(round((y-gy0)/GRID)))
def cpos(cx,cy): return (gx0+cx*GRID,gy0+cy*GRID)
def poly_contains(pts,x,y):
    ins=False; j=len(pts)-1
    for i in range(len(pts)):
        xi,yi=pts[i]; xj,yj=pts[j]
        if ((yi>y)!=(yj>y)) and (x<(xj-xi)*(y-yi)/(yj-yi+1e-12)+xi): ins=not ins
        j=i
    return ins

def apron_penalty(cx,cy,dx,dy):
    px,py=cpos(cx,cy)
    if dy!=0:
        if 4.7<=px<=5.6 and -9.6<=py<=8.3: return 40
        if 20.1<=px<=21.0 and -9.6<=py<=8.3: return 40
        if px<3.3 and -9.6<=py<=8.3: return 30
    if dx!=0:
        if 9.55<=py<=10.5 and 4.9<=px<=20.3: return 40
    return 0

def blocked(net,tw):
    infl=CLR+tw/2
    blk=bytearray(W*H)
    def mark(cx,cy):
        if 0<=cx<W and 0<=cy<H: blk[cy*W+cx]=1
    def mark_seg(x1,y1,x2,y2,r):
        c0x,c0y=cell(min(x1,x2)-r,min(y1,y2)-r); c1x,c1y=cell(max(x1,x2)+r,max(y1,y2)+r)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if segdist(px,py,x1,y1,x2,y2)<=r: mark(cx,cy)
    def mark_rect(x,y,hx,hy,r):
        c0x,c0y=cell(x-hx-r,y-hy-r); c1x,c1y=cell(x+hx+r,y+hy+r)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if abs(px-x)<=hx+r and abs(py-y)<=hy+r: mark(cx,cy)
    def mark_poly(pts):
        mnx=min(p[0] for p in pts)-infl; mxx=max(p[0] for p in pts)+infl
        mny=min(p[1] for p in pts)-infl; mxy=max(p[1] for p in pts)+infl
        c0x,c0y=cell(mnx,mny); c1x,c1y=cell(mxx,mxy)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if poly_contains(pts,px,py): mark(cx,cy); continue
                for i in range(len(pts)):
                    a=pts[i]; b=pts[(i+1)%len(pts)]
                    if segdist(px,py,a[0],a[1],b[0],b[1])<=infl: mark(cx,cy); break
    for pts in zones_block: mark_poly(pts)
    for (x1,y1,x2,y2,n,layer,w) in segs:
        if n==net or layer!=F_CU: continue
        mark_seg(x1,y1,x2,y2,infl+w/2)
    for (x,y,n,w) in vias:
        if n==net: continue
        c0x,c0y=cell(x-infl-w/2,y-infl-w/2); c1x,c1y=cell(x+infl+w/2,y+infl+w/2)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if (px-x)**2+(py-y)**2<=(infl+w/2)**2: mark(cx,cy)
    for (x,y,sxx,syy,n) in pads:
        if n==net: continue
        mark_rect(x,y,sxx/2,syy/2,infl)
    for (x,y,w,h,n,num) in u1_padlist:
        if n==net: continue
        mark_rect(x,y,w/2,h/2,infl)
    return blk

def route(start_xy,goal_pts,blk,ownpads):
    sx,sy=cell(*start_xy)
    goalset=set()
    for gx,gy in goal_pts:
        cx,cy=cell(gx,gy)
        for dy in range(-3,4):
            for dx in range(-3,4): goalset.add((cx+dx,cy+dy))
    b=bytearray(blk)
    for (x,y,sxx,syy) in ownpads:
        c0x,c0y=cell(x-sxx/2,y-syy/2); c1x,c1y=cell(x+sxx/2,y+syy/2)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)): b[cy*W+cx]=0
    INF=float('inf'); dist={(sx,sy,-1):0.0}; prev={}; pq=[(0.0,sx,sy,-1)]; found=None
    while pq:
        d,cx,cy,dirn=heapq.heappop(pq)
        if d>dist.get((cx,cy,dirn),INF): continue
        if (cx,cy) in goalset: found=(cx,cy,dirn); break
        for ndir,(dx,dy) in enumerate([(1,0),(-1,0),(0,1),(0,-1)]):
            nx,ny=cx+dx,cy+dy
            if not(0<=nx<W and 0<=ny<H) or b[ny*W+nx]: continue
            nd=d+1+(6 if dirn>=0 and dirn!=ndir else 0)+apron_penalty(nx,ny,dx,dy)
            st=(nx,ny,ndir)
            if nd<dist.get(st,INF):
                dist[st]=nd; prev[st]=(cx,cy,dirn); heapq.heappush(pq,(nd,nx,ny,ndir))
    if not found: return None
    path=[]; st=found
    while st in prev:
        path.append(cpos(st[0],st[1])); st=prev[st]
    path.append(cpos(st[0],st[1])); path.reverse()
    simp=[path[0]]
    for i in range(1,len(path)-1):
        ax,ay=simp[-1]; bx,by=path[i]; cx2,cy2=path[i+1]
        if abs((bx-ax)*(cy2-ay)-(cx2-ax)*(by-ay))>1e-9: simp.append((bx,by))
    simp.append(path[-1])
    return simp

def add_track(x1,y1,x2,y2,net,w):
    t=pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I_MM(x1,y1)); t.SetEnd(pcbnew.VECTOR2I_MM(x2,y2))
    t.SetWidth(int(w*MM)); t.SetLayer(F_CU); t.SetNet(board.FindNet(net))
    board.Add(t); segs.append((x1,y1,x2,y2,net,F_CU,w))
    return (x1,y1,x2,y2)

def tree_pts(newsegs):
    pts=[]
    for (x1,y1,x2,y2) in newsegs:
        L=math.hypot(x2-x1,y2-y1); m=max(1,int(L/0.35))
        for i in range(m+1): pts.append((x1+(x2-x1)*i/m,y1+(y2-y1)*i/m))
    return pts

# order: fewest terminals first, U1 nets last per net? -> route simple first
netlist=sorted(todo.items(), key=lambda kv:len(kv[1]))
fail=[]; t0=time.time()
ownpad_of_net=defaultdict(list)
for (x,y,w,h,n,num) in u1_padlist:
    if n: ownpad_of_net[n].append((x,y,w,h))
for net,terms in netlist:
    tw = PWR_W if net in ('GND','+3V3_ESP') else SIG_W
    blk=blocked(net,tw)
    # dedupe terminals
    seen=set(); terms2=[]
    for p in terms:
        k=(round(p[0],3),round(p[1],3))
        if k not in seen: seen.add(k); terms2.append(p)
    tree=[terms2[0]]; newsegs=[]; remaining=terms2[1:]
    ownpads=ownpad_of_net.get(net,[])+[(terms2[0][0],terms2[0][1],0.6,0.6)]
    ok=True
    while remaining:
        goals=tree+tree_pts(newsegs)
        # pick the remaining terminal nearest to any goal
        best=None
        for ti,tt in enumerate(remaining):
            d=min((tt[0]-g[0])**2+(tt[1]-g[1])**2 for g in goals)
            if best is None or d<best[0]: best=(d,ti)
        _,ti=best; tt=remaining[ti]
        path=route(tt,goals,blk,ownpads+[(tt[0],tt[1],0.6,0.6)])
        if not path: ok=False; break
        path[0]=tt
        ex,ey=path[-1]
        anch=min(goals,key=lambda a:(a[0]-ex)**2+(a[1]-ey)**2)
        if math.hypot(anch[0]-ex,anch[1]-ey)>GRID: path.append(anch)
        for i in range(len(path)-1):
            newsegs.append(add_track(path[i][0],path[i][1],path[i+1][0],path[i+1][1],net,tw))
        remaining.pop(ti); tree.append(tt)
    if not ok:
        fail.append(net); print(" FAIL",net,f"({len(terms2)-len(remaining)}/{len(terms2)})")
    else:
        print(" ok",net,len(terms2),"term",len(newsegs),"segs")
print(f"done {len(netlist)-len(fail)}/{len(netlist)} nets in {time.time()-t0:.0f}s; failed:{fail}")
board.Save(OUT)
print("saved",OUT)
