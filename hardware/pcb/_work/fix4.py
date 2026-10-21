#!/usr/bin/env python3
# fix4.py — repair pass 4: fix remaining 19 violations + 1 unconnected
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

T = 'F.Cu'; L2 = 'In2.Cu'; L3 = 'In3.Cu'; B = 'B.Cu'; L6 = 'In6.Cu'
W = 0.1017; WU = 0.129; WI = 0.5; WG = 0.3

# ===== RIP (fix3 additions that collided) =====
R('GND', 27.55, -0.89, 27.55, -0.3, T); R('GND', 27.55, -0.3, 27.4, 0.1, T); RV('GND', 27.4, 0.1)
RV('VBUS', 26.45, 0.8); R('VBUS', 26.45, 0.8, 28.3, -1.2, L3)
R('VBUS', 31.95, -4.5, 31.7, -5.0, T); RV('VBUS', 31.7, -5.0)
R('VBUS', 31.7, -5.0, 31.6, -5.9, L3); R('VBUS', 31.6, -5.9, 31.5, -6.6, L3)
RV('IN1P', 30.0, -2.1); R('IN1P', 31.49, -1.9, 30.9, -1.6, T); R('IN1P', 30.9, -1.6, 30.0, -2.1, T)
R('IN1P', 30.0, -2.1, 29.921, -1.339, L2)
R('USB_DM', 23.0, -6.55, 22.7, -6.45, T); R('USB_DM', 22.7, -6.45, 22.7, -3.6, T)
R('USB_DM', 22.7, -3.6, 23.0, -3.4, T); R('USB_DM', 23.0, -3.4, 25.0, -3.35, T)
R('USB_DM', 25.0, -3.35, 25.55, -3.3, T); R('USB_DM', 25.55, -3.3, 25.617, -3.2, T)
R('USB_DM', 26.8, -6.4, 25.0, -6.42, T); R('USB_DM', 25.0, -6.42, 24.2, -6.35, T)
R('USB_DM', 24.2, -6.35, 23.925, -6.287, T)
R('SPARE_IN', 25.2, -7.8, 25.2, -7.7, L6); R('SPARE_IN', 25.2, -7.7, 24.2, -6.7, L6)
R('SPARE_IN', 24.2, -6.7, 24.2, -3.5, L6); R('SPARE_IN', 24.2, -3.5, 25.2, -2.9, L6)
R('SPARE_IN', 25.2, -2.9, 25.2, -0.85, L6)
R('+3V3_ANA', 35.0, -1.55, 35.2, -2.0, T); R('+3V3_ANA', 35.2, -2.0, 35.2, -3.45, T)
R('+3V3_ANA', 35.2, -3.45, 35.807, -3.612, T)

# ===== ADD =====
add = []
def A(net, pts, lay, w):
    for a, b in zip(pts, pts[1:]): add.append(('seg', net, lay, a[0], a[1], b[0], b[1], w))
def V(x, y, net, size=0.4004, drill=0.2002): add.append(('via', net, 'FB', x, y, size, drill))

# R2 pad2 GND stub -> via east of TP18 pad (r0.5), clear of SCK via
A('GND', [(27.55, -0.89), (28.0, -0.3), (28.6, -0.1), (28.6, 0.55)], T, WG); V(28.6, 0.55, 'GND')
# VBUS trunk via: in the 0.03mm window between DP col (edge 26.314) and INT col (edge 26.945)
V(26.63, 0.8, 'VBUS'); A('VBUS', [(26.63, 0.8), (28.3, -1.2)], L3, WI)
# VBUS east pad: exit pad west edge -> via -> In3 to feed node
A('VBUS', [(31.65, -4.3), (31.4, -4.3)], T, 0.5); V(31.4, -4.3, 'VBUS')
A('VBUS', [(31.4, -4.3), (31.45, -5.5), (31.5, -6.6)], L3, WI)
# IN1P via south of VBUS In3 col
V(30.05, -2.55, 'IN1P'); A('IN1P', [(31.49, -1.9), (30.7, -1.9), (30.05, -2.55)], T, W)
A('IN1P', [(30.05, -2.55), (29.921, -1.339)], L2, W)
# DM link B at x22.28 (clears DP via 22.9 and GND via 22.677)
A('USB_DM', [(23.0, -6.55), (22.28, -6.5), (22.28, -3.6), (22.9, -3.45), (25.0, -3.35), (25.55, -3.3), (25.617, -3.2)], T, WU)
# DM escape tail north of ESP_TX via (24.8,-6.78) & (24.2,-6.9)
A('USB_DM', [(26.8, -6.4), (25.5, -6.38), (24.5, -6.38), (23.925, -6.287)], T, WU)
# SPARE_IN In6 col x23.7 (clears ESP_TX vias at 24.2/-6.9 & 24.8/-6.78)
A('SPARE_IN', [(25.2, -7.8), (23.7, -7.1), (23.7, -3.5), (25.2, -2.9), (25.2, -0.85)], L6, W)
# +3V3_ANA: via-hop the GND wall at y-2.69
V(35.0, -1.55, '+3V3_ANA'); A('+3V3_ANA', [(35.0, -1.55), (35.2, -3.3)], L2, W)
V(35.2, -3.3, '+3V3_ANA'); A('+3V3_ANA', [(35.2, -3.3), (35.807, -3.612)], T, W)

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
