import pcbnew, math, heapq, re, sys, os
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
SRC=sys.argv[1]; DST=sys.argv[2]; RPT=sys.argv[3]
ORIG=BASE+'_work/pre_promote_backup.kicad_pcb'
GRID=0.05; SIG_W=0.127; L_IN3=8; L_B=2
CLR=0.235           # center-to-center margin for same-width tracks
DIL=int(math.ceil(CLR/GRID))          # ~4 cells
VIA_R=0.30; VDIL=int(math.ceil((VIA_R+0.18)/GRID))  # ~10 cells

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

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

lines=open(RPT).read().split('\n')
bad=[]; unconn=[]
for i,l in enumerate(lines):
    m=re.match(r'\[([a-z_]+)\]: (.*)',l)
    if not m: continue
    typ=m.group(1); objs=[]
    for l2 in lines[i+1:i+8]:
        mm=re.match(r'\s*@\(([-\d.]+) mm,\s*([-\d.]+) mm\): (.*)',l2)
        if mm: objs.append((float(mm.group(1)),float(mm.group(2)),mm.group(3)))
        elif l2.strip()=='': break
    if typ in ('shorting_items','clearance','hole_clearance','hole_to_hole','tracks_crossing','items_not_allowed'):
        bad+=objs
    elif typ=='unconnected_items':
        unconn.append(objs)
print('bad',len(bad),'unconn',len(unconn))

# phase1: delete new items at bad points
if os.environ.get('PHASE')!='2':
    delset=set()
    for t in board.GetTracks():
        if fp_of(t) in orig: continue
        if isinstance(t,pcbnew.PCB_VIA):
            p=t.GetPosition(); px,py=p.x/1e6,p.y/1e6
            for x,y,d in bad:
                if math.hypot(x-px,y-py)<0.5: delset.add(t.m_Uuid.AsString()); break
        else:
            s,e=t.GetStart(),t.GetEnd(); x1,y1,x2,y2=s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6
            for x,y,d in bad:
                if segdist(x,y,x1,y1,x2,y2)<0.15: delset.add(t.m_Uuid.AsString()); break
    for t in list(board.GetTracks()):
        try:
            if t.m_Uuid.AsString() in delset: board.Remove(t)
        except Exception: pass
    board.Save(DST); print('deleted',len(delset),'->',DST); sys.exit(0)

# ---- rasterize ----
occ={}; vocc={}
def mark(mp,l,ix,iy,n): mp.setdefault((l,ix,iy),set()).add(n)
def ras_seg(x1,y1,x2,y2,net,layer):
    L=math.hypot(x2-x1,y2-y1); n=max(1,int(L/GRID)+1)
    for i in range(n+1):
        x=x1+(x2-x1)*i/n; y=y1+(y2-y1)*i/n
        ix,iy=round(x/GRID),round(y/GRID)
        for dx in range(-DIL,DIL+1):
            for dy in range(-DIL,DIL+1): mark(occ,layer,ix+dx,iy+dy,net)
def ras_via(x,y,net):
    ix,iy=round(x/GRID),round(y/GRID)
    for dx in range(-VDIL,VDIL+1):
        for dy in range(-VDIL,VDIL+1):
            if dx*dx+dy*dy<=VDIL*VDIL:
                vocc.setdefault((ix+dx,iy+dy),set()).add(net)
    for l in (0,L_B,L_IN3):
        for dx in range(-VDIL,VDIL+1):
            for dy in range(-VDIL,VDIL+1):
                if dx*dx+dy*dy<=VDIL*VDIL: mark(occ,l,ix+dx,iy+dy,net)
def ras_pad(x,y,sx,sy,net):
    hx=sx/2+CLR; hy=sy/2+CLR
    for ix in range(int((x-hx)/GRID),int((x+hx)/GRID)+1):
        for iy in range(int((y-hy)/GRID),int((y+hy)/GRID)+1):
            cx,cy=ix*GRID,iy*GRID
            if abs(cx-x)<hx and abs(cy-y)<hy: mark(occ,0,ix,iy,net)
    hx2=max(sx,sy)/2+VIA_R+0.18
    for ix in range(int((x-hx2)/GRID),int((x+hx2)/GRID)+1):
        for iy in range(int((y-hx2)/GRID),int((y+hx2)/GRID)+1):
            cx,cy=ix*GRID,iy*GRID
            if math.hypot(cx-x,cy-y)<hx2: vocc.setdefault((ix,iy),set()).add(net)

for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition(); ras_via(p.x/1e6,p.y/1e6,t.GetNetname())
    else:
        s,e=t.GetStart(),t.GetEnd()
        ras_seg(s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6,t.GetNetname(),t.GetLayer())
for m in board.GetFootprints():
    for pd in m.Pads():
        nn=pd.GetNetname()
        if nn in ('','Net-'): nn='__NONET_%s_%s'%(m.GetReference(),str(pd.GetName()))
        b=pd.GetPosition()
        ras_pad(b.x/1e6,b.y/1e6,pd.GetSize().x/1e6,pd.GetSize().y/1e6,nn)
# keepout zones that forbid tracks
for z in board.Zones():
    try:
        if not z.GetDoNotAllowTracks(): continue
    except Exception: continue
    outs=z.Outline()
    if outs is None: continue
    for li in (0,L_B,L_IN3):
        if not z.IsOnLayer(li): continue
        for pi in range(outs.OutlineCount()):
            ol=outs.COutline(pi)
            pts=[(ol.CPoint(i).x/1e6,ol.CPoint(i).y/1e6) for i in range(ol.PointCount())]
            if not pts: continue
            xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
            def inside(px,py,pts):
                c=False;j=len(pts)-1
                for i in range(len(pts)):
                    xi,yi=pts[i];xj,yj=pts[j]
                    if ((yi>py)!=(yj>py)) and (px<(xj-xi)*(py-yi)/(yj-yi)+xi): c=not c
                    j=i
                return c
            for ix in range(int(min(xs)/GRID),int(max(xs)/GRID)+1):
                for iy in range(int(min(ys)/GRID),int(max(ys)/GRID)+1):
                    if inside(ix*GRID,iy*GRID,pts): mark(occ,li,ix,iy,'__KO__')
print('occ',len(occ),'vocc',len(vocc))

def blk(ix,iy,net,layer):
    ns=occ.get((layer,ix,iy))
    return bool(ns) and ns!={net}
def vblk(ix,iy,net):
    ns=vocc.get((ix,iy))
    if ns and ns!={net}: return True
    for l in (0,L_B,L_IN3):
        ns=occ.get((l,ix,iy))
        if ns and '__KO__' in ns: return True
    return False

LAYERS=(0,L_IN3,L_B)
def astar3(s0,goals,net,rad):
    # s0:(x,y,l_hint or -1 any), goals: list of (x,y) any layer allowed
    gcells={}
    for g in goals:
        gcells[(round(g[0]/GRID),round(g[1]/GRID))]=True
    si=(round(s0[0]/GRID),round(s0[1]/GRID),s0[2] if len(s0)>2 else 0)
    openh=[(0.0,0,si)]; came={si:None}; ctr=0
    g0=goals[0]
    while openh:
        f,_,c=heapq.heappop(openh)
        if (c[0],c[1]) in gcells:
            path=[c]
            while came[c]: c=came[c];path.append(c)
            return path
        for dx,dy in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)):
            nc=(c[0]+dx,c[1]+dy,c[2])
            if nc in came: continue
            if math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1])>rad: continue
            if blk(nc[0],nc[1],net,c[2]): continue
            came[nc]=c; ctr+=1
            heapq.heappush(openh,(math.hypot(nc[0]*GRID-g0[0],nc[1]*GRID-g0[1])+math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1])+0.02,ctr,nc))
        # via transitions
        for l2 in LAYERS:
            if l2==c[2]: continue
            if blk(c[0],c[1],net,l2) or vblk(c[0],c[1],net): continue
            nc=(c[0],c[1],l2)
            if nc in came: continue
            came[nc]=c; ctr+=1
            heapq.heappush(openh,(math.hypot(nc[0]*GRID-g0[0],nc[1]*GRID-g0[1])+math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1])+0.8,ctr,nc))
    return None

def add_seg(x1,y1,x2,y2,net,layer):
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(x1*1e6),int(y1*1e6)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*1e6),int(y2*1e6)));t.SetWidth(int(SIG_W*1e6))
    t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t);ras_seg(x1,y1,x2,y2,net,layer)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(x*1e6),int(y*1e6)))
    v.SetWidth(int(0.6*1e6));v.SetDrill(int(0.3*1e6));v.SetNet(board.FindNet(net))
    v.SetLayerPair(0,2);board.Add(v);ras_via(x,y,net)

def emit(path,net):
    # path: list of (ix,iy,l) cells start->...->goal (reversed already? ours appends backwards)
    if len(path)<2:
        return
    # group contiguous same-layer runs -> segments between direction changes
    runs=[]; cur=[path[0]]
    for p in path[1:]:
        if p[2]==cur[-1][2]: cur.append(p)
        else: runs.append(cur); cur=[p]
    runs.append(cur)
    for r in runs:
        pts=[(c[0]*GRID,c[1]*GRID) for c in r]
        # simplify collinear
        simp=[pts[0]]
        for i in range(1,len(pts)-1):
            ax,ay=pts[i-1]; bx,by=pts[i]; cx,cy=pts[i+1]
            if (bx-ax,by-ay)!=(cx-bx,cy-by): simp.append(pts[i])
        simp.append(pts[-1])
        for i in range(len(simp)-1):
            add_seg(simp[i][0],simp[i][1],simp[i+1][0],simp[i+1][1],net,r[0][2])
    # vias at layer-change cells
    for i in range(len(path)-1):
        if path[i][2]!=path[i+1][2]:
            add_via(path[i+1][0]*GRID,path[i+1][1]*GRID,net)

ok=0; fail=[]
for objs in unconn:
    if len(objs)<2: continue
    (ax,ay,ad),(bx,by,bd)=objs[0],objs[1]
    mn=re.search(r'\[([^\]]+)\]',ad) or re.search(r'\[([^\]]+)\]',bd)
    net=mn.group(1) if mn else None
    if not net or board.FindNet(net) is None: fail.append((net,ax,ay));continue
    dist=math.hypot(ax-bx,ay-by); rad=dist*1.8+4.0
    done=False
    if dist<0.7:
        la=0 if 'Top Layer' in ad else (L_IN3 if 'Layer 4' in ad else (L_B if 'Bottom' in ad else -1))
        lb=0 if 'Top Layer' in bd else (L_IN3 if 'Layer 4' in bd else (L_B if 'Bottom' in bd else -1))
        mx,my=(ax+bx)/2,(ay+by)/2
        mix,miy=round(mx/GRID),round(my/GRID)
        if la!=lb and not vblk(mix,miy,net):
            add_via(mix*GRID,miy*GRID,net); done=True
        elif la==lb and la>=0:
            add_seg(ax,ay,bx,by,net,la); done=True
        elif not vblk(mix,miy,net):
            add_via(mix*GRID,miy*GRID,net); done=True
    if not done:
        p=astar3((ax,ay,0),[(bx,by)],net,rad)
        if p: emit(p,net); done=True
    if done: ok+=1
    else: fail.append((net,round(ax,2),round(ay,2),round(bx,2),round(by,2)))
print('routed',ok,'fail',len(fail))
for f in fail: print(' FAIL',f)
board.Save(DST); print('saved',DST)
