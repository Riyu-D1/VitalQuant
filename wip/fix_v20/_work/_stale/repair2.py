import pcbnew, math, heapq, re, sys, os
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
SRC=sys.argv[1]; DST=sys.argv[2]; RPT=sys.argv[3]
ORIG=BASE+'_work/pre_promote_backup.kicad_pcb'
GRID=0.05; SIG_W=0.127; L_IN3=8
CLR_SEG=0.20; VIA_R=0.30

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

# parse DRC
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
print('bad pts',len(bad),'unconn pairs',len(unconn))

# delete NEW items at bad points
delset=set()
if os.environ.get('PHASE')=='2':
    delset=None
if delset is not None:
    for t in board.GetTracks():
        o=fp_of(t) in orig
        if o: continue
        if isinstance(t,pcbnew.PCB_VIA):
            p=t.GetPosition(); px,py=p.x/1e6,p.y/1e6
            for x,y,d in bad:
                if math.hypot(x-px,y-py)<0.45: delset.add(t.m_Uuid.AsString()); break
        else:
            s,e=t.GetStart(),t.GetEnd(); x1,y1,x2,y2=s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6
            for x,y,d in bad:
                if segdist(x,y,x1,y1,x2,y2)<0.13: delset.add(t.m_Uuid.AsString()); break
    uids=delset
    for t in list(board.GetTracks()):
        try:
            if t.m_Uuid.AsString() in uids: board.Remove(t)
        except Exception: pass
    board.Save(DST)
    print('deleted',len(delset))
if os.environ.get('PHASE')=='1':
    print('phase1 done'); sys.exit(0)

# rebuild geometry
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

def blocked(cx,cy,net,layer):
    for x1,y1,x2,y2,n,l in segs:
        if n!=net and l==layer and segdist(cx,cy,x1,y1,x2,y2)<CLR_SEG: return True
    for vx,vy,n in vias:
        if n!=net and math.hypot(cx-vx,cy-vy)<VIA_R+0.18: return True
    if layer==0:
        for px,py,n,sx,sy in pads:
            if n!=net and abs(cx-px)<sx/2+0.14 and abs(cy-py)<sy/2+0.14: return True
    return False
def via_ok(cx,cy,net):
    for x1,y1,x2,y2,n,l in segs:
        if n!=net and segdist(cx,cy,x1,y1,x2,y2)<VIA_R+0.20: return False
    for vx,vy,n in vias:
        if n!=net and math.hypot(cx-vx,cy-vy)<VIA_R*2+0.20: return False
    for px,py,n,sx,sy in pads:
        if n!=net and math.hypot(cx-px,cy-py)<max(sx,sy)/2+VIA_R+0.18: return False
    return True
def astar(s0,goals,net,layer,rad):
    def cell(x,y): return (round(x/GRID),round(y/GRID))
    g0=goals[0]
    openh=[(0,s0[0],s0[1])]; came={cell(*s0):None}
    while openh:
        f,cx,cy=heapq.heappop(openh)
        for g in goals:
            if math.hypot(cx-g[0],cy-g[1])<GRID*1.5:
                path=[(cx,cy)];c=cell(cx,cy)
                while came[c]: path.append(came[c]);c=cell(*came[c])
                return path
        for dx,dy in ((GRID,0),(-GRID,0),(0,GRID),(0,-GRID),(GRID,GRID),(GRID,-GRID),(-GRID,GRID),(-GRID,-GRID)):
            nx,ny=cx+dx,cy+dy
            if math.hypot(nx-s0[0],ny-s0[1])>rad: continue
            c=cell(nx,ny)
            if c in came: continue
            if blocked(nx,ny,net,layer): continue
            came[c]=(cx,cy)
            heapq.heappush(openh,(math.hypot(nx-g0[0],ny-g0[1])+math.hypot(nx-s0[0],ny-s0[1]),nx,ny))
    return None
def add_seg(x1,y1,x2,y2,net,layer):
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(x1*1e6),int(y1*1e6)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*1e6),int(y2*1e6)));t.SetWidth(int(SIG_W*1e6))
    t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t);segs.append((x1,y1,x2,y2,net,layer))
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(x*1e6),int(y*1e6)))
    v.SetWidth(int(0.6*1e6));v.SetDrill(int(0.3*1e6));v.SetNet(board.FindNet(net))
    v.SetLayerPair(0,2);board.Add(v);vias.append((x,y,net))

# route each unconnected pair
ok=0; fail=[]
for objs in unconn:
    if len(objs)<2: continue
    (ax,ay,ad),(bx,by,bd)=objs[0],objs[1]
    # net from description
    mn=re.search(r'\[([^\]]+)\]',ad) or re.search(r'\[([^\]]+)\]',bd)
    net=mn.group(1) if mn else None
    if not net: fail.append(('nonet',ax,ay)); continue
    done=False
    for layer in (0,L_IN3):
        p=astar((ax,ay),[(bx,by)],net,layer,8.0)
        if p:
            for i in range(len(p)-1): add_seg(p[i][0],p[i][1],p[i+1][0],p[i+1][1],net,layer)
            done=True;break
    if not done:
        # via-assisted: F.Cu from a to via site, via, In3 to b
        for rad in (1.0,2.0,3.0,4.5):
            site=None
            for ix in range(int((ax-rad)/GRID),int((ax+rad)/GRID)+1):
                for iy in range(int((ay-rad)/GRID),int((ay+rad)/GRID)+1):
                    cx,cy=ix*GRID,iy*GRID
                    if math.hypot(cx-ax,cy-ay)>rad: continue
                    if via_ok(cx,cy,net): site=(cx,cy);break
                if site:break
            if site:
                p1=astar((ax,ay),[site],net,0,rad+1)
                p2=astar(site,[(bx,by)],net,L_IN3,9.0)
                if p1 and p2:
                    for i in range(len(p1)-1): add_seg(p1[i][0],p1[i][1],p1[i+1][0],p1[i+1][1],net,0)
                    add_via(*site,net)
                    for i in range(len(p2)-1): add_seg(p2[i][0],p2[i][1],p2[i+1][0],p2[i+1][1],net,L_IN3)
                    done=True;break
    if done: ok+=1
    else: fail.append((net,ax,ay,bx,by))
print('routed',ok,'failed',len(fail))
for f in fail: print(' FAIL',f)
board.Save(DST); print('saved',DST)
