import pcbnew,math,re,heapq,sys
from collections import deque
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
RPT=sys.argv[1] if len(sys.argv)>1 else BASE+'_work/drc_fix4j_4.rpt'
board=pcbnew.LoadBoard(BASE+'vitalq_v2.kicad_pcb')
MM=1e6
TOP,BOT=0,2
CULAY=[0,2,4,6,8,10,12,14]
RLAY=[0,6,8,12,2]
U1NETS={'+3V3_ESP','ESP_EN','LSM6_INT1','TMP117_ALERT','GAUGE_ALRT','CHG_PG','CS_AD5940','CS_FLASH','STATUS_LED','CS_AFE4900','I2C_SDA','USB_DM_S3','USB_DP_S3','EXP_INT','I2C_SCL','CS_ADS1292','SPI_MOSI','SPI_SCK','SPI_MISO','AD5940_GPIO0','AFE4900_ADC_RDY','ESP_IO0','CS_MAX86178','MAX86178_INT','MIC_CLK','MIC_DAT','SPARE_IN','ESP_RX','ESP_TX','ADS1292_DRDY','FSR_ADC','GND'}
BLOCK={'USB_DP','USB_DM','IN1P_AC','ECG_P'}
ALLN=U1NETS|BLOCK
LAYMAP={'Top Layer':0,'Bottom Layer':2,'Ground Layer 2':4,'Layer 3':6,'Signal Layer 4':8,'Power Layer 5':10,'Signal Layer 6':12,'Ground Layer 7':14}

# ===== snapshot ALL board data before any mutation =====
SEG=[];VIA=[]
for t in board.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();VIA.append([t,p.x/MM,p.y/MM,t.GetNetname()])
    else:
        s,e=t.GetStart(),t.GetEnd()
        SEG.append([t,s.x/MM,s.y/MM,e.x/MM,e.y/MM,t.GetLayer(),t.GetNetname()])
PADS=[]
for m in board.GetFootprints():
    ref=m.GetReference();fl=m.IsFlipped()
    for pd in m.Pads():
        bb=pd.GetBoundingBox()
        PADS.append((bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM,pd.GetNetname(),ref,BOT if fl else TOP,pd.GetDrillSizeX()/MM))
ZRULE=[]
for z in board.Zones():
    if z.GetIsRuleArea() and (z.GetDoNotAllowTracks() or z.GetDoNotAllowVias()):
        bb=z.GetBoundingBox()
        ZRULE.append(((bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM),list(z.GetLayerSet().Seq())))
EDGES=[]
for d in board.GetDrawings():
    if d.GetLayerName()=='Edge.Cuts':
        s,e=d.GetStart(),d.GetEnd()
        EDGES.append((s.x/MM,s.y/MM,e.x/MM,e.y/MM))

ORIG_SEG=set();ORIG_VIA=set()
for ln in open(BASE+'_work/orig_sigs.txt'):
    p=ln.split()
    if p[0]=='V':ORIG_VIA.add((float(p[2]),float(p[3]),p[1]))
    else:ORIG_SEG.add((float(p[2]),float(p[3]),float(p[4]),float(p[5]),p[1]))
def is_orig_seg(x1,y1,x2,y2,n):
    return (round(x1,2),round(y1,2),round(x2,2),round(y2,2),n) in ORIG_SEG or (round(x2,2),round(y2,2),round(x1,2),round(y1,2),n) in ORIG_SEG
def is_orig_via(x,y,n): return (round(x,2),round(y,2),n) in ORIG_VIA

# ===== parse report =====
items=[];pairs=[]
cur=None;pending=None
for ln in open(RPT):
    m=re.match(r'\[([a-z_]+)\]',ln)
    if m:cur=m.group(1);pending=None;continue
    if cur=='unconnected_items':
        for c in re.finditer(r'@\(([-\d.]+) mm, ([-\d.]+) mm\): (Via|Track|Pad|Arc)\s*(?:\[([^\]]*)\])?(?: of (\w+))? on ([^,\n(]+)',ln):
            x,y,typ,net,ref,lay=c.groups()
            it=(cur,float(x),float(y),typ,net or '',ref or '',lay.strip(),LAYMAP.get(lay.strip()))
            if pending is None:pending=it
            else:pairs.append((pending,it));pending=None
        continue
    for c in re.finditer(r'@\(([-\d.]+) mm, ([-\d.]+) mm\): (Via|Track|Pad|Arc|Segment|Zone|Footprint)\s*(?:\[([^\]]*)\])?(?: of (\w+))? on ([^,\n(]+)',ln):
        x,y,typ,net,ref,lay=c.groups()
        items.append((cur,float(x),float(y),typ,net or '',ref or '',lay.strip(),LAYMAP.get(lay.strip())))
print('flagged',len(items),'pairs',len(pairs))

# ===== mutations: dedupe vias, delete flagged non-orig items =====
seen=[];dups=[]
for i,(t,x,y,n) in enumerate(VIA):
    for kx,ky,kn in seen:
        if kn==n and abs(x-kx)<0.3 and abs(y-ky)<0.3:dups.append(i);break
    else:seen.append((x,y,n))
for i in dups:board.Remove(VIA[i][0])
VIA=[v for i,v in enumerate(VIA) if i not in dups]

BADT={'shorting_items','clearance','tracks_crossing','track_dangling','hole_clearance','via_dangling','items_not_allowed','copper_sliver','copper_edge_clearance','zones_intersect','holes_co_located','hole_to_hole'}
delseg=set();delvia=set()
for kind,x,y,typ,net,ref,lay,layp in items:
    if kind not in BADT:continue
    if typ=='Track' and net in ALLN:
        for i,(t,x1,y1,x2,y2,ly,n) in enumerate(SEG):
            if i in delseg or n!=net or is_orig_seg(x1,y1,x2,y2,n):continue
            dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy
            tt=max(0,min(1,((x-x1)*dx+(y-y1)*dy)/L2)) if L2 else 0
            d=min(math.hypot(x-x1,y-y1),math.hypot(x-x2,y-y2),math.hypot(x-x1-tt*dx,y-y1-tt*dy))
            if d<0.3:delseg.add(i);break
    elif typ=='Via' and net in ALLN:
        for i,(t,vx,vy,n) in enumerate(VIA):
            if i in delvia or n!=net or is_orig_via(vx,vy,n):continue
            if math.hypot(x-vx,y-vy)<0.3:delvia.add(i);break
# record endpoints of deleted segs so we can re-bridge in the same pass
REJOIN=[]
for i in delseg:
    t,x1,y1,x2,y2,ly,n=SEG[i]
    REJOIN.append((n,x1,y1,x2,y2,ly))
for i in sorted(delseg,reverse=True):board.Remove(SEG[i][0])
for i in sorted(delvia,reverse=True):board.Remove(VIA[i][0])
SEG=[s for i,s in enumerate(SEG) if i not in delseg]
VIA=[v for i,v in enumerate(VIA) if i not in delvia]
print('vias deduped',len(dups),'del segs',len(delseg),'vias',len(delvia),'rejoin',len(REJOIN))

# ===== occupancy raster =====
CS=0.1
X0,Y0,X1,Y1=-4,-20,56,66
W=int((X1-X0)/CS);H=int((Y1-Y0)/CS)
def cell(x,y):return int((x-X0)/CS),int((y-Y0)/CS)
def cpnt(i,j):return X0+(i+0.5)*CS,Y0+(j+0.5)*CS
OCCN={L:{} for L in CULAY}
def put(L,i,j,n):
    s=OCCN[L].get((i,j))
    if s is None:OCCN[L][(i,j)]={n}
    else:s.add(n)
def markpt(L,cx,cy,r,n):
    i0,j0=cell(cx-r,cy-r);i1,j1=cell(cx+r,cy+r)
    for i in range(i0,i1+1):
        for j in range(j0,j1+1):
            px,py=cpnt(i,j)
            if math.hypot(px-cx,py-cy)<r:put(L,i,j,n)
def markseg(L,x1,y1,x2,y2,r,n):
    st=int(math.hypot(x2-x1,y2-y1)/CS)+1
    for k in range(st+1):
        t=k/max(1,st);markpt(L,x1+t*(x2-x1),y1+t*(y2-y1),r,n)
for t,x1,y1,x2,y2,ly,n in SEG:
    if ly in OCCN:markseg(ly,x1,y1,x2,y2,0.30,n)
for t,vx,vy,n in VIA:
    for L in CULAY:markpt(L,vx,vy,0.50,n)
for bx1,by1,bx2,by2,n,ref,ly,dr in PADS:
    markpt(ly,(bx1+bx2)/2,(by1+by2)/2,max(bx2-bx1,by2-by1)/2+0.30,n if n else '<nc>')
    if dr>0:
        for L in CULAY:markpt(L,(bx1+bx2)/2,(by1+by2)/2,dr/2+0.25,'<drill>')
for r,lays in ZRULE:
    x1,y1,x2,y2=r
    for L in lays:
        if L in OCCN:
            i0,j0=cell(x1,y1);i1,j1=cell(x2,y2)
            for i in range(i0,i1+1):
                for j in range(j0,j1+1):put(L,i,j,'<ko>')
for (x1,y1,x2,y2) in EDGES:
    st=int(math.hypot(x2-x1,y2-y1)/CS)+1
    for k in range(st+1):
        t=k/max(1,st);cx,cy=x1+t*(x2-x1),y1+t*(y2-y1)
        for L in CULAY:markpt(L,cx,cy,0.35,'<edge>')
# outside-outline blocking: chain edge segs into polygon, ray-cast per row
poly=[]
first=EDGES[0][0:2];curv=EDGES[0][2:4];poly=[first,curv]
rem=EDGES[1:]
while rem:
    nxt=None
    for k,e in enumerate(rem):
        if math.hypot(e[0]-curv[0],e[1]-curv[1])<0.02:nxt=k;break
        if math.hypot(e[2]-curv[0],e[3]-curv[1])<0.02:rem[k]=(e[2],e[3],e[0],e[1]);nxt=k;break
    if nxt is None:break
    curv=rem.pop(nxt)[2:4]
    if math.hypot(curv[0]-poly[0][0],curv[1]-poly[0][1])<0.02:break
    poly.append(curv)
def inside(x,y):
    c=False;n=len(poly)
    for a in range(n):
        xa,ya=poly[a];xb,yb=poly[(a+1)%n]
        if (ya>y)!=(yb>y) and x<(xb-xa)*(y-ya)/(yb-ya)+xa:c=not c
    return c
for j in range(H):
    py=cpnt(0,j)[1]
    for i in range(W):
        if not inside(cpnt(i,j)[0],py):
            for L in CULAY:put(L,i,j,'<out>')
OCCALL={}
for L in CULAY:
    for c,ns in OCCN[L].items():
        s=OCCALL.get(c)
        if s is None:OCCALL[c]=set(ns)
        else:s|=ns
def free(L,i,j,net):
    s=OCCN[L].get((i,j))
    return s is None or s<={net}
def viafree(i,j,net):
    s=OCCALL.get((i,j))
    return s is None or s<={net}

def astar(net,pts,goals):
    allc=[cell(x,y)+(L,) for x,y,L in pts]+list(goals)
    i0=max(0,min(c[0] for c in allc)-80);j0=max(0,min(c[1] for c in allc)-80)
    i1=min(W-1,max(c[0] for c in allc)+80);j1=min(H-1,max(c[1] for c in allc)+80)
    w=i1-i0+1;h=j1-j0+1
    hf={L:[[40000]*w for _ in range(h)] for L in RLAY}
    for L in RLAY:
        dq=deque()
        for (gi,gj,gl) in goals:
            if gl==L and i0<=gi<=i1 and j0<=gj<=j1:
                hf[L][gj-j0][gi-i0]=0;dq.append((gi,gj))
        while dq:
            gi,gj=dq.popleft();d=hf[L][gj-j0][gi-i0]+1
            for di in(-1,0,1):
                for dj in(-1,0,1):
                    ni,nj=gi+di,gj+dj
                    if i0<=ni<=i1 and j0<=nj<=j1 and hf[L][nj-j0][ni-i0]>d:
                        hf[L][nj-j0][ni-i0]=d;dq.append((ni,nj))
    pq=[];cnt=0;dist={};prev={}
    for x,y,L in pts:
        i,j=cell(x,y)
        if not(i0<=i<=i1 and j0<=j<=j1):continue
        u=(i,j,L)
        if u not in dist:
            dist[u]=0;heapq.heappush(pq,(hf[L][j-j0][i-i0],cnt,u));cnt+=1
    while pq:
        f,_,u=heapq.heappop(pq)
        if u in goals:
            path=[];q=u
            while q in prev:path.append(q);q=prev[q]
            path.append(q);path.reverse();return path
        i,j,L=u;du=dist[u]
        for di in(-1,0,1):
            for dj in(-1,0,1):
                if di==0 and dj==0:continue
                ni,nj=i+di,j+dj
                if not(i0<=ni<=i1 and j0<=nj<=j1):continue
                if not free(L,ni,nj,net):continue
                nd=du+(1.414 if di and dj else 1.0)
                v=(ni,nj,L)
                if nd<dist.get(v,1e18):
                    dist[v]=nd;prev[v]=u
                    heapq.heappush(pq,(nd+hf[L][nj-j0][ni-i0],cnt,v));cnt+=1
        for NL in RLAY:
            if NL==L:continue
            if not viafree(i,j,net):continue
            if not free(NL,i,j,net):continue
            v=(i,j,NL);nd=du+12
            if nd<dist.get(v,1e18):
                dist[v]=nd;prev[v]=u
                heapq.heappush(pq,(nd+hf[NL][j-j0][i-i0],cnt,v));cnt+=1
        if len(dist)>1500000:break
    return None

def add_seg(x1,y1,x2,y2,net,layer,w=0.127):
    if math.hypot(x2-x1,y2-y1)<0.03:return
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(round(x1*MM)),int(round(y1*MM))))
    t.SetEnd(pcbnew.VECTOR2I(int(round(x2*MM)),int(round(y2*MM))))
    t.SetWidth(int(w*MM));t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(round(x*MM)),int(round(y*MM))))
    v.SetWidth(int(0.4*MM));v.SetDrill(int(0.2*MM));v.SetNet(board.FindNet(net));v.SetLayerPair(0,2);board.Add(v)
def regseg(x1,y1,x2,y2,net,layer):
    markseg(layer,x1,y1,x2,y2,0.30,net)
def regvia(x,y,net):
    for L in CULAY:markpt(L,x,y,0.50,net)
def emit(net,path,ptA=None,ptB=None):
    L=path[0][2];x1,y1=cpnt(*path[0][:2]);di0=dj0=None;out=[]
    for k in range(1,len(path)):
        i,j,l=path[k]
        if l!=L:
            ex,ey=cpnt(*path[k-1][:2]);out.append((x1,y1,ex,ey,L))
            add_via(ex,ey,net);regvia(ex,ey,net)
            L=l;x1,y1=ex,ey;di0=dj0=None;continue
        di,dj=i-path[k-1][0],j-path[k-1][1]
        if di0 is None:di0,dj0=di,dj
        elif (di,dj)!=(di0,dj0):
            ex,ey=cpnt(*path[k-1][:2]);out.append((x1,y1,ex,ey,L));x1,y1=ex,ey;di0,dj0=di,dj
    ex,ey=cpnt(*path[-1][:2]);out.append((x1,y1,ex,ey,L))
    if ptA is not None:
        ax,ay=ptA
        if math.hypot(ax-out[0][0],ay-out[0][1])>1e-6:
            if math.hypot(ax-out[0][0],ay-out[0][1])<0.45:out[0]=(ax,ay,out[0][2],out[0][3],out[0][4])
            else:out.insert(0,(ax,ay,out[0][0],out[0][1],out[0][4]))
    if ptB is not None:
        bx,by=ptB
        if math.hypot(bx-out[-1][2],by-out[-1][3])>1e-6:
            if math.hypot(bx-out[-1][2],by-out[-1][3])<0.45:out[-1]=(out[-1][0],out[-1][1],bx,by,out[-1][4])
            else:out.append((out[-1][2],out[-1][3],bx,by,out[-1][4]))
    # verify compressed segs stay inside free cells; else emit per-cell steps
    def segfree(x1,y1,x2,y2,L,net):
        st=int(math.hypot(x2-x1,y2-y1)/CS)+1
        for k in range(st+1):
            t=k/max(1,st);cx,cy=x1+t*(x2-x1),y1+t*(y2-y1)
            ci,cj=cell(cx,cy)
            if not free(L,ci,cj,net):return False
        return True
    for a in out:
        if segfree(*a[:4],a[4],net):
            add_seg(a[0],a[1],a[2],a[3],net,a[4]);regseg(*a[:4],net,a[4])
        else:
            # fallback: raw cell steps on same layer
            L2=a[4]
            # rebuild raw path for this run: emit stepwise
            st=int(math.hypot(a[2]-a[0],a[3]-a[1])/CS)+1
            px,py=a[0],a[1]
            for k in range(st+1):
                t=(k+1)/st;nx,ny=a[0]+t*(a[2]-a[0]),a[1]+t*(a[3]-a[1])
                ci,cj=cell(nx,ny)
                nx,ny=cpnt(ci,cj)
                if segfree(px,py,nx,ny,L2,net):
                    add_seg(px,py,nx,ny,net,L2);regseg(px,py,nx,ny,net,L2)
                    px,py=nx,ny

def itemcells(it,goalmode):
    kind,x,y,typ,net,ref,lay,layp=it
    if typ=='Via':
        ci,cj=cell(x,y);out=[]
        for L in RLAY:
            for ii in range(ci-3,ci+4):
                for jj in range(cj-3,cj+4):
                    px,py=cpnt(ii,jj)
                    if math.hypot(px-x,py-y)<0.3:
                        out.append((ii,jj,L) if goalmode else (x,y,L))
        return out
    lays=[layp] if layp in RLAY else list(RLAY)
    ci,cj=cell(x,y);out=[]
    for layv in lays:
        for ii in range(ci-4,ci+5):
            for jj in range(cj-4,cj+5):
                px,py=cpnt(ii,jj)
                if math.hypot(px-x,py-y)<0.4:
                    out.append((ii,jj,layv) if goalmode else (x,y,layv))
    return out

# re-bridge deleted segment endpoints in the same pass
LAYNAME={v:k for k,v in LAYMAP.items()}
REJ=[(('rejoin',x1,y1,'Track',n,'',LAYNAME.get(ly,''),ly),('rejoin',x2,y2,'Track',n,'',LAYNAME.get(ly,''),ly)) for (n,x1,y1,x2,y2,ly) in REJOIN]
pairs=REJ+pairs

ok=0;fails=[]
for A,B in pairs:
    ka,xa,ya,ta,na,ra,la,lap=A
    kb,xb,yb,tb,nb,rb,lb,lbp=B
    if na!=nb:continue
    pts=itemcells(A,False)
    g=set(itemcells(B,True))
    p=astar(na,pts,g)
    if p:
        emit(na,p,(xa,ya),(xb,yb));ok+=1
        for it in (A,B):
            k2,x2,y2,t2,n2,r2,l2,lp2=it
            if t2 in('Track','Arc') and lp2 not in RLAY:
                ci,cj=cell(x2,y2)
                if viafree(ci,cj,na):add_via(x2,y2,na);regvia(x2,y2,na)
    else:fails.append((na,xa,ya,ta,la,'|',xb,yb,tb,lb))
fixed2=0;still=[]
for (na,xa,ya,ta,la,_,xb,yb,tb,lb) in fails:
    d=math.hypot(xb-xa,yb-ya)
    lap=LAYMAP.get(la);lbp=LAYMAP.get(lb);done=False
    if d<1.6:
        mx,my=(xa+xb)/2,(ya+yb)/2
        for r in [0.25,0.4,0.6,0.9,1.2]:
            for adeg in range(0,360,20):
                vx,vy=mx+r*math.cos(math.radians(adeg)),my+r*math.sin(math.radians(adeg))
                ci,cj=cell(vx,vy)
                if not viafree(ci,cj,na):continue
                add_via(vx,vy,na)
                for (x_,y_,l_) in ((xa,ya,lap),(xb,yb,lbp)):
                    if l_ in RLAY:add_seg(x_,y_,vx,vy,na,l_)
                regvia(vx,vy,na)
                fixed2+=1;done=True;break
            if done:break
    if not done:still.append((na,xa,ya,ta,la,'|',xb,yb,tb,lb))
print('routed',ok,'via-fixed',fixed2,'fails',len(still))
for f in still[:30]:print('FAIL',f)
board.Save(BASE+'vitalq_v2.kicad_pcb')
print('saved p7')
