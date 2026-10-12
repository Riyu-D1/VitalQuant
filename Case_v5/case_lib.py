"""Shared helpers + design constants for the VitalQ case_v5."""
import json
import numpy as np
import trimesh
from shapely.geometry import Polygon, box as sbox
from shapely.affinity import translate

G = json.load(open('geometry.json'))

# ---------- board geometry ----------
BX0, BY0, BX1, BY1 = -2.0, -10.5, 48.0, 64.5      # exact board outline
BT      = G['thickness_mm']                       # 1.5386 real stackup
BT_MAX  = G.get('thickness_max_mm', 1.6)          # fab spec upper bound
CL   = 0.3                                        # board->case clearance
IX0, IY0, IX1, IY1 = BX0-CL, BY0-CL, BX1+CL, BY1+CL   # inner cavity
WALL = 1.6                                        # tightened from 1.8
OX0, OY0, OX1, OY1 = IX0-WALL, IY0-WALL, IX1+WALL, IY1+WALL  # outer shell

# z planes (z=0 = skin-facing bottom of base)
ZFLOOR   = 1.6    # base floor thickness (spec min)
ZBCAV    = 1.8    # B cavity depth: >= max B-comp over solid floor (1.0) + 0.3,
                  # and keeps pad->skin gap 3.4 = pogo working height 4.0 - 0.6
ZB       = ZFLOOR+ZBCAV          # 3.4  board B surface (pad plane)
ZT       = ZB+BT                  # 4.94 board F surface @ real stackup
ZT_MAX   = ZB+BT_MAX              # 5.0  board F surface @ 1.6 mm worst case
ZSEAM    = 3.55                   # base/top split (just above B surface)
ZFCAV    = ZB+BT_MAX+3.0          # 8.0  F cavity ceiling: clears U1 (2.7+0.3)
ZLID     = ZFCAV+WALL             # 9.6  outer top of lid
ZBAY_IN  = ZLID                   # 9.6  battery bay floor (lid top)
ZBAY_HI  = ZBAY_IN+4.4            # 14.0 bay interior ceiling (402535 cell 4.0 + foam)
ZBAY_OUT = ZBAY_HI+1.6            # 15.6 blister rim top incl. door groove

RO = 3.0   # outer corner radius (spec >= 2)
RI = 2.0   # inner corner radius (spec min 2)

# ---------- helpers ----------
def rrect(x0, y0, x1, y1, r):
    """rounded rectangle polygon (CCW), quadrant arcs."""
    pts = []
    for cx, cy, a0 in [(x1-r, y1-r, 0), (x0+r, y1-r, 90), (x0+r, y0+r, 180), (x1-r, y0+r, 270)]:
        for i in range(9):
            a = np.radians(a0 + i*11.25)
            pts.append((cx + r*np.cos(a), cy + r*np.sin(a)))
    return Polygon(pts)

def roff(off):
    """rounded rect at case-outer bounds offset by `off` (positive = inward),
    with concentric corner radius."""
    return rrect(OX0+off, OY0+off, OX1-off, OY1-off, RO-off)

def prism(poly, z0, z1):
    m = trimesh.creation.extrude_polygon(poly, z1-z0)
    m.apply_translation([0, 0, z0])
    return m

def rbox(x0, y0, z0, x1, y1, z1, r=0):
    if r > 0:
        return prism(rrect(x0, y0, x1, y1, r), z0, z1)
    m = trimesh.creation.box([x1-x0, y1-y0, z1-z0])
    m.apply_translation([(x0+x1)/2, (y0+y1)/2, (z0+z1)/2])
    return m

def cyl(x, y, z0, z1, r, n=32):
    m = trimesh.creation.cylinder(r, z1-z0, sections=n)
    m.apply_translation([x, y, (z0+z1)/2])
    return m

def U(meshes):
    return trimesh.boolean.union(meshes, engine='manifold')

def D(a, b):
    return trimesh.boolean.difference([a, b], engine='manifold')

def box_at(cx, cy, w, h, z0, z1):
    return trimesh.creation.box([w, h, z1-z0]).apply_translation([cx, cy, (z0+z1)/2])

def wedge_ring(inner_poly, z_bot, z_top, w_top, steps=5):
    """45°-ish rising ring: thin at inner wall (z_bot) -> w_top wide at z_top.
    Used for the board seat ledge (printable, no >45° overhang)."""
    rings = []
    for i in range(steps):
        t0, t1 = i/steps, (i+1)/steps
        w0 = 0.02 + (w_top-0.02)*t0
        w1 = 0.02 + (w_top-0.02)*t1
        ring = inner_poly.difference(inner_poly.buffer(-w1))
        rings.append(prism(ring, z_bot + (z_top-z_bot)*t0,
                              z_bot + (z_top-z_bot)*t1))
    return U(rings)

def wedge_ring_down(inner_poly, z_top, z_bot, w_bot, steps=5):
    """Down-pointing 45° ring: thin at inner wall (z_top) -> w_bot wide at z_bot.
    Mirror of wedge_ring for the lid's board-clamp ledge."""
    rings = []
    for i in range(steps):
        t0, t1 = i/steps, (i+1)/steps
        w0 = w_bot - (w_bot-0.02)*t0
        w1 = w_bot - (w_bot-0.02)*t1
        ring = inner_poly.difference(inner_poly.buffer(-w1))
        rings.append(prism(ring, z_bot + (z_top-z_bot)*t0,
                              z_bot + (z_top-z_bot)*t1))
    return U(rings)

def edge_profile_loft(outer_poly, inset0=1.7, chamfer=1.3, arc_z=2.8, nrings=9):
    """Bottom-edge rounding printable face-down: 45° chamfer for the first
    `chamfer` mm, then a bezier arc tangent-continuous to the wall at arc_z.
    Each loft ring uses the inset at its TOP edge so the stepped surface
    stays inside the smooth curve (small ledges, not overhangs).
    Returns solid z0..arc_z; stack a prism of the full profile on top."""
    i0 = inset0
    ic = i0 - chamfer           # inset at end of the 45° chamfer
    zc = chamfer
    # quadratic bezier in (z, i): P0 tangent -45°, P1 tangent horizontal-wall (di/dz=0)
    P0 = np.array([zc, ic])
    C  = np.array([zc + ic, 0.0])
    P1 = np.array([arc_z, 0.0])
    def iz(t):
        p = (1-t)**2*P0 + 2*t*(1-t)*C + t*t*P1
        return max(p[1], 0.0)
    parts = []
    for k in range(4):
        z0 = k*(chamfer/4.0); z1 = (k+1)*(chamfer/4.0)
        # bottom-edge inset: each ring sits inside the one above -> the step
        # ledges face UP (no undercut) in the as-drawn orientation
        parts.append(prism(outer_poly.buffer(-(i0 - z0)), z0, z1))
    for k in range(nrings):
        t0, t1 = k/nrings, (k+1)/nrings
        z0 = zc + (arc_z-zc)*t0; z1 = zc + (arc_z-zc)*t1
        parts.append(prism(outer_poly.buffer(-iz(t0)), z0, z1))
    return U(parts)

def gabled_slot_cutter(x0, y0, z0, x1, y1, z1, apex=0.9, axis='x'):
    """Box slot + 45° gable roof so the ceiling prints without support.
    axis = ridge direction ('x' -> ridge along x, roof rises in the yz plane;
    'y' -> ridge along y)."""
    base = rbox(x0, y0, z0, x1, y1, z1)
    if axis == 'x':
        tri = Polygon([(y0, z1), ((y0+y1)/2, z1+apex), (y1, z1)])
        roof = trimesh.creation.extrude_polygon(tri, x1-x0)
        # remap (x,y,z)->(z,x,y): triangle lands in the YZ plane, prism along +x
        roof.apply_transform(np.array([[0., 0., 1., 0.],
                                       [1., 0., 0., 0.],
                                       [0., 1., 0., 0.],
                                       [0., 0., 0., 1.]]))
        roof.apply_translation([x0, 0, 0])
    else:
        tri = Polygon([(x0, z1), ((x0+x1)/2, z1+apex), (x1, z1)])
        roof = trimesh.creation.extrude_polygon(tri, y1-y0)
        # trimesh extrudes along +z; rotate so it runs along +y instead
        roof.apply_transform(trimesh.transformations.rotation_matrix(np.pi/2, [1, 0, 0]))
        # after rotation the prism sits y<0; shift back into place
        roof.apply_translation([0, y1, 0])
    return U([base, roof])

def fp(ref):
    for f in G['components']:
        if f['ref'] == ref:
            return f
    raise KeyError(ref)

def pads(ref):
    return [(p['pos'][0], p['pos'][1], p['size'][0], p['size'][1]) for p in fp(ref)['pads'] if p['num']]

def comp_boxes(min_h=0.0, side=None, clearance=0.0, ztop=ZT):
    """component collision boxes in case z-coords (board B surface at ZB).
    ztop: F surface used (ZT real or ZT_MAX worst-case)."""
    out = []
    for f in G['components']:
        if side and f['side'] != side:
            continue
        h = f['h3d']
        if h <= min_h:
            continue
        bb = f.get('model3d_bbox') or [f['bbox']['x'], f['bbox']['y'],
                                       f['bbox']['x']+f['bbox']['w'], f['bbox']['y']+f['bbox']['h']]
        x0, y0, x1, y1 = bb
        x0 -= clearance; y0 -= clearance; x1 += clearance; y1 += clearance
        if f['side'] == 'F':
            out.append((f['ref'], rbox(x0, y0, ztop, x1, y1, ztop+h)))
        else:
            out.append((f['ref'], rbox(x0, y0, ZB-h, x1, y1, ZB)))
    return out

def board_solid(thickness=None):
    """PCB slab for renders/collision tests (with real drilled holes).
    thickness: None -> real stackup BT; pass BT_MAX for worst case."""
    t = thickness or BT
    m = rbox(BX0, BY0, ZB, BX1, BY1, ZB+t)
    holes = []
    for h in G['holes']:
        d = h['drill']
        if h['shape'] == '2' or d[0] != d[1]:   # slot -> box
            holes.append(box_at(h['pos'][0], h['pos'][1], d[0]+0.05, d[1]+0.05, ZB-0.1, ZB+t+0.1))
        else:
            holes.append(cyl(h['pos'][0], h['pos'][1], ZB-0.1, ZB+t+0.1, d[0]/2+0.025))
    return D(m, U(holes))
