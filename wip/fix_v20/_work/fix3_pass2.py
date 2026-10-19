import pcbnew, math, re, sys
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
MM=1e6
board=pcbnew.LoadBoard(BASE+'vitalq_v2.kicad_pcb')
oldb=pcbnew.LoadBoard(BASE+'_work/pre_promote_backup.kicad_pcb')
oldsig=set()
for t in oldb.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();oldsig.add(('v',round(p.x/MM,2),round(p.y/MM,2),t.GetNetname()))
    else:
        s,e=t.GetStart(),t.GetEnd()
        oldsig.add(('t',round(s.x/MM,2),round(s.y/MM,2),round(e.x/MM,2),round(e.y/MM,2),t.GetNetname(),t.GetLayer()))
def isnew(t):
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();return ('v',round(p.x/MM,2),round(p.y/MM,2),t.GetNetname()) not in oldsig
    s,e=t.GetStart(),t.GetEnd()
    return ('t',round(s.x/MM,2),round(s.y/MM,2),round(e.x/MM,2),round(e.y/MM,2),t.GetNetname(),t.GetLayer()) not in oldsig
TRKS=list(board.GetTracks())
# geometry index for old items: endpoints per net
endpts={}   # net -> list of (x,y)
for t in oldb.GetTracks():
    if isinstance(t,pcbnew.PCB_VIA):
        p=t.GetPosition();endpts.setdefault(t.GetNetname(),[]).append((p.x/MM,p.y/MM))
    else:
        s,e=t.GetStart(),t.GetEnd()
        endpts.setdefault(t.GetNetname(),[]).append((s.x/MM,s.y/MM))
        endpts.setdefault(t.GetNetname(),[]).append((e.x/MM,e.y/MM))
for m in oldb.GetFootprints():
    for pd in m.Pads():
        if pd.GetNetname():
            p=pd.GetPosition();endpts.setdefault(pd.GetNetname(),[]).append((p.x/MM,p.y/MM))
# pad rects of U1
u1=[m for m in board.GetFootprints() if m.GetReference()=='U1'][0]
padrects=[]
for pd in u1.Pads():
    bb=pd.GetBoundingBox();padrects.append((bb.GetX()/MM,bb.GetY()/MM,(bb.GetX()+bb.GetWidth())/MM,(bb.GetY()+bb.GetHeight())/MM))
def drect(px,py,r):
    dx=max(r[0]-px,0,px-r[2]);dy=max(r[1]-py,0,py-r[3])
    return math.hypot(dx,dy)

# === 1. snap new-track endpoints to nearest same-net copper end ===
snapped=0
for t in TRKS:
    if isinstance(t,pcbnew.PCB_VIA): continue
    if not isnew(t): continue
    nn=t.GetNetname()
    cands=endpts.get(nn,[])
    for which in (0,1):
        p=t.GetStart() if which==0 else t.GetEnd()
        px,py=p.x/MM,p.y/MM
        # does another same-net item touch this endpoint already?
        ok=False
        for cx,cy in cands:
            if math.hypot(cx-px,cy-py)<0.06: ok=True;break
        if ok: continue
        # find nearest end within 0.6mm
        best=None;bd=0.6
        for cx,cy in cands:
            d=math.hypot(cx-px,cy-py)
            if d<bd: bd=d;best=(cx,cy)
        if best:
            v=pcbnew.VECTOR2I(int(best[0]*MM),int(best[1]*MM))
            if which==0: t.SetStart(v)
            else: t.SetEnd(v)
            snapped+=1
print('snapped',snapped)

# === 2. trim ORIGINAL stub ends that sit within 0.35mm of any U1 pad (different net) ===
trimmed=0
for t in TRKS:
    if isinstance(t,pcbnew.PCB_VIA) or isnew(t): continue
    for which in (0,1):
        p=t.GetStart() if which==0 else t.GetEnd()
        px,py=p.x/MM,p.y/MM
        dm=min(drect(px,py,r) for r in padrects)
        if dm<0.35:
            q=t.GetEnd() if which==0 else t.GetStart()
            ox,oy=q.x/MM,q.y/MM
            ux,uy=px-ox,py-oy;L=math.hypot(ux,uy)
            if L<0.1:
                board.Remove(t);continue
            ux,uy=ux/L,uy/L
            nx_,ny_=px-ux*(0.4-dm),py-uy*(0.4-dm)
            v=pcbnew.VECTOR2I(int(nx_*MM),int(ny_*MM))
            if which==0: t.SetStart(v)
            else: t.SetEnd(v)
            trimmed+=1
print('trimmed',trimmed)
board.Save(BASE+'vitalq_v2.kicad_pcb')
print('saved')
