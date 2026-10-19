import pcbnew,math,re,heapq,sys
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
RPT=sys.argv[1] if len(sys.argv)>1 else BASE+'_work/drc_fix4g.rpt'
board=pcbnew.LoadBoard(BASE+'vitalq_v2.kicad_pcb')
MM=1e6
TOP,BOT=0,2
L3,L4,L6=6,8,12
RLAY=[TOP,L4,L6,BOT]
U1NETS={'+3V3_ESP','ESP_EN','LSM6_INT1','TMP117_ALERT','GAUGE_ALRT','CHG_PG','CS_AD5940','CS_FLASH','STATUS_LED','CS_AFE4900','I2C_SDA','USB_DM_S3','USB_DP_S3','EXP_INT','I2C_SCL','CS_ADS1292','SPI_MOSI','SPI_SCK','SPI_MISO','AD5940_GPIO0','AFE4900_ADC_RDY','ESP_IO0','CS_MAX86178','MAX86178_INT','MIC_CLK','MIC_DAT','SPARE_IN','ESP_RX','ESP_TX','ADS1292_DRDY','FSR_ADC','GND'}
BLOCK={'USB_DP','USB_DM','IN1P_AC','ECG_P'}
ALLN=U1NETS|BLOCK

def snap():
    seg=[];via=[]
    for t in board.GetTracks():
        if isinstance(t,pcbnew.PCB_VIA):
            p=t.GetPosition();via.append([t,p.x/MM,p.y/MM,t.GetNetname()])
        else:
            s,e=t.GetStart(),t.GetEnd()
            seg.append([t,s.x/MM,s.y/MM,e.x/MM,e.y/MM,t.GetLayer(),t.GetNetname()])
    return seg,via
SEG,VIA=snap()
ob=pcbnew.LoadBoard(BASE+'_work/pre_promote_backup.kicad_pcb')
ORIG_SEG=set();ORIG_VIA=set()
for t in ob.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();ORIG_VIA.add((round(p.x/MM,2),round(p.y/MM,2),t.GetNetname()))
    else:
        s,e=t.GetStart(),t.GetEnd()
        ORIG_SEG.add((round(s.x/MM,2),round(s.y/MM,2),round(e.x/MM,2),round(e.y/MM,2),t.GetNetname()))
del ob
def is_orig_seg(x1,y1,x2,y2,n):
    return (round(x1,2),round(y1,2),round(x2,2),round(y2,2),n) in ORIG_SEG or (round(x2,2),round(y2,2),round(x1,2),round(y1,2),n) in ORIG_SEG
def is_orig_via(x,y,n): return (round(x,2),round(y,2),n) in ORIG_VIA
PADS=[]
for m in board.GetFootprints():
    ref=m.GetReference();fl=m.IsFlipped()
    for pd in m.Pads():
        bb=pd.GetBoundingBox()
        PADS.append((bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM,pd.GetNetname(),ref,BOT if fl else TOP,pd.GetDrillSizeX()/MM))
LAYMAP={'Top Layer':TOP,'Bottom Layer':BOT,'Layer 3':L3,'Signal Layer 4':L4,'Power Layer 5':10,'Signal Layer 6':L6,'Ground Layer 7':14,'Ground Layer 2':4}

# ---- parse DRC: flagged items + unconnected pairs ----
items=[];pairs=[]
cur=None;pending=None
for ln in open(RPT):
    m=re.match(r'\[([a-z_]+)\]',ln)
    if m: cur=m.group(1);pending=None;continue
    for c in re.finditer(r'@\(([-\d.]+) mm, ([-\d.]+) mm\): (Via|Track|Pad|Arc|Zone|Footprint)\s*(?:\[([^\]]*)\])?(?: of (\w+))? on ([^,\n]+)',ln):
        x,y,typ,net,ref,lay=c.groups()
        net=net or ''
        layp=LAYMAP.get(lay.split(' ')[0]+' '+('Layer' if 'Layer' in lay else ''),None)
        if layp is None:
            for k,v in LAYMAP.items():
                if lay.startswith(k) or lay==k: layp=v
        it=(cur,float(x),float(y),typ,net,ref or '',lay,layp)
        items.append(it)
        if cur=='unconnected_items':
            if pending is None: pending=it
            else: pairs.append((pending,it));pending=None
print('items',len(items),'unconn pairs',len(pairs))
BADT={'shorting_items','clearance','tracks_crossing','track_dangling','hole_clearance','via_dangling','items_not_allowed','copper_sliver'}
delseg=set();delvia=set()
for kind,x,y,typ,net,ref,lay,layp in items:
    if kind not in BADT:continue
    if typ=='Track' and (net in ALLN):
        for i,(t,x1,y1,x2,y2,ly,n) in enumerate(SEG):
            if i in delseg or n!=net or is_orig_seg(x1,y1,x2,y2,n):continue
            dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy
            tt=max(0,min(1,((x-x1)*dx+(y-y1)*dy)/L2)) if L2 else 0
            d=min(math.hypot(x-x1,y-y1),math.hypot(x-x2,y-y2),math.hypot(x-x1-tt*dx,y-y1-tt*dy))
            if d<0.3:delseg.add(i);break
    elif typ=='Via' and (net in ALLN):
        for i,(t,vx,vy,n) in enumerate(VIA):
            if i in delvia or n!=net or is_orig_via(vx,vy,n):continue
            if math.hypot(x-vx,y-vy)<0.3:delvia.add(i);break
for i in sorted(delseg,reverse=True):board.Remove(SEG[i][0])
for i in sorted(delvia,reverse=True):board.Remove(VIA[i][0])
SEG=[s for i,s in enumerate(SEG) if i not in delseg]
VIA=[v for i,v in enumerate(VIA) if i not in delvia]
print('del segs',len(delseg),'vias',len(delvia))

CS=0.1
X0,Y0,X1,Y1=-4,-20,56,66
W=int((X1-X0)/CS);H=int((Y1-Y0)/CS)
def cell(x,y):return int((x-X0)/CS),int((y-Y0)/CS)
def cpnt(i,j):return X0+(i+0.5)*CS,Y0+(j+0.5)*CS
KO=(-2.5,-16.55,25.5,-15.25)
occ={L:set() for L in RLAY};VBLKN={}
def markset(s,cx,cy,r):
    i0,j0=cell(cx-r,cy-r);i1,j1=cell(cx+r,cy+r)
    for i in range(i0,i1+1):
        for j in range(j0,j1+1):
            px,py=cpnt(i,j)
            if math.hypot(px-cx,py-cy)<r:s.add((i,j))
def markseg(L,x1,y1,x2,y2,r):
    st=int(math.hypot(x2-x1,y2-y1)/CS)+1
    for k in range(st+1):
        t=k/max(1,st);markset(occ[L],x1+t*(x2-x1),y1+t*(y2-y1),r)
def marksegv(x1,y1,x2,y2,r,n):
    st=int(math.hypot(x2-x1,y2-y1)/CS)+1
    i0,j0=cell(min(x1,x2)-r,min(y1,y2)-r);i1,j1=cell(max(x1,x2)+r,max(y1,y2)+r)
    for i in range(i0,i1+1):
        for j in range(j0,j1+1):
            px,py=cpnt(i,j)
            dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy
            tt=max(0,min(1,((px-x1)*dx+(py-y1)*dy)/L2)) if L2 else 0
            if math.hypot(px-x1-tt*dx,py-y1-tt*dy)<r:
                VBLKN.setdefault((i,j),set()).add(n)
def markv(cx,cy,r,n):
    i0,j0=cell(cx-r,cy-r);i1,j1=cell(cx+r,cy+r)
    for i in range(i0,i1+1):
        for j in range(j0,j1+1):
            px,py=cpnt(i,j)
            if math.hypot(px-cx,py-cy)<r:
                VBLKN.setdefault((i,j),set()).add(n)
for t,x1,y1,x2,y2,ly,n in SEG:
    if ly in occ:markseg(ly,x1,y1,x2,y2,0.30)
    marksegv(x1,y1,x2,y2,0.40,n)
for t,vx,vy,n in VIA:
    for L in occ:markset(occ[L],vx,vy,0.50)
    markv(vx,vy,0.70,n)
for bx1,by1,bx2,by2,n,ref,ly,dr in PADS:
    if ly in occ:markset(occ[ly],(bx1+bx2)/2,(by1+by2)/2,max(bx2-bx1,by2-by1)/2+0.30)
    markv((bx1+bx2)/2,(by1+by2)/2,max(bx2-bx1,by2-by1)/2+0.40,n or '<pad>')
for i in range(cell(KO[0],KO[1])[0],cell(KO[2],KO[1])[0]+1):
    for j in range(cell(KO[0],KO[1])[1],cell(KO[2],KO[3])[1]+1):
        for L in occ:occ[L].add((i,j))
        VBLKN.setdefault((i,j),set()).add('<ko>')
SN={}
def sameset(net):
    if net in SN:return SN[net]
    s={L:set() for L in RLAY}
    for t,x1,y1,x2,y2,ly,n in SEG:
        if n==net and ly in s:
            st=int(math.hypot(x2-x1,y2-y1)/CS)+1
            for k in range(st+1):
                tt=k/max(1,st);cx,cy=x1+tt*(x2-x1),y1+tt*(y2-y1)
                i0,j0=cell(cx-0.25,cy-0.25);i1,j1=cell(cx+0.25,cy+0.25)
                for i in range(i0,i1+1):
                    for j in range(j0,j1+1):
                        px,py=cpnt(i,j)
                        if math.hypot(px-cx,py-cy)<0.25:s[ly].add((i,j))
    for t,vx,vy,n in VIA:
        if n==net:
            for L in s:
                i0,j0=cell(vx-0.3,vy-0.3);i1,j1=cell(vx+0.3,vy+0.3)
                for i in range(i0,i1+1):
                    for j in range(j0,j1+1):
                        px,py=cpnt(i,j)
                        if math.hypot(px-vx,py-vy)<0.3:s[L].add((i,j))
    for bx1,by1,bx2,by2,n,ref,ly,dr in PADS:
        if n==net and ly in s:
            cx,cy=(bx1+bx2)/2,(by1+by2)/2
            i0,j0=cell(bx1-0.2,by1-0.2);i1,j1=cell(bx2+0.2,by2+0.2)
            for i in range(i0,i1+1):
                for j in range(j0,j1+1):
                    px,py=cpnt(i,j)
                    if bx1-0.2<=px<=bx2+0.2 and by1-0.2<=py<=by2+0.2:s[ly].add((i,j))
    SN[net]=s;return s

from collections import deque
def astar(net,pts,goals):
    sm=sameset(net)
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
                if (ni,nj) in occ[L] and (ni,nj) not in sm[L]:continue
                nd=du+(1.414 if di and dj else 1.0);v=(ni,nj,L)
                if nd<dist.get(v,1e18):
                    dist[v]=nd;prev[v]=u
                    heapq.heappush(pq,(nd+hf[L][nj-j0][ni-i0],cnt,v));cnt+=1
        for NL in RLAY:
            if NL==L:continue
            if (i,j) in occ[NL] and (i,j) not in sm[NL]:continue
            vb=VBLKN.get((i,j))
            if vb and not vb<={net}:continue
            v=(i,j,NL);nd=du+12
            if nd<dist.get(v,1e18):
                dist[v]=nd;prev[v]=u
                heapq.heappush(pq,(nd+hf[NL][j-j0][i-i0],cnt,v));cnt+=1
        if len(dist)>1200000:break
    return None

def add_seg(x1,y1,x2,y2,net,layer,w=0.127):
    if math.hypot(x2-x1,y2-y1)<0.03:return
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(round(x1*MM)),int(round(y1*MM))))
    t.SetEnd(pcbnew.VECTOR2I(int(round(x2*MM)),int(round(y2*MM))))
    t.SetWidth(int(w*MM));t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(round(x*MM)),int(round(y*MM))))
    v.SetWidth(int(0.4*MM));v.SetDrill(int(0.2*MM));v.SetNet(board.FindNet(net));v.SetLayerPair(0,2);board.Add(v)
def emit(net,path,ptA=None,ptB=None):
    L=path[0][2];x1,y1=cpnt(*path[0][:2]);di0=dj0=None;out=[]
    for k in range(1,len(path)):
        i,j,l=path[k]
        if l!=L:
            ex,ey=cpnt(*path[k-1][:2]);out.append((x1,y1,ex,ey,L))
            add_via(ex,ey,net);markv(ex,ey,0.70,net)
            for LL in occ:markset(occ[LL],ex,ey,0.50)
            L=l;x1,y1=ex,ey;di0=dj0=None;continue
        di,dj=i-path[k-1][0],j-path[k-1][1]
        if di0 is None:di0,dj0=di,dj
        elif (di,dj)!=(di0,dj0):
            ex,ey=cpnt(*path[k-1][:2]);out.append((x1,y1,ex,ey,L));x1,y1=ex,ey;di0,dj0=di,dj
    ex,ey=cpnt(*path[-1][:2]);out.append((x1,y1,ex,ey,L))
    # exact endpoints: prepend/append stubs to real item coords on same layer
    if ptA is not None:
        ax,ay=ptA
        if math.hypot(ax-out[0][0],ay-out[0][1])>1e-6:
            if math.hypot(ax-out[0][0],ay-out[0][1])<0.45:
                out[0]=(ax,ay,out[0][2],out[0][3],out[0][4])
            else:
                out.insert(0,(ax,ay,out[0][0],out[0][1],out[0][4]))
    if ptB is not None:
        bx,by=ptB
        if math.hypot(bx-out[-1][2],by-out[-1][3])>1e-6:
            if math.hypot(bx-out[-1][2],by-out[-1][3])<0.45:
                out[-1]=(out[-1][0],out[-1][1],bx,by,out[-1][4])
            else:
                out.append((out[-1][2],out[-1][3],bx,by,out[-1][4]))
    # verify each emitted seg against all enemy copper; if violation, emit raw cells
    def segok(x1,y1,x2,y2,net,layer):
        for t,X1,Y1,X2,Y2,ly,n in SEG:
            if n==net:continue
            if ly!=layer:continue
            # seg-seg min distance
            def p2s(px,py,ax,ay,bx,by):
                dx,dy=bx-ax,by-ay;L2=dx*dx+dy*dy
                tt=max(0,min(1,((px-ax)*dx+(py-ay)*dy)/L2)) if L2 else 0
                return math.hypot(px-ax-tt*dx,py-ay-tt*dy)
            d=min(p2s(x1,y1,X1,Y1,X2,Y2),p2s(x2,y2,X1,Y1,X2,Y2),
                  p2s(X1,Y1,x1,y1,x2,y2),p2s(X2,Y2,x1,y1,x2,y2))
            if d<0.25:return False
        for t,vx,vy,n in VIA:
            if n!=net:
                dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy
                tt=max(0,min(1,((vx-x1)*dx+(vy-y1)*dy)/L2)) if L2 else 0
                if math.hypot(vx-x1-tt*dx,vy-y1-tt*dy)<0.36:return False
        for bx1,by1,bx2,by2,n,ref,ly,dr in PADS:
            if n!=net and ly==layer:
                # bbox inflate 0.23
                dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy
                tt=max(0,min(1,(((bx1+bx2)/2-x1)*dx+((by1+by2)/2-y1)*dy)/L2)) if L2 else 0
                # cheap check: sample seg midpoints
                ok=True
                for k in range(9):
                    px,py=x1+(x2-x1)*k/8,y1+(y2-y1)*k/8
                    ddx=max(bx1-px,0,px-bx2);ddy=max(by1-py,0,py-by2)
                    if math.hypot(ddx,ddy)<0.20:ok=False;break
                if not ok:return False
        return True
    sm=SN.get(net)
    fin=[]
    def rec(a):
        if segok(*a[:4],net,a[4]):fin.append(a);return
        if math.hypot(a[2]-a[0],a[3]-a[1])<0.35:fin.append(a);return
        mx,my=(a[0]+a[2])/2,(a[1]+a[3])/2
        rec((a[0],a[1],mx,my,a[4]));rec((mx,my,a[2],a[3],a[4]))
    for a in out:rec(a)
    for a in fin:
        add_seg(a[0],a[1],a[2],a[3],net,a[4])
        marksegv(a[0],a[1],a[2],a[3],0.40,net)
        if a[4] in occ:markseg(a[4],a[0],a[1],a[2],a[3],0.30)

def itemcells(it,goalmode):
    kind,x,y,typ,net,ref,lay,layp=it
    sm=sameset(net) if net else {L:set() for L in RLAY}
    if typ=='Via':
        out=[]
        ci,cj=cell(x,y)
        for L in RLAY:
            for ii in range(ci-3,ci+4):
                for jj in range(cj-3,cj+4):
                    px,py=cpnt(ii,jj)
                    if math.hypot(px-x,py-y)<0.3:out.append((x,y,L) if not goalmode else (ii,jj,L))
        return out
    lays=[layp] if layp in RLAY else list(RLAY)
    ci,cj=cell(x,y)
    out=[]
    for layv in lays:
        for ii in range(ci-4,ci+5):
            for jj in range(cj-4,cj+5):
                px,py=cpnt(ii,jj)
                if math.hypot(px-x,py-y)<0.4:
                    if goalmode:out.append((ii,jj,layv))
                    else:out.append((x,y,layv))
    return out

ok=0;fails=[]
for A,B in pairs:
    ka,xa,ya,ta,na,ra,la,lap=A
    kb,xb,yb,tb,nb,rb,lb,lbp=B
    if na!=nb:continue
    pts=itemcells(A,False)
    g=set(itemcells(B,True))
    # also accept reaching any same-net copper near B
    if nb:
        sm=sameset(nb)
        blay=lbp if lbp in RLAY else (None if tb!='Via' else None)
        if tb=='Via':
            ci,cj=cell(xb,yb)
            for L in RLAY:
                for ii in range(ci-3,ci+4):
                    for jj in range(cj-3,cj+4):
                        px,py=cpnt(ii,jj)
                        if math.hypot(px-xb,py-yb)<0.32:g.add((ii,jj,L))
    p=astar(na,pts,g)
    if p:
        emit(na,p,(xa,ya),(xb,yb));ok+=1
        # endpoint on non-routable layer -> drop via at the item
        for (it,cells) in ((A,pts),(B,None)):
            k2,x2,y2,t2,n2,r2,l2,lp2=it
            if t2 in('Track','Arc') and lp2 not in RLAY:
                ci,cj=cell(x2,y2)
                vb=VBLKN.get((ci,cj))
                if vb is None or vb<={na}:add_via(x2,y2,na)
    else:fails.append((na,xa,ya,ta,la,'|',xb,yb,tb,lb))
# fallback: micro-gap cross-layer / via joins
fixed2=0;still=[]
for (na,xa,ya,ta,la,_,xb,yb,tb,lb) in fails:
    d=math.hypot(xb-xa,yb-ya)
    lap=LAYMAP.get(la);lbp=LAYMAP.get(lb)
    done=False
    if d<1.6:
        mx,my=(xa+xb)/2,(ya+yb)/2
        for r in [0.25,0.4,0.6,0.9,1.2]:
            for adeg in range(0,360,20):
                vx,vy=mx+r*math.cos(math.radians(adeg)),my+r*math.sin(math.radians(adeg))
                ci,cj=cell(vx,vy)
                vb=VBLKN.get((ci,cj))
                if vb and not vb<={na}:continue
                add_via(vx,vy,na)
                # stub each item to via on its own layer
                for (x_,y_,l_) in ((xa,ya,lap),(xb,yb,lbp)):
                    if l_ in RLAY:add_seg(x_,y_,vx,vy,na,l_)
                markv(vx,vy,0.70,na);marksegv(xa,ya,vx,vy,0.40,na);marksegv(xb,yb,vx,vy,0.40,na)
                if lap in occ:markseg(lap,xa,ya,vx,vy,0.30)
                if lbp in occ:markseg(lbp,xb,yb,vx,vy,0.30)
                fixed2+=1;done=True;break
            if done:break
    if not done:still.append((na,xa,ya,ta,la,'|',xb,yb,tb,lb))
print('routed',ok,'via-fixed',fixed2,'fails',len(still))
for f in still[:30]:print('FAIL',f)
board.Save(BASE+'vitalq_v2.kicad_pcb')
print('saved p6')
