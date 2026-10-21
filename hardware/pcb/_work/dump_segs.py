#!/usr/bin/env python3
# Dump every segment/via in region.
import re, json

src = open('vitalq_v2.kicad_pcb').read().splitlines()

def parse_kv(line, tag):
    m = re.search(r'\(%s\s+([^)]*)\)' % tag, line)
    return m.group(1).split() if m else None

X0,X1,Y0,Y1 = 20.0, 38.0, -11.0, 0.5
segs=[]; vias=[]
i=0; n=len(src)
while i<n:
    ln=src[i]
    m=re.match(r'^\t\((segment|via)\b',ln)
    if not m: i+=1; continue
    kind=m.group(1); block=[ln]; d=ln.count('(')-ln.count(')'); i+=1
    while i<n and d>0:
        block.append(src[i]); d+=src[i].count('(')-src[i].count(')'); i+=1
    if kind=='segment':
        sx=sy=ex=ey=None; lay=net=None; w=None
        for l in block:
            v=parse_kv(l,'start')
            if v: sx,sy=float(v[0]),float(v[1])
            v=parse_kv(l,'end')
            if v: ex,ey=float(v[0]),float(v[1])
            v=parse_kv(l,'layer')
            if v: lay=v[0].strip('"')
            v=parse_kv(l,'net')
            if v: net=v[0].strip('"')
            v=parse_kv(l,'width')
            if v: w=float(v[0])
        if sx is None: continue
        inR = (X0<=sx<=X1 or X0<=ex<=X1) and (Y0<=sy<=Y1 or Y0<=ey<=Y1)
        if not inR:
            inR = min(sx,ex)<X1 and max(sx,ex)>X0 and min(sy,ey)<Y1 and max(sy,ey)>Y0
        if inR: segs.append(dict(n=net,l=lay,w=w,s=[sx,sy],e=[ex,ey]))
    else:
        ax=ay=None; net=None
        for l in block:
            v=parse_kv(l,'at')
            if v: ax,ay=float(v[0]),float(v[1])
            v=parse_kv(l,'net')
            if v: net=v[0].strip('"')
        if ax and X0<=ax<=X1 and Y0<=ay<=Y1: vias.append(dict(n=net,p=[ax,ay]))

json.dump({'segs':segs,'vias':vias}, open('_work/region.json','w'), indent=0)
print(len(segs),'segs',len(vias),'vias')
for s in sorted(segs,key=lambda s:(s['l'],s['n'])):
    print('%-14s %-7s w=%s  (%.3f,%.3f)-(%.3f,%.3f)' % (s['n'],s['l'],s['w'],*s['s'],*s['e']))
print('--- VIAS ---')
for v in sorted(vias,key=lambda v:(v['n'],v['p'])):
    print('%-14s (%.3f,%.3f)' % (v['n'],*v['p']))
