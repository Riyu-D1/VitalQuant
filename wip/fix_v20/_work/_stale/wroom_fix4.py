import pcbnew, math, heapq
board = pcbnew.LoadBoard('/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/_work/stage4.kicad_pcb')
GRID=0.05; CLR=0.10; SIG_W=0.127; L_IN3=8

def segdist(px,py,x1,y1,x2,y2):
    dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
    if L2==0: return math.hypot(px-x1,py-y1)
    t=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-x1-t*dx,py-y1-t*dy)

segs=[];vias=[];pads=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition(); vias.append((p.x/1e6,p.y/1e6,t.GetNetname()))
    else:
        s=t.GetStart();e=t.GetEnd()
        segs.append((s.x/1e6,s.y/1e6,e.x/1e6,e.y/1e6,t.GetNetname(),t.GetLayer()))
for m in board.GetFootprints():
    for pd in m.Pads():
        if pd.GetNetname() in ('','Net-'): continue
        b=pd.GetPosition(); pads.append((b.x/1e6,b.y/1e6,pd.GetNetname(),pd.GetSize().x/1e6,pd.GetSize().y/1e6,m.GetReference()))

def blocked(cx,cy,net,layer):
    for x1,y1,x2,y2,n,l in segs:
        if l!=layer or n==net: continue
        if segdist(cx,cy,x1,y1,x2,y2)<CLR+SIG_W: return True
    for vx,vy,n in vias:
        if n!=net and math.hypot(cx-vx,cy-vy)<CLR+SIG_W: return True
    for px,py,n,sx,sy,r in pads:
        if n!=net and abs(cx-px)<sx/2+CLR and abs(cy-py)<sy/2+CLR: return True
    return False

def via_ok(cx,cy,net):
    for x1,y1,x2,y2,n,l in segs:
        if n!=net and segdist(cx,cy,x1,y1,x2,y2)<CLR+0.30: return False
    for vx,vy,n in vias:
        if n!=net and math.hypot(cx-vx,cy-vy)<CLR+0.60: return False
    for px,py,n,sx,sy,r in pads:
        if n!=net and math.hypot(cx-px,cy-py)<max(sx,sy)/2+CLR+0.30: return False
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
    t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t);segs.append((x1,y1,x2,y2,net,layer))

def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(x*1e6),int(y*1e6)))
    v.SetWidth(int(0.6*1e6));v.SetDrill(int(0.3*1e6));v.SetNet(board.FindNet(net))
    v.SetLayerPair(0,2);board.Add(v);vias.append((x,y,net))

gx,gy=21.65572,11.887806; net='GND'
# 1) route on F.Cu to nearby same-net copper
tgt=[(x1,y1) for x1,y1,x2,y2,n,l in segs if n==net and l==0]+[(x2,y2) for x1,y1,x2,y2,n,l in segs if n==net and l==0]
tgt+=[(vx,vy) for vx,vy,n in vias if n==net]+[(px,py) for px,py,n,sx,sy,r in pads if n==net]
tgt=[g for g in tgt if math.hypot(g[0]-gx,g[1]-gy)<8]
print('GND targets near:',len(tgt))
solved=False
if tgt:
    tgt.sort(key=lambda g:math.hypot(g[0]-gx,g[1]-gy))
    p=astar((gx,gy),tgt[:40],net,0,8.0)
    if p:
        for i in range(len(p)-1): add_seg(p[i][0],p[i][1],p[i+1][0],p[i+1][1],net,0)
        print('GND F.Cu route ok len',len(p));solved=True
if not solved:
    for rad in [0.6,1.0,1.5,2.0,3.0]:
        best=None
        for ix in range(int((gx-rad)/GRID),int((gx+rad)/GRID)+1):
            for iy in range(int((gy-rad)/GRID),int((gy+rad)/GRID)+1):
                cx,cy=ix*GRID,iy*GRID
                if math.hypot(cx-gx,cy-gy)>rad or math.hypot(cx-gx,cy-gy)<0.35: continue
                if not via_ok(cx,cy,net): continue
                d=math.hypot(cx-gx,cy-gy)
                if best is None or d<best[0]: best=(d,cx,cy)
        if best:
            _,vx_,vy_=best
            p=astar((gx,gy),[(vx_,vy_)],net,0,rad+0.5)
            if p:
                for i in range(len(p)-1): add_seg(p[i][0],p[i][1],p[i+1][0],p[i+1][1],net,0)
                add_via(vx_,vy_,net);print('GND via@',vx_,vy_);solved=True;break
        print('GND rad',rad,'no site')
print('solved:',solved)
board.Save('/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/_work/stage5.kicad_pcb')
print('saved stage5')
