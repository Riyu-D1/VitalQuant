#!/usr/bin/env python3
"""Check exported gerbers for copper inside the antenna keepout rect.
Keepout (board mm): x 2.5..20.5, y -22.25..-16.3 (KiCad exports keep board coords,
%TF.SameCoordinates,Original).
Counts D01 draws / D03 flashes / G36 region fill inside the rect.
For draws/flashes also expands the test point by half the aperture size.
"""
import re, os, json, math

GD = '/Users/deepakdiwan/Downloads/VitalQ_Hand/sim_v24/_work/gerbers/'
# NOTE: KiCad gerber export negates Y vs .kicad_pcb coords.
# Keepout in pcb coords: x 2.5..20.5, y -22.25..-16.3 -> gerber Y 16.3..22.25
KO = dict(xmin=2.5, xmax=20.5, ymin=16.3, ymax=22.25)
# sliver of keepout that lies INSIDE the board edge (edge pcb y=-16.55 -> gerber 16.55)
KO_BAND = dict(xmin=2.5, xmax=20.5, ymin=16.3, ymax=16.55)

def in_r(x, y, r):
    return r['xmin'] <= x <= r['xmax'] and min(r['ymin'], r['ymax']) <= y <= max(r['ymin'], r['ymax'])

def seg_hits_rect(x1, y1, x2, y2, r):
    # endpoints, midpoint, and 10-sample interpolation
    for t in [i/10.0 for i in range(11)]:
        x = x1 + (x2-x1)*t; y = y1 + (y2-y1)*t
        if in_r(x, y, r):
            return (x, y)
    return None

def parse_gerber(path):
    txt = open(path).read()
    txt = re.sub(r'%AM.*?\*%', '', txt, flags=re.S)          # aperture macros
    txt = re.sub(r'%TF.*?%', '', txt, flags=re.S)
    fmt = re.search(r'%FS[LT][AI]X(\d)(\d)Y(\d)(\d)\*%', txt)
    xi, xd, yi, yd = int(fmt.group(1)), int(fmt.group(2)), int(fmt.group(3)), int(fmt.group(4))
    def conv(s, dec):
        return float(s) / (10 ** dec)
    # aperture definitions %ADDnnShape,size...
    ap = {}
    for m in re.finditer(r'%ADD(\d+)([A-Za-z0-9_$+.]+?)(?:,(.*?))?\*%', txt, re.S):
        num_ = int(m.group(1)); shape = m.group(2); params = m.group(3) or ''
        size = 0.0
        pm = re.match(r'([0-9.]+)', params)
        if pm: size = float(pm.group(1))
        if shape == 'R':  # rect apertures "wXh"
            pm2 = re.match(r'([0-9.]+)[Xx]([0-9.]+)', params)
            if pm2: size = max(float(pm2.group(1)), float(pm2.group(2)))
        ap[num_] = (shape, size)
    ops = []
    cur_ap = None
    x = y = 0.0
    d01_mode = None   # current modal d-code when coord line has no D
    region = None
    for tok in txt.split('*'):
        t = tok.strip()
        if not t or t.startswith('G04') or t.startswith('%'):
            continue
        if t == 'G36':
            region = []
            continue
        if t == 'G37':
            if region is not None:
                ops.append(('region', region))
            region = None
            continue
        m = re.fullmatch(r'D(\d+)', t)
        if m:
            n = int(m.group(1))
            if n == 1: d01_mode = 1
            elif n == 2: d01_mode = 2
            elif n == 3: d01_mode = 3
            elif n >= 10: cur_ap = n
            continue
        m = re.fullmatch(r'X(-?\d+)?(?:Y(-?\d+))?(?:I(-?\d+)J(-?\d+))?(?:D(\d+))?', t) or \
            re.fullmatch(r'Y(-?\d+)(?:D(\d+))?', t)
        if m:
            gs = m.groups()
            if t.startswith('X'):
                X = conv(gs[0], xd) if gs[0] else x
                Y = conv(gs[1], yd) if len(gs) > 1 and gs[1] else y
                dc = gs[4] if len(gs) > 4 else None
            else:
                X = x
                Y = conv(gs[0], yd)
                dc = gs[1] if len(gs) > 1 else None
            dc = int(dc) if dc else d01_mode
            if region is not None:
                region.append((X, Y))
            elif dc == 1:
                ops.append(('draw', x, y, X, Y, ap.get(cur_ap, ('?', 0))[1]))
            elif dc == 3:
                ops.append(('flash', X, Y, ap.get(cur_ap, ('?', 0))[1]))
            x, y = X, Y
            continue
    return ops

report = {}
hits = []
for f in sorted(os.listdir(GD)):
    if not f.endswith(('.gtl', '.gbl', '.g1', '.g2', '.g3', '.g4', '.g5', '.g6')):
        continue
    ops = parse_gerber(os.path.join(GD, f))
    n_ko = n_band = 0
    for op in ops:
        if op[0] == 'draw':
            _, x1, y1, x2, y2, d = op
            h = seg_hits_rect(x1, y1, x2, y2, KO)
            if h: hits.append(dict(layer=f, type='draw', a=[x1, y1], b=[x2, y2], hit=list(h), ap=d)); n_ko += 1
            elif seg_hits_rect(x1, y1, x2, y2, KO_BAND): n_band += 1
        elif op[0] == 'flash':
            _, X, Y, d = op
            r = d/2.0
            if in_r(X, Y, KO) or (X+r > KO['xmin'] and X-r < KO['xmax'] and Y+r > KO['ymin'] and Y-r < KO['ymax']):
                hits.append(dict(layer=f, type='flash', at=[X, Y], ap=d)); n_ko += 1
            elif in_r(X, Y, KO_BAND): n_band += 1
        elif op[0] == 'region':
            pts = op[1]
            # vertex inside OR any polygon edge crosses rect
            inside = False
            for pt in pts:
                if in_r(pt[0], pt[1], KO): inside = True; break
            if not inside:
                for i in range(len(pts)):
                    a, b = pts[i], pts[(i+1) % len(pts)]
                    if seg_hits_rect(a[0], a[1], b[0], b[1], KO): inside = True; break
            if inside:
                hits.append(dict(layer=f, type='region', npts=len(pts), first=list(pts[0]))); n_ko += 1
    report[f] = dict(ops=len(ops), keepout=n_ko, band=n_band)
    print(f'{f}: {len(ops)} ops | in-keepout {n_ko} | in-board-sliver {n_band}')

print('\nTOTAL keepout hits:', len(hits))
for h in hits[:40]:
    print(' ', h)
json.dump(dict(report=report, hits=hits), open('/Users/deepakdiwan/Downloads/VitalQ_Hand/sim_v24/data/gerber_keepout_hits.json', 'w'), indent=1)
