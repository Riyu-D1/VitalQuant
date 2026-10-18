"""Island-graph solver: BFS free-space components joined by legal via sites.
Runs at coarse grid for speed; coordinates stay in mm."""
import sys, json, math, collections
sys.path.insert(0,'scripts')
import numpy as np
from router import Router

GEO=None
def _g():
    global GEO
    if GEO is None: GEO=json.load(open('geom.json'))
    return GEO

def via_ok(net,x,y,vrad=0.15,clr=0.15):
    g=_g()
    for t in g['tracks']:
        if t['net']==net: continue
        x1,y1,x2,y2=t['x1'],t['y1'],t['x2'],t['y2']
        dx,dy=x2-x1,y2-y1; L2=dx*dx+dy*dy
        tt=0 if L2==0 else max(0,min(1,((x-x1)*dx+(y-y1)*dy)/L2))
        d=math.hypot(x1+tt*dx-x,y1+tt*dy-y)
        if d < vrad+clr+t['w']/2: return False
    for p in g['pads']:
        if p['net']==net: continue
        d=math.hypot(p['x']-x,p['y']-y)
        rr=max(p['w'],p['h'])/2
        if d < vrad+clr+rr: return False
    for v in g['vias']:
        if v['net']==net: continue
        d=math.hypot(v['x']-x,v['y']-y)
        if d < v['d']/2+vrad+clr or d < v['drill']/2+0.075+0.15: return False
    for k in g['keepouts']:
        if not k['via']: continue
        poly=k['poly']; ins=False; j=len(poly)-1
        for i in range(len(poly)):
            xi,yi=poly[i]; xj,yj=poly[j]
            if ((yi>y)!=(yj>y)) and (x < (xj-xi)*(y-yi)/(yj-yi)+xi): ins=not ins
            j=i
        if ins: return False
    if not (-2+0.2 < x < 48-0.2 and -10.5+0.2 < y < 64.5-0.2): return False
    return True

class Coarse:
    """Downsampled view of a Router's blocked masks (factor f, conservative OR)."""
    def __init__(self, r, net, f=4, layers=(0,1,3)):
        self.r=r; self.f=f; self.res=r.res*f
        blocked,goal,viaok=r.masks_for(net,0.1016,0.1016)
        self.free={}
        for li in layers:
            b=blocked[li]
            H,W=b.shape
            Hc,Wc=H//f,W//f
            bb=b[:Hc*f,:Wc*f].reshape(Hc,f,Wc,f).any(axis=(1,3))
            self.free[li]=~bb
        self.x0,self.y0=r.x0,r.y0
        self.Hc,self.Wc=self.free[layers[0]].shape
    def cell(self,x,y):
        return int(round((x-self.x0)/self.res)), int(round((y-self.y0)/self.res))
    def xy(self,cx,cy):
        return self.x0+cx*self.res, self.y0+cy*self.res

def label_comps(freem):
    H,W=freem.shape
    lab=np.zeros((H,W),dtype=np.int32); cur=0
    ys,xs=np.nonzero(freem)
    for x0,y0 in zip(xs.tolist(),ys.tolist()):
        if lab[y0,x0]: continue
        cur+=1
        dq=collections.deque([(x0,y0)]); lab[y0,x0]=cur
        while dq:
            x,y=dq.popleft()
            for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)):
                nx,ny=x+dx,y+dy
                if 0<=nx<W and 0<=ny<H and freem[ny,nx] and not lab[ny,nx]:
                    lab[ny,nx]=cur; dq.append((nx,ny))
    return lab,cur

def solve(net, p_from, p_to, layers=(0,1,3), f=4, scan_step=1):
    """p_from: (x,y) of source via (exists on all layers).
    p_to: (x,y) of destination pad (F only by default).
    Returns list of via positions [(x,y)...] chain plan or None."""
    r=Router('geom.json')
    c=Coarse(r,net,f,layers)
    labs={}; 
    for li in layers:
        labs[li],n=label_comps(c.free[li])
    src=c.cell(*p_from); dst=c.cell(*p_to)
    # source component per layer (via1 exists on all -> comp at its cell)
    srcs={(li,labs[li][src[1],src[0]]) for li in layers}
    dsts={(0,labs[0][dst[1],dst[0]])}   # pad on F
    # via_ok cells on coarse grid (subsample every scan_step cells)
    edges=collections.defaultdict(list)  # comp -> [(comp2, (x,y))]
    Hc,Wc=c.Hc,c.Wc
    for cy in range(0,Hc,scan_step):
        for cx in range(0,Wc,scan_step):
            x,y=c.xy(cx,cy)
            if not via_ok(net,x,y): continue
            cs={(li,labs[li][cy,cx]) for li in layers if labs[li][cy,cx]}
            cs={(l,c2) for l,c2 in cs if c2}
            for a in cs:
                for b in cs:
                    if a!=b: edges[a].append((b,(x,y)))
    # BFS on components from srcs to any dst comp
    start=set(srcs)
    prev={}; q=collections.deque(start)
    seen=set(start)
    target=None
    while q and target is None:
        comp=q.popleft()
        if comp in dsts: target=comp; break
        for nb,pt in edges.get(comp,[]):
            if nb not in seen:
                seen.add(nb); prev[nb]=(comp,pt); q.append(nb)
    if target is None: return None
    # reconstruct: list of via points + layer sequence
    chain=[]; cur=target
    while cur not in start:
        p,pt=prev[cur]; chain.append((pt,cur,p)); cur=p
    chain.reverse()
    vias=[pt for pt,a,b in chain]
    return vias, chain, (srcs,dsts)
