import pcbnew, math, heapq
board = pcbnew.LoadBoard('/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/_work/stage3.kicad_pcb')
GRID=0.05; CLR=0.10; SIG_W=0.127
L_IN3=8

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

# collect geometry
segs=[];vias=[];pads=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition(); vias.append((p.x/1e6,p.y/1e6,t.GetNetname()))
    else:
        s=t.GetStart();e=t.GetEnd();l=t.GetLayer()
        segs.append((s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6,t.GetNetname(),l))
for m in board.GetFootprints():
    for pd in m.Pads():
        if pd.GetNetname() in ('','Net-'): continue
        b=pd.GetPosition(); pads.append((b.x/1e6,b.y/1e6,pd.GetNetname(),pd.GetSize().x/1e6,pd.GetSize().y/1e6,m.GetReference()))

def blocked_cell(cx,cy,net,layer):
    # point vs segs on same layer (for routing layer clearance)
    for x1,y1,x2,y2,n,l in segs:
        if l!=layer or n==net: continue
        if segdist(cx,cy,x1,y1,x2,y2)<CLR+SIG_W: return True
    for vx,vy,n in vias:
        if n!=net and segdist(cx,cy,vx,vy,vx,vy)<CLR+SIG_W: return True
    for px,py,n,sx,sy,r in pads:
        if n!=net and abs(cx-px)<sx/2+CLR and abs(cy-py)<sy/2+CLR: return True
    return False

def via_ok(cx,cy,net):
    # via clearance vs ALL-layer copper
    for x1,y1,x2,y2,n,l in segs:
        if n!=net and segdist(cx,cy,x1,y1,x2,y2)<CLR+0.35: return False
    for vx,vy,n in vias:
        if n!=net and math.hypot(cx-vx,cy-vy)<CLR+0.7: return False
    for px,py,n,sx,sy,r in pads:
        if n!=net and math.hypot(cx-px,cy-py)<max(sx,sy)/2+CLR+0.35: return False
    return True

def astar(start,goals,net,layer,rad=6.0):
    gx,gy=goals[0]
    cells={}
    def cell(x,y): return (round(x/GRID),round(y/GRID))
    openh=[(0,start[0],start[1])]; came={cell(*start):None}
    while openh:
        f,cx,cy=heapq.heappop(openh)
        for gx0,gy0 in goals:
            if math.hypot(cx-gx0,cy-gy0)<GRID*1.5:
                path=[(cx,cy)];c=cell(cx,cy)
                while came[c]: path.append(came[c]);c=cell(*came[c])
                return path
        for dx,dy in ((GRID,0),(-GRID,0),(0,GRID),(0,-GRID),(GRID,GRID),(GRID,-GRID),(-GRID,GRID),(-GRID,-GRID)):
            nx,ny=cx+dx,cy+dy
            if math.hypot(nx-start[0],ny-start[1])>rad: continue
            if cell(nx,ny) in came: continue
            if blocked_cell(nx,ny,net,layer): continue
            came[cell(nx,ny)]=(cx,cy)
            heapq.heappush(openh,(math.hypot(nx-gx,ny-gy)+math.hypot(nx-start[0],ny-start[1]),nx,ny))
    return None

def add_seg(x1,y1,x2,y2,net,layer,w=SIG_W):
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(x1*1e6),int(y1*1e6)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*1e6),int(y2*1e6)));t.SetWidth(int(w*1e6))
    t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t)
    segs.append((x1,y1,x2,y2,net,layer))

def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(x*1e6),int(y*1e6)))
    v.SetWidth(int(0.6*1e6));v.SetDrill(int(0.3*1e6))
    v.SetNet(board.FindNet(net));v.SetLayerPair(0,2);board.Add(v)
    vias.append((x,y,net)); return v

nc=board.GetNetInfo()

# --- GND at (21.65572,11.887806): via straight into GND pours ---
gx,gy=21.65572,11.887806
done_gnd=False
for r in [0,0.1,0.2,0.3,0.5,0.8]:
    for ang in range(0,360,30):
        cx=gx+r*math.cos(math.radians(ang)); cy=gy+r*math.sin(math.radians(ang))
        if via_ok(cx,cy,'GND'):
            add_via(cx,cy,'GND');print('GND via@',cx,cy);done_gnd=True;break
    if done_gnd: break
if not done_gnd: print('GND via FAIL')

# --- Q3_B2 at (11.49,10.5): route on F.Cu to a via site, then via drops to In3 ---
tx,ty=11.49,10.5; net='Q3_B2'
# candidate via sites in expanding ring
solved=False
for rad in [1.0,1.5,2.0,2.5,3.0,4.0]:
    best=None
    for ix in range(int((tx-rad)/GRID),int((tx+rad)/GRID)+1):
        for iy in range(int((ty-rad)/GRID),int((ty+rad)/GRID)+1):
            cx,cy=ix*GRID,iy*GRID
            if math.hypot(cx-tx,cy-ty)>rad: continue
            if math.hypot(cx-tx,cy-ty)<0.5: continue
            if not via_ok(cx,cy,net): continue
            # check below on In3 is free to route away
            if blocked_cell(cx,cy,net,L_IN3): continue
            d=math.hypot(cx-tx,cy-ty)
            if best is None or d<best[0]: best=(d,cx,cy)
    if best:
        _,vx_,vy_=best
        path=astar((tx,ty),[(vx_,vy_)],net,0,rad+0.5)
        if path:
            for i in range(len(path)-1):
                add_seg(path[i][0],path[i][1],path[i+1][0],path[i+1][1],net,0)
            add_via(vx_,vy_,net)
            # now route on In3 from via to nearest same-net copper
            tgt=[(x1,y1) for x1,y1,x2,y2,n,l in segs if n==net and l==L_IN3]+\
                [(x2,y2) for x1,y1,x2,y2,n,l in segs if n==net and l==L_IN3]
            tgt+=[(vx,vy) for vx,vy,n in vias if n==net]
            if tgt:
                p2=astar((vx_,vy_),tgt,net,L_IN3,8.0)
                if p2:
                    for i in range(len(p2)-1):
                        add_seg(p2[i][0],p2[i][1],p2[i+1][0],p2[i+1][1],net,L_IN3)
                    print('Q3_B2 via@',vx_,vy_,'L8 hop',len(p2));solved=True;break
            print('Q3_B2 via placed but L8 hop failed',vx_,vy_);solved=True;break
        else:
            print('Q3_B2 pad->via route fail at',vx_,vy_)
    else:
        print('Q3_B2 no via site within',rad)
print('Q3_B2 solved:',solved)

board.Save('/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/_work/stage4.kicad_pcb')
print('saved stage4')
