import pcbnew,math,re,heapq
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
board=pcbnew.LoadBoard(BASE+'vitalq_v2.kicad_pcb')
MM=1e6
TOP,BOT=0,2
L3,L4,L6=6,8,12
RLAY=[TOP,L4,L6,BOT]      # routable layers (L4/L6/L3 inner signal; no pours)
U1NETS={'+3V3_ESP','ESP_EN','LSM6_INT1','TMP117_ALERT','GAUGE_ALRT','CHG_PG','CS_AD5940','CS_FLASH','STATUS_LED','CS_AFE4900','I2C_SDA','USB_DM_S3','USB_DP_S3','EXP_INT','I2C_SCL','CS_ADS1292','SPI_MOSI','SPI_SCK','SPI_MISO','AD5940_GPIO0','AFE4900_ADC_RDY','ESP_IO0','CS_MAX86178','MAX86178_INT','MIC_CLK','MIC_DAT','SPARE_IN','ESP_RX','ESP_TX','ADS1292_DRDY','FSR_ADC','GND'}
BLOCK={'USB_DP','USB_DM','IN1P_AC','ECG_P'}
ALLN=U1NETS|BLOCK

def snap():
    seg=[];via=[]
    for t in board.GetTracks():
        if isinstance(t,pcbnew.PCB_VIA):
            p=t.GetPosition();via.append((t,p.x/MM,p.y/MM,t.GetNetname()))
        else:
            s,e=t.GetStart(),t.GetEnd()
            seg.append((t,s.x/MM,s.y/MM,e.x/MM,e.y/MM,t.GetLayer(),t.GetNetname()))
    return seg,via
SEG,VIA=snap()

# original-copper signatures (never delete, only block)
ob=pcbnew.LoadBoard(BASE+'_work/pre_promote_backup.kicad_pcb')
ORIG_SEG=set();ORIG_VIA=set()
for t in ob.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();ORIG_VIA.add((round(p.x/MM,2),round(p.y/MM,2),t.GetNetname()))
    else:
        s,e=t.GetStart(),t.GetEnd()
        ORIG_SEG.add((round(s.x/MM,2),round(s.y/MM,2),round(e.x/MM,2),round(e.y/MM,2),t.GetNetname()))
ob=None
def is_orig_seg(x1,y1,x2,y2,n):
    return (round(x1,2),round(y1,2),round(x2,2),round(y2,2),n) in ORIG_SEG or (round(x2,2),round(y2,2),round(x1,2),round(y1,2),n) in ORIG_SEG
def is_orig_via(x,y,n): return (round(x,2),round(y,2),n) in ORIG_VIA

# pads incl pos/layer/net/bbox
PADS=[]
for m in board.GetFootprints():
    ref=m.GetReference();fl=m.IsFlipped()
    for pd in m.Pads():
        bb=pd.GetBoundingBox()
        PADS.append((bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM,pd.GetNetname(),ref,BOT if fl else TOP))

# ---- parse DRC report → flagged item descriptors ----
def parse(f):
    out=[]
    cur=None
    for ln in open(f):
        m=re.match(r'\[([a-z_]+)\]',ln)
        if m: cur=m.group(1);continue
        for c in re.finditer(r'@\(([-\d.]+) mm, ([-\d.]+) mm\): (Via|Track|Pad|Arc) \[([^\]]+)\](?: of (\w+))? on ([^,\s]+)',ln):
            x,y,typ,net,ref,lay=c.groups()
            out.append((cur,float(x),float(y),typ,net,ref,lay))
    return out
FLAGGED=parse(BASE+'_work/drc_fix4e.rpt')
BADT={'shorting_items','clearance','tracks_crossing','track_dangling','hole_clearance','via_dangling','items_not_allowed','copper_sliver','zones_intersect'}
delseg=set();delvia=set()
for kind,x,y,typ,net,ref,lay in FLAGGED:
    if kind not in BADT: continue
    if typ=='Track':
        for i,(t,x1,y1,x2,y2,ly,n) in enumerate(SEG):
            if i in delseg or n!=net: continue
            if is_orig_seg(x1,y1,x2,y2,n): continue
            d=min(math.hypot(x-x1,y-y1),math.hypot(x-x2,y-y2),
                  abs((x2-x1)*(y-y1)-(y2-y1)*(x-x1))/max(1e-9,math.hypot(x2-x1,y2-y1)))
            if d<0.35: delseg.add(i);break
    elif typ=='Via':
        for i,(t,vx,vy,n) in enumerate(VIA):
            if i in delvia or n!=net: continue
            if is_orig_via(vx,vy,n): continue
            if math.hypot(x-vx,y-vy)<0.35: delvia.add(i);break
# always also delete pad-less-dead-island copper later; first delete flagged
for i in sorted(delseg,reverse=True): board.Remove(SEG[i][0])
for i in sorted(delvia,reverse=True): board.Remove(VIA[i][0])
SEG=[s for i,s in enumerate(SEG) if i not in delseg]
VIA=[v for i,v in enumerate(VIA) if i not in delvia]
print('deleted segs',len(delseg),'vias',len(delvia))

# ---- occupancy raster for A* ----
CS=0.1
X0,Y0,X1,Y1=-4,-20,56,66
W=int((X1-X0)/CS);H=int((Y1-Y0)/CS)
def cell(x,y): return int((x-X0)/CS),int((y-Y0)/CS)
def cpnt(i,j): return X0+(i+0.5)*CS,Y0+(j+0.5)*CS
KO=(-2.5,-16.55,25.5,-15.25)
occ={L:set() for L in RLAY}
VBLK=set()   # cells where a via cannot go (any layer, conservative)
def markset(s,cx,cy,r):
    i0,j0=cell(cx-r,cy-r);i1,j1=cell(cx+r,cy+r)
    for i in range(i0,i1+1):
        for j in range(j0,j1+1):
            px,py=cpnt(i,j)
            if math.hypot(px-cx,py-cy)<r: s.add((i,j))
def mark(L,cx,cy,r): markset(occ[L],cx,cy,r)
def markseg(L,x1,y1,x2,y2,r):
    steps=int(math.hypot(x2-x1,y2-y1)/CS)+1
    for k in range(steps+1):
        t=k/max(1,steps);mark(L,x1+t*(x2-x1),y1+t*(y2-y1),r)
def marksegv(x1,y1,x2,y2,r):
    steps=int(math.hypot(x2-x1,y2-y1)/CS)+1
    for k in range(steps+1):
        t=k/max(1,steps);markset(VBLK,x1+t*(x2-x1),y1+t*(y2-y1),r)
for t,x1,y1,x2,y2,ly,n in SEG:
    if ly in occ: markseg(ly,x1,y1,x2,y2,0.17)
    marksegv(x1,y1,x2,y2,0.28)
for t,vx,vy,n in VIA:
    for L in occ: mark(L,vx,vy,0.45)
    markset(VBLK,vx,vy,0.55)
for bx1,by1,bx2,by2,n,ref,ly in PADS:
    if ly in occ:
        cx,cy=(bx1+bx2)/2,(by1+by2)/2
        mark(ly,cx,cy,max(bx2-bx1,by2-by1)/2+0.17)
    markset(VBLK,(bx1+bx2)/2,(by1+by2)/2,max(bx2-bx1,by2-by1)/2+0.3)
# KO strip on all layers
for i in range(cell(KO[0],KO[1])[0],cell(KO[2],KO[1])[0]+1):
    for j in range(cell(KO[0],KO[1])[1],cell(KO[2],KO[3])[1]+1):
        for L in occ: occ[L].add((i,j))
        VBLK.add((i,j))
# same-net passable: we can't unmark per route cheaply -> use per-route blocked = occ minus same-net
def raster(net):
    # blocked = all obstacles minus same-net contributions: rebuild per net too heavy;
    # instead mark same-net items as free by keeping separate samesets
    return None
# build same-net sets per net lazily
SN={}
def sameset(net):
    if net in SN: return SN[net]
    s={L:set() for L in RLAY}
    for t,x1,y1,x2,y2,ly,n in SEG:
        if n==net and ly in s:
            steps=int(math.hypot(x2-x1,y2-y1)/CS)+1
            for k in range(steps+1):
                tt=k/max(1,steps);cx,cy=x1+tt*(x2-x1),y1+tt*(y2-y1)
                i0,j0=cell(cx-0.17,cy-0.17);i1,j1=cell(cx+0.17,cy+0.17)
                for i in range(i0,i1+1):
                    for j in range(j0,j1+1):
                        px,py=cpnt(i,j)
                        if math.hypot(px-cx,py-cy)<0.17: s[ly].add((i,j))
    for bx1,by1,bx2,by2,n,ref,ly in PADS:
        if n==net and ly in s:
            cx,cy=(bx1+bx2)/2,(by1+by2)/2
            i0,j0=cell(cx-0.4,cy-0.4);i1,j1=cell(cx+0.4,cy+0.4)
            for i in range(i0,i1+1):
                for j in range(j0,j1+1):
                    px,py=cpnt(i,j)
                    if bx1-0.17<=px<=bx2+0.17 and by1-0.17<=py<=by2+0.17: s[ly].add((i,j))
    SN[net]=s;return s
def via_ok(net,x,y):
    for t,vx,vy,n in VIA:
        if n!=net and math.hypot(vx-x,vy-y)<0.55: return False
    for bx1,by1,bx2,by2,n,ref,ly in PADS:
        if n!=net:
            dx=max(bx1-x,0,x-bx2);dy=max(by1-y,0,y-by2)
            if math.hypot(dx,dy)<0.3: return False
    for t,x1,y1,x2,y2,ly,n in SEG:
        if n!=net and ly in RLAY:
            dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy
            tt=max(0,min(1,((x-x1)*dx+(y-y1)*dy)/L2)) if L2 else 0
            if math.hypot(x-x1-tt*dx,y-y1-tt*dy)<0.28: return False
    if KO[0]-0.3<=x<=KO[2]+0.3 and KO[1]-0.3<=y<=KO[3]+0.3: return False
    return True

def astar(net,pts,goals):
    # pts: [(x,y,layer)] start cells; goals: set of (i,j,layer)
    sm=sameset(net)
    allc=[cell(x,y)+(L,) for x,y,L in pts]+list(goals)
    i0=min(c[0] for c in allc)-60;j0=min(c[1] for c in allc)-60
    i1=max(c[0] for c in allc)+60;j1=max(c[1] for c in allc)+60
    i0=max(0,i0);j0=max(0,j0);i1=min(W-1,i1);j1=min(H-1,j1)
    w=i1-i0+1;h=j1-j0+1
    # multi-source BFS distance-to-goal field per layer (admissible heuristic, /sqrt2)
    from collections import deque
    hf={L:[[1e9]*w for _ in range(h)] for L in RLAY}
    for L in RLAY:
        dq=deque()
        for (gi,gj,gl) in goals:
            if gl==L and i0<=gi<=i1 and j0<=gj<=j1:
                hf[L][gj-j0][gi-i0]=0;dq.append((gi,gj))
        while dq:
            gi,gj=dq.popleft()
            d=hf[L][gj-j0][gi-i0]+1
            for di in (-1,0,1):
                for dj in (-1,0,1):
                    ni,nj=gi+di,gj+dj
                    if i0<=ni<=i1 and j0<=nj<=j1 and hf[L][nj-j0][ni-i0]>d:
                        hf[L][nj-j0][ni-i0]=d;dq.append((ni,nj))
    pq=[];cnt=0;dist={};prev={}
    for x,y,L in pts:
        i,j=cell(x,y)
        if not(i0<=i<=i1 and j0<=j<=j1):continue
        u=(i,j,L);dist[u]=0;heapq.heappush(pq,(hf[L][j-j0][i-i0],cnt,u));cnt+=1
    while pq:
        f,_,u=heapq.heappop(pq)
        if u in goals:
            path=[];q=u
            while q in prev:path.append(q);q=prev[q]
            path.append(q);path.reverse();return path
        i,j,L=u
        du=dist[u]
        for di in (-1,0,1):
            for dj in (-1,0,1):
                if di==0 and dj==0:continue
                ni,nj=i+di,j+dj
                if not(i0<=ni<=i1 and j0<=nj<=j1):continue
                if (ni,nj) in occ[L] and (ni,nj) not in sm[L]:continue
                nd=du+(1.414 if di and dj else 1.0)
                v=(ni,nj,L)
                if nd<dist.get(v,1e18):
                    dist[v]=nd;prev[v]=u
                    heapq.heappush(pq,(nd+hf[L][nj-j0][ni-i0],cnt,v));cnt+=1
        x,y=cpnt(i,j)
        for NL in RLAY:
            if NL!=L and ((i,j) not in occ[NL] or (i,j) in sm[NL]) and (i,j) not in VBLK:
                v=(i,j,NL);nd=du+12
                if nd<dist.get(v,1e18):
                    dist[v]=nd;prev[v]=u
                    heapq.heappush(pq,(nd+hf[NL][j-j0][i-i0],cnt,v));cnt+=1
        if len(dist)>800000:break
    return None

def emit(net,path):
    # compress collinear runs per layer, add vias at transitions; update rasters
    L=path[0][2];x1,y1=cpnt(*path[0][:2])
    di0,dj0=None,None
    out=[]
    for k in range(1,len(path)):
        i,j,l=path[k]
        if l!=L:
            ex,ey=cpnt(*path[k-1][:2]);out.append((x1,y1,ex,ey,L));vx,vy=ex,ey
            add_via_at(vx,vy,net)
            markset(VBLK,vx,vy,0.55)
            for LL in occ: mark(LL,vx,vy,0.45)
            L=l;x1,y1=vx,vy
            di0=None;continue
        di,dj=i-path[k-1][0],j-path[k-1][1]
        if di0 is None: di0,dj0=di,dj
        elif (di,dj)!=(di0,dj0):
            ex,ey=cpnt(*path[k-1][:2]);out.append((x1,y1,ex,ey,L));x1,y1=ex,ey;di0,dj0=di,dj
    ex,ey=cpnt(*path[-1][:2]);out.append((x1,y1,ex,ey,L))
    sm=SN.get(net)
    for a in out:
        add_seg(a[0],a[1],a[2],a[3],net,a[4],w=0.127)
        marksegv(a[0],a[1],a[2],a[3],0.28)
        if a[4] in occ: markseg(a[4],a[0],a[1],a[2],a[3],0.17)
        if sm and a[4] in sm:
            steps=int(math.hypot(a[2]-a[0],a[3]-a[1])/CS)+1
            for k in range(steps+1):
                t=k/max(1,steps);cx,cy=a[0]+t*(a[2]-a[0]),a[1]+t*(a[3]-a[1])
                i0_,j0_=cell(cx-0.17,cy-0.17);i1_,j1_=cell(cx+0.17,cy+0.17)
                for ii in range(i0_,i1_+1):
                    for jj in range(j0_,j1_+1):
                        px,py=cpnt(ii,jj)
                        if math.hypot(px-cx,py-cy)<0.17: sm[a[4]].add((ii,jj))
def add_seg(x1,y1,x2,y2,net,layer,w=0.127):
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(round(x1*MM)),int(round(y1*MM))))
    t.SetEnd(pcbnew.VECTOR2I(int(round(x2*MM)),int(round(y2*MM))))
    t.SetWidth(int(w*MM));t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t)
def add_via_at(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(round(x*MM)),int(round(y*MM))))
    v.SetWidth(int(0.4*MM));v.SetDrill(int(0.2*MM));v.SetNet(board.FindNet(net));v.SetLayerPair(0,2);board.Add(v)

# ---- islands per net (fresh, post-delete) ----
def islands(net):
    items=[]
    for t,x1,y1,x2,y2,ly,n in SEG:
        if n==net: items.append(('s',x1,y1,x2,y2,ly,t))
    for t,vx,vy,n in VIA:
        if n==net: items.append(('v',vx,vy,vx,vy,-1,t))
    for i,(bx1,by1,bx2,by2,n,ref,ly) in enumerate(PADS):
        if n==net: items.append(('p',bx1,by1,bx2,by2,ly,None))
    n=len(items);par=list(range(n))
    def f(a):
        while par[a]!=a:par[a]=par[par[a]];a=par[a]
        return a
    def u(a,b):par[f(a)]=f(b)
    for a in range(n):
        ka,x1,y1,x2,y2,la,_=items[a]
        for b in range(a+1,n):
            kb,X1,Y1,X2,Y2,lb,_=items[b]
            if la>=0 and lb>=0 and la!=lb: continue
            def touch():
                for (ex,ey) in [(x1,y1),(x2,y2)]:
                    if X1-0.08<=ex<=X2+0.08 and Y1-0.08<=ey<=Y2+0.08:return True
                for (ex,ey) in [(X1,Y1),(X2,Y2)]:
                    if x1-0.08<=ex<=x2+0.08 and y1-0.08<=ey<=y2+0.08:return True
                return False
            if touch():u(a,b)
    isl={}
    for i in range(n):
        isl.setdefault(f(i),[]).append(i)
    out=[]
    for r,ids in isl.items():
        cells=[];haspad=False
        for i in ids:
            k,x1,y1,x2,y2,ly,obj=items[i]
            if k=='p': haspad=True
            lay=ly if ly>=0 else TOP
            i0,j0=cell(min(x1,x2),min(y1,y2));i1,j1=cell(max(x1,x2),max(y1,y2))
            for ii in range(i0,i1+1):
                for jj in range(j0,j1+1):
                    for LL in ([TOP,BOT] if ly==-1 else [lay]):
                        if LL in RLAY:cells.append((ii,jj,LL))
        out.append((haspad,cells,ids,items))
    return out

repaired=0;fails=[]
for net in sorted(ALLN-{'GND','+3V3_ESP'}):
    isl=islands(net)
    padisl=[x for x in isl if x[0]]
    dead=[x for x in isl if not x[0]]
    # delete pad-less dead islands' copper (spec cleanup E)
    for _,_,ids,itms in dead:
        for i in ids:
            k,x1,y1,x2,y2,ly,obj=itms[i]
            if obj is not None:
                try: board.Remove(obj)
                except: pass
    if len(padisl)<2: continue
    padisl.sort(key=lambda x:-len(x[1]))
    main=set(padisl[0][1])
    for hp,cells,ids,itms in padisl[1:]:
        goals=set(main)
        starts=[];seen=set()
        for (i,j,L) in cells:
            if (i,j,L) not in seen:
                seen.add((i,j,L));starts.append((*cpnt(i,j),L))
        starts=starts[:60]
        p=astar(net,starts,goals)
        if p:
            emit(net,p);repaired+=1
            for c in cells: main.add(c)
            for c in p: main.add(c)
        else: fails.append(net)
print('repaired',repaired,'fails',fails)
board.Save(BASE+'vitalq_v2.kicad_pcb')
print('saved p5')
