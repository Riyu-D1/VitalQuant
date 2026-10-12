#!/usr/bin/env python3
"""Render case_v6 views to PNG (matplotlib, shaded mesh faces + 2D sections)."""
import numpy as np, trimesh, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from matplotlib.collections import LineCollection
from case_lib import board_solid, ZLID

BASE = trimesh.load('case_base.stl')
TOP  = trimesh.load('case_top.stl')
CUP  = trimesh.load('bay_cup.stl')
PLATE= trimesh.load('pin_plate.stl')
DOOR = trimesh.load('battery_door.stl')
FLAP = trimesh.load('usb_flap.stl')
GASK = trimesh.load('gasket.stl')
CAP  = trimesh.load('button_cap.stl')
PIPE = trimesh.load('light_pipe.stl')
PLUG = trimesh.load('plug.stl')
CONT = trimesh.load('contacts_assy.stl')
BOARD = board_solid()

def hex2rgb(h):
    return np.array([int(h[i:i+2], 16)/255 for i in (1, 3, 5)])

COL = {'top': hex2rgb('#e8b04b'), 'base': hex2rgb('#4d7ea8'),
       'board': hex2rgb('#2f6b3f'), 'cup': hex2rgb('#c98a2e'),
       'plate': hex2rgb('#7a5a9e'), 'door': hex2rgb('#a85a7a'),
       'flap': hex2rgb('#d05353'), 'gasket': hex2rgb('#3aa08a'),
       'cap': hex2rgb('#d05353'), 'pipe': hex2rgb('#f0e060'),
       'plug': hex2rgb('#888888'), 'cont': hex2rgb('#8a6a12')}

LIGHT = np.array([-0.45, -0.55, 0.7]); LIGHT /= np.linalg.norm(LIGHT)

def facecolors(m, base_rgb, alpha):
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

def T(m, dx=0, dy=0, dz=0):
    out = m.copy(); out.apply_translation([dx, dy, dz]); return out

# ---------- assembled placements ----------
cup_a   = T(CUP, dz=ZLID)                                    # tray on lid top
door_a  = T(DOOR, dz=ZLID)                                   # door in grooves
plate_a = PLATE.copy()                                       # flipped pins-down
plate_a.apply_transform(trimesh.transformations.reflection_matrix([0,0,0],[0,0,1]))
plate_a.apply_translation([0, 0, ZLID+0.85])
cap1 = T(CAP, 45.30024, 34.61124, 7.05)                      # SW1
cap2 = T(CAP, 2.02538, 57.16984, 7.05)                       # SW2
pipe_a = T(PIPE, 4.0, 42.9, 6.1)                             # tip over D25
plug_a = T(PLUG, 26.45, 53.4, ZLID-0.55)                     # J8 plug
cell = trimesh.creation.box([24.3, 35.0, 4.0])               # 402535 cell
cell.apply_translation([17.3, 22.5, ZLID+0.85+2.0])
COL['cell'] = hex2rgb('#333344')
COL['strap'] = hex2rgb('#2b2b30')

def sbox(x0, y0, z0, x1, y1, z1):
    b = trimesh.creation.box([abs(x1-x0), abs(y1-y0), abs(z1-z0)])
    b.apply_translation([(x0+x1)/2, (y0+y1)/2, (z0+z1)/2])
    return b

def strap_end(side):
    """~30 x 1.3 mm elastic strap end wrapped around the lug bar in a
    hairpin: low leg arrives through the channel under the bar, riser up
    the outboard face, high leg folds back over the bar top (sewn end).
    Render prop (not a printed part)."""
    if side == 'L':
        spans = [(-30.0, 13.0, 5.05, -3.6, 42.0, 6.35),    # low leg -> channel
                 (-9.4, 13.0, 5.60, -8.1, 42.0, 10.35),    # outboard riser
                 (-20.0, 13.0, 9.62, -3.6, 42.0, 10.92)]   # high leg over bar
    else:
        spans = [(49.5, 13.0, 5.05, 76.0, 42.0, 6.35),
                 (54.1, 13.0, 5.60, 55.4, 42.0, 10.35),
                 (49.5, 13.0, 9.62, 66.0, 42.0, 10.92)]
    return trimesh.util.concatenate([sbox(*s) for s in spans])

def tail_strip(pts, y0=13.0, w=29.0, t=1.3):
    """Strap tail drooping onto the chest: chain of boxes along (x,z) pts."""
    segs = []
    for (ax_, az), (bx, bz) in zip(pts[:-1], pts[1:]):
        L = np.hypot(bx-ax_, bz-az)
        s = trimesh.creation.box([L+0.6, w, t])
        s.apply_transform(trimesh.transformations.rotation_matrix(
            np.arctan2(bz-az, bx-ax_), [0, 1, 0]))
        s.apply_translation([(ax_+bx)/2, y0+w/2, (az+bz)/2])
        segs.append(s)
    return trimesh.util.concatenate(segs)

# chest surface proxy: R95 cylinder, axis along y, touching the skin plane
chest = trimesh.creation.cylinder(radius=95.0, height=84.0, sections=96)
chest.apply_transform(trimesh.transformations.rotation_matrix(
    -np.pi/2, [1, 0, 0]))
chest.apply_translation([23.0, 27.0, -95.3])
COL['chest'] = hex2rgb('#d9b596')

low_L = tail_strip([(-9.5, 5.7), (-15, 3.0), (-21, -2.5), (-27, -10.0),
                    (-30, -15.5), (-34, -18.6)])
high_L = tail_strip([(-10, 10.3), (-15, 7.5), (-20, 3.5), (-25, -3.0),
                     (-29, -10.0)])
low_R = tail_strip([(55.5, 5.7), (61, 3.0), (67, -2.5), (73, -10.0),
                    (76, -15.5), (80, -18.6)])
high_R = tail_strip([(56, 10.3), (61, 7.5), (66, 3.5), (71, -3.0),
                     (75, -10.0)])

strap_L = strap_end('L'); strap_R = strap_end('R')

ASSY = [(BASE, 'base', 1.0), (TOP, 'top', 1.0), (cup_a, 'cup', 1.0),
        (door_a, 'door', 1.0), (plate_a, 'plate', 1.0),
        (GASK, 'gasket', 1.0), (FLAP, 'flap', 1.0),
        (cap1, 'cap', 1.0), (cap2, 'cap', 1.0), (pipe_a, 'pipe', 1.0),
        (plug_a, 'plug', 1.0), (cell, 'cell', 1.0)]

def parts_color(ps):
    return [(m, COL[k], a) for m, k, a in ps]

# ---------- 1. assembled 3/4 view (shell translucent over board+contacts) ---
view3d(parts_color([(BASE,'base',1),(BOARD,'board',0.8),(CONT,'cont',1),
                    (GASK,'gasket',0.9),(TOP,'top',0.45),(cup_a,'cup',0.75),
                    (door_a,'door',0.9),(cell,'cell',0.55),(FLAP,'flap',1),
                    (cap1,'cap',1),(cap2,'cap',1),(pipe_a,'pipe',1),
                    (strap_L,'strap',0.9),(strap_R,'strap',0.9)]),
       'render_assembled.png', elev=24, azim=-52,
       title='VitalQ case_v6 — assembled, 30mm strap looped on lugs')

# ---------- 2. top view -----------------------------------------------------
view3d(parts_color(ASSY), 'render_top.png', elev=90, azim=-90,
       title='VitalQ case_v6 — top (bay cup, door, strap lugs, caps, pipe)')

# ---------- 3. bottom / skin face -------------------------------------------
view3d(parts_color([(BASE,'base',1),(TOP,'top',1),(CONT,'cont',1),
                    (GASK,'gasket',0.55),(cup_a,'cup',1)]),
       'render_bottom.png', elev=-90, azim=-90,
       title='VitalQ case_v6 — skin face (pogo tips, slug, gasket, apertures)')

# ---------- 4. exploded -----------------------------------------------------
ex_top   = T(TOP, dz=14)
ex_cap1  = T(cap1, dz=26); ex_cap2 = T(cap2, dz=26); ex_pipe = T(pipe_a, dz=26)
ex_plug  = T(plug_a, dz=26); ex_flap = T(FLAP, dz=20)
ex_cup   = T(cup_a, dz=30); ex_door = T(door_a, dz=30); ex_cell = T(cell, dz=38)
ex_plate = T(plate_a, dz=38)
ex_gask  = T(GASK, dz=-4); ex_cont = T(CONT, dz=-6)
view3d(parts_color([(BASE,'base',1),(ex_gask,'gasket',0.8),(BOARD,'board',0.85),
                    (ex_cont,'cont',1),(ex_top,'top',0.9),
                    (ex_cup,'cup',0.9),(ex_plate,'plate',1),(ex_cell,'cell',0.5),
                    (ex_door,'door',1),(ex_cap1,'cap',1),(ex_cap2,'cap',1),
                    (ex_pipe,'pipe',1),(ex_plug,'plug',1),(ex_flap,'flap',1)]),
       'render_exploded.png', elev=16, azim=-58,
       title='VitalQ case_v6 — exploded (base / gasket+contacts+board / lid / cup stack)',
       box=(62, 82, 60))

# ---------- 2D sections -----------------------------------------------------
def section(meshes, normal, origin, fname, title, ulim=None, vlim=None,
            annot=None):
    """Filled 2D section: Path3D->to_planar->polygons_full; lines fallback."""
    fig, ax = plt.subplots(figsize=(11, 5.5))
    for m, c, a in meshes:
        try:
            sec = m.section(plane_normal=normal, plane_origin=origin)
        except Exception:
            continue
        if sec is None:
            continue
        try:
            Tp = np.eye(4)
            if abs(normal[1]) > 0.5:          # y-plane -> Path2D in (x,z)
                Tp[:3, :3] = [[1, 0, 0], [0, 0, 1], [0, 1, 0]]
            else:                              # x-plane -> Path2D in (y,z)
                Tp[:3, :3] = [[0, 1, 0], [0, 0, 1], [1, 0, 0]]
            p2, _ = sec.to_2D(to_2D=Tp, check=False)
            drew = False
            for poly in p2.polygons_full:
                xs, ys = poly.exterior.xy
                ax.fill(xs, ys, color=np.hstack([c, a*0.55]),
                        edgecolor=np.hstack([c, a]), lw=1.0, zorder=2)
                for hole in poly.interiors:
                    hx, hy = hole.xy
                    ax.fill(hx, hy, color='white',
                            edgecolor=np.hstack([c, a]), lw=0.8, zorder=3)
                drew = True
            if drew:
                continue
        except Exception:
            pass
        try:
            segs = trimesh.intersections.mesh_plane(
                m, plane_normal=normal, plane_origin=origin)
            if len(segs):
                lines = [[(p[0][0], p[0][2]), (p[1][0], p[1][2])] if abs(normal[1]) > 0.5
                         else [(p[0][1], p[0][2]), (p[1][1], p[1][2])] for p in segs]
                ax.add_collection(LineCollection(
                    lines, colors=np.hstack([c, a]), linewidths=1.1))
        except Exception:
            continue
    ax.autoscale(); ax.set_aspect('equal')
    if ulim: ax.set_xlim(*ulim)
    if vlim: ax.set_ylim(*vlim)
    ax.grid(alpha=0.3)
    if annot:
        ax.annotate(*annot)
    ax.set_title(title)
    plt.tight_layout(); plt.savefig(fname, dpi=150); plt.close()
    print('wrote', fname)

# J7: plane y=62.9 cuts pads at (1.1,62.9)+(3.3,62.9) and the J5/J6 pad row
sec_parts = parts_color([(BASE,'base',0.95),(TOP,'top',0.95),
                         (BOARD,'board',0.7),(CONT,'cont',1.0)])
fig_y = lambda y, f, t, u, v, a=None: section(
    sec_parts, [0,1,0], [0,y,0], f, t, ulim=u, vlim=v, annot=a)

fig_y(62.9, 'render_section_j7.png',
      'VitalQ case_v6 — section at y=62.9: J7 BioZ pogo pins reach skin plane',
      u=(-8, 30), v=(-3.5, 11), a=('skin plane z=0', (12, 0.35)))
fig_y(59.85, 'render_section_j3.png',
      'VitalQ case_v6 — section at y=59.85: J3 FSR pogo pins reach skin plane',
      u=(34, 48.5), v=(-3.5, 11), a=('skin plane z=0', (44, 0.35)))

# ---------- worn view: pod seated on the chest, straps curving away -------
view3d(parts_color([(BASE,'base',1),(TOP,'top',0.92),(cup_a,'cup',0.9),
                    (door_a,'door',0.9),(strap_L,'strap',1),
                    (strap_R,'strap',1),(low_L,'strap',1),
                    (high_L,'strap',1),(low_R,'strap',1),
                    (high_R,'strap',1),(chest,'chest',0.55)]),
       'render_worn.png', elev=14, azim=-58,
       title='VitalQ case_v6 — worn: 30 mm strap wraps the lugs, tails follow chest',
       lim=((-42, -18, -24), (88, 72, 13)), box=(130, 90, 37))

# lug: plane y=27.5 through the L staple -> bar over channel, skirt, strap
section(sec_parts + [(strap_L.copy(), COL['strap'], 0.85)],
        [0,1,0], [0,27.5,0], 'render_section_lug.png',
        'VitalQ case_v6 — section at y=27.5: lug bar, strap channel, pinch, skirt',
        ulim=(-14, 10), vlim=(-1, 13), annot=('30mm strap', (-12.5, 11.4)))

# USB-C: plane x=26 through the -y wall window + J1 pocket
section(sec_parts + [(FLAP.copy(), COL['flap'], 1.0)],
        [1,0,0], [26,0,0], 'render_section_usbc.png',
        'VitalQ case_v6 — section at x=26: USB-C window, J1 pocket, dust flap',
        ulim=(-20, 6), vlim=(-1, 12), annot=('J1 mouth', (-11.4, 7.4)))
print('done')
