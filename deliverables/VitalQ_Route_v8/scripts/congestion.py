"""Congestion heatmap: track+via+pad density per board area + unconnected endpoints."""
import json, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

g = json.load(open('geom.json'))
d = json.load(open('drc_baseline_quilter_1_1.json'))

X0, X1, Y0, Y1 = -2.0, 48.0, -10.5, 64.5
BW = 1.0  # 1mm bins
nx, ny = int((X1 - X0) / BW) + 1, int((Y1 - Y0) / BW) + 1
heat = np.zeros((ny, nx))

def add(x, y, wgt):
    gx, gy = int((x - X0) / BW), int((y - Y0) / BW)
    if 0 <= gx < nx and 0 <= gy < ny:
        heat[gy, gx] += wgt

for t in g['tracks']:
    n = max(1, int(np.hypot(t['x2'] - t['x1'], t['y2'] - t['y1']) / BW))
    for i in range(n + 1):
        f = i / n
        add(t['x1'] + (t['x2'] - t['x1']) * f, t['y1'] + (t['y2'] - t['y1']) * f, 1.0 / n)
for v in g['vias']:
    add(v['x'], v['y'], 1.5)
for p in g['pads']:
    add(p['x'], p['y'], 0.5)

fig, ax = plt.subplots(figsize=(11, 16))
ax.set_facecolor('#111')
im = ax.imshow(heat, extent=[X0, X1, Y1, Y0], cmap='hot', alpha=0.85,
               interpolation='bilinear', vmin=0, vmax=np.percentile(heat, 99))
# board outline
edge = g['edge']
if edge:
    xs = [p[0] for p in edge] + [edge[0][0]]
    ys = [p[1] for p in edge] + [edge[0][1]]
    ax.plot(xs, ys, 'w-', lw=1.2)
# unconnected endpoints
ex, ey = [], []
for ui in d['unconnected_items']:
    for it in ui['items']:
        ex.append(it['pos']['x']); ey.append(it['pos']['y'])
ax.scatter(ex, ey, s=45, facecolor='none', edgecolor='#00ffff', lw=1.4, zorder=10, label='unconnected endpoint')
# keepouts outline
for k in g['keepouts']:
    if k['name'] in ('hv_inner', 'antenna_keepout', 'hole_keepout'):
        xs = [p[0] for p in k['poly']] + [k['poly'][0][0]]
        ys = [p[1] for p in k['poly']] + [k['poly'][0][1]]
        ax.plot(xs, ys, 'y:', lw=0.5)
ax.set_xlim(X0, X1); ax.set_ylim(Y1, Y0)
ax.set_title('Copper density heatmap + unconnected endpoints (cyan)')
ax.legend(loc='upper right', fontsize=8)
plt.colorbar(im, fraction=0.03, label='copper items / mm²')
plt.tight_layout()
plt.savefig('renders/congestion.png', dpi=130)
print('wrote renders/congestion.png')
