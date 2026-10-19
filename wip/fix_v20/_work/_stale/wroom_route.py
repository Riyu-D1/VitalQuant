#!/usr/bin/env python3
# stage 2: route new U1 pads to nearest same-net copper inside RZ on F.Cu
import pcbnew, math, heapq, time
from collections import defaultdict

PCB = '/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/_work/stage1.kicad_pcb'
RZ = (1.2, -10.55, 24.2, 12.2)
MM = 1000000
GRID = 0.05
CLR = 0.10          # clearance margin vs other copper
SIG_W = 0.127       # signal track width
PWR_W = 0.25        # power track width

board = pcbnew.LoadBoard(PCB)
u1 = board.FindFootprintByReference('U1')

def inside(x,y,m=0.0):
    return RZ[0]-m<=x<=RZ[2]+m and RZ[1]-m<=y<=RZ[3]+m

# ---------- collect obstacles ----------
# keepout zones (all-layer): rect polys -> treat via polygon points
zones_block = []
for z in board.Zones():
    if z.GetIsRuleArea() or (hasattr(z,'GetIsKeepout') and z.GetIsKeepout()):
        layers = [l for l in range(64) if z.IsOnLayer(l)]
        if pcbnew.F_Cu in layers or len(layers)>10 or z.GetLayer()==pcbnew.F_Cu:
            pts=[]
            try:
                out = z.Outline().Outline(0)
                for i in range(out.PointCount()):
                    p=out.CPoint(i); pts.append((p.x/MM,p.y/MM))
            except Exception:
                try:
                    for i in range(z.GetNumCorners()):
                        c=z.GetCornerPosition(i); pts.append((c.x/MM,c.y/MM))
                except Exception: pass
            if pts: zones_block.append(pts)
print("keepout zones:",len(zones_block))

# F.Cu copper objects + other-layer objects for via blocking
segs=[]; vias=[]; pads=[]
for t in board.GetTracks():
    if isinstance(t, pcbnew.PCB_VIA):
        p=t.GetPosition(); vias.append((p.x/MM,p.y/MM,t.GetNetname(),t.GetDrill()/MM,t.GetWidth(pcbnew.F_Cu)/MM))
    else:
        s,e=t.GetStart(),t.GetEnd()
        segs.append((s.x/MM,s.y/MM,e.x/MM,e.y/MM,t.GetNetname(),t.GetLayer(),t.GetWidth()/MM))

for fp_ in board.GetFootprints():
    if fp_.GetReference()=='U1': continue
    for pad in fp_.Pads():
        if not pad.IsOnLayer(pcbnew.F_Cu): continue
        p=pad.GetPosition(); sz=pad.GetSize()
        pads.append((p.x/MM,p.y/MM,sz.x/MM,sz.y/MM,pad.GetNetname(),
                     pad.GetOrientationDegrees() if hasattr(pad,'GetOrientationDegrees') else 0))

u1pads = {}   # num -> (x,y,sx,sy,net)
for pad in u1.Pads():
    p=pad.GetPosition(); sz=pad.GetSize()
    u1pads[pad.GetNumber()] = (p.x/MM,p.y/MM,sz.x/MM,sz.y/MM,pad.GetNetname())

# F.Cu filled zones of other nets (pours) inside RZ - treat as blockers for other nets
fcu_pours=[]
for z in board.Zones():
    if z.GetIsRuleArea(): continue
    if z.GetLayer()==pcbnew.F_Cu:
        pts=[]
        try:
            out=z.Outline().Outline(0)
            for i in range(out.PointCount()):
                p=out.CPoint(i); pts.append((p.x/MM,p.y/MM))
        except Exception: pass
        if pts: fcu_pours.append((z.GetNetname(),pts))
print("fcu pours:",[(n,len(p)) for n,p in fcu_pours])

def poly_contains(pts,x,y):
    inside_=False; j=len(pts)-1
    for i in range(len(pts)):
        xi,yi=pts[i]; xj,yj=pts[j]
        if ((yi>y)!=(yj>y)) and (x < (xj-xi)*(y-yi)/(yj-yi+1e-12)+xi): inside_=not inside_
        j=i
    return inside_

def seg_dist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx, py-y1-t*dy)

# grid dims
gx0,gy0 = RZ[0],RZ[1]
W=int((RZ[2]-RZ[0])/GRID)+2; H=int((RZ[3]-RZ[1])/GRID)+2
def cell(x,y): return (int(round((x-gx0)/GRID)), int(round((y-gy0)/GRID)))
def cpos(cx,cy): return (gx0+cx*GRID, gy0+cy*GRID)

# ---------- anchors: endpoints+midpoints of same-net kept copper in RZ ----------
def anchors_for(net):
    pts=[]
    for (x1,y1,x2,y2,n,layer,w) in segs:
        if n!=net: continue
        if layer!=pcbnew.F_Cu: continue   # only F.Cu endpoints reachable on F.Cu
        L=math.hypot(x2-x1,y2-y1); n_=max(1,int(L/0.3))
        for i in range(n_+1):
            x,y=x1+(x2-x1)*i/n_, y1+(y2-y1)*i/n_
            if inside(x,y): pts.append((x,y))
    for (x,y,n,d,w) in vias:
        if n==net and inside(x,y): pts.append((x,y))
    for (x,y,sx,sy,n,rot) in pads:
        if n==net and inside(x,y): pts.append((x,y))
    return pts

def blocked(net, extra=0.0):
    """boolean grid for net `net`"""
    infl = CLR + SIG_W/2 + extra
    blk = bytearray(W*H)
    # rasterize: iterate objects, mark bbox
    def mark(cx,cy):
        if 0<=cx<W and 0<=cy<H: blk[cy*W+cx]=1
    def mark_disc(x,y,r):
        c0x,c0y=cell(x-r,y-r); c1x,c1y=cell(x+r,y+r)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if (px-x)**2+(py-y)**2<=r*r: mark(cx,cy)
    def mark_seg(x1,y1,x2,y2,r):
        c0x,c0y=cell(min(x1,x2)-r,min(y1,y2)-r); c1x,c1y=cell(max(x1,x2)+r,max(y1,y2)+r)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if seg_dist(px,py,x1,y1,x2,y2)<=r: mark(cx,cy)
    for pts in zones_block:
        mnx=min(p[0] for p in pts)-infl; mxx=max(p[0] for p in pts)+infl
        mny=min(p[1] for p in pts)-infl; mxy=max(p[1] for p in pts)+infl
        c0x,c0y=cell(mnx,mny); c1x,c1y=cell(mxx,mxy)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                # inside poly or within infl of boundary
                if poly_contains(pts,px,py):
                    mark(cx,cy)
                else:
                    hit=False
                    for i in range(len(pts)):
                        a=pts[i]; b=pts[(i+1)%len(pts)]
                        if seg_dist(px,py,a[0],a[1],b[0],b[1])<=infl: hit=True;break
                    if hit: mark(cx,cy)
    for (x1,y1,x2,y2,n,layer,w) in segs:
        if n==net: continue
        if layer!=pcbnew.F_Cu: continue
        mark_seg(x1,y1,x2,y2,infl+w/2)
    for (x,y,n,d,w) in vias:
        if n==net: continue
        if not inside(x,y,0.5): continue
        mark_disc(x,y,infl+w/2)
    for (x,y,sx,sy,n,rot) in pads:
        if n==net: continue
        r=math.hypot(sx,sy)/2
        mark_disc(x,y,infl+r*0.75)   # approx rotate
    for num,(x,y,sx,sy,n) in u1pads.items():
        if n==net: continue
        mark_seg(x-sx/2,y-sy/2,x+sx/2,y+sy/2,infl+0.45)  # rect pad approx: row/col pads are axis-aligned
    for (n,pts) in fcu_pours:
        if n==net: continue
        mnx=min(p[0] for p in pts)-infl; mxx=max(p[0] for p in pts)+infl
        mny=min(p[1] for p in pts)-infl; mxy=max(p[1] for p in pts)+infl
        c0x,c0y=cell(mnx,mny); c1x,c1y=cell(mxx,mxy)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)):
                px,py=cpos(cx,cy)
                if poly_contains(pts,px,py): mark(cx,cy)
    return blk

def route(start_xy, goals_xy, blk, ownpad=None):
    """Dijkstra w/ bend penalty. start_xy=(x,y) on pad; goals set of cells."""
    sx,sy=cell(*start_xy)
    goalset=set()
    for gx,gy in goals_xy:
        cx,cy=cell(gx,gy)
        for dy in range(-2,3):
            for dx in range(-2,3): goalset.add((cx+dx,cy+dy))
    # unblock own pad region
    if ownpad:
        ox,oy,osx,osy,on_=ownpad
        c0x,c0y=cell(ox-osx/2,oy-osy/2); c1x,c1y=cell(ox+osx/2,oy+osy/2)
        for cy in range(max(0,c0y),min(H,c1y+1)):
            for cx in range(max(0,c0x),min(W,c1x+1)): blk[cy*W+cx]=0
    INF=float('inf')
    dist={(sx,sy,-1):0.0}
    prev={}
    pq=[(0.0,sx,sy,-1)]
    found=None
    while pq:
        d,cx,cy,dirn=heapq.heappop(pq)
        if d>dist.get((cx,cy,dirn),INF): continue
        if (cx,cy) in goalset:
            found=(cx,cy,dirn); break
        for ndir,(dx,dy) in enumerate([(1,0),(-1,0),(0,1),(0,-1)]):
            nx,ny=cx+dx,cy+dy
            if not(0<=nx<W and 0<=ny<H): continue
            try:
                if blk[ny*W+nx]: continue
            except TypeError:
                print("BADIDX", repr(nx), repr(ny), "cx",repr(cx),"cy",repr(cy),"sx",repr(sx),"sy",repr(sy)); raise
            nd=d+1+(6 if dirn>=0 and dirn!=ndir else 0)
            st=(nx,ny,ndir)
            if nd<dist.get(st,INF):
                dist[st]=nd; prev[st]=(cx,cy,dirn); heapq.heappush(pq,(nd,nx,ny,ndir))
    if not found: return None
    path=[]
    st=found
    while st in prev:
        path.append(cpos(st[0],st[1])); st=prev[st]
    path.append(cpos(st[0],st[1]))
    path.reverse()
    # simplify: collapse collinear
    simp=[path[0]]
    for i in range(1,len(path)-1):
        ax,ay=simp[-1]; bx,by=path[i]; cx,cy=path[i+1]
        if abs((bx-ax)*(cy-ay)-(cx-ax)*(by-ay))>1e-9: simp.append((bx,by))
    simp.append(path[-1])
    return simp

def add_track(x1,y1,x2,y2,net,width,layer=pcbnew.F_Cu):
    t=pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I_MM(x1,y1)); t.SetEnd(pcbnew.VECTOR2I_MM(x2,y2))
    t.SetWidth(int(width*MM)); t.SetLayer(layer); t.SetNet(board.FindNet(net))
    board.Add(t)

TO_ROUTE = ['1','2','3','4','5','6','7','8','9','10','11','12','13','14','15',
            '17','18','19','20','21','22','23','27','31','32','33','34','35',
            '36','37','38','39','40']
# order: migrants/long runs first is unnecessary; sort by pad y so adjacent nets don't interleave oddly
blk_cache={}
routed=[]; failed=[]
t0=time.time()
for pnum in TO_ROUTE:
    x,y,sx,sy,net = u1pads[pnum]
    if not net:
        continue
    if net not in blk_cache:
        blk_cache[net]=blocked(net)
    blk = bytearray(blk_cache[net])   # copy so we can unblock own pad
    anchors = anchors_for(net)
    if not anchors:
        failed.append((pnum,net,'no anchors')); continue
    # choose up to 8 nearest anchors as goals
    anchors.sort(key=lambda a:(a[0]-x)**2+(a[1]-y)**2)
    goals=anchors[:8]
    w = PWR_W if net in ('GND','+3V3_ESP') else SIG_W
    path = route((x,y),goals,blk,ownpad=(x,y,sx,sy,net))
    if not path:
        failed.append((pnum,net,'no path')); continue
    path[0]=(x,y)
    # append exact anchor: nearest anchor to path end
    ex,ey=path[-1]
    anch=min(goals,key=lambda a:(a[0]-ex)**2+(a[1]-ey)**2)
    path.append(anch)
    for i in range(len(path)-1):
        add_track(path[i][0],path[i][1],path[i+1][0],path[i+1][1],net,w)
        segs.append((path[i][0],path[i][1],path[i+1][0],path[i+1][1],net,pcbnew.F_Cu,w))
    routed.append((pnum,net,len(path)))
    blk_cache[net]=blk   # persist own-pad unblock? no - revert: keep original
    blk_cache[net]=blocked.__wrapped__ if False else blk_cache[net]
print(f"routed {len(routed)} / {len(TO_ROUTE)} in {time.time()-t0:.0f}s")
print("failed:",failed)
for r in routed: print("  pad",r[0],r[1],f"{r[2]}pts")

# ---- EPAD vias: 4 vias at corner subpad centers -> In1 GND
gndnet = board.FindNet('GND')
for vx,vy in [(9.85,-2.44),(12.65,-2.44),(9.85,0.36),(12.65,0.36)]:
    v=pcbnew.PCB_VIA(board)
    v.SetPosition(pcbnew.VECTOR2I_MM(vx,vy))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetWidth(int(0.45*MM)); v.SetDrill(int(0.2*MM))
    v.SetNet(gndnet)
    board.Add(v)
print("epad vias placed")

board.Save(PCB)
print("saved",PCB)
