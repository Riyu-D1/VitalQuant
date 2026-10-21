"""Shared helpers + design constants for the VitalQ case_v8 wired-contact case."""
import json
import numpy as np
import trimesh
from shapely.geometry import Polygon, box as sbox
from shapely.affinity import translate

G = json.load(open('geometry.json'))

# ---------- board geometry ----------
BX0, BY0, BX1, BY1 = -2.0, -10.5, 48.0, 64.5      # main board rectangle (fix_v22)
# fix_v22 outline adds two tabs beyond the main rectangle (checked, extraction):
TAB_N = (-2.05, -16.55, 22.5, BY0)      # antenna tab (WROOM overhangs to -22.31)
TAB_E = (48.0, 20.0, 53.6, 29.9)        # east power tab (D33/Q6 sit here)
BT      = G['thickness_mm']                       # 1.5386 real stackup
BT_MAX  = G.get('thickness_max_mm', 1.6)          # fab spec upper bound
CL   = 0.3                                        # board->case clearance
IX0, IY0, IX1, IY1 = BX0-CL, BY0-CL, BX1+CL, BY1+CL   # inner cavity
WALL = 1.6                                        # tightened from 1.8
OX0, OY0, OX1, OY1 = IX0-WALL, IY0-WALL, IX1+WALL, IY1+WALL  # outer shell
# cavity/shell polygons follow the true outline: main rect + both tabs
BPOLY = sbox(BX0,BY0,BX1,BY1).union(sbox(*TAB_N)).union(sbox(*TAB_E))
IPOLY = BPOLY.buffer(CL)                    # interior cavity incl. tabs
OPOLY = BPOLY.buffer(CL+WALL)               # outer shell incl. tabs

# z planes (z=0 = skin-facing bottom of base)
ZFLOOR   = 1.6    # base floor thickness (spec min)
ZBCAV    = 1.8    # B cavity depth: >= max B-comp over solid floor (1.0) + 0.3,
                  # leaves a 1.8 mm wire-routing gap below the board B.Cu plane.
ZB       = ZFLOOR+ZBCAV          # 3.4  board B surface (pad plane)
ZT       = ZB+BT                  # 4.94 board F surface @ real stackup
ZT_MAX   = ZB+BT_MAX              # 5.0  board F surface @ 1.6 mm worst case
ZSEAM    = 3.55                   # base/top split (just above B surface)
WIRE_CHANNEL_FLOOR = 1.00          # shallow raceway leaves 1.0 mm continuous skin wall
WIRE_CHANNEL_TOP = 2.20            # floor raceway rises 0.60 mm above floor top; board roof gap 1.20 mm
WIRE_LEAD_Z = (ZFLOOR, WIRE_CHANNEL_TOP)
WIRE_CHANNEL_WIDTH = 1.00
WIRE_LANE_SPACING = 1.40       # nominal lead centre spacing to avoid envelope overlap
WIRE_FANOUT_ESCAPE_Y = 62.0    # route each land wire toward +y before turning into trunk
WIRE_BUNDLE_CHANNEL = (-2.0, 63.70, 48.30, 65.20)  # joined edge manifold outside land row where possible
WIRE_EXIT_MANIFOLD_X = 40.0
WIRE_ROUTE_LANE_Y = 64.20
WIRE_EXIT_X = (36.0, 44.0)              # 8 mm width for a compact 18-lead bundle
WIRE_EXIT_Z = (0.55, 3.45)           # 4 x 2.90 mm side-wall slot; stops below the 3.55 mm seam
WIRE_EXIT_Y = (IY1, OY1 + 0.15)    # full +y wall, begins at cavity edge
WIRE_TIE_POSTS = ((36.5, 62.0), (43.5, 62.0))
WIRE_TIE_POST_RADIUS = 0.75
WIRE_TIE_POST_Z = (ZFLOOR - 0.45, 2.40)  # 1.0 mm clearance below board B.Cu at z=3.4
WIRE_LEAD_ENVELOPE = 0.80          # insulated 30 AWG wire plus solder bend envelope
WIRE_CLEARANCE_MIN = 1.20          # required remaining land-to-raceway clearance
WIRE_NETS = {
    'J5': {'1': 'ECG1_PAD', '2': 'ECG2_PAD', '3': 'RLD_PAD', '4': 'AFE_P_PAD', '5': 'AFE_N_PAD'},
    'J6': {'1': 'EDA_CE_PAD', '2': 'EDA_RE_PAD', '3': 'EDA_SE_PAD', '4': 'EDA_DE_PAD'},
    'J7': {'1': 'BIOZ_FP_PAD', '2': 'BIOZ_FN_PAD', '3': 'BIOZ_SP_PAD', '4': 'BIOZ_SN_PAD'},
    'J11': {'1': 'J11_WE', '2': 'J11_RE', '3': 'J11_CE'},
    'J13': {'1': 'J13_INP', '2': 'J13_INM'},
    'J3': {'1': 'FSR_PAD_1', '2': 'FSR_PAD_2'},
}  # net labels for the pogo land sites (report-only; positions come from geometry.json)
ZFCAV    = ZB+BT_MAX+3.55         # 8.55 F cavity ceiling: clears WROOM U1 (3.25+0.3)
ZLID     = ZFCAV+WALL             # 9.6  outer top of lid
ZBAY_IN  = ZLID                   # 9.6  battery bay floor (lid top)
ZBAY_HI  = ZBAY_IN+4.4            # 14.0 bay interior ceiling (402535 cell 4.0)
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

def rect_cone_cut(x0, y0, x1, y1, z_lo, z_hi, w_lo, w_hi, steps=6):
    """Stepped tapered cutter for a rectangular hole: the (x0,y0,x1,y1)
    poly is buffered by w that varies linearly from w_lo at z_lo to w_hi
    at z_hi (45 deg when |dw| == |dz|). Each ring uses the widest width in
    its z-range so the cut fully covers the ideal cone (no residual lips)."""
    parts = []
    for k in range(steps):
        t0, t1 = k/steps, (k+1)/steps
        wk = max(w_lo + (w_hi-w_lo)*t0, w_lo + (w_hi-w_lo)*t1)
        parts.append(prism(sbox(x0, y0, x1, y1).buffer(max(wk, 0.0), resolution=4),
                           z_lo+(z_hi-z_lo)*t0, z_lo+(z_hi-z_lo)*t1))
    return U(parts)

def cyl_cone_cut(x, y, z_lo, z_hi, r_lo, r_hi, steps=3):
    """Stepped tapered cutter for a round hole: radius varies linearly
    r_lo@z_lo -> r_hi@z_hi. Same widest-per-ring rule as rect_cone_cut."""
    parts = []
    for k in range(steps):
        t0, t1 = k/steps, (k+1)/steps
        rk = max(r_lo + (r_hi-r_lo)*t0, r_lo + (r_hi-r_lo)*t1)
        parts.append(cyl(x, y, z_lo+(z_hi-z_lo)*t0, z_lo+(z_hi-z_lo)*t1, rk))
    return U(parts)

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
    m = prism(BPOLY, ZB, ZB+t)              # board solid follows true outline
    holes = []
    for h in G['holes']:
        d = h['drill']
        if h['shape'] == '2' or d[0] != d[1]:   # slot -> box
            holes.append(box_at(h['pos'][0], h['pos'][1], d[0]+0.05, d[1]+0.05, ZB-0.1, ZB+t+0.1))
        else:
            holes.append(cyl(h['pos'][0], h['pos'][1], ZB-0.1, ZB+t+0.1, d[0]/2+0.025))
    return D(m, U(holes))
