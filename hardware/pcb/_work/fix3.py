#!/usr/bin/env python3
# fix3.py — repair pass 3: fix remaining 19 violations + 2 unconnected
import re, uuid

FN = 'vitalq_v2.kicad_pcb'
src = open(FN).read()

# ---------- footprint move: R2 -> (27.55,-1.4) ----------
m = re.search(r'\(property\s+"Reference"\s+"R2"', src)
fp_start = src.rfind('\t(footprint', 0, m.start())
fp_end = src.find('\t(footprint', fp_start + 1)
if fp_end != -1 and m.start() > fp_end:
    raise SystemExit('R2 ref not inside footprint block')
i = fp_start; depth = 0
while True:
    depth += src[i] == '('; depth -= src[i] == ')'
    if depth <= 0 and src[i] == ')': break
    i += 1
blk = src[fp_start:i+1]
blk2 = re.sub(r'\(at\s+27\.3\s+-1\.4', '(at 27.55 -1.4', blk, count=1)
assert blk2 != blk, 'R2 at not found'
src = src[:fp_start] + blk2 + src[i+1:]

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

T = 'F.Cu'; L2 = 'In2.Cu'; L3 = 'In3.Cu'; B = 'B.Cu'; L5 = 'In5.Cu'; L6 = 'In6.Cu'
W = 0.1017; WU = 0.129; WV = 0.5; WI = 0.5; WG = 0.3

# ===== RIP =====
# GND rear stub (ECG_P short) + R2 pad2 stub
R('GND', 23.8, -8.3, 24.0, -8.9, T); R('GND', 24.0, -8.9, 24.75, -8.85, T)
R('GND', 27.3, -0.89, 27.3, -0.15, T); RV('GND', 27.3, -0.15)
# +3V3_ANA tail (C32 GND stub collision)
R('+3V3_ANA', 35.0, -1.55, 35.4, -1.95, T); R('+3V3_ANA', 35.4, -1.95, 35.4, -3.0, T)
R('+3V3_ANA', 35.4, -3.0, 35.807, -3.612, T)
# ESP_TX tail (CC1 crossing) — redo with earlier via-hop
R('ESP_TX', 31.516, -6.78, 33.4, -7.3, T); R('ESP_TX', 33.4, -7.3, 34.9, -7.5, T)
R('ESP_TX', 34.9, -7.5, 35.3, -7.3, T); RV('ESP_TX', 35.3, -7.3)
R('ESP_TX', 35.3, -7.3, 36.35, -8.35, B)
# CC1 stub (DM merge collision)
R('USB_CC1', 30.75, -3.755, 30.75, -4.5, T); R('USB_CC1', 30.75, -4.5, 30.3, -5.0, T)
R('USB_CC1', 30.3, -5.0, 30.0, -5.5, T); R('USB_CC1', 30.0, -5.5, 30.0, -6.426, T)
# DM escape (DP proximity)
R('USB_DM', 29.25, -4.75, 28.5, -5.6, T); R('USB_DM', 28.5, -5.6, 26.8, -6.35, T)
R('USB_DM', 26.8, -6.35, 25.0, -6.3, T); R('USB_DM', 25.0, -6.3, 23.925, -6.287, T)
# DP escape (DM proximity)
R('USB_DP', 28.75, -4.6, 28.2, -5.0, T); R('USB_DP', 28.2, -5.0, 27.6, -5.7, T)
R('USB_DP', 27.6, -5.7, 26.0, -6.15, T); R('USB_DP', 26.0, -6.15, 24.6, -6.05, T)
R('USB_DP', 24.6, -6.05, 23.793, -6.086, T)
# SPARE_IN jog (ESP_TX via hole short)
R('SPARE_IN', 25.2, -7.8, 25.2, -7.2, L6); R('SPARE_IN', 25.2, -7.2, 24.45, -6.6, L6)
R('SPARE_IN', 24.45, -6.6, 24.35, -6.3, L6); R('SPARE_IN', 24.35, -6.3, 24.35, -3.5, L6)
R('SPARE_IN', 24.35, -3.5, 25.2, -2.9, L6); R('SPARE_IN', 25.2, -2.9, 25.2, -0.85, L6)
# VBUS: trunk via too close to INT col; east via too close to DRDY col
RV('VBUS', 26.85, 0.8); R('VBUS', 26.85, 0.8, 28.3, -1.2, L3)
RV('VBUS', 31.5, -5.05); R('VBUS', 31.95, -4.5, 31.6, -4.9, T); R('VBUS', 31.6, -4.9, 31.5, -5.05, T)
R('VBUS', 31.5, -5.05, 31.5, -6.6, L3)
# CC2 via2 too close to DRDY y-1.66 B.Cu
RV('USB_CC2', 28.4, -1.7); R('USB_CC2', 27.75, -2.55, 28.4, -1.7, L2)
R('USB_CC2', 28.4, -1.7, 28.3, -1.91, T); R('USB_CC2', 28.3, -1.91, 27.57, -1.91, T)
# IN1P via too close to VBUS In3 + DRDY
RV('IN1P', 30.0, -1.2); R('IN1P', 31.49, -1.9, 30.9, -1.5, T); R('IN1P', 30.9, -1.5, 30.0, -1.2, T)
R('IN1P', 30.0, -1.2, 29.921, -1.339, L2)
# DP via1 too close to VBUS In3 col
RV('USB_DP', 29.65, -2.55); R('USB_DP', 29.65, -2.55, 28.2, -3.05, L2)

# ===== ADD =====
add = []
def A(net, pts, lay, w):
    for a, b in zip(pts, pts[1:]): add.append(('seg', net, lay, a[0], a[1], b[0], b[1], w))
def V(x, y, net, size=0.4004, drill=0.2002): add.append(('via', net, 'FB', x, y, size, drill))

# GND rear-west ring tangent stub (clears ECG_P diag)
A('GND', [(23.8, -8.3), (24.1, -8.55), (24.68, -8.75)], T, WG)
# R2 pad2 GND stub + via (clear of SPI_SCK B.Cu y-0.432)
A('GND', [(27.55, -0.89), (27.55, -0.3), (27.4, 0.1)], T, WG); V(27.4, 0.1, 'GND')
# +3V3_ANA reroute west of C32 GND stub
A('+3V3_ANA', [(35.0, -1.55), (35.2, -2.0), (35.2, -3.45), (35.807, -3.612)], T, W)
# ESP_TX: earlier via-hop (clears CC1 diagonal)
A('ESP_TX', [(31.516, -6.78), (32.4, -7.0), (33.0, -7.1)], T, W); V(33.0, -7.1, 'ESP_TX')
A('ESP_TX', [(33.0, -7.1), (36.35, -8.35)], B, W)
# CC1: drop south between pads, join y-6.426 run
A('USB_CC1', [(30.75, -3.755), (30.75, -4.9), (30.45, -5.35), (30.0, -5.75), (30.0, -6.426)], T, W)
# DM escape deeper + tail split from DP
A('USB_DM', [(29.25, -4.75), (28.4, -5.7), (26.8, -6.4), (25.0, -6.42), (24.2, -6.35), (23.925, -6.287)], T, WU)
# DM island links: trunk<->col, escape<->trunk (west detour)
A('USB_DM', [(25.617, -2.55), (25.87, -2.55)], T, WU)
A('USB_DM', [(23.0, -6.55), (22.7, -6.45), (22.7, -3.6), (23.0, -3.4), (25.0, -3.35), (25.55, -3.3), (25.617, -3.2)], T, WU)
# DP escape (keeps NPTH clearance)
A('USB_DP', [(28.75, -4.6), (28.2, -5.0), (27.6, -5.7), (26.0, -6.15), (24.6, -6.05), (23.9, -6.03), (23.793, -6.086)], T, WU)
# SPARE_IN In6 jog clear of ESP_TX via (24.8,-6.78)
A('SPARE_IN', [(25.2, -7.8), (25.2, -7.7), (24.2, -6.7), (24.2, -3.5), (25.2, -2.9), (25.2, -0.85)], L6, W)
# VBUS trunk via clear of INT In4 col x27.01
V(26.45, 0.8, 'VBUS'); A('VBUS', [(26.45, 0.8), (28.3, -1.2)], L3, WI)
# VBUS east pad via clear of DRDY B.Cu col x31.2
A('VBUS', [(31.95, -4.5), (31.7, -5.0)], T, WV); V(31.7, -5.0, 'VBUS')
A('VBUS', [(31.7, -5.0), (31.6, -5.9), (31.5, -6.6)], L3, WI)
# CC2 via2 south of DRDY y-1.66
V(28.5, -2.1, 'USB_CC2'); A('USB_CC2', [(27.75, -2.55), (28.5, -2.1)], L2, W)
A('USB_CC2', [(28.5, -2.1), (28.35, -1.91), (27.81, -1.91)], T, W)
# IN1P via south, clear of VBUS In3 + DRDY
V(30.0, -2.1, 'IN1P'); A('IN1P', [(31.49, -1.9), (30.9, -1.6), (30.0, -2.1)], T, W)
A('IN1P', [(30.0, -2.1), (29.921, -1.339)], L2, W)
# DP via1 west, clear of VBUS In3 col (x~29.62 at y-2.55)
V(29.2, -2.55, 'USB_DP'); A('USB_DP', [(29.2, -2.55), (28.2, -3.05)], L2, WU)

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
