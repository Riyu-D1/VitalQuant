"""Analyze all unconnected items: endpoints, escape test, via sites, classification."""
import json, re, math, sys
from collections import deque
sys.path.insert(0, 'scripts')
import numpy as np
from router import Router, LNAME

d = json.load(open('drc_baseline_quilter_1_1.json'))
r = Router('geom.json')
REV = {0: 'F', 1: 'B', 2: 'G2', 3: 'L3', 4: 'P4', 5: 'G5'}

PLANE_NETS = {'GND', '+3V3', '+3V3_ANA', 'VBAT_SYS', 'VBUS', 'VDD_CP2102', '+1V8'}

def parse_item(desc):
    m = re.match(r"Pad (\S+) \[(.+?)\] of (\S+) on (.+)", desc)
    if m:
        return {'kind': 'pad', 'no': m.group(1), 'net': m.group(2), 'ref': m.group(3), 'layer': m.group(4)}
    m = re.match(r"(Track|Via|Arc) \[(.+?)\] on (.+?)(?:,|$)", desc)
    if m:
        return {'kind': m.group(1).lower(), 'net': m.group(2), 'layer': m.group(3)}
    m = re.match(r"(NPTH|PTH) pad of (\S+)", desc)
    if m:
        return {'kind': 'hole', 'ref': m.group(2)}
    return {'kind': 'other', 'raw': desc}

def pad_lookup(ref, no):
    for p in r.g['pads']:
        if p['ref'] == ref and p['no'] == no:
            return p
    return None

def flood_extent(mask, scell, maxcells=40000):
    """BFS on free cells from scell; returns (escaped_dist_mm or None, ncells)."""
    H, W = mask.shape
    sx, sy = scell
    if not (0 <= sx < W and 0 <= sy < H):
        return None, 0
    if mask[sy, sx]:
        # start itself blocked by foreign copper; try ring
        found = False
        for rad in range(1, 4):
            for dx in range(-rad, rad + 1):
                for dy in (-rad, rad):
                    nx, ny = sx + dx, sy + dy
                    if 0 <= nx < W and 0 <= ny < H and not mask[ny, nx]:
                        sx, sy = nx, ny; found = True; break
                if found: break
            if found: break
        if not found:
            return 0.0, 0
    seen = np.zeros_like(mask)
    q = deque([(sx, sy)])
    seen[sy, sx] = 1
    n = 0
    best = 0
    while q and n < maxcells:
        x, y = q.popleft()
        n += 1
        dd = math.hypot(x - scell[0], y - scell[1]) * r.res
        if dd > best: best = dd
        if dd > 4.0:
            return best, n
        for dx, dy in ((-1,0),(1,0),(0,-1),(0,1),(-1,-1),(1,1),(-1,1),(1,-1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < W and 0 <= ny < H and not mask[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = 1
                q.append((nx, ny))
    return best, n

def nearest_via(viaok, cell, maxd_mm=3.0):
    sx, sy = cell
    md = int(maxd_mm / r.res)
    x0, x1 = max(0, sx - md), min(r.W, sx + md + 1)
    y0, y1 = max(0, sy - md), min(r.H, sy + md + 1)
    sub = viaok[y0:y1, x0:x1]
    ys, xs = np.nonzero(sub)
    if len(ys) == 0:
        return None
    d2 = (xs + x0 - sx) ** 2 + (ys + y0 - sy) ** 2
    i = int(d2.argmin())
    gx, gy = int(xs[i] + x0), int(ys[i] + y0)
    return (gx * r.res + r.x0, gy * r.res + r.y0, math.sqrt(d2[i]) * r.res)

def over_zone(net, x, y):
    """Return list of layer indices where (x,y) is inside net's zone fill."""
    out = []
    for li, zm in (r.zone_own.get(net) or {}).items():
        gx, gy = r.cell(x, y)
        if zm[gy, gx]:
            out.append(li)
    return out

results = []
for ui in d['unconnected_items']:
    it = ui['items']
    a, b = it[0], it[1]
    pa, pb = parse_item(a['description']), parse_item(b['description'])
    ax, ay = a['pos']['x'], a['pos']['y']
    bx, by = b['pos']['x'], b['pos']['y']
    dist = math.hypot(ax - bx, ay - by)
    net = pa.get('net') or pb.get('net')
    rec = {'net': net, 'a': a['description'], 'b': b['description'],
           'ax': ax, 'ay': ay, 'bx': bx, 'by': by, 'dist': round(dist, 3)}

    blocked, goal, viaok = r.masks_for(net, 0.1016, 0.1016)

    for tag, P, x, y in (('A', pa, ax, ay), ('B', pb, bx, by)):
        info = {}
        if P['kind'] == 'pad':
            p = pad_lookup(P['ref'], P['no'])
            if p:
                li = 0 if p['side'] == 'F' else 1
                cell = r.cell(p['x'], p['y'])
                esc, ncells = flood_extent(blocked[li], cell)
                info['escape_mm'] = None if esc is None else round(esc, 2)
                info['layer'] = REV[li]
                vs = nearest_via(viaok, cell, 3.0)
                if vs:
                    info['via'] = (round(vs[0], 2), round(vs[1], 2), round(vs[2], 2))
                    info['via_over_zone'] = [REV[l] for l in over_zone(net, vs[0], vs[1])]
        elif P['kind'] in ('track', 'arc', 'via'):
            # endpoint is a dangling track end / via - find layer & test escape there
            ln = P.get('layer', '')
            li = {v: k for k, v in {'Top Layer':0,'Bottom Layer':1,'Ground Layer 2':2,'Layer 3':3,'Power Layer 4':4,'Ground Layer 5':5}.items()}.get(ln)
            if li is not None:
                cell = r.cell(x, y)
                esc, ncells = flood_extent(blocked[li], cell)
                info['escape_mm'] = None if esc is None else round(esc, 2)
                info['layer'] = REV[li]
        rec['info' + tag] = info

    # classification
    ia, ib = rec.get('infoA', {}), rec.get('infoB', {})
    aesc = ia.get('escape_mm'); besc = ib.get('escape_mm')
    via_z = ia.get('via_over_zone') or []
    if net in PLANE_NETS and (ia.get('via') or ib.get('via')):
        cls = 'PLANE' if (via_z or net == 'GND') else 'PLANE?'
    elif (aesc or 0) >= 1.0 and (besc or 0) >= 1.0:
        cls = 'EASY' if dist < 8 else 'MEDIUM'
    elif (aesc or 0) == 0 or (besc or 0) == 0:
        cls = 'HARD'
    else:
        cls = 'MEDIUM'
    rec['cls'] = cls
    results.append(rec)

json.dump(results, open('analysis_unconn.json', 'w'), indent=1)
for rec in results:
    ia, ib = rec.get('infoA', {}), rec.get('infoB', {})
    print(f"{str(rec['net']):>22} {rec['cls']:6} d={rec['dist']:6.2f} "
          f"A:{ia.get('layer','?'):2} esc={ia.get('escape_mm')} via={ia.get('via')}{ia.get('via_over_zone') or ''} | "
          f"B:{ib.get('layer','?'):2} esc={ib.get('escape_mm')}  || {rec['a'][:38]} <-> {rec['b'][:38]}")
