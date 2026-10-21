#!/usr/bin/env python3
# fix5.py — repair pass 5: fix remaining 12 violations
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

T = 'F.Cu'; L2 = 'In2.Cu'; L3 = 'In3.Cu'; L4 = 'In4.Cu'; L6 = 'In6.Cu'
W = 0.1017; WU = 0.129; WI = 0.5; WG = 0.3

# ===== RIP =====
# +3V3 hop: wrong layer (PWDN is In2), wrong via spot
RV('+3V3_ANA', 35.0, -1.55); R('+3V3_ANA', 35.0, -1.55, 35.2, -3.3, L2)
# SPARE_IN col x23.7 hits GND via (23.9,-5.6)
R('SPARE_IN', 25.2, -7.8, 23.7, -7.1, L6); R('SPARE_IN', 23.7, -7.1, 23.7, -3.5, L6)
R('SPARE_IN', 23.7, -3.5, 25.2, -2.9, L6); R('SPARE_IN', 25.2, -2.9, 25.2, -0.85, L6)
# IN1P via hits DP merge horiz
RV('IN1P', 30.05, -2.55); R('IN1P', 31.49, -1.9, 30.7, -1.9, T); R('IN1P', 30.7, -1.9, 30.05, -2.55, T)
R('IN1P', 30.05, -2.55, 29.921, -1.339, L2)
# VBUS east pad: via+stub hit SBU pad B8; In3 tail re-do
R('VBUS', 31.65, -4.3, 31.4, -4.3, T); RV('VBUS', 31.4, -4.3)
R('VBUS', 31.4, -4.3, 31.45, -5.5, L3); R('VBUS', 31.45, -5.5, 31.5, -6.6, L3)
# DP via1 -> merge window vs VBUS In3 col
RV('USB_DP', 29.2, -2.55); R('USB_DP', 29.2, -2.55, 28.2, -3.05, L2)

# ===== ADD =====
add = []
def A(net, pts, lay, w):
    for a, b in zip(pts, pts[1:]): add.append(('seg', net, lay, a[0], a[1], b[0], b[1], w))
def V(x, y, net, size=0.4004, drill=0.2002): add.append(('via', net, 'FB', x, y, size, drill))

# VBUS east pad: via-in-pad -> In3 south to node (31.5,-6.6)
V(31.95, -4.4, 'VBUS'); A('VBUS', [(31.95, -4.4), (31.3, -5.1), (31.5, -6.6)], L3, WI)
# DP via1 inside merge window, west of VBUS In3 col line
V(29.05, -2.55, 'USB_DP'); A('USB_DP', [(29.05, -2.55), (28.2, -3.05)], L2, WU)
# IN1P via further from DP merge horiz
V(30.3, -2.55, 'IN1P'); A('IN1P', [(31.49, -1.9), (30.9, -1.9), (30.3, -2.55)], T, W)
A('IN1P', [(30.3, -2.55), (29.921, -1.339)], L2, W)
# SPARE_IN In6 col x23.45 (clears GND via 23.9 and ESP_TX via 24.2)
A('SPARE_IN', [(25.2, -7.8), (23.45, -6.95), (23.45, -3.5), (25.2, -2.9), (25.2, -0.85)], L6, W)
# +3V3_ANA hop on In4 (empty corridor), via north of PWDN In2 wall
A('+3V3_ANA', [(35.0, -1.55), (34.9, -1.0)], T, W); V(34.9, -1.0, '+3V3_ANA')
A('+3V3_ANA', [(34.9, -1.0), (35.2, -3.3)], L4, W)

# ===== apply =====
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
