"""Shared helpers + design constants for the VitalQ case."""
import json
import numpy as np
import trimesh
from shapely.geometry import Polygon, box as sbox
from shapely.affinity import translate

G = json.load(open('geometry.json'))

# ---------- board geometry ----------
BX0, BY0, BX1, BY1 = -2.0, -10.5, 48.0, 64.5      # exact board outline
CL   = 0.3                                        # board->case clearance
IX0, IY0, IX1, IY1 = BX0-CL, BY0-CL, BX1+CL, BY1+CL   # inner cavity
WALL = 1.8
OX0, OY0, OX1, OY1 = IX0-WALL, IY0-WALL, IX1+WALL, IY1+WALL  # outer shell

# z planes (z=0 = skin-facing bottom of base)
ZFLOOR   = 1.6    # base floor thickness
ZBCAV    = 1.8    # cavity depth below board B surface
ZB       = ZFLOOR+ZBCAV          # 3.4  board B surface
ZT       = ZB+1.6                # 5.0  board F surface
ZSEAM    = 3.45                  # base/top split (just above B surface)
ZFCAV    = 8.0                   # F cavity ceiling (inner lid)
ZLID     = ZFCAV+WALL            # 9.8  outer top of lid  (wall=1.8 rounded to 1.6 ok)
ZBAY_IN  = 9.7                   # battery bay interior floor (on lid top)
ZBAY_HI  = ZBAY_IN+4.6           # 14.3 battery bay interior ceiling
ZBAY_OUT = ZBAY_HI+1.4           # 15.7 blister outer top

RO = 4.0   # outer corner radius
RI = 2.0   # inner corner radius (spec min 2)

# ---------- helpers ----------
def rrect(x0, y0, x1, y1, r):
    """rounded rectangle polygon (CCW), quadrant arcs."""
    import math
    pts = []
    for cx, cy, a0 in [(x1-r, y1-r, 0), (x0+r, y1-r, 90), (x0+r, y0+r, 180), (x1-r, y0+r, 270)]:
        for i in range(9):
            a = np.radians(a0 + i*11.25)
            pts.append((cx + r*np.cos(a), cy + r*np.sin(a)))
    return Polygon(pts)

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

def fp(ref):
    for f in G['components']:
        if f['ref'] == ref:
            return f
    raise KeyError(ref)

def pads(ref):
    return [(p['pos'][0], p['pos'][1], p['size'][0], p['size'][1]) for p in fp(ref)['pads'] if p['num']]

def comp_boxes(min_h=0.0, side=None, clearance=0.0):
    """component collision boxes in case z-coords (board B surface at ZB)."""
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
            out.append((f['ref'], rbox(x0, y0, ZT, x1, y1, ZT+h)))
        else:
            out.append((f['ref'], rbox(x0, y0, ZB-h, x1, y1, ZB)))
    return out

def board_solid():
    """PCB slab for renders/collision tests (with real drilled holes)."""
    m = rbox(BX0, BY0, ZB, BX1, BY1, ZT)
    holes = []
    for h in G['holes']:
        d = h['drill']
        if h['shape'] == '2' or d[0] != d[1]:   # slot -> box
            holes.append(box_at(h['pos'][0], h['pos'][1], d[0]+0.05, d[1]+0.05, ZB-0.1, ZT+0.1))
        else:
            holes.append(cyl(h['pos'][0], h['pos'][1], ZB-0.1, ZT+0.1, d[0]/2+0.025))
    return D(m, U(holes))
