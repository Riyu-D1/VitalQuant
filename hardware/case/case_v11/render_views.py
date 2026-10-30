#!/usr/bin/env python3
"""Render case_v11 views to PNG (matplotlib, shaded mesh faces + 2D sections).

v11: case STLs and the real fix_v24 board GLB are in the PHYSICAL frame
(x=kicad x, y=-kicad y, z up from skin). Views: hero, exploded, skin side,
closed (top), plus assembled-transparent and strap-loop context.
"""
import numpy as np, trimesh, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from case_lib import ZLID

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

def real_board():
    """Real fix_v24 board (slab + significant models) in the physical
    frame at the seat z=3.4.  GLB: (x=kicad x, y=height, z=kicad y) m."""
    slab = trimesh.load('_work/board_v24_slab.glb').to_geometry()
    sc = trimesh.load('_work/board_v24_pads.glb')
    geoms = [g for g in sc.dump(concatenate=False)
             if g.is_volume and abs(g.volume) > 0.5]
    all_g = trimesh.util.concatenate([slab] + geoms)
    m = trimesh.Trimesh(np.c_[all_g.vertices[:, 0], -all_g.vertices[:, 2],
                              all_g.vertices[:, 1]] * 1000.0, all_g.faces)
    m.apply_translation([0, 0, 3.4])
    return m

BOARD = real_board()

def hex2rgb(h):
    return np.array([int(h[i:i+2], 16)/255 for i in (1, 3, 5)])

COL = {'top': hex2rgb('#e8b04b'), 'base': hex2rgb('#4d7ea8'),
       'board': hex2rgb('#2f6b3f'), 'cup': hex2rgb('#c98a2e'),
       'plate': hex2rgb('#7a5a9e'), 'door': hex2rgb('#a85a7a'),
       'flap': hex2rgb('#d05353'), 'gasket': hex2rgb('#3aa08a'),
       'cap': hex2rgb('#d05353'), 'pipe': hex2rgb('#f0e060'),
       'plug': hex2rgb('#888888'), 'cont': hex2rgb('#8a6a12'),
       'cell': hex2rgb('#333344'), 'strap': hex2rgb('#2b2b30'),
       'chest': hex2rgb('#d9b596')}

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
    ax.set_xlim(lim[0][0], lim[1][0]); ax.set_ylim(lim[0][1], lim[1][1])
    ax.set_zlim(lim[0][2], lim[1][2])
    ax.set_box_aspect(box or (lim[1][0]-lim[0][0], lim[1][1]-lim[0][1],
                              max(lim[1][2]-lim[0][2], 15)))
    ax.view_init(elev=elev, azim=azim)
    ax.set_title(title); ax.set_xlabel('x'); ax.set_ylabel('y')
    plt.tight_layout(); plt.savefig(fname, dpi=140); plt.close()
    print('wrote', fname)

def T(m, dx=0, dy=0, dz=0):
    out = m.copy(); out.apply_translation([dx, dy, dz]); return out

# ---------- assembled placements (physical frame) ---------------------------
cup_a   = T(CUP, dz=ZLID)
door_a  = T(DOOR, dz=ZLID)
plate_a = PLATE.copy()
plate_a.apply_transform(trimesh.transformations.reflection_matrix([0,0,0],[0,0,1]))
plate_a.apply_translation([0, 0, ZLID+0.85])
cap1 = T(CAP, 45.30024, -34.61124, 7.05)                     # SW1
cap2 = T(CAP, 2.02538, -57.16984, 7.05)                      # SW2
pipe_a = T(PIPE, 4.0, -42.9, 6.1)                            # tip over D25
plug_a = T(PLUG, 26.45, -53.4, ZLID-0.55)                    # J8 plug
cell = trimesh.creation.box([24.3, 35.0, 4.0])               # 402535 cell
cell.apply_translation([17.3, -22.5, ZLID+0.85+2.0])

def sbox(x0, y0, z0, x1, y1, z1):
    b = trimesh.creation.box([abs(x1-x0), abs(y1-y0), abs(z1-z0)])
    b.apply_translation([(x0+x1)/2, (y0+y1)/2, (z0+z1)/2])
    return b

def strap_end(side):
    """Strap end prop on the lugs (phys frame: strap legs at -y side)."""
    if side == 'L':
        spans = [(-30.0, -42.0, 5.05, -3.6, -13.0, 6.35),
                 (-9.4, -42.0, 5.60, -8.1, -13.0, 10.35),
                 (-20.0, -42.0, 9.62, -3.6, -13.0, 10.92)]
    else:
        spans = [(49.5, -42.0, 6.85, 78.0, -13.0, 8.15)]
    return trimesh.util.concatenate([sbox(*s) for s in spans])

def tail_strip(pts, y0=-42.0, w=29.0, t=1.3):
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
chest.apply_translation([23.0, -27.0, -95.3])

low_L = tail_strip([(-9.5, 5.7), (-15, 3.0), (-21, -2.5), (-27, -10.0),
                    (-30, -15.5), (-34, -18.6)])
high_L = tail_strip([(-10, 10.3), (-15, 7.5), (-20, 3.5), (-25, -3.0),
                     (-29, -10.0)])
low_R = tail_strip([(58.3, 7.5), (64, 5.0), (70, -1.0), (75, -9.0),
                    (78, -15.5), (82, -18.6)])
strap_L = strap_end('L'); strap_R = strap_end('R')

ASSY = [(BASE, 'base', 1.0), (TOP, 'top', 1.0), (cup_a, 'cup', 1.0),
        (door_a, 'door', 1.0), (plate_a, 'plate', 1.0),
        (GASK, 'gasket', 1.0), (FLAP, 'flap', 1.0),
        (cap1, 'cap', 1.0), (cap2, 'cap', 1.0), (pipe_a, 'pipe', 1.0),
        (plug_a, 'plug', 1.0), (cell, 'cell', 1.0)]

def parts_color(ps):
    return [(m, COL[k], a) for m, k, a in ps]

# ---------- hero: translucent top over real board + contacts ----------------
view3d(parts_color([(BASE,'base',1),(BOARD,'board',0.85),(CONT,'cont',1),
                    (GASK,'gasket',0.9),(TOP,'top',0.45),(cup_a,'cup',0.75),
                    (door_a,'door',0.9),(cell,'cell',0.55),(FLAP,'flap',1),
                    (cap1,'cap',1),(cap2,'cap',1),(pipe_a,'pipe',1),
                    (strap_L,'strap',0.9),(strap_R,'strap',0.9)]),
       'renders/hero.png', elev=24, azim=-52,
       title='VitalQ case_v11 — real fix_v24 board in case (physical frame)')

# ---------- closed: opaque assembled, top face ------------------------------
view3d(parts_color(ASSY), 'renders/closed.png', elev=90, azim=-90,
       title='VitalQ case_v11 — closed (bay cup, door, lugs, caps, pipe)')

# ---------- skin side --------------------------------------------------------
view3d(parts_color([(BASE,'base',1),(TOP,'top',1),(CONT,'cont',1),
                    (GASK,'gasket',0.55),(cup_a,'cup',1)]),
       'renders/skin_side.png', elev=-90, azim=-90,
       title='VitalQ case_v11 — skin face (pogos, slug, gasket, apertures)')

# ---------- exploded ----------------------------------------------------------
ex_top   = T(TOP, dz=14)
ex_cap1  = T(cap1, dz=26); ex_cap2 = T(cap2, dz=26); ex_pipe = T(pipe_a, dz=26)
ex_plug  = T(plug_a, dz=26); ex_flap = T(FLAP, dz=20)
ex_cup   = T(cup_a, dz=30); ex_door = T(door_a, dz=30); ex_cell = T(cell, dz=38)
ex_plate = T(plate_a, dz=22)
ex_board = T(BOARD, dz=7)
ex_cont  = T(CONT, dz=2)
ex_gask  = T(GASK, dz=1)
view3d(parts_color([(ex_top,'top',1),(BASE,'base',1),(ex_board,'board',1),
                    (ex_cont,'cont',1),(ex_gask,'gasket',0.8),
                    (ex_cup,'cup',0.9),(ex_door,'door',0.9),
                    (ex_plate,'plate',0.9),(ex_cell,'cell',0.8),
                    (ex_cap1,'cap',1),(ex_cap2,'cap',1),
                    (ex_pipe,'pipe',1),(ex_plug,'plug',1),
                    (ex_flap,'flap',1)]),
       'renders/exploded.png', elev=22, azim=-58,
       title='VitalQ case_v11 — exploded (real fix_v24 board)')

# ---------- worn context (strap + chest) -------------------------------------
view3d(parts_color([(chest,'chest',1),(BASE,'base',1),(TOP,'top',1),
                    (cup_a,'cup',1),(door_a,'door',1),
                    (strap_L,'strap',0.95),(strap_R,'strap',0.95),
                    (low_L,'strap',0.95),(low_R,'strap',0.95),
                    (high_L,'strap',0.95)]),
       'renders/worn.png', elev=18, azim=-40,
       title='VitalQ case_v11 — worn on chest (elastic strap on lugs)')
print('done')
