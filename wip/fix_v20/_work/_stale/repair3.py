import pcbnew, math, heapq, re, sys, os
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
SRC=sys.argv[1]; DST=sys.argv[2]; RPT=sys.argv[3]
ORIG=BASE+'_work/pre_promote_backup.kicad_pcb'
GRID=0.05; SIG_W=0.127; L_IN3=8
CLR=0.20          # edge clearance margin (grid cells dilated)
VIA_R=0.32        # via keep radius
DIL=int(math.ceil(CLR/GRID))
VDIL=int(math.ceil((VIA_R+0.18)/GRID))

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

# --- fingerprint originals ---
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

# --- parse DRC ---
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
print('bad pts',len(bad),'unconn',len(unconn))

if os.environ.get('PHASE')!='2':
    delset=set()
    for t in board.GetTracks():
        if fp_of(t) in orig: continue
        if isinstance(t,pcbnew.PCB_VIA):
            p=t.GetPosition(); px,py=p.x/1e6,p.y/1e6
            for x,y,d in bad:
                if math.hypot(x-px,y-py)<0.45: delset.add(t.m_Uuid.AsString()); break
        else:
            s,e=t.GetStart(),t.GetEnd(); x1,y1,x2,y2=s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6
            for x,y,d in bad:
                if segdist(x,y,x1,y1,x2,y2)<0.13: delset.add(t.m_Uuid.AsString()); break
    for t in list(board.GetTracks()):
        try:
            if t.m_Uuid.AsString() in delset: board.Remove(t)
        except Exception: pass
    board.Save(DST); print('deleted',len(delset),'saved',DST); sys.exit(0)

# --- rebuild + rasterize ---
segs=[];vias=[];pads=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();vias.append((p.x/1e6,p.y/1e6,t.GetNetname()))
    else:
        s,e=t.GetStart(),t.GetEnd()
        segs.append((s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6,t.GetNetname(),t.GetLayer()))
for m in board.GetFootprints():
    for pd in m.Pads():
        if pd.GetNetname() in ('','Net-'): continue
        b=pd.GetPosition()
        pads.append((b.x/1e6,b.y/1e6,pd.GetNetname(),pd.GetSize().x/1e6,pd.GetSize().y/1e6))
occ={}   # (layer,ix,iy) -> set(nets)   seg/track occupancy (dilated)
vocc={}  # (ix,iy) -> set(nets)         via/pad clearance occupancy for via placement
def mark(mp,l,ix,iy,n):
    mp.setdefault((l,ix,iy),set()).add(n)
def ras_seg(x1,y1,x2,y2,net,layer):
    L=math.hypot(x2-x1,y2-y1); n=max(1,int(L/GRID))
    for i in range(n+1):
        x=x1+(x2-x1)*i/n; y=y1+(y2-y1)*i/n
        ix,iy=round(x/GRID),round(y/GRID)
        for dx in range(-DIL,DIL+1):
            for dy in range(-DIL,DIL+1):
                mark(occ,layer,ix+dx,iy+dy,net)
def ras_via(x,y,net):
    ix,iy=round(x/GRID),round(y/GRID)
    for dx in range(-VDIL,VDIL+1):
        for dy in range(-VDIL,VDIL+1):
            if dx*dx+dy*dy<=VDIL*VDIL:
                vocc.setdefault((ix+dx,iy+dy),set()).add(net)
    for l in (0,L_IN3):
        for dx in range(-VDIL,VDIL+1):
            for dy in range(-VDIL,VDIL+1):
                if dx*dx+dy*dy<=VDIL*VDIL:
                    mark(occ,l,ix+dx,iy+dy,net)
def ras_pad(x,y,sx,sy,net):
    hx=sx/2+CLR; hy=sy/2+CLR
    for ix in range(int((x-hx)/GRID),int((x+hx)/GRID)+1):
        for iy in range(int((y-hy)/GRID),int((y+hy)/GRID)+1):
            cx,cy=ix*GRID,iy*GRID
            if abs(cx-x)<hx and abs(cy-y)<hy: mark(occ,0,ix,iy,net)
    # pads also exclude vias
    hx2=max(sx,sy)/2+VIA_R+0.18
    for ix in range(int((x-hx2)/GRID),int((x+hx2)/GRID)+1):
        for iy in range(int((y-hx2)/GRID),int((y+hx2)/GRID)+1):
            cx,cy=ix*GRID,iy*GRID
            if math.hypot(cx-x,cy-y)<hx2: vocc.setdefault((ix,iy),set()).add(net)
for s in segs: ras_seg(*s)
for v in vias: ras_via(*v)
for p in pads: ras_pad(p[0],p[1],p[3],p[4],p[2])
print('occ cells',len(occ),'vocc',len(vocc))

def blk(ix,iy,net,layer):
    ns=occ.get((layer,ix,iy))
    if not ns: return False
    return not (ns=={net})
def vblk(ix,iy,net):
    ns=vocc.get((ix,iy))
    if ns and ns!={net}: return True
    return blk(ix,iy,net,0) or blk(ix,iy,net,L_IN3) or blk(ix,iy,net,2)
def astar(s0,goals,net,layer,rad):
    g0=goals[0]
    si=(round(s0[0]/GRID),round(s0[1]/GRID))
    gset={(round(g[0]/GRID),round(g[1]/GRID)) for g in goals}
    openh=[(0,si)]; came={si:None}
    while openh:
        f,c=heapq.heappop(openh)
        if c in gset:
            path=[c]
            while came[c]: c=came[c];path.append(c)
            return [(p[0]*GRID,p[1]*GRID) for p in path]
        cx,cy=c
        for d in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)):
            nc=(cx+d[0],cy+d[1])
            if nc in came: continue
            if math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1])>rad: continue
            if blk(nc[0],nc[1],net,layer): continue
            came[nc]=c
            heapq.heappush(openh,(math.hypot(nc[0]*GRID-g0[0],nc[1]*GRID-g0[1])+math.hypot(nc[0]*GRID-s0[0],nc[1]*GRID-s0[1]),nc))
    return None
def add_seg(x1,y1,x2,y2,net,layer):
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(x1*1e6),int(y1*1e6)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*1e6),int(y2*1e6)));t.SetWidth(int(SIG_W*1e6))
    t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t);ras_seg(x1,y1,x2,y2,net,layer)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(x*1e6),int(y*1e6)))
    v.SetWidth(int(0.6*1e6));v.SetDrill(int(0.3*1e6));v.SetNet(board.FindNet(net))
    v.SetLayerPair(0,2);board.Add(v);ras_via(x,y,net)

ok=0; fail=[]
for objs in unconn:
    if len(objs)<2: continue
    (ax,ay,ad),(bx,by,bd)=objs[0],objs[1]
    mn=re.search(r'\[([^\]]+)\]',ad) or re.search(r'\[([^\]]+)\]',bd)
    net=mn.group(1) if mn else None
    if not net or board.FindNet(net) is None:
        fail.append((net,ax,ay)); continue
    dist=math.hypot(ax-bx,ay-by); rad0=dist*1.6+3.0
    done=False
    for layer in (0,L_IN3,2):
        p=astar((ax,ay),[(bx,by)],net,layer,rad0)
        if p:
            for i in range(len(p)-1): add_seg(p[i][0],p[i][1],p[i+1][0],p[i+1][1],net,layer)
            done=True;break
    if not done:
        for rad in (1.0,2.0,3.0,4.5,6.0,8.0):
            site=None;bd_=1e9
            for ix in range(int((ax-rad)/GRID),int((ax+rad)/GRID)+1):
                for iy in range(int((ay-rad)/GRID),int((ay+rad)/GRID)+1):
                    if math.hypot(ix*GRID-ax,iy*GRID-ay)>rad: continue
                    if vblk(ix,iy,net): continue
                    d=math.hypot(ix*GRID-ax,iy*GRID-ay)
                    if d<bd_: bd_=d;site=(ix*GRID,iy*GRID)
            if site:
                p1=astar((ax,ay),[site],net,0,rad+1)
                p2=astar(site,[(bx,by)],net,L_IN3,dist+6.0)
                if p1 and p2:
                    for i in range(len(p1)-1): add_seg(p1[i][0],p1[i][1],p1[i+1][0],p1[i+1][1],net,0)
                    add_via(*site,net)
                    for i in range(len(p2)-1): add_seg(p2[i][0],p2[i][1],p2[i+1][0],p2[i+1][1],net,L_IN3)
                    done=True;break
    if done: ok+=1
    else: fail.append((net,ax,ay,bx,by))
print('routed',ok,'fail',len(fail))
for f in fail: print(' FAIL',f)
board.Save(DST); print('saved',DST)
