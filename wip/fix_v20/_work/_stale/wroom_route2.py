#!/usr/bin/env python3
# WROOM v5 — 2-layer (F.Cu + In3/L8) rip-and-reroute of RZ
import pcbnew, math, heapq, time
from collections import defaultdict

PCB = '/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/vitalq_v2.kicad_pcb'
OUT = '/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/_work/stage2.kicad_pcb'
RZ = (0.0, -10.55, 24.2, 12.2)
NEWX, NEWY = 11.5, -0.5
MM = 1000000
GRID = 0.05
CLR = 0.10
SIG_W = 0.127
PWR_W = 0.25
VIA_W, VIA_D = 0.45, 0.2
LF, LI = 0, 8   # F.Cu + "Signal Layer 4" (In3, id 8 — nearly empty)

def inside(x,y,m=0.0):
    return RZ[0]-m<=x<=RZ[2]+m and RZ[1]-m<=y<=RZ[3]+m
def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

board = pcbnew.LoadBoard(PCB)
u1 = board.FindFootprintByReference('U1')
u1nets = set(p.GetNetname() for p in u1.Pads() if p.GetNetname())

moved = {'C7':(0.9,0.72),'C8':(1.0,2.29),'C50':(15.0,10.7),'C64':(23.4,-3.0),
         'R71':(12.0,10.5),'R73':(16.5,10.7),'R67':(23.14,-0.07),'FID4':(0.3,-4.6)}
for fp_ in board.GetFootprints():
    r=fp_.GetReference()
    if r in moved: fp_.SetPosition(pcbnew.VECTOR2I_MM(*moved[r])); print("move",r)

# ---- rip F.Cu + clip everything; keep inner; vias: keep only inner-anchored ----
del_seg=[]; kept_inner=[]; terminals=defaultdict(list)  # net -> [(x,y,layer)]
inner_stub_pts=defaultdict(list)
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
    layer=t.GetLayer()
    if i1!=i2:
        if i1:
            nx,ny=clip(x1,y1,x2,y2); t.SetStart(pcbnew.VECTOR2I_MM(nx,ny))
        else:
            nx,ny=clip(x2,y2,x1,y1); t.SetEnd(pcbnew.VECTOR2I_MM(nx,ny))
        if layer==LF: terminals[n].append((nx,ny,LF))
        else:
            kept_inner.append(t); inner_stub_pts[n].append((nx,ny,layer))
        continue
    if layer==LF: del_seg.append(t)
    else: kept_inner.append(t)
for t in del_seg: board.Remove(t)
print("del F.Cu",len(del_seg),"inner kept",len(kept_inner))

del_via=[]; kept_vias=[]
for t in board.GetTracks():
    if not isinstance(t,pcbnew.PCB_VIA): continue
    p=t.GetPosition(); vx,vy=p.x/MM,p.y/MM
    if not inside(vx,vy): continue
    n=t.GetNetname()
    anch=any(segdist(vx,vy,s.GetStart().x/MM,s.GetStart().y/MM,s.GetEnd().x/MM,s.GetEnd().y/MM)<0.35 for s in kept_inner)
    if anch:
        kept_vias.append((vx,vy,n)); terminals[n].append((vx,vy,'B'))
    else: del_via.append(t)
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
'28':None,'29':'IO36','30':'IO37','31':'CS_MAX86178','32':'MAX86178_INT','33':'MIC_CLK',
'34':'MIC_DAT','35':'SPARE_IN','36':'ESP_RX','37':'ESP_TX','38':'ADS1292_DRDY',
'39':'FSR_ADC','40':'GND','41':'GND'}
# pads 28-30 must be NC: leave net None
PADNET['28']=None;PADNET['29']=None;PADNET['30']=None
pads_by_num=defaultdict(list)
for pad in fp.Pads(): pads_by_num[pad.GetNumber()].append(pad)
for pnum,nn in PADNET.items():
    net=board.FindNet(nn) if nn else None
    for pad in pads_by_num.get(pnum,[]): pad.SetNet(net)
for pad in fp.Pads():
    n=pad.GetNetname()
    if n:
        p=pad.GetPosition(); terminals[n].append((p.x/MM,p.y/MM,LF))
for fp_ in board.GetFootprints():
    if fp_.GetReference()=='U1': continue
    for pad in fp_.Pads():
        if not (pad.IsOnLayer(LF) or pad.IsOnLayer(2)): continue
        p=pad.GetPosition(); n=pad.GetNetname()
        if n and inside(p.x/MM,p.y/MM): terminals[n].append((p.x/MM,p.y/MM,LF))

# bridge vias at inner-layer stub endpoints -> terminal reachable on F + L8
def place_via(x,y,net):
    v=pcbnew.PCB_VIA(board); v.SetPosition(pcbnew.VECTOR2I_MM(x,y))
    v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetWidth(int(VIA_W*MM)); v.SetDrill(int(VIA_D*MM))
    v.SetNet(board.FindNet(net) if net else None); board.Add(v); return v
for n,pts in inner_stub_pts.items():
    for (x,y,layer) in pts:
        place_via(x,y,n); terminals[n].append((x,y,'B'))
print("bridge vias at inner stubs:",sum(len(v) for v in inner_stub_pts.values()))

gndnet=board.FindNet('GND')
for dx in (-2.9,-1.5,-0.1):
    for dy in (-1.94,-0.54,0.86):
        place_via(NEWX+dx,NEWY+dy,'GND'); terminals['GND'].append((NEWX+dx,NEWY+dy,'B'))
print("9 EPAD vias")

todo={n:pts for n,pts in terminals.items() if len(set((round(a,3),round(b,3)) for a,b,_ in pts))>=2}
print("nets:",len(todo))

# ---- model ----
segs=[];vias=[];pads=[];zones_block=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition(); vias.append((p.x/MM,p.y/MM,t.GetNetname(),t.GetWidth(LF)/MM))
    else:
        s,e=t.GetStart(),t.GetEnd()
        segs.append((s.x/MM,s.y/MM,e.x/MM,e.y/MM,t.GetNetname(),t.GetLayer(),t.GetWidth()/MM))
for fp_ in board.GetFootprints():
    if fp_.GetReference()=='U1': continue
    for pad in fp_.Pads():
        if not pad.IsOnLayer(LF): continue
        p=pad.GetPosition(); sz=pad.GetSize(); r=int(pad.GetOrientationDegrees())%180
        w,h=(sz.x/MM,sz.y/MM) if r==0 else (sz.y/MM,sz.x/MM)
        pads.append((p.x/MM,p.y/MM,w,h,pad.GetNetname()))
for z in board.Zones():
    if not z.GetIsRuleArea() or not z.GetDoNotAllowTracks(): continue
    lf = z.IsOnLayer(LF); li = z.IsOnLayer(LI)
    pts=[]
    try:
        out=z.Outline().Outline(0)
        for i in range(out.PointCount()):
            p=out.CPoint(i); pts.append((p.x/MM,p.y/MM))
    except Exception: pass
    if pts: zones_block.append((pts,lf,li))
zones_block.append(([(NEWX-9,NEWY-9.8),(NEWX+9,NEWY-9.8),(NEWX+9,NEWY-15.75),(NEWX-9,NEWY-15.75)],True,True))
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
        if 3.45<=px<=4.35 and -9.6<=py<=8.3: return 40   # left col apron
        if 18.85<=px<=19.75 and -9.6<=py<=8.3: return 40 # right col apron
        if px<2.05 and -9.6<=py<=8.3: return 30          # west lane
    if dx!=0:
        if 9.55<=py<=10.5 and 3.65<=px<=19.05: return 40 # bottom row apron
    return 0

def blocked(net,tw):
    infl=CLR+tw/2; viainfl=CLR+VIA_W/2
    blkF=bytearray(W*H); blkI=bytearray(W*H)
    def mark(blk,cx,cy):
        if 0<=cx<W and 0<=cy<H: blk[cy*W+cx]=1
    def mark_seg(blk,x1,y1,x2,y2,r):
        c0x,c0y=cell(min(x1,x2)-r,min(y1,y2)-r); c1x,c1y=cell(max(x1,x2)+r,max(y1,y2)+r)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if segdist(px,py,x1,y1,x2,y2)<=r: mark(blk,cx,cy)
    def mark_rect(blk,x,y,hx,hy,r):
        c0x,c0y=cell(x-hx-r,y-hy-r); c1x,c1y=cell(x+hx+r,y+hy+r)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if abs(px-x)<=hx+r and abs(py-y)<=hy+r: mark(blk,cx,cy)
    def mark_poly(blk,pts):
        mnx=min(p[0] for p in pts)-infl; mxx=max(p[0] for p in pts)+infl
        mny=min(p[1] for p in pts)-infl; mxy=max(p[1] for p in pts)+infl
        c0x,c0y=cell(mnx,mny); c1x,c1y=cell(mxx,mxy)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if poly_contains(pts,px,py): mark(blk,cx,cy); continue
                for i in range(len(pts)):
                    a=pts[i]; b=pts[(i+1)%len(pts)]
                    if segdist(px,py,a[0],a[1],b[0],b[1])<=infl: mark(blk,cx,cy); break
    blkV=bytearray(W*H)  # cells where a new via would violate clearance on ANY layer
    for (pts,lf,li) in zones_block:
        if lf: mark_poly(blkF,pts)
        if li: mark_poly(blkI,pts)
        mark_poly(blkV,pts)
    for (x1,y1,x2,y2,n,layer,w) in segs:
        if n==net: continue
        if layer==LF: mark_seg(blkF,x1,y1,x2,y2,infl+w/2)
        elif layer==LI: mark_seg(blkI,x1,y1,x2,y2,infl+w/2)
        if layer!=LF: mark_seg(blkV,x1,y1,x2,y2,viainfl+w/2)
        else: mark_seg(blkV,x1,y1,x2,y2,viainfl+w/2)
    for (x,y,n,w) in vias:
        if n==net: continue
        r=infl+w/2
        c0x,c0y=cell(x-r,y-r); c1x,c1y=cell(x+r,y+r)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if (px-x)**2+(py-y)**2<=r*r: mark(blkF,cx,cy); mark(blkI,cx,cy)
        rv=viainfl+w/2
        c0x,c0y=cell(x-rv,y-rv); c1x,c1y=cell(x+rv,y+rv)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if (px-x)**2+(py-y)**2<=rv*rv: mark(blkV,cx,cy)
    for (x,y,sxx,syy,n) in pads:
        if n==net: continue
        mark_rect(blkF,x,y,sxx/2,syy/2,infl)
        mark_rect(blkV,x,y,sxx/2,syy/2,viainfl)
    for (x,y,w,h,n,num) in u1_padlist:
        if n==net: continue
        mark_rect(blkF,x,y,w/2,h/2,infl)
        mark_rect(blkV,x,y,w/2,h/2,viainfl)
    return blkF,blkI,blkV

def route(start_xyz,goal_pts,blks,ownpads):
    """start (x,y,layer); goals list (x,y,layer|'B')"""
    blkF,blkI,blkV=blks
    sx,sy=cell(start_xyz[0],start_xyz[1]); sl=1 if start_xyz[2]==LI else 0
    goalset=set()
    for gx,gy,gl in goal_pts:
        cx,cy=cell(gx,gy)
        for dy in range(-3,4):
            for dx in range(-3,4):
                if gl in (LF,'B'): goalset.add((cx+dx,cy+dy,0))
                if gl in (LI,'B'): goalset.add((cx+dx,cy+dy,1))
    bF=bytearray(blkF); bI=bytearray(blkI)
    for (x,y,sxx,syy) in ownpads:
        c0x,c0y=cell(x-sxx/2,y-syy/2); c1x,c1y=cell(x+sxx/2,y+syy/2)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)): bF[cy*W+cx]=0
    INF=float('inf'); dist={(sx,sy,sl,-1):0.0}; prev={}; pq=[(0.0,sx,sy,sl,-1)]
    if start_xyz[2]=='B':  # via/through terminal: seed both layers
        dist[(sx,sy,1-sl,-1)]=0.0; heapq.heappush(pq,(0.0,sx,sy,1-sl,-1))
    found=None
    while pq:
        d,cx,cy,cl,dirn=heapq.heappop(pq)
        if d>dist.get((cx,cy,cl,dirn),INF): continue
        if (cx,cy,cl) in goalset: found=(cx,cy,cl,dirn); break
        blk = bF if cl==0 else bI
        for ndir,(dx,dy) in enumerate([(1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)]):
            nx,ny=cx+dx,cy+dy
            if not(0<=nx<W and 0<=ny<H): continue
            if blk[ny*W+nx]: continue
            if dx and dy:  # no corner cutting
                if blk[cy*W+nx] or blk[ny*W+cx]: continue
            cost=1.42 if (dx and dy) else 1
            nd=d+cost+(6 if dirn>=0 and dirn!=ndir else 0)+(apron_penalty(nx,ny,dx,dy) if cl==0 else 0)
            st=(nx,ny,cl,ndir)
            if nd<dist.get(st,INF):
                dist[st]=nd; prev[st]=(cx,cy,cl,dirn); heapq.heappush(pq,(nd,nx,ny,cl,ndir))
        # via transition: needs via-safe cell + target layer free
        nl=1-cl
        tb = bF if nl==0 else bI
        if blkV[cy*W+cx]==0 and tb[cy*W+cx]==0:
            st=(cx,cy,nl,-1); nd=d+10
            if nd<dist.get(st,INF):
                dist[st]=nd; prev[st]=(cx,cy,cl,dirn); heapq.heappush(pq,(nd,cx,cy,nl,-1))
    if not found: return None
    path=[]; st=found
    while st in prev:
        path.append((st[0],st[1],st[2])); st=prev[st]
    path.append((st[0],st[1],st[2])); path.reverse()
    return path

def add_track(x1,y1,x2,y2,net,w,layer):
    t=pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I_MM(x1,y1)); t.SetEnd(pcbnew.VECTOR2I_MM(x2,y2))
    t.SetWidth(int(w*MM)); t.SetLayer(layer); t.SetNet(board.FindNet(net))
    board.Add(t); segs.append((x1,y1,x2,y2,net,layer,w))
    return (x1,y1,x2,y2)

def cellpath_to_segs(path,net,tw):
    """path: list of (cx,cy,layer 0/1). Emit tracks + vias. Returns new seg list."""
    out=[]
    pts=[(cpos(p[0],p[1])[0],cpos(p[0],p[1])[1],p[2]) for p in path]
    # simplify per layer-run
    runs=[]; cur=[pts[0]]
    for p in pts[1:]:
        if p[2]!=cur[-1][2]:
            runs.append(cur); cur=[p]
        else: cur.append(p)
    runs.append(cur)
    prev_end=None
    for run in runs:
        simp=[run[0]]
        for i in range(1,len(run)-1):
            ax,ay,_=simp[-1]; bx,by,_=run[i]; cx2,cy2,_=run[i+1]
            if abs((bx-ax)*(cy2-ay)-(cx2-ax)*(by-ay))>1e-9: simp.append(run[i])
        simp.append(run[-1])
        for i in range(len(simp)-1):
            lay = LF if run[0][2]==0 else LI
            out.append(add_track(simp[i][0],simp[i][1],simp[i+1][0],simp[i+1][1],net,tw,lay))
        # via at layer transition point (end of this run if next run different layer)
    # vias where consecutive runs switch layer
    k=0
    for r_i in range(len(runs)-1):
        vx,vy = runs[r_i][-1][0],runs[r_i][-1][1]
        place_via(vx,vy,net)
        out.append((vx,vy,vx,vy))
    return out

def tree_pts(newsegs):
    pts=[]
    for (x1,y1,x2,y2) in newsegs:
        L=math.hypot(x2-x1,y2-y1); m=max(1,int(L/0.35))
        for i in range(m+1): pts.append((x1+(x2-x1)*i/m,y1+(y2-y1)*i/m))
    return pts

netlist=sorted(todo.items(), key=lambda kv:len(kv[1]))
fail={}; t0=time.time()
ownpad_of_net=defaultdict(list)
for (x,y,w,h,n,num) in u1_padlist:
    if n: ownpad_of_net[n].append((x,y,w,h))
U1PAD=(0,0,0)  # sentinel
def is_u1pad(net,t):
    return any(abs(t[0]-x)<0.01 and abs(t[1]-y)<0.01 for x,y,_,_ in ownpad_of_net.get(net,[]))
for net,terms in netlist:
    tw = PWR_W if net in ('GND','+3V3_ESP') else SIG_W
    blks=blocked(net,tw)
    seen=set(); terms2=[]
    for p in terms:
        k=(round(p[0],3),round(p[1],3))
        if k not in seen: seen.add(k); terms2.append(p)
    # seed: a U1 pad if present else first terminal
    seed=next((t for t in terms2 if is_u1pad(net,t)),terms2[0])
    tree=[seed]; newsegs=[]
    remaining=[t for t in terms2 if t is not seed]
    # U1 pads first, then by nearest-to-tree
    remaining.sort(key=lambda t:0 if is_u1pad(net,t) else 1)
    ownpads=ownpad_of_net.get(net,[])+[(seed[0],seed[1],0.6,0.6)]
    while remaining:
        goals=tree+[(x,y,'B') for (x,y) in tree_pts(newsegs)]
        u1rem=[t for t in remaining if is_u1pad(net,t)]
        cand = u1rem if u1rem else remaining
        best=None
        for tt in cand:
            d=min((tt[0]-g[0])**2+(tt[1]-g[1])**2 for g in goals)
            if best is None or d<best[0]: best=(d,tt)
        tt=best[1]
        path=route(tt,goals,blks,ownpads+[(tt[0],tt[1],0.6,0.6)])
        if not path:
            fail.setdefault(net,[]).append(tt); remaining.remove(tt); continue
        for s in cellpath_to_segs(path,net,tw): newsegs.append(s)
        remaining.remove(tt); tree.append(tt)
    if net in fail: print(" PART",net,f"({len(terms2)-len(fail[net])}/{len(terms2)})")
    else: print(" ok",net,len(terms2),"term")
print(f"done; failed:{ {k:len(v) for k,v in fail.items()} }")

# retry pass for leftovers (fresh obstacles incl. new tracks)
still={}
for net,left in list(fail.items()):
    tw = PWR_W if net in ('GND','+3V3_ESP') else SIG_W
    blks=blocked(net,tw)
    goals=[(x,y,'B') for (x,y,n,l,w) in [(s[0],s[1],0,0,0) for s in []]]
    goals=[(s[0],s[1],'B') for s in [(0,0)] if False]
    goals=[]
    for (x1,y1,x2,y2,n,layer,w) in segs:
        if n==net and inside(x1,y1):
            L=math.hypot(x2-x1,y2-y1); m=max(1,int(L/0.4))
            for i in range(m+1): goals.append((x1+(x2-x1)*i/m,y1+(y2-y1)*i/m,'B'))
    for (x,y,n,w) in vias:
        if n==net and inside(x,y): goals.append((x,y,'B'))
    for tt in left:
        path=route(tt,goals,blks,ownpad_of_net.get(net,[])+[(tt[0],tt[1],0.6,0.6)])
        if path:
            for s in cellpath_to_segs(path,net,tw): pass
            print(" retry ok",net,tt[:2])
        else:
            still.setdefault(net,[]).append(tt); print(" retry FAIL",net,tt[:2])
fail=still
print(f"after retry:{ {k:len(v) for k,v in fail.items()} }")

# phase 3: via-at-terminal fixup — drop a via at/near each stranded terminal so it
# is reachable on L8, then re-route that terminal to the net's copper.
import itertools
final_fail={}
for net,left in fail.items():
    tw = PWR_W if net in ('GND','+3V3_ESP') else SIG_W
    for tt in left:
        tx,ty=tt[0],tt[1]
        is_stub = any(n==net and l==LF and (abs(x1-tx)<0.06 and abs(y1-ty)<0.06 or abs(x2-tx)<0.06 and abs(y2-ty)<0.06) for (x1,y1,x2,y2,n,l,w) in segs)
        maxrad = 0.15 if is_stub else 1.0
        blks=blocked(net,tw)
        placed=None
        for rad,ang in itertools.product([0.0,0.15,0.45,0.7,1.0],range(0,360,45)):
            if rad>maxrad: continue
            vx=tx+rad*math.cos(math.radians(ang)); vy=ty+rad*math.sin(math.radians(ang))
            if not inside(vx,vy): continue
            cx,cy=cell(vx,vy)
            if blks[2][cy*W+cx]: continue
            place_via(vx,vy,net)
            vias.append((vx,vy,net,VIA_W))
            terminals[net].append((vx,vy,'B'))
            rv=CLR+VIA_W; c0x,c0y=cell(vx-rv,vy-rv); c1x,c1y=cell(vx+rv,vy+rv)
            for cy_ in range(max(0,c0y),min(H,c1y+1)):
                for cx_ in range(max(0,c0x),min(W,c1x+1)):
                    px_,py_=cpos(cx_,cy_)
                    if (px_-vx)**2+(py_-vy)**2<=rv*rv: blks[2][cy_*W+cx_]=1
            placed=(vx,vy); break
        goals=[]
        for (x1,y1,x2,y2,n,layer,w) in segs:
            if n==net and inside(x1,y1):
                L=math.hypot(x2-x1,y2-y1); m=max(1,int(L/0.4))
                for i in range(m+1): goals.append((x1+(x2-x1)*i/m,y1+(y2-y1)*i/m,'B'))
        for (x,y,n,w) in vias:
            if n==net and inside(x,y): goals.append((x,y,'B'))
        if placed: goals.append((placed[0],placed[1],'B'))
        tgt = (tt[0],tt[1],'B') if is_stub else (tt[0],tt[1],LF)
        path=route(tgt,goals,blks,ownpad_of_net.get(net,[])+[(tt[0],tt[1],0.6,0.6)])
        if path:
            for s in cellpath_to_segs(path,net,tw): pass
            print(" fixup ok",net,round(tx,2),round(ty,2),"via@",placed)
        else:
            final_fail.setdefault(net,[]).append(tt); print(" fixup FAIL",net,round(tx,2),round(ty,2))
print(f"FINAL failed:{ {k:len(v) for k,v in final_fail.items()} }")
board.Save(OUT)
print("saved",OUT)
