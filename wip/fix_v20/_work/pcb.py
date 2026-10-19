#!/usr/bin/env python3
"""Minimal KiCad 10 .kicad_pcb parser for add-only routing work."""
import re, math
from dataclasses import dataclass, field

def blocks(t, key, start=0):
    out=[]; i=start; tag='('+key
    while True:
        j=t.find(tag,i)
        if j<0: break
        c=t[j+len(tag):j+len(tag)+1]
        if c and c not in ' \t\n':
            i=j+len(tag); continue
        d=0;k=j
        while k<len(t):
            if t[k]=='(': d+=1
            elif t[k]==')':
                d-=1
                if d==0: break
            k+=1
        out.append((j,k+1,t[j:k+1])); i=k+1
    return out

def pt(b,tag):
    m=re.search(r'\(%s ([-\d.e]+) ([-\d.e]+)[ )]'%tag,b)
    return (float(m.group(1)),float(m.group(2))) if m else None

def prop(b,name):
    m=re.search(r'\(property "%s" "([^"]*)"'%re.escape(name),b)
    return m.group(1) if m else None

@dataclass
class Pad:
    num:str; at:tuple; size:tuple; layers:list; net:str; shape:str; rot:float=0.0
    drill:float=None; roundrect_rratio:float=0.0

@dataclass
class FP:
    ref:str; value:str; lib:str; at:tuple; rot:float; layer:str
    pads:list=field(default_factory=list)
    crtyd:list=field(default_factory=list)   # list of (x1,y1,x2,y2) local
    raw:str=""

class PCB:
    def __init__(self, path):
        self.path=path
        self.txt=open(path).read()
        t=self.txt
        self.segs=[]   # (start,end,width,layer,net)
        self.vias=[]   # (at,size,drill,layers,net)
        self.arcs=[]   # (start,mid,end,width,layer,net)
        self.fps=[]
        self.zones=[]  # dicts
        self.gr=[]     # edge cuts lines/polys
        self._parse()

    def _parse(self):
        t=self.txt
        for _,_,b in blocks(t,'segment'):
            self.segs.append(dict(start=pt(b,'start'),end=pt(b,'end'),
                width=float(re.search(r'\(width ([\d.e-]+)\)',b).group(1)),
                layer=re.search(r'\(layer "([^"]+)"\)',b).group(1),
                net=(re.search(r'\(net "([^"]+)"\)',b) or [None,None])[1] if re.search(r'\(net "([^"]+)"\)',b) else None))
        for _,_,b in blocks(t,'via'):
            self.vias.append(dict(at=pt(b,'at'),
                size=float(re.search(r'\(size ([\d.e-]+)\)',b).group(1)),
                drill=float(re.search(r'\(drill ([\d.e-]+)\)',b).group(1)),
                layers=re.search(r'\(layers ([^)]+)\)',b).group(1).replace('"','').split(),
                net=re.search(r'\(net "([^"]+)"\)',b).group(1) if re.search(r'\(net "([^"]+)"\)',b) else None))
        for _,_,b in blocks(t,'arc'):
            self.arcs.append(dict(start=pt(b,'start'),mid=pt(b,'mid'),end=pt(b,'end'),
                width=float(re.search(r'\(width ([\d.e-]+)\)',b).group(1)),
                layer=re.search(r'\(layer "([^"]+)"\)',b).group(1),
                net=re.search(r'\(net "([^"]+)"\)',b).group(1) if re.search(r'\(net "([^"]+)"\)',b) else None))
        # footprints
        for j,k,b in blocks(t,'footprint'):
            m=re.match(r'\(footprint "([^"]+)"',b)
            lib=m.group(1) if m else '?'
            at=pt(b,'at'); rot=0.0
            m3=re.search(r'\(at [-\d.e]+ [-\d.e]+ ([-\d.e]+)\)',b)
            if m3: rot=float(m3.group(1))
            lay=re.search(r'\(layer "([^"]+)"\)',b).group(1)
            fp=FP(ref=prop(b,'Reference') or '?',value=prop(b,'Value') or '',
                  lib=lib,at=at,rot=rot,layer=lay,raw=b)
            for _,_,pb in blocks(b,'pad'):
                mn=re.match(r'\(pad "([^"]*)"',pb)
                num=mn.group(1) if mn else ''
                pat=pt(pb,'at'); prot=0.0
                m3=re.search(r'\(at [-\d.e]+ [-\d.e]+ ([-\d.e]+)\)',pb)
                if m3: prot=float(m3.group(1))
                sz=re.search(r'\(size ([-\d.e]+) ([-\d.e]+)\)',pb)
                lays=re.search(r'\(layers ([^)]+)\)',pb)
                net=re.search(r'\(net "([^"]+)"\)',pb)
                shape=re.match(r'\(pad "[^"]+" (\w+)',pb)
                fp.pads.append(Pad(num=num,at=pat,rot=prot,
                    size=(float(sz.group(1)),float(sz.group(2))),
                    layers=lays.group(1).replace('"','').split() if lays else [],
                    net=net.group(1) if net else None,
                    shape=shape.group(1) if shape else 'smd'))
            for _,_,rb in blocks(b,'fp_rect'):
                s=pt(rb,'start'); e=pt(rb,'end')
                lay=re.search(r'\(layer "([^"]+)"\)',rb).group(1)
                if 'CrtYd' in lay:
                    fp.crtyd.append((s[0],s[1],e[0],e[1]))
            self.fps.append(fp)
        # zones
        for _,_,b in blocks(t,'zone'):
            net=re.search(r'\(net "([^"]+)"\)',b)
            lays=re.findall(r'"([A-Za-z0-9_.]+)"',re.search(r'\(layers? ([^)]+)\)',b).group(1)) if re.search(r'\(layers? ([^)]+)\)',b) else []
            name=re.search(r'\(name "([^"]+)"\)',b)
            ko={}
            mk=re.search(r'\(keepout(.*?)\)\s*\(placement',b,re.S)
            filled='(fill yes' in b
            # boundary polygon(s): (polygon (pts (xy..)..))
            polys=[]
            for _,_,pb in blocks(b,'polygon'):
                for _,_,ptb in blocks(pb,'pts'):
                    pts=[(float(m.group(1)),float(m.group(2))) for m in re.finditer(r'\(xy ([-\d.e]+) ([-\d.e]+)\)',ptb)]
                    polys.append(pts)
            fills=[]
            for _,_,fb in blocks(b,'filled_polygon'):
                lay=re.search(r'\(layer "([^"]+)"\)',fb)
                for _,_,ptb in blocks(fb,'pts'):
                    pts=[(float(m.group(1)),float(m.group(2))) for m in re.finditer(r'\(xy ([-\d.e]+) ([-\d.e]+)\)',ptb)]
                    fills.append((lay.group(1) if lay else None,pts))
            self.zones.append(dict(net=net.group(1) if net else None,layers=lays,
                name=name.group(1) if name else '',filled=filled,polys=polys,fills=fills,raw=b))
        # edge cuts
        for key in ('gr_line','gr_poly','gr_rect','gr_arc','gr_circle','gr_curve'):
            for _,_,b in blocks(t,key):
                if 'Edge.Cuts' in b:
                    self.gr.append((key,b))

    def pad_global(self,fp,pad):
        """pad position in board coords (KiCad y-down: rot = CW in math frame)"""
        if pad.at is None or fp.at is None: return None
        lx,ly=pad.at
        r=math.radians(fp.rot)
        x=fp.at[0]+lx*math.cos(r)+ly*math.sin(r)
        y=fp.at[1]-lx*math.sin(r)+ly*math.cos(r)
        return (x,y)

    def local_to_global(self,fp,lx,ly):
        r=math.radians(fp.rot)
        return (fp.at[0]+lx*math.cos(r)+ly*math.sin(r),
                fp.at[1]-lx*math.sin(r)+ly*math.cos(r))

    def pads_on_net(self,net):
        out=[]
        for fp in self.fps:
            for p in fp.pads:
                if p.net==net:
                    out.append((fp.ref,fp.lib,p.num,self.pad_global(fp,p),p))
        return out

if __name__=='__main__':
    import sys
    p=PCB(sys.argv[1] if len(sys.argv)>1 else 'vitalq_v2.kicad_pcb')
    print("segs",len(p.segs),"vias",len(p.vias),"arcs",len(p.arcs),"fps",len(p.fps),"zones",len(p.zones))
    from collections import Counter
    print(Counter(z['name'] for z in p.zones).most_common(15))
