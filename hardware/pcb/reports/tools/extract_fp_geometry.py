#!/usr/bin/env python3
"""Extract per-footprint pad GEOMETRY from vitalq_v2.kicad_pcb.

For each requested reference designator (or all), dump every pad:
  number, type, shape, local x/y, size W x H, mask/paste overrides, layers.
Pure s-expression parse. Read-only wrt design files.
"""
import re, sys, math, json

PATH = sys.argv[1] if len(sys.argv) > 1 else "hardware/pcb/vitalq_v2.kicad_pcb"

src = open(PATH, encoding="utf-8").read()
TOK = re.compile(r'\s*([()]|"(?:[^"\\]|\\.)*"|[^\s()]+)')
toks = TOK.findall(src)
pos = 0
def parse():
    global pos
    out = []
    while pos < len(toks):
        t = toks[pos]; pos += 1
        if t == '(':
            out.append(parse())
        elif t == ')':
            return out
        else:
            out.append(t)
    return out

def tstr(s):
    return s[1:-1] if s.startswith('"') else s

def tree():
    global pos
    root = []
    while pos < len(toks):
        t = toks[pos]
        if t == '(':
            pos += 1
            root.append(parse())
        else:
            pos += 1
            root.append(t)
    return root

doc = tree()[0]
assert doc[0] == 'kicad_pcb'

def children(node, name):
    return [ch for ch in node[1:] if isinstance(ch, list) and ch and ch[0] == name]

def child(node, name):
    c = children(node, name)
    return c[0] if c else None

fps = {}
for ch in doc[1:]:
    if not (isinstance(ch, list) and ch[0] == 'footprint'):
        continue
    lib = tstr(ch[1])
    ref = '?'
    for p in ch[1:]:
        if isinstance(p, list) and p[0] == 'property' and tstr(p[1]) == 'Reference':
            ref = tstr(p[2])
    pads = []
    for p in ch[1:]:
        if not (isinstance(p, list) and p[0] == 'pad'):
            continue
        num = tstr(p[1])
        ptype = p[2]
        shape = p[3]
        at = child(p, 'at') or []
        px = float(at[1]); py = float(at[2])
        prot = float(at[3]) if len(at) > 3 else 0.0
        size = child(p, 'size') or [None, 0, 0]
        w, h = float(size[1]), float(size[2])
        layers = child(p, 'layers')
        lyr = [tstr(x) for x in layers[1:]] if layers else []
        # mask / paste / clearances
        smr = child(p, 'solder_mask_margin')
        spr = child(p, 'solder_paste_margin')
        spsr = child(p, 'solder_paste_margin_ratio')
        smrr = child(p, 'solder_mask_margin_ratio')
        rrr = child(p, 'roundrect_rratio')
        chamfer = child(p, 'chamfer_ratio')
        drill = child(p, 'drill')
        # drill node can be (drill oval Dx Dy) or (drill D)
        dvals = [x for x in drill[1:] if x != 'oval'] if drill else []
        pads.append(dict(num=num, ptype=ptype, shape=shape,
                         x=px, y=py, rot=prot, w=w, h=h,
                         layers=lyr,
                         smm=float(smr[1]) if smr else None,
                         spm=float(spr[1]) if spr else None,
                         spmr=float(spsr[1]) if spsr else None,
                         smmr=float(smrr[1]) if smrr else None,
                         rr=float(rrr[1]) if rrr else None,
                         drill=[float(x) for x in dvals]))
    fps[ref] = dict(lib=lib, pads=pads)

def summarize(ref):
    f = fps[ref]
    xs = sorted({round(p['x'],4) for p in f['pads'] if p['ptype']=='smd'})
    ys = sorted({round(p['y'],4) for p in f['pads'] if p['ptype']=='smd'})
    def diffs(v):
        return sorted({round(abs(v[i+1]-v[i]),3) for i in range(len(v)-1) if abs(v[i+1]-v[i])>0.01})
    return dict(lib=f['lib'], n=len(f['pads']),
                xpitch=diffs(xs), ypitch=diffs(ys), pads=f['pads'])

out = {ref: summarize(ref) for ref in sorted(fps)}
json.dump(out, sys.stdout, indent=1)
print()
