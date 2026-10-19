import pcbnew, math, heapq, re, sys
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
SRC=sys.argv[1] if len(sys.argv)>1 else BASE+'vitalq_v2.kicad_pcb'
DST=sys.argv[2] if len(sys.argv)>2 else SRC
ORIG=BASE+'_work/pre_promote_backup.kicad_pcb'
GRID=0.05; CLR_T=0.28      # track-to-anything edge gap
VIA_R=0.30; SIG_W=0.127; L_IN3=8

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

# original fingerprint set
ob=pcbnew.LoadBoard(ORIG)
orig=set()
for t in ob.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition(); orig.add(('v',round(p.x/1e5),round(p.y/1e5)))
    else:
        s,e=t.GetStart(),t.GetEnd()
        orig.add(('t',round(s.x/1e5),round(s.y/1e5),round(e.x/1e5),round(e.y/1e5)))
board=pcbnew.LoadBoard(SRC)

def fp_of(t):
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition(); return ('v',round(p.x/1e5),round(p.y/1e5))
    s,e=t.GetStart(),t.GetEnd(); return ('t',round(s.x/1e5),round(s.y/1e5),round(e.x/1e5),round(e.y/1e5))

tracks=[];vias=[];pads=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();vias.append((p.x/1e6,p.y/1e6,t.GetNetname(),t,fp_of(t) in orig))
    else:
        s,e=t.GetStart(),t.GetEnd()
        tracks.append((s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6,t.GetNetname(),t.GetLayer(),t,fp_of(t) in orig))
for m in board.GetFootprints():
    for pd in m.Pads():
        if pd.GetNetname() in ('','Net-'): continue
        b=pd.GetPosition()
        pads.append((b.x/1e6,b.y/1e6,pd.GetNetname(),pd.GetSize().x/1e6,pd.GetSize().y/1e6,m.GetReference(),pd.GetName()))
# pad line: use rotated size if needed (approx via bbox)
print('tracks',len(tracks),'vias',len(vias),'pads',len(pads))

# ---- 1) parse DRC report, delete NEW items involved in shorts/clearance ----
lines=open(BASE+'verify/drc_v20.rpt').read().split('\n')
bad_pts=[]   # positions of violations to kill new items
unconn_pts=[]
for i,l in enumerate(lines):
    m=re.match(r'\[([a-z_]+)\]: (.*)',l)
    if not m: continue
    typ=m.group(1); objs=[]
    for l2 in lines[i+1:i+6]:
        mm=re.match(r'\s*@\(([-\d.]+) mm,\s*([-\d.]+) mm\): (.*)',l2)
        if mm: objs.append((float(mm.group(1)),float(mm.group(2)),mm.group(3)))
        elif l2.strip()=='':break
    if typ in ('shorting_items','clearance','hole_clearance','hole_to_hole','tracks_crossing','items_not_allowed'):
        bad_pts.extend(objs)
    elif typ=='unconnected_items':
        unconn_pts.append(objs)

delset=set()
for x,y,desc in bad_pts:
    for k,(x1,y1,x2,y2,n,l,t,o) in enumerate(tracks):
        if o: continue
        if min(segdist(x,y,x1,y1,x2,y2),math.hypot(x-x1,y-y1),math.hypot(x-x2,y-y2))<0.12:
            delset.add(id(t))
    for vx,vy,n,t,o in vias:
        if not o and math.hypot(x-vx,y-vy)<0.45: delset.add(id(t))
# delete
nd=0
for t in list(board.GetTracks()):
    if id(t) in delset: board.Remove(t); nd+=1
print('deleted bad new items:',nd)
tracks=[r for r in tracks if id(r[6]) not in delset]
vias=[v for v in vias if id(v[3]) not in delset]

# ---- 2) connectivity union-find per net ----
# items: pads (point-ish), tracks (seg), vias (point)
parent={}
def find(a):
    while parent[a]!=a: parent[a]=parent[parent[a]];a=parent[a]
    return a
def union(a,b):
    ra,rb=find(a),find(b)
    if ra!=rb: parent[ra]=rb
seq=[0]
def nid():
    seq[0]+=1; return seq[0]-1
# index all copper items by net
net_items={}
def touch_pad(px,py,hx,hy, x1,y1,x2,y2):
    # does segment pass through pad bbox
    if x1==x2==px and min(py-hy,py+hy)<=y1<=max(py-hy,py+hy): pass
    if segdist(px,py,x1,y1,x2,y2) < max(hx,hy): return True
    # endpoints inside bbox
    if abs(x1-px)<hx and abs(y1-py)<hy: return True
    if abs(x2-px)<hx and abs(y2-py)<hy: return True
    return False

allsegs=[(r[0],r[1],r[2],r[3],r[4],r[5]) for r in tracks]
allpts=[(v[0],v[1],v[2],'via') for v in vias]
# build per-net
from collections import defaultdict
byseg=defaultdict(list); bypt=defaultdict(list); bypad=defaultdict(list)
for s in allsegs: byseg[s[4]].append(s)
for p in allpts: bypt[p[2]].append(p)
for p in pads: bypad[p[2]].append(p)

nets=sorted(set(list(byseg)+list(bypt)+list(bypad)))
disconnected={}
for net in nets:
    if not net or net.startswith('Net-'): continue
    items=[]; ids={}
    for s in byseg.get(net,[]):
        i=nid(); items.append(('s',)+s); ids[i]=i; parent[i]=i
    for p in bypt.get(net,[]):
        i=nid(); items.append(('p',p[0],p[1])); parent[i]=i
    for p in bypad.get(net,[]):
        i=nid(); items.append(('d',p[0],p[1],p[3]/2,p[4]/2,p[6])); parent[i]=i
    # union: O(n^2) but per net small
    n=len(items)
    for a in range(n):
        A=items[a]
        for b in range(a+1,n):
            B=items[b]
            if A[0]=='s' and B[0]=='s':
                if A[5]!=B[5]: continue  # different layer no touch (vias handle)
                # seg-seg touch if any endpoint pair close or crossing: approx endpoints
                if min(segdist(A[1],A[2],B[1],B[2],B[3],B[4]),segdist(A[3],A[4],B[1],B[2],B[3],B[4]),segdist(B[1],B[2],A[1],A[2],A[3],A[4]),segdist(B[3],B[4],A[1],A[2],A[3],A[4]))<0.08:
                    union(a,b)
            elif A[0]=='s' and B[0]=='p':
                if segdist(B[1],B[2],A[1],A[2],A[3],A[4])<0.42: union(a,b)
            elif A[0]=='p' and B[0]=='s':
                if segdist(A[1],A[2],B[1],B[2],B[3],B[4])<0.42: union(a,b)
            elif A[0]=='s' and B[0]=='d':
                if touch_pad(B[1],B[2],B[3],B[4],A[1],A[2],A[3],A[4]): union(a,b)
            elif A[0]=='d' and B[0]=='s':
                if touch_pad(A[1],A[2],A[3],A[4],B[1],B[2],B[3],B[4]): union(a,b)
            elif A[0]=='p' and B[0]=='p':
                if math.hypot(A[1]-B[1],A[2]-B[2])<0.7: union(a,b)
            elif A[0]=='d' and B[0]=='p':
                if math.hypot(A[1]-B[1],A[2]-B[2])<max(A[3],A[4])+0.42: union(a,b)
            elif A[0]=='p' and B[0]=='d':
                if math.hypot(A[1]-B[1],A[2]-B[2])<max(B[3],B[4])+0.42: union(a,b)
            elif A[0]=='d' and B[0]=='d':
                if abs(A[1]-B[1])<A[3]+B[3] and abs(A[2]-B[2])<A[4]+B[4]: union(a,b)
    groups=defaultdict(list)
    for i in range(len(items)): groups[find(i)].append(i)
    pads_in=[g for g in groups.values() if any(items[i][0]=='d' for i in g)]
    if len(pads_in)>1 or (pads_in and len(groups)>len(pads_in)):
        disconnected[net]=(items,groups)
print('disconnected nets:',{k:len(v[1]) for k,v in disconnected.items()})

# ---- 3) reroute disconnected clusters ----
def blocked(cx,cy,net,layer):
    for x1,y1,x2,y2,n,l in allsegs:
        if n==net: continue
        if l==layer and segdist(cx,cy,x1,y1,x2,y2)<CLR_T: return True
        if l in (0,2) and layer in (0,2): pass
    for vx,vy,n,t,o in vias:
        if n!=net and math.hypot(cx-vx,cy-vy)<VIA_R+0.17: return True
    for px,py,n,sx,sy,r,pd_ in pads:
        if n!=net and layer==0 and abs(cx-px)<sx/2+0.13 and abs(cy-py)<sy/2+0.13: return True
    return False
def via_ok(cx,cy,net):
    for x1,y1,x2,y2,n,l in allsegs:
        if n!=net and segdist(cx,cy,x1,y1,x2,y2)<VIA_R+0.165: return False
    for vx,vy,n,t,o in vias:
        if n!=net and math.hypot(cx-vx,cy-vy)<VIA_R*2+0.15: return False
    for px,py,n,sx,sy,r,pd_ in pads:
        if n!=net and math.hypot(cx-px,cy-py)<max(sx,sy)/2+VIA_R+0.16: return False
    return True
def astar(start,goals,net,layer,rad):
    def cell(x,y): return (round(x/GRID),round(y/GRID))
    openh=[(0,start[0],start[1])]; came={cell(*start):None}
    while openh:
        f,cx,cy=heapq.heappop(openh)
        for g in goals:
            if math.hypot(cx-g[0],cy-g[1])<GRID*1.5:
                path=[(cx,cy)];c=cell(cx,cy)
                while came[c]: path.append(came[c]);c=cell(*came[c])
                return path
        for dx,dy in ((GRID,0),(-GRID,0),(0,GRID),(0,-GRID),(GRID,GRID),(GRID,-GRID),(-GRID,GRID),(-GRID,-GRID)):
            nx,ny=cx+dx,cy+dy
            if math.hypot(nx-start[0],ny-start[1])>rad: continue
            if cell(nx,ny) in came: continue
            if blocked(nx,ny,net,layer): continue
            came[cell(nx,ny)]=(cx,cy)
            heapq.heappush(openh,(math.hypot(nx-goals[0][0],ny-goals[0][1])+math.hypot(nx-start[0],ny-start[1]),nx,ny))
    return None
def add_seg(x1,y1,x2,y2,net,layer):
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(x1*1e6),int(y1*1e6)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*1e6),int(y2*1e6)));t.SetWidth(int(SIG_W*1e6))
    t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t);allsegs.append((x1,y1,x2,y2,net,layer))
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(x*1e6),int(y*1e6)))
    v.SetWidth(int(0.6*1e6));v.SetDrill(int(0.3*1e6));v.SetNet(board.FindNet(net))
    v.SetLayerPair(0,2);board.Add(v);vias.append((x,y,net,v,False))

fixed=0
for net,(items,groups) in disconnected.items():
    gr=list(groups.values())
    # anchor: biggest group; connect each other group to it
    gr.sort(key=lambda g:-sum(1 for i in g if items[i][0]=='d'))
    main=gr[0]
    def gpoints(g):
        pts=[]
        for i in g:
            A=items[i]
            if A[0]=='s': pts+= [(A[1],A[2]),(A[3],A[4])]
            elif A[0] in ('p','d'): pts.append((A[1],A[2]))
        return pts
    for g in gr[1:]:
        sp=gpoints(g); tp=gpoints(main)
        # closest pair
        best=None
        for a in sp:
            for b in tp:
                d=math.hypot(a[0]-b[0],a[1]-b[1])
                if best is None or d<best[0]: best=(d,a,b)
        if not best: continue
        d,s0,t0=best
        ok=False
        for layer in (0,L_IN3):
            p=astar(s0,[t0]+tp,net,layer,max(6.0,d+3))
            if p:
                for i in range(len(p)-1): add_seg(p[i][0],p[i][1],p[i+1][0],p[i+1][1],net,layer)
                ok=True;break
        if not ok:
            # try via-assisted: via near s0, in3 to t0
            for rad in (1.0,2.0,3.0):
                placed=None
                for ix in range(int((s0[0]-rad)/GRID),int((s0[0]+rad)/GRID)+1):
                    for iy in range(int((s0[1]-rad)/GRID),int((s0[1]+rad)/GRID)+1):
                        cx,cy=ix*GRID,iy*GRID
                        if math.hypot(cx-s0[0],cy-s0[1])>rad or not via_ok(cx,cy,net): continue
                        placed=(cx,cy);break
                    if placed:break
                if placed:
                    p1=astar(s0,[placed],net,0,rad+0.5)
                    p2=astar(placed,[t0]+tp,net,L_IN3,8.0)
                    if p1 and p2:
                        for i in range(len(p1)-1): add_seg(p1[i][0],p1[i][1],p1[i+1][0],p1[i+1][1],net,0)
                        add_via(*placed,net)
                        for i in range(len(p2)-1): add_seg(p2[i][0],p2[i][1],p2[i+1][0],p2[i+1][1],net,L_IN3)
                        ok=True;break
        print(net,'group join', 'ok' if ok else 'FAIL', round(d,2))
        if ok:
            fixed+=1
            main=main+g
print('repaired joins:',fixed)
board.Save(DST)
print('saved',DST)
