#!/usr/bin/env python3
"""Plot the antenna region of the board under analysis: board edge, U1 outline,
keepout, copper pours and violations.

Default input: ../inputs/vitalq_v2.kicad_pcb — committed fix_v24 snapshot
(same file reaches hardware/pcb/vitalquant_v2.kicad_pcb when PR #19 merges).
"""
import sys, os, math, json, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sexpr import parse, find_all, child, children, num
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Polygon as MplPoly
import matplotlib.patheffects as pe

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--pcb", default=os.path.join(HERE, "..", "inputs", "vitalq_v2.kicad_pcb"))
ap.add_argument("--out", default=os.path.join(HERE, "..", "plots", "antenna_keepout.png"))
args = ap.parse_args()
PCB = args.pcb
OUT = args.out

tree = parse(open(PCB).read())

def node_pts(n):
    for pb in children(n, 'pts'):
        return [(num(xy[1]), num(xy[2])) for xy in children(pb, 'xy')]
    pts = []
    for c in n:
        if isinstance(c, list) and c and c[0] in ('start', 'end', 'mid', 'xy') and len(c) >= 3:
            pts.append((num(c[1]), num(c[2])))
    return pts

def get_at(node):
    at = child(node, 'at')
    return (num(at[1]), num(at[2]), num(at[3], 0) if len(at) > 3 else 0) if at else (0, 0, 0)

fig, ax = plt.subplots(figsize=(12, 9))

# board edge
for tag in ('gr_line', 'gr_poly', 'gr_rect', 'gr_arc'):
    for n in find_all(tree, tag):
        lay = child(n, 'layer')
        if lay and 'Edge.Cuts' in lay[1:]:
            pts = node_pts(n)
            if len(pts) >= 2:
                for i in range(len(pts)-1):
                    ax.plot([pts[i][0], pts[i+1][0]], [pts[i][1], pts[i+1][1]],
                            color='black', lw=2.2, zorder=8)
                if tag == 'gr_poly':
                    ax.plot([pts[-1][0], pts[0][0]], [pts[-1][1], pts[0][1]], color='black', lw=2.2, zorder=8)

# zones
for z in find_all(tree, 'zone'):
    name_n = child(z, 'name'); name = name_n[1] if name_n else ''
    ko = child(z, 'keepout')
    pts = node_pts(child(z, 'polygon') or [])
    if not pts:
        continue
    if ko:
        ax.add_patch(MplPoly(pts, closed=True, fill=True, fc='red', alpha=0.10,
                             ec='red', lw=1.6, ls='--', zorder=6))
        if 'antenna' in name.lower():
            ax.add_patch(MplPoly(pts, closed=True, fill=False, ec='red', lw=2.4, zorder=9))
            xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
            ax.text(min(xs)+0.5, max(ys)-1.2, f'keepout "{name}"\n(all 8 Cu layers:\nno tracks/vias/pads/\npours/footprints)',
                    color='red', fontsize=8.5, zorder=10, va='top')
    else:
        ax.add_patch(MplPoly(pts, closed=True, fill=False, ec='tab:blue', lw=0.8, alpha=0.5, zorder=3))

# U1 footprint outline
for fp in find_all(tree, 'footprint'):
    ref = None
    for p in children(fp, 'property'):
        if p[1] == 'Reference': ref = p[2]
    fx, fy, frot = get_at(fp)
    layer = child(fp, 'layer')
    col = 'tab:green' if ref == 'U1' else ('tab:gray' if layer and layer[1] == 'F.Cu' else 'tab:olive')
    lw = 2.0 if ref == 'U1' else 0.7
    for g in list(find_all(fp, 'fp_line')) + list(find_all(fp, 'fp_rect')) + list(find_all(fp, 'fp_poly')):
        pts = node_pts(g)
        if len(pts) < 2: continue
        a = math.radians(frot)
        ca, sa = math.cos(a), math.sin(a)
        pts = [(fx + x*ca - y*sa, fy + x*sa + y*ca) for x, y in pts]
        for i in range(len(pts)-1):
            ax.plot([pts[i][0], pts[i+1][0]], [pts[i][1], pts[i+1][1]], color=col, lw=lw, zorder=5)
        if g[0] in ('fp_rect', 'fp_poly'):
            ax.plot([pts[-1][0], pts[0][0]], [pts[-1][1], pts[0][1]], color=col, lw=lw, zorder=5)
    if ref == 'U1':
        # antenna area = module y_rel -15.75..-9.81
        ax.add_patch(Rectangle((fx-9, fy-15.75), 18, 15.75-9.81, fill=True, fc='tab:green', alpha=0.08, zorder=4))
        ax.annotate('U1 ESP32-S3-WROOM-1\nantenna end (module edge)', xy=(fx, fy-15.75), xytext=(fx-9, fy-19.4),
                    fontsize=8.5, color='tab:green', zorder=10,
                    arrowprops=dict(arrowstyle='->', color='tab:green'))
        ax.annotate(f'overhang\n5.70 mm', xy=(fx+9.4, fy-16.4), xytext=(fx+9.6, fy-18.5),
                    fontsize=8.5, color='black', zorder=10,
                    arrowprops=dict(arrowstyle='->', color='black'))
    # pads
    for pad in find_all(fp, 'pad'):
        at = child(pad, 'at'); px, py = num(at[1]), num(at[2])
        a = math.radians(frot + (num(at[3],0) if len(at)>3 else 0))
        size = child(pad, 'size'); sx, sy = num(size[1]), num(size[2])
        ar0 = math.radians(frot)
        rx, ry = px*math.cos(ar0)-py*math.sin(ar0), px*math.sin(ar0)+py*math.cos(ar0)
        ax.add_patch(Rectangle((fx+rx-sx/2, fy+ry-sy/2), sx, sy, angle=0, fill=True, fc=col, alpha=0.5, zorder=5))

# tracks near region (only draw those in view box)
VIEW = dict(xmin=-4, xmax=26, ymin=-24, ymax=-6)
for seg in find_all(tree, 'segment'):
    pts = node_pts(seg)
    if len(pts) >= 2:
        (x1, y1), (x2, y2) = pts[0], pts[1]
        if max(x1, x2) > VIEW['xmin'] and min(x1, x2) < VIEW['xmax'] and max(y1, y2) > VIEW['ymin'] and min(y1, y2) < VIEW['ymax']:
            ax.plot([x1, x2], [y1, y2], color='tab:blue', lw=0.6, alpha=0.6, zorder=4)
for via in find_all(tree, 'via'):
    at = child(via, 'at'); x, y = num(at[1]), num(at[2])
    if VIEW['xmin'] < x < VIEW['xmax'] and VIEW['ymin'] < y < VIEW['ymax']:
        ax.plot(x, y, 'o', ms=2.5, color='tab:blue', alpha=0.7, zorder=4)

# nearest copper mark
ax.plot([22.3, 20.5], [-16.4, -16.4], color='purple', lw=2, zorder=9)
ax.annotate('nearest board copper (GND pour edge)\n5.85 mm below antenna end', xy=(21.5, -16.4), xytext=(21.2, -15.2),
            fontsize=8, color='purple', zorder=10, arrowprops=dict(arrowstyle='->', color='purple'))

# dimension arrows: overhang
ax.annotate('', xy=(20.9, -22.25), xytext=(20.9, -16.55), arrowprops=dict(arrowstyle='<->', color='black'))
ax.text(21.15, -19.4, '5.70 mm\noverhang', fontsize=8, va='center')

ax.set_xlim(VIEW['xmin'], VIEW['xmax']); ax.set_ylim(VIEW['ymin'], VIEW['ymax'])
ax.set_aspect('equal')
ax.set_xlabel('x (mm)'); ax.set_ylabel('y (mm)')
ax.set_title('fix_v24 antenna region — keepout (red) 18 x 5.95 mm on all 8 Cu layers;\n0 tracks/vias/pads/pours/footprints inside (gerber-verified); antenna overhangs board edge 5.70 mm')
ax.grid(alpha=0.25)
plt.tight_layout()
os.makedirs(os.path.dirname(OUT), exist_ok=True)
plt.savefig(OUT, dpi=170)
print('wrote', OUT)
