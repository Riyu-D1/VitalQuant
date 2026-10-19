import pcbnew, math, heapq, re, sys, os
from collections import defaultdict
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
SRC=sys.argv[1]; DST=sys.argv[2]
ORIG=BASE+'_work/pre_promote_backup.kicad_pcb'
GRID=0.05; SIG_W=0.127; PWR_W=0.25
CLR=0.24; DIL=int(math.ceil(CLR/GRID))
VIA_R=0.32; VDIL=int(math.ceil((VIA_R+0.18)/GRID))
LAYERS=(0,8,2)   # F.Cu, In3, B.Cu
LNAME={0:'F.Cu',8:'In3',2:'B.Cu'}

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

# --- wipe NEW tracks/vias ---
ob=pcbnew.LoadBoard(ORIG); orig=set()
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
trk=list(board.GetTracks())
nd=0
for t in trk:
    if isinstance(t,pcbnew.PCB_VIA): continue
    if fp_of(t) not in orig:
        try: board.Remove(t); nd+=1
        except Exception: pass
print('wiped',nd)

# --- rasterize remaining geometry ---
occ={}   # (layer,ix,iy)->set(nets)  diluted copper occupancy
vocc={}  # (ix,iy)->set(nets)        via-exclusion
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
                for l in range(30): mark(occ,l,ix+dx,iy+dy,net)
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
def inside_poly(px,py,pts):
    c=False;j=len(pts)-1
    for i in range(len(pts)):
        xi,yi=pts[i];xj,yj=pts[j]
        if ((yi>py)!=(yj>py)) and (px<(xj-xi)*(py-yi)/(yj-yi)+xi): c=not c
        j=i
    return c

allsegs=[];allvias=[]
for t in trk:
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();allvias.append((p.x/1e6,p.y/1e6,t.GetNetname()));ras_via(p.x/1e6,p.y/1e6,t.GetNetname())
    else:
        if fp_of(t) not in orig: continue
        s,e=t.GetStart(),t.GetEnd()
        allsegs.append((s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6,t.GetNetname(),t.GetLayer()))
        ras_seg(s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6,t.GetNetname(),t.GetLayer())
for m in board.GetFootprints():
    for pd in m.Pads():
        nn=pd.GetNetname()
        if nn in ('','Net-'): nn='__NC_%s_%s'%(m.GetReference(),str(pd.GetName()))
        b=pd.GetPosition(); bb=pd.GetBoundingBox()
        pl=2 if pd.IsFlipped() else 0
        try:
            ls=pd.GetLayerSet()
            pl=2 if (ls.Contains(2) and not ls.Contains(0)) else 0
        except Exception: pass
        ras_pad((bb.GetX()+bb.GetWidth()/2)/1e6,(bb.GetY()+bb.GetHeight()/2)/1e6,
                bb.GetWidth()/1e6,bb.GetHeight()/1e6,nn,pl)
# zones: keepouts block; filled pours occupy their net
for z in board.Zones():
    zn=z.GetNetname() or '__KO__'
    outs=z.Outline()
    if outs is None: continue
    isko=False
    try: isko=z.GetDoNotAllowTracks()
    except Exception: isko=False
    for li in LAYERS:
        if not z.IsOnLayer(li): continue
        # collect all outline pts
        for pi in range(outs.OutlineCount()):
            ol=outs.COutline(pi)
            pts=[(ol.CPoint(i).x/1e6,ol.CPoint(i).y/1e6) for i in range(ol.PointCount())]
            if not pts: continue
            xs=[p[0] for p in pts];ys=[p[1] for p in pts]
            if isko:
                for ix in range(int(min(xs)/GRID),int(max(xs)/GRID)+1):
                    for iy in range(int(min(ys)/GRID),int(max(ys)/GRID)+1):
                        if inside_poly(ix*GRID,iy*GRID,pts): mark(occ,li,ix,iy,'__KO__')
# filled zones occupancy on routing layers
for z in board.Zones():
    zn=z.GetNetname()
    if not zn: continue
    for li in LAYERS:
        try:
            if not z.HasFilledPolysForLayer(li): continue
            fp=z.GetFilledPolysList(li)
        except Exception: continue
        if fp is None: continue
        for pi in range(fp.OutlineCount()):
            ol=fp.COutline(pi)
            pts=[(ol.CPoint(i).x/1e6,ol.CPoint(i).y/1e6) for i in range(ol.PointCount())]
            if len(pts)<3: continue
            xs=[p[0] for p in pts];ys=[p[1] for p in pts]
            x0=max(min(xs),-5.0);x1=min(max(xs),50.0)
            y0=max(min(ys),-15.0);y1=min(max(ys),20.0)
            for ix in range(int(x0/GRID),int(x1/GRID)+1):
                for iy in range(int(y0/GRID),int(y1/GRID)+1):
                    if inside_poly(ix*GRID,iy*GRID,pts): mark(occ,li,ix,iy,zn)

def blk(ix,iy,net,layer):
    ns=occ.get((layer,ix,iy))
    return bool(ns) and (ns!={net})
VD2=6
def vblk(ix,iy,net):
    ns=vocc.get((ix,iy))
    if ns and ns!={net}: return True
    for l2 in range(30):
        for dx in range(-VD2,VD2+1):
            for dy in range(-VD2,VD2+1):
                if dx*dx+dy*dy>VD2*VD2: continue
                ns=occ.get((l2,ix+dx,iy+dy))
                if ns and ns!={net}: return True
    return False

# bridge vias at dangling inner-layer stub endpoints (dead ends not on routing layers)
usecount=defaultdict(int)
for x1,y1,x2,y2,net,l in allsegs:
    usecount[(net,l,round(x1,3),round(y1,3))]+=1
    usecount[(net,l,round(x2,3),round(y2,3))]+=1
nbv=0
for x1,y1,x2,y2,net,l in allsegs:
    if l in LAYERS: continue
    for (ex,ey) in ((x1,y1),(x2,y2)):
        if usecount.get((net,l,round(ex,3),round(ey,3)),0)>1: continue
        ix,iy=round(ex/GRID),round(ey/GRID)
        ns=vocc.get((ix,iy))
        if ns and net in ns: continue
        # full-disc via check on every copper layer
        clear=True
        for l2 in range(30):
            ok=True
            for dx in range(-VDIL,VDIL+1):
                for dy in range(-VDIL,VDIL+1):
                    if dx*dx+dy*dy>VDIL*VDIL: continue
                    ons=occ.get((l2,ix+dx,iy+dy))
                    if ons and ons!={net}: ok=False;break
                if not ok: break
            if not ok: clear=False;break
        if not clear: continue
        v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(ex*1e6),int(ey*1e6)))
        v.SetWidth(int(0.6*1e6));v.SetDrill(int(0.3*1e6))
        v.SetNet(board.FindNet(net));v.SetLayerPair(0,2);board.Add(v)
        ras_via(ex,ey,net);allvias.append((ex,ey,net));nbv+=1
print('bridge vias added:',nbv)
print('occ',len(occ),'vocc',len(vocc))

# --- components per net via BFS over occ cells ---
netcells=defaultdict(set)
for (l,ix,iy),ns in occ.items():
    for n in ns:
        if not n.startswith('__'): netcells[n].add((l,ix,iy))
# vias join layers: vocc cell marks all-layer; find via cells
viacells=defaultdict(list)
for (ix,iy),ns in vocc.items():
    for n in ns: viacells[n].append((ix,iy))
comps={}
for n,cells in netcells.items():
    seen=set(); comp=[]
    for c0 in cells:
        if c0 in seen: continue
        q=[c0]; seen.add(c0); grp=[]
        while q:
            l,x,y=q.pop(); grp.append((l,x,y))
            for dx in (-1,0,1):
                for dy in (-1,0,1):
                    nc=(l,x+dx,y+dy)
                    if nc in cells and nc not in seen: seen.add(nc);q.append(nc)
            # layer link at via cells: same (x,y) ANY layer if net n via here
            ns=vocc.get((x,y))
            if ns and n in ns:
                for l2 in range(30):
                    nc=(l2,x,y)
                    if nc in cells and nc not in seen: seen.add(nc);q.append(nc)
        comp.append(grp)
    comps[n]=comp

def astar3(s0,goalcells,net,rad,center):
    si=(round(s0[0]/GRID),round(s0[1]/GRID),s0[2])
    openh=[(0.0,0,si)]; came={si:None}; ctr=0
    g0=center
    while openh:
        f,_,c=heapq.heappop(openh)
        if (c[0],c[1],c[2]) in goalcells or (c[0],c[1]) in goalcells:
            path=[c]
            while came[c]: c=came[c];path.append(c)
            return path[::-1]
        for dx,dy in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)):
            nc=(c[0]+dx,c[1]+dy,c[2])
            if nc in came: continue
            if math.hypot(nc[0]*GRID-center[0],nc[1]*GRID-center[1])>rad: continue
            if blk(nc[0],nc[1],net,nc[2]): continue
            came[nc]=c; ctr+=1
            h=math.hypot(nc[0]*GRID-g0[0],nc[1]*GRID-g0[1])
            g=math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1])
            heapq.heappush(openh,(g+h+0.02,ctr,nc))
        for l2 in LAYERS:
            if l2==c[2]: continue
            if blk(c[0],c[1],net,l2) or vblk(c[0],c[1],net): continue
            nc=(c[0],c[1],l2)
            if nc in came: continue
            came[nc]=c; ctr+=1
            h=math.hypot(nc[0]*GRID-g0[0],nc[1]*GRID-g0[1])
            g=math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1])
            heapq.heappush(openh,(g+h+0.8,ctr,nc))
    return None

def add_seg(x1,y1,x2,y2,net,layer):
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(x1*1e6),int(y1*1e6)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*1e6),int(y2*1e6)))
    t.SetWidth(int((PWR_W if net.startswith('+') or net in('VBAT','VBUS','GND') else SIG_W)*1e6))
    t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t);ras_seg(x1,y1,x2,y2,net,layer)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(x*1e6),int(y*1e6)))
    v.SetWidth(int(0.6*1e6));v.SetDrill(int(0.3*1e6));v.SetNet(board.FindNet(net))
    v.SetLayerPair(0,2);board.Add(v);ras_via(x,y,net)

def emit(path,net):
    path=[(x,y,l if l in LAYERS else 0) for x,y,l in path]
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
    for i in range(len(path)-1):
        if path[i][2]!=path[i+1][2]:
            add_via(path[i][0]*GRID,path[i][1]*GRID,net)

# --- route: for each net with >1 component, connect components pairwise ---
tot_ok=0; tot_fail=[]
order=sorted(comps.items(), key=lambda kv: len(kv[1]))
for net,comp in order:
    if net.startswith('__') or len(comp)<2: continue
    # sort components by size desc; anchor = largest
    comp=sorted(comp,key=len,reverse=True)
    anchor=set((ix,iy) for l,ix,iy in comp[0])
    for g in comp[1:]:
        # representative start = first cell, goal = anchor cells (any layer)
        gset=set((ix,iy) for l,ix,iy in comp[0]) | anchor
        # pick start cell nearest anchor centroid
        axc=sum(i for i,_ in gset)/len(gset); ayc=sum(j for _,j in gset)/len(gset)
        cells=[(ix,iy,l) for l,ix,iy in g if l in LAYERS]
        if not cells: cells=[(ix,iy,l) for l,ix,iy in g]  # inner-only: start anyway
        s=min(cells,key=lambda c:math.hypot(c[0]*GRID-axc,c[1]*GRID-ayc))
        l0=s[2]
        xs=[p[0] for p in gset];ys=[p[1] for p in gset]
        cx,cy=sum(xs)/len(xs),sum(ys)/len(ys)
        d=max(math.hypot(s[0]-ix,s[1]-iy) for ix,iy in gset)
        p=astar3((s[0]*GRID,s[1]*GRID,s[2]),gset,net,max(14.0,d*GRID*3+6),(cx*GRID,cy*GRID))
        if p:
            emit(p,net)
            # merge: add path cells to anchor & occ already updated by ras
            for c in p: anchor.add((c[0],c[1]))
            tot_ok+=1
        else:
            tot_fail.append((net,s[2],s[0]*GRID,s[1]*GRID,len(g)))
print('joined',tot_ok,'fail',len(tot_fail))
from collections import Counter
print(Counter(f[0] for f in tot_fail))
for f in tot_fail[:40]: print(' FAIL',f)
board.Save(DST);print('saved',DST)
