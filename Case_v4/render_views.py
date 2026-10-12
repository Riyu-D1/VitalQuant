#!/usr/bin/env python3
"""Render case_v3 views to PNG (matplotlib, shaded mesh faces)."""
import numpy as np, trimesh, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from matplotlib.collections import LineCollection
from case_lib import board_solid

BASE = trimesh.load('case_base.stl')
TOP  = trimesh.load('case_top.stl')
BTN  = trimesh.load('button_plunger.stl')
PIPE = trimesh.load('light_pipe.stl')
CONT = trimesh.load('contacts_assy.stl')
BOARD = board_solid()

def hex2rgb(h):
    return np.array([int(h[i:i+2], 16)/255 for i in (1, 3, 5)])

COL = {'top': hex2rgb('#e8b04b'), 'base': hex2rgb('#4d7ea8'),
       'board': hex2rgb('#2f6b3f'), 'btn': hex2rgb('#d05353'),
       'pipe': hex2rgb('#f0e060'), 'cont': hex2rgb('#8a6a12')}

LIGHT = np.array([-0.45, -0.55, 0.7]); LIGHT /= np.linalg.norm(LIGHT)

def facecolors(m, base_rgb, alpha):
    """Lambert shading per face (double-sided)."""
    n = m.face_normals
    d = np.abs(n @ LIGHT)
    c = np.clip(base_rgb[None, :] * (0.45 + 0.6*d[:, None]), 0, 1)
    return np.hstack([c, np.full((len(c), 1), alpha)])

def add_mesh(ax, m, rgb, alpha=1.0):
    ax.add_collection3d(Poly3DCollection(
        m.triangles, facecolors=facecolors(m, rgb, alpha),
        edgecolor='none'))

def view3d(parts, fname, elev, azim, title, lim=None, box=None):
    fig = plt.figure(figsize=(9, 10))
    ax = fig.add_subplot(111, projection='3d')
    for m, c, a in parts:
        add_mesh(ax, m, c, a)
    if lim is None:
        allv = np.vstack([m.vertices for m, _, _ in parts])
        lim = (allv.min(0), allv.max(0))
    ax.set_xlim(lim[0][0], lim[1][0]); ax.set_ylim(lim[0][1], lim[1][1]); ax.set_zlim(lim[0][2], lim[1][2])
    ax.set_box_aspect(box or (lim[1][0]-lim[0][0], lim[1][1]-lim[0][1], max(lim[1][2]-lim[0][2], 15)))
    ax.view_init(elev=elev, azim=azim)
    ax.set_title(title); ax.set_xlabel('x'); ax.set_ylabel('y')
    plt.tight_layout(); plt.savefig(fname, dpi=140); plt.close()
    print('wrote', fname)

# --- exploded view: base, contacts+board, top, buttons/pipe lifted ---
tp = TOP.copy(); tp.apply_translation([0, 0, 22])
bp = BTN.copy();  bp.apply_translation([0, 0, 34])
pp = PIPE.copy(); pp.apply_translation([0, 0, 34])
view3d([(BASE, COL['base'], 1), (BOARD, COL['board'], 0.85), (CONT, COL['cont'], 1),
        (tp, COL['top'], 0.95), (bp, COL['btn'], 1), (pp, COL['pipe'], 1)],
       'render_exploded.png', elev=18, azim=-58,
       title='VitalQ case_v4 — exploded (base / contacts+board / top / plunger+pipe)')

# --- top view (assembled) ---
view3d([(BASE, COL['base'], 1), (TOP, COL['top'], 1), (BTN, COL['btn'], 1), (PIPE, COL['pipe'], 1)],
       'render_top.png', elev=90, azim=-90, title='VitalQ case_v4 — top')

# --- bottom view (skin side, contacts protruding) ---
view3d([(BASE, COL['base'], 1), (TOP, COL['top'], 1), (CONT, COL['cont'], 1)],
       'render_bottom.png', elev=-90, azim=-90,
       title='VitalQ case_v4 — skin side (pogo contacts + U11 slug)')

# --- assembled 3/4 view, semi-transparent top ---
view3d([(BASE, COL['base'], 1), (BOARD, COL['board'], 0.8), (CONT, COL['cont'], 1),
        (TOP, COL['top'], 0.55)],
       'render_assembled.png', elev=24, azim=-52,
       title='VitalQ case_v4 — assembled (top shell translucent)')

# --- 3D clipped section through an electrode row (y=62.9, J5 pads) ---
def clip(m, y_max):
    """Drop triangles fully beyond y_max (keeps -y side, section edge visible)."""
    keep = (m.triangles[:, :, 1] <= y_max).any(axis=1)
    out = m.copy(); out.update_faces(keep); out.remove_infinite_values()
    return out

CY = 62.9
# J7 region viewed from the skin side: 4 pogo tips protrude through the
# new base aperture at x0.2-4.5, y59.6-64.0 (west edge, left of J6).
view3d([(BASE, COL['base'], 0.45), (BOARD, COL['board'], 0.45), (CONT, COL['cont'], 1)],
       'render_section_j7_3d.png', elev=-38, azim=-108,
       title='VitalQ case_v4 — J7 region from skin side (4 BioZ pogo pins)',
       lim=(np.array([-4, 58, -3]), np.array([9, 65, 7])), box=(13, 7, 10))

# J3 region (fix_v9): FSR contact pads moved to B.Cu at (42.0/44.2, 59.85) —
# 2 pogo tips through the new base aperture east of J13/R122.
view3d([(BASE, COL['base'], 0.45), (BOARD, COL['board'], 0.45), (CONT, COL['cont'], 1)],
       'render_section_j3_3d.png', elev=-38, azim=-108,
       title='VitalQ case_v4 — J3 region from skin side (2 FSR pogo pins)',
       lim=(np.array([37, 55, -3]), np.array([49, 64, 7])), box=(12, 9, 10))

# --- 2D section through the board at y=27 ---
def section(meshes, plane_y, fname, title):
    fig, ax = plt.subplots(figsize=(10, 6))
    for m, c, a in meshes:
        try:
            segs = trimesh.intersections.mesh_plane(
                m, plane_normal=[0, 1, 0], plane_origin=[0, plane_y, 0])
        except Exception:
            continue
        if len(segs) == 0:
            continue
        lines = [[(p[0][0], p[0][2]), (p[1][0], p[1][2])] for p in segs]
        ax.add_collection(LineCollection(lines, colors=np.hstack([c, a]), linewidths=1.1))
    ax.autoscale(); ax.set_aspect('equal')
    ax.set_xlabel('x (mm)'); ax.set_ylabel('z (mm)')
    ax.set_title(title); ax.grid(alpha=0.3)
    plt.tight_layout(); plt.savefig(fname, dpi=150); plt.close()
    print('wrote', fname)

section([(BASE, COL['base'], 0.9), (TOP, COL['top'], 0.9), (BOARD, COL['board'], 0.7)],
        27.0, 'render_section.png',
        'VitalQ case_v4 — cross-section at y=27 (through board, mic, BME280)')

# --- 2D section through J7 (y=62.9): relocated BioZ pins in aperture ---
fig, ax = plt.subplots(figsize=(11, 5))
for m, c, a in [(BASE, COL['base'], 0.95), (TOP, COL['top'], 0.95),
                (BOARD, COL['board'], 0.7), (CONT, COL['cont'], 1.0)]:
    try:
        segs = trimesh.intersections.mesh_plane(
            m, plane_normal=[0, 1, 0], plane_origin=[0, 62.9, 0])
    except Exception:
        continue
    if len(segs):
        ax.add_collection(LineCollection(
            [[(p[0][0], p[0][2]), (p[1][0], p[1][2])] for p in segs],
            colors=np.hstack([c, a]), linewidths=1.6))
ax.axhline(0, color='k', lw=0.8, ls='--'); ax.annotate('skin plane z=0', (8, 0.2))
ax.autoscale(); ax.set_aspect('equal')
ax.set_xlim(-8, 32); ax.set_ylim(-3, 11)
ax.set_xlabel('x (mm)'); ax.set_ylabel('z (mm)')
ax.set_title('VitalQ case_v4 — section at y=62.9 through J7 pads: pogo pins reach the skin plane')
ax.grid(alpha=0.3)
plt.tight_layout(); plt.savefig('render_section_electrode.png', dpi=150); plt.close()
print('wrote render_section_electrode.png (2D)')
print('done')
