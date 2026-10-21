#!/usr/bin/env python3
# fix7.py — repair pass 7: last violation (CS_ADS1292 vs +3V3 via)
import re, uuid

FN = 'vitalq_v2.kicad_pcb'
src = open(FN).read()
lines = src.splitlines(True)
def block_lines(s):
    d = 0
    while True:
        d += lines[s].count('(') - lines[s].count(')'); s += 1
        if d <= 0: return s
segs = []; vias = []
i = 0; n = len(lines)
while i < n:
    m2 = re.match(r'^\t\((segment|via)\b', lines[i])
    if m2:
        j = block_lines(i)
        (segs if m2.group(1) == 'segment' else vias).append((i, j))
        i = j
    else: i += 1

def gk(l, t):
    r = re.search(r'\(%s\s+([^)]*)\)' % t, l); return r.group(1).split() if r else None
def parse(rng):
    net = lay = None; sx = sy = ex = ey = None
    for k in range(*rng):
        l = lines[k]
        v = gk(l, 'net')
        if v: net = v[0].strip('"')
        v = gk(l, 'start')
        if v: sx, sy = float(v[0]), float(v[1])
        v = gk(l, 'end')
        if v: ex, ey = float(v[0]), float(v[1])
        v = gk(l, 'at')
        if v: sx, sy, ex, ey = float(v[0]), float(v[1]), float(v[0]), float(v[1])
        v = gk(l, 'layer')
        if v: lay = v[0].strip('"')
        v = gk(l, 'layers')
        if v: lay = v[0].strip('"')
    return net, lay, sx, sy, ex, ey
seginfo = [(*parse((a, b)), a, b) for a, b in segs]
viainfo = [(*parse((a, b)), a, b) for a, b in vias]

EP = 0.011
def eq(a, b): return abs(a - b) < EP
rip = []
def R(net, x1, y1, x2, y2, lay):
    hit = 0
    for k, (nt, ly, sx, sy, ex, ey, a, b) in enumerate(seginfo):
        if k in rip: continue
        if nt == net and ly == lay and ((eq(sx, x1) and eq(sy, y1) and eq(ex, x2) and eq(ey, y2)) or (eq(sx, x2) and eq(sy, y2) and eq(ex, x1) and eq(ey, y1))):
            rip.append(k); hit += 1
    if hit != 1: print('RIP MISS', net, lay, x1, y1, x2, y2)
vrip = []
def RV(net, x, y):
    hit = 0
    for k, (nt, ly, sx, sy, ex, ey, a, b) in enumerate(viainfo):
        if k in vrip: continue
        if nt == net and eq(sx, x) and eq(sy, y): vrip.append(k); hit += 1
    if hit != 1: print('RIPV MISS', net, x, y)

T = 'F.Cu'; L4 = 'In4.Cu'; W = 0.1017

R('+3V3_ANA', 35.0, -1.55, 35.2, -0.4, T); RV('+3V3_ANA', 35.2, -0.4)
R('+3V3_ANA', 35.2, -0.4, 35.2, -3.3, L4)

add = []
def A(net, pts, lay, w):
    for a, b in zip(pts, pts[1:]): add.append(('seg', net, lay, a[0], a[1], b[0], b[1], w))
def V(x, y, net, size=0.4004, drill=0.2002): add.append(('via', net, 'FB', x, y, size, drill))

# +3V3 via east of CS diag (34.277,-1.173)-(35.464,0.014), north of MISO y-0.819, below CS horiz y0.014
A('+3V3_ANA', [(35.0, -1.55), (35.4, -0.8), (35.9, -0.35)], T, W); V(35.9, -0.35, '+3V3_ANA')
A('+3V3_ANA', [(35.9, -0.35), (35.6, -1.8), (35.2, -3.3)], L4, W)

kill = sorted(set([seginfo[k][6:] for k in rip] + [viainfo[k][6:] for k in vrip]), key=lambda t: -t[0])
for a, b in kill:
    del lines[a:b]
src = ''.join(lines)
ins = '\n'
for it in add:
    if it[0] == 'seg':
        _, net, lay, x1, y1, x2, y2, w = it
        ins += '\t(segment\n\t\t(start %.6f %.6f)\n\t\t(end %.6f %.6f)\n\t\t(width %.6f)\n\t\t(layer "%s")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n' % (x1, y1, x2, y2, w, lay, net, uuid.uuid4())
    else:
        _, net, _, x, y, size, drill = it
        ins += '\t(via\n\t\t(at %.6f %.6f)\n\t\t(size %.4f)\n\t\t(drill %.4f)\n\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n' % (x, y, size, drill, net, uuid.uuid4())
idx = src.rfind('\n)')
open(FN, 'w').write(src[:idx+1] + ins + ')')
print('ripped', len(rip), 'segs', len(vrip), 'vias | added', sum(1 for i in add if i[0] == 'seg'), 'segs', sum(1 for i in add if i[0] == 'via'), 'vias')
