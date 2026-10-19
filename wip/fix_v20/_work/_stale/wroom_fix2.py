#!/usr/bin/env python3
# wroom fix2 — operate on stage2 output; expanded grid beyond RZ so stragglers
# can wrap around obstacle walls; via-bridge at each stranded terminal.
import pcbnew, math, heapq, time, itertools
from collections import defaultdict

SRC='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/_work/stage2.kicad_pcb'
OUT='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/_work/stage3.kicad_pcb'
RZ=(1.2,-10.55,24.2,12.2)
GX=(-0.8,-12.55,26.7,14.7)   # expanded grid
MM=1000000; GRID=0.05; CLR=0.10; SIG_W=0.127; PWR_W=0.25; VIA_W,VIA_D=0.45,0.2
LF,LI=0,8
board=pcbnew.LoadBoard(SRC)

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

STRANDED={
 'CHG_DIS':[(20.204964,12.200067729323308)],
 'VBAT':[(24.200695380952382,5.174294619047619)],
 'AD5940_GPIO0':[(24.20169947368421,6.508469235588972)],
 'MIC_CLK':[(24.20190823809524,6.600580619047619)],
 'CE_ISO':[(16.909022255639098,12.200802255639099)],
 'Q3_B2':[(11.49,10.5)],
 'ESP_RX':[(15.99,10.7)],
 'ESP_EN':[(5.779258,10.010631),(8.122568,12.20194426065163),(19.111314759398496,12.200267759398496),(15.698920273182956,12.201812726817042)],
 'USB_DM':[(18.731552794486216,12.201530205513784),(22.552037,12.200958551378447),(24.20178319799499,7.795112829573935),(24.201141586466164,-0.23505655639097744)],
 'USB_DP':[(24.201285952380953,10.23295),(11.020797095238096,12.201614904761904),(24.199882989974938,-0.5671099899749373)],
 'GND':[(21.65572,11.887806)],
}

segs=[];vias=[];pads=[];zones_block=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition(); vias.append((p.x/MM,p.y/MM,t.GetNetname(),t.GetWidth(LF)/MM))
    else:
        s,e=t.GetStart(),t.GetEnd()
        segs.append((s.x/MM,s.y/MM,e.x/MM,e.y/MM,t.GetNetname(),t.GetLayer(),t.GetWidth()/MM))
for fp_ in board.GetFootprints():
    for pad in fp_.Pads():
        if not (pad.IsOnLayer(LF) or pad.IsOnLayer(2)): continue
        p=pad.GetPosition(); sz=pad.GetSize(); r=int(pad.GetOrientationDegrees())%180
        w,h=(sz.x/MM,sz.y/MM) if r==0 else (sz.y/MM,sz.x/MM)
        pads.append((p.x/MM,p.y/MM,w,h,pad.GetNetname(),fp_.GetReference()))
for z in board.Zones():
    if not z.GetIsRuleArea() or not z.GetDoNotAllowTracks(): continue
    lf=z.IsOnLayer(LF); li=z.IsOnLayer(LI)
    pts=[]
    try:
        out=z.Outline().Outline(0)
        for i in range(out.PointCount()):
            p=out.CPoint(i); pts.append((p.x/MM,p.y/MM))
    except Exception: pass
    if pts: zones_block.append((pts,lf,li))
zones_block.append(([(3.75,-10.3),(21.75,-10.3),(21.75,-16.25),(3.75,-16.25)],True,True))

gx0,gy0=GX[0],GX[1]
W=int((GX[2]-GX[0])/GRID)+2; H=int((GX[3]-GX[1])/GRID)+2
def cell(x,y): return (int(round((x-gx0)/GRID)),int(round((y-gy0)/GRID)))
def cpos(cx,cy): return (gx0+cx*GRID,gy0+cy*GRID)
def poly_contains(pts,x,y):
    ins=False; j=len(pts)-1
    for i in range(len(pts)):
        xi,yi=pts[i]; xj,yj=pts[j]
        if ((yi>y)!=(yj>y)) and (x<(xj-xi)*(y-yi)/(yj-yi+1e-12)+xi): ins=not ins
        j=i
    return ins

def blocked(net,tw):
    infl=CLR+tw/2; viainfl=CLR+VIA_W/2
    blkF=bytearray(W*H); blkI=bytearray(W*H); blkV=bytearray(W*H)
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
    for (pts,lf,li) in zones_block:
        if lf: mark_poly(blkF,pts)
        if li: mark_poly(blkI,pts)
        mark_poly(blkV,pts)
    for (x1,y1,x2,y2,n,layer,w) in segs:
        if n==net: continue
        if layer==LF: mark_seg(blkF,x1,y1,x2,y2,infl+w/2)
        elif layer==LI: mark_seg(blkI,x1,y1,x2,y2,infl+w/2)
        mark_seg(blkV,x1,y1,x2,y2,viainfl+w/2)
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
    for (x,y,sxx,syy,n,ref) in pads:
        if n==net: continue
        mark_rect(blkF,x,y,sxx/2,syy/2,infl)
        mark_rect(blkV,x,y,sxx/2,syy/2,viainfl)
    return blkF,blkI,blkV

def route(start_xyz,goalset_pts,blks,ownpads):
    blkF,blkI,blkV=blks
    sx,sy=cell(start_xyz[0],start_xyz[1]); sl=1 if start_xyz[2]==LI else 0
    goalset=set()
    for gx,gy,gl in goalset_pts:
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
    if start_xyz[2]=='B':
        dist[(sx,sy,1-sl,-1)]=0.0; heapq.heappush(pq,(0.0,sx,sy,1-sl,-1))
    found=None
    while pq:
        d,cx,cy,cl,dirn=heapq.heappop(pq)
        if d>dist.get((cx,cy,cl,dirn),INF): continue
        if (cx,cy,cl) in goalset: found=(cx,cy,cl,dirn); break
        blk = bF if cl==0 else bI
        for ndir,(dx,dy) in enumerate([(1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)]):
            nx,ny=cx+dx,cy+dy
            if not(0<=nx<W and 0<=ny<H) or blk[ny*W+nx]: continue
            if dx and dy and (blk[cy*W+nx] or blk[ny*W+cx]): continue
            nd=d+(1.42 if (dx and dy) else 1)+(6 if dirn>=0 and dirn!=ndir else 0)
            st=(nx,ny,cl,ndir)
            if nd<dist.get(st,INF):
                dist[st]=nd; prev[st]=(cx,cy,cl,dirn); heapq.heappush(pq,(nd,nx,ny,cl,ndir))
        nl=1-cl
        tb=bF if nl==0 else bI
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

def place_via(x,y,net):
    v=pcbnew.PCB_VIA(board); v.SetPosition(pcbnew.VECTOR2I_MM(x,y))
    v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetWidth(int(VIA_W*MM)); v.SetDrill(int(VIA_D*MM))
    v.SetNet(board.FindNet(net) if net else None); board.Add(v)
    vias.append((x,y,net,VIA_W)); return v

def add_track(x1,y1,x2,y2,net,w,layer):
    t=pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I_MM(x1,y1)); t.SetEnd(pcbnew.VECTOR2I_MM(x2,y2))
    t.SetWidth(int(w*MM)); t.SetLayer(layer); t.SetNet(board.FindNet(net))
    board.Add(t); segs.append((x1,y1,x2,y2,net,layer,w))

def emit(path,net,tw):
    pts=[(cpos(p[0],p[1])[0],cpos(p[0],p[1])[1],p[2]) for p in path]
    runs=[]; cur=[pts[0]]
    for p in pts[1:]:
        if p[2]!=cur[-1][2]: runs.append(cur); cur=[p]
        else: cur.append(p)
    runs.append(cur)
    for run in runs:
        simp=[run[0]]
        for i in range(1,len(run)-1):
            ax,ay,_=simp[-1]; bx,by,_=run[i]; cx2,cy2,_=run[i+1]
            if abs((bx-ax)*(cy2-ay)-(cx2-ax)*(by-ay))>1e-9: simp.append(run[i])
        simp.append(run[-1])
        lay=LF if run[0][2]==0 else LI
        for i in range(len(simp)-1):
            add_track(simp[i][0],simp[i][1],simp[i+1][0],simp[i+1][1],net,tw,lay)
    for r_i in range(len(runs)-1):
        place_via(runs[r_i][-1][0],runs[r_i][-1][1],net)

def net_goals(net):
    g=[]
    for (x1,y1,x2,y2,n,layer,w) in segs:
        if n!=net: continue
        L=math.hypot(x2-x1,y2-y1); m=max(1,int(L/0.4))
        for i in range(m+1):
            px,py=x1+(x2-x1)*i/m,y1+(y2-y1)*i/m
            g.append((px,py,'B' if layer!=LF else LF))
    for (x,y,n,w) in vias:
        if n==net: g.append((x,y,'B'))
    return g

fail={}
for net,terms in STRANDED.items():
    tw=PWR_W if net in ('GND','+3V3_ESP') else SIG_W
    for tt in terms:
        tx,ty=tt
        is_stub=any(n==net and l==LF and (abs(x1-tx)<0.06 and abs(y1-ty)<0.06 or abs(x2-tx)<0.06 and abs(y2-ty)<0.06) for (x1,y1,x2,y2,n,l,w) in segs)
        is_pad=not is_stub
        blks=blocked(net,tw)
        placed=None
        maxrad=0.15 if is_stub else 1.8
        for rad,ang in itertools.product([0.0,0.15,0.45,0.7,1.0,1.4,1.8],[0,45,90,135,180,225,270,315]):
            if rad>maxrad: continue
            vx=tx+rad*math.cos(math.radians(ang)); vy=ty+rad*math.sin(math.radians(ang))
            cx,cy=cell(vx,vy)
            if not(0<=cx<W and 0<=cy<H): continue
            if blks[2][cy*W+cx]: continue
            place_via(vx,vy,net); placed=(vx,vy); break
        goals=net_goals(net)
        if placed: goals.append((placed[0],placed[1],'B'))
        path=route((tx,ty,'B' if is_stub else LF),goals,blks,[(tx,ty,0.6,0.6)])
        if path:
            emit(path,net,tw); print(" ok",net,round(tx,2),round(ty,2),"via@",placed)
        else:
            fail.setdefault(net,[]).append(tt); print(" FAIL",net,round(tx,2),round(ty,2))
print("remaining:",{k:len(v) for k,v in fail.items()})
board.Save(OUT); print("saved",OUT)
