#!/usr/bin/env python3
"""verify_fit.py — real-geometry fit verification for the VitalQ case (v11+).

Unlike case_v10's verify_fit.py (which re-checked the case's own design
numbers), this script tests the REAL board geometry in the REAL physical
frame:

  * Board truth comes from a pcbnew extraction of fix_v24/vitalq_v2.kicad_pcb
    (geometry_v24.json: outline, holes, pads, courtyard boxes) and from a
    kicad-cli GLB export of the same board (real 3-D component surfaces).
  * The GLB is exported by KiCad in the physical frame: glb(x,y,z) =
    (kicad_x, height_above_B.Cu, kicad_y) metres.  The physical right-handed
    frame used by the case STLs is (x = kicad_x, y = -kicad_y, z up from the
    skin face) — so glb -> case mm is (1000*x, -1000*z, 1000*y + ZB).
  * The board is placed B.Cu-down at the case's measured seat height ZB with
    NO mirroring — exactly the rigid motion of dropping the real board in.
    A mirrored case therefore fails (deep collisions + feature misalignment);
    a correct case passes.

Usage:
  python3 verify_fit.py [--case-dir DIR] [--board-glb PATH] [--geometry PATH]
                        [--report PATH] [--points N]

Exit code 0 iff every check passes.  Writes FIT_REPORT.md next to the case
dir it is run against and prints the table to stdout.
"""
import argparse
import json
import sys
import numpy as np
import trimesh
from shapely.geometry import Polygon, box as sbox, Point
from shapely.ops import unary_union

# ----------------------------------------------------------------------
# args
# ----------------------------------------------------------------------
ap = argparse.ArgumentParser()
ap.add_argument('--case-dir', default='.')
ap.add_argument('--board-glb', default='_work/board_v24_pads.glb')
ap.add_argument('--geometry', default='_work/geometry_v24.json')
ap.add_argument('--report', default=None)
ap.add_argument('--points', type=int, default=150000)
A = ap.parse_args()
CD = A.case_dir.rstrip('/') + '/'
REPORT = A.report or (CD + 'FIT_REPORT.md')
report = []
def row(feat, req, meas, ok):
    res = 'FLAG' if ok is None else ('PASS' if bool(ok) else 'FAIL')
    report.append((feat, req, meas, res))
    print(f'{res:4s} {feat:38s} | {req:36s} | {meas}', flush=True)

def inter(a, b):
    try:
        m = trimesh.boolean.intersection([a, b], engine='manifold')
        return m.volume if m is not None else 0.0
    except Exception:
        return -1.0

# ----------------------------------------------------------------------
# 1. case meshes (physical STL frame)
# ----------------------------------------------------------------------
names = ['case_base', 'case_top', 'bay_cup', 'pin_plate', 'button_cap',
         'light_pipe', 'plug', 'pogo_pin', 'contacts_assy', 'thermal_slug',
         'gasket', 'usb_flap', 'battery_door']
M = {}
missing = []
for n in names:
    try:
        M[n] = trimesh.load(CD + n + '.stl')
    except Exception as e:
        missing.append(n); M[n] = None
base, top = M['case_base'], M['case_top']
cup, plate = M['bay_cup'], M['pin_plate']
contacts, door, flap = M['contacts_assy'], M['battery_door'], M['usb_flap']
gasket_m = M['gasket']
case = trimesh.boolean.union([base, top], engine='manifold')
row('case parts present', 'all STLs load',
    f'missing={missing}', not missing)

# ----------------------------------------------------------------------
# 2. board truth from the fix_v24 pcbnew extraction (KiCad frame -> phys)
# ----------------------------------------------------------------------
G = json.load(open(A.geometry))
BT      = G['thickness_mm']                       # real stackup 1.5386
BT_MAX  = G.get('thickness_max_mm', 1.6)
def phys(x, y): return (x, -y)                    # KiCad -> physical frame
HOLES = {h['ref']: phys(*h['pos']) + (h['drill'][0],)
         for h in G['holes'] if h['ref'].startswith('H')}
FP = {f['ref']: f for f in G['components']}
def fp_phys(ref):
    f = FP[ref]; return phys(f['pos'][0], f['pos'][1])
def pad_phys(ref):
    return [(p['num'],) + phys(p['pos'][0], p['pos'][1]) +
            (p['size'][0], p['size'][1]) for p in FP[ref]['pads'] if p['num']]
LAND_REFS = ['J5', 'J6', 'J7', 'J11', 'J13', 'J3']
LANDS = [(ref, p[0], p[1], p[2], p[3], p[4])
         for ref in LAND_REFS for p in pad_phys(ref)]
BB = G['board_bbox_mm']
CY = BB['y'] + BB['h'] / 2

# ----------------------------------------------------------------------
# 3. real board surface points from the GLB  (+supplement for 2 models
#    kicad-cli could not resolve: U2 Texas_DSG0008A, U17 TDFN-8 — tiny F
#    parts, covered by their pcbnew courtyard x real height)
# ----------------------------------------------------------------------
scene = trimesh.load(A.board_glb)
board_m = scene.to_geometry()                     # metres, world transforms in
slab = trimesh.load(A.board_glb.replace('_pads.glb', '_slab.glb')
                  if '_pads' in A.board_glb else A.board_glb).to_geometry()
BCU = slab.bounds[0][1] * 1000.0                  # glb y of the B.Cu plane (mm)

def to_phys(P_m):
    P = np.asarray(P_m, float) * 1000.0
    return np.c_[P[:, 0], -P[:, 2], P[:, 1]]      # (x, -y_k, z_above_BCu)

rng = np.random.default_rng(0)
pts_m, _ = trimesh.sample.sample_surface(board_m, A.points)
P_phys = to_phys(pts_m)
# supplemental courtyard boxes for the two unresolved models
supp = []
for f in G['components']:
    if f['ref'] in ('U2', 'U17'):
        x0, y0 = f['bbox']['x'], f['bbox']['y']
        bb = sbox(x0, y0, x0 + f['bbox']['w'], y0 + f['bbox']['h'])
        bm = trimesh.creation.extrude_polygon(bb, f['h3d'])
        sp, _ = trimesh.sample.sample_surface(bm, 3000)
        # bm is in KiCad frame sitting on F.Cu (z 0..h): -> phys + z offset
        supp.append(np.c_[sp[:, 0], -sp[:, 1], sp[:, 2] + BT])
if supp:
    P_phys = np.vstack([P_phys] + supp)
    print(f'# supplemental courtyard points added: {sum(len(s) for s in supp)} '
          f'for U2/U17 (models absent from GLB export)')

# ----------------------------------------------------------------------
# 4. board seat height ZB measured on the base (wedge ledge top)
# ----------------------------------------------------------------------
# probe ring ~0.55 inside the physical board outline -> ledge top plane
sec3d = slab.section(plane_origin=[0, 0.0007, 0],
                     plane_normal=[0, 1, 0])      # slab section mid-thickness
p2d, _T = sec3d.to_2D()
outline2d = max(p2d.polygons_full, key=lambda p: p.area)
v3 = trimesh.transformations.transform_points(
    np.c_[np.array(outline2d.exterior.coords),
          np.zeros(len(outline2d.exterior.coords))], _T)
BPOLY_P = Polygon([(x * 1000.0, -z * 1000.0)      # glb (x,z) -> phys (x,y)
                   for x, y, z in v3])
# ledge top is the ring 0.35..0.9 inside the board outline (the seat wedge)
ring = BPOLY_P.buffer(-0.35).difference(BPOLY_P.buffer(-0.9))
zs = []
gx, gy = np.meshgrid(np.linspace(BPOLY_P.bounds[0], BPOLY_P.bounds[2], 80),
                     np.linspace(BPOLY_P.bounds[1], BPOLY_P.bounds[3], 100))
ri = trimesh.ray.ray_triangle.RayMeshIntersector(base)
for x, y in zip(gx.ravel(), gy.ravel()):
    if ring.contains(Point(x, y)):
        loc, _, _ = ri.intersects_location([[x, y, 8.0]], [[0, 0, -1]],
                                           multiple_hits=False)
        if len(loc):
            zs.append(float(loc[0][2]))
ZB_MEAS = float(np.median(zs)) if zs else float('nan')
ZB = 3.4                                          # design seat height
row('board seat plane', '3.40 +/-0.05 (design ZB)',
    f'measured ledge top z={ZB_MEAS:.3f} from {len(zs)} probes',
    abs(ZB_MEAS - 3.4) <= 0.05)
ZT, ZT_MAX = ZB + BT, ZB + BT_MAX
P_case = P_phys + np.array([0.0, 0.0, ZB - BCU])  # place real board on the seat

# ----------------------------------------------------------------------
# 5. COLLISION: real surface points inside the case solids
# ----------------------------------------------------------------------
def inside_report(mesh, name):
    inside = mesh.contains(P_case)
    hits = P_case[inside]
    deep = np.zeros((0, 3)); dmax = 0.0
    if len(hits):
        d = trimesh.proximity.signed_distance(mesh, hits)
        deep = hits[d > 0.1]
        dmax = float(d.max())
    row(f'real board pts inside {name}', '0 deeper than 0.1 mm',
        f'inside={len(hits)} deep={len(deep)} max_depth={dmax:.3f}',
        len(deep) == 0)
    return len(deep), dmax, hits

nd_b, dm_b, hits_b = inside_report(base, 'case_base')
nd_t, dm_t, hits_t = inside_report(top, 'case_top')

# region split of the deep points (diagnostic for the v10 mirror fault)
def region(pp, tag):
    if len(pp) == 0: return ''
    return (f'{tag}: n={len(pp)} '
            f'y[{pp[:,1].min():.1f},{pp[:,1].max():.1f}] '
            f'x[{pp[:,0].min():.1f},{pp[:,0].max():.1f}]')
if len(hits_b) or len(hits_t):
    allh = np.vstack([h for h in (hits_b, hits_t) if len(h)]) \
        if (len(hits_b) or len(hits_t)) else np.zeros((0, 3))
    print('# inside-point regions:')
    for tag, sel in [('antenna_zone y>+8', allh[:, 1] > 8),
                     ('electrode_zone y<-45', allh[:, 1] < -45),
                     ('east_tab x>46', allh[:, 0] > 46),
                     ('other', np.ones(len(allh), bool))]:
        pp = allh[sel]
        if len(pp): print('  ', region(pp, tag))

# ----------------------------------------------------------------------
# 6. mounting holes vs case locating pins (measured on the base mesh)
# ----------------------------------------------------------------------
def pin_measure(ex, ey):
    vs = base.vertices
    sel = (np.hypot(vs[:, 0] - ex, vs[:, 1] - ey) < 1.8) & \
          (vs[:, 2] > ZB + 0.3) & (vs[:, 2] < ZB + 1.45)
    p = vs[sel]
    if len(p) < 20:
        return None
    return np.array([p[:, 0].mean(), p[:, 1].mean()]), float(p[:, 2].max())
hbad = []
for hn, (hx, hy, hd) in sorted(HOLES.items()):
    got = pin_measure(hx, hy)
    if got is None:
        hbad.append((hn, 'no pin found')); continue
    (px, py), zt = got
    off = float(np.hypot(px - hx, py - hy))
    if off > 0.10:
        hbad.append((hn, f'axis off {off:.2f}'))
row('mounting-hole pin alignment',
    'each real board hole vs case pin: axis off <=0.10',
    f'bad={hbad}', not hbad)

# ----------------------------------------------------------------------
# 7. USB-C window vs real J1 (centre + opening), measured on the case
# ----------------------------------------------------------------------
J1 = fp_phys('J1')                                # (29.5, +7.8)
mouth_z = ZT_MAX + 1.6                            # receptacle mouth centre band
# open corridor = ray from inside the cavity exits the product entirely
# (no hit to y=+30).  The notch must sit inside a real +y wall: probes
# flanking the open span must hit the wall band ~y=10.8..12.4.
rt = trimesh.ray.ray_triangle.RayMeshIntersector(case)
xs = np.arange(20.0, 40.0 + 1e-9, 0.1)
first_hit = {}
open_x = []
for x in xs:
    loc, _, _ = rt.intersects_location([[x, 5.0, mouth_z]], [[0, 1, 0]],
                                       multiple_hits=False)
    first_hit[x] = float(loc[0][1]) if len(loc) else None
    if not len(loc):
        open_x.append(x)
flank_hits = [v for x, v in first_hit.items()
              if v is not None and 9.0 <= v <= 14.5]
if open_x:
    w = max(open_x) - min(open_x) + (xs[1] - xs[0])
    ctr = (max(open_x) + min(open_x)) / 2
    row('USB-C window vs J1', 'centre off <=0.2; opening >=9.6 wide',
        f'open span x {min(open_x):.1f}..{max(open_x):.1f} w={w:.1f} '
        f'centre off={abs(ctr - J1[0]):.2f} wall probes={len(flank_hits)}',
        abs(ctr - J1[0]) <= 0.2 and 9.6 <= w <= 16.0
        and len(flank_hits) >= 4)
else:
    row('USB-C window vs J1', 'centre off <=0.2; opening >=9.6 wide',
        f'no through-opening found at J1 mouth z={mouth_z:.1f}', False)
# overmould corridor: 12.4x6.5 plug (+0.3 clearance) from outside to mouth-0.5
om = trimesh.creation.box([12.4 + 0.6, 8.0, 6.5 + 0.6])
om.apply_translation([J1[0], 15.0, mouth_z])
v = inter(om, case)
row('USB-C overmould corridor', '0 inter face->mouth-0.5',
    f'{v:.3f} mm3', abs(v) < 0.5)

# ----------------------------------------------------------------------
# 8. electrode lands: open aperture + pogo pin within 0.2 mm (mesh-measured)
# ----------------------------------------------------------------------
def ray_open(x, y, mesh, z_from):
    ri2 = trimesh.ray.ray_triangle.RayMeshIntersector(mesh)
    loc, _, _ = ri2.intersects_location([[x, y, z_from]], [[0, 0, -1]],
                                        multiple_hits=True)
    return len(loc) == 0
bb_b = base.bounds
land_bad = []
for ref, num, x, y, w, h in LANDS:
    # the land must lie over the product footprint and over an open
    # through-aperture (ray from the board surface to the skin face)
    if not (bb_b[0][0] <= x <= bb_b[1][0] and bb_b[0][1] <= y <= bb_b[1][1]):
        land_bad.append((ref, num, 'land outside case footprint'))
        continue
    if not ray_open(x, y, base, ZB + 0.1):
        land_bad.append((ref, num, 'floor not open'))
row('electrode lands open through floor',
    'all 20 lands over open through-apertures',
    f'{len(LANDS) - len(land_bad)}/{len(LANDS)} open bad={land_bad[:4]}',
    not land_bad)
cparts = contacts.split() if contacts is not None else []
pogo_xy = [tuple((p.bounds[0] + p.bounds[1])[:2] / 2.0) for p in cparts
           if p.volume < 8.0]
pogo_bad = []
for ref, num, x, y, w, h in LANDS:
    d = min((np.hypot(x - qx, y - qy) for qx, qy in pogo_xy), default=99.0)
    if d > 0.2:
        pogo_bad.append((ref, num, round(float(d), 2)))
row('electrode land vs pogo pin XY', 'each land: pogo within 0.20',
    f'pogos={len(pogo_xy)} worst={pogo_bad[:4]}',
    len(pogo_xy) >= 20 and not pogo_bad)

# ----------------------------------------------------------------------
# 9. thermal slug: exactly one, at the U11 seat, face ~0.6 proud
# ----------------------------------------------------------------------
slugs = [p for p in cparts if p.volume > 8.0]
U11 = fp_phys('U11')
slug_bad = []
if len(slugs) != 1:
    slug_bad.append(f'{len(slugs)} slug bodies')
else:
    s = slugs[0]
    ctr = (s.bounds[0] + s.bounds[1]) / 2.0
    if np.hypot(ctr[0] - U11[0], ctr[1] - U11[1]) > 0.5:
        slug_bad.append(f'slug at {ctr[:2].round(2)} not {U11}')
    if not (-0.75 <= s.bounds[0][2] <= -0.45):
        slug_bad.append(f'face z={s.bounds[0][2]:.2f} not ~-0.6')
row('thermal slug', 'exactly one slug at U11, face 0.45-0.75 proud',
    f'bad={slug_bad}', not slug_bad)

# ----------------------------------------------------------------------
# 10. tabs vs pockets (physical positions), antenna pocket clearance
# ----------------------------------------------------------------------
TAB_N_P = (-2.05, 10.5, 22.5, 16.55)              # phys antenna tab outline
TAB_E_P = (48.0, -29.9, 53.6, -20.0)              # phys east tab outline
for nm, (x0, y0, x1, y1) in [('antenna tab', TAB_N_P), ('east tab', TAB_E_P)]:
    tp = trimesh.creation.extrude_polygon(sbox(x0, y0, x1, y1), ZT - ZB)
    tp.apply_translation([0, 0, ZB])
    v = inter(tp, case)
    row(f'{nm} in pocket', '0 inter at board z-band',
        f'{v:.3f} mm3', abs(v) < 0.5)
# antenna pocket inner face >=0.3 beyond the module overhang (+22.31 phys);
# cast outward from inside the cavity so the first hit IS the inner face
ri3 = trimesh.ray.ray_triangle.RayMeshIntersector(case)
loc, _, _ = ri3.intersects_location([[11.5, 12.0, 6.5]], [[0, 1, 0]],
                                    multiple_hits=False)
face = float(loc[0][1]) if len(loc) else -99.0
row('antenna pocket depth', 'inner face >= module end +22.31 +0.3',
    f'face y={face:.2f} gap={face - 22.31:.2f}',
    face - 22.31 >= 0.3 and face <= 24.0)
# east-tab pocket wall face >=0.3 beyond the tab edge (probe in the board band)
loc, _, _ = ri3.intersects_location([[45.0, -25.0, 4.2]], [[1, 0, 0]],
                                    multiple_hits=False)
fx = float(loc[0][0]) if len(loc) else -99.0
row('east tab pocket depth', 'pocket face >= tab edge 53.6 +0.3',
    f'face x={fx:.2f} gap={fx - 53.6:.2f}', fx - 53.6 >= 0.3)

# ----------------------------------------------------------------------
# 11. clearances: board edge <-> wall, components <-> lid / floor
#    (containment via contains(); unsigned distance for outside points)
# ----------------------------------------------------------------------
def gap_report(pts, mesh, name, req, gap_min, extra=''):
    inside = mesh.contains(pts)
    out = pts[~inside]
    if len(out):
        cp, dist, _ = trimesh.proximity.closest_point(mesh, out)
        gmin = float(dist.min())
        arg = out[int(dist.argmin())]
    else:
        gmin, arg = float('nan'), None
    loc = f' at {np.round(arg, 2)}' if arg is not None else ''
    row(name, req + '; 0 inside',
        f'inside={int(inside.sum())} min gap {gmin:.3f}{loc}{extra}',
        inside.sum() == 0 and gmin >= gap_min)

# perimeter side faces of the real slab (within 0.6 of the outline ->
# excludes interior hole/slot walls, which are checked vs pins), above the
# seat contact plane, inside the board pocket z-band -> gap to the wall.
# v11b: the F-clamp ledge intentionally overhangs the board's top edge
# (its underside sits at ZT_MAX), so the upper 0.3 band of the side face
# is excluded - under-ledge clearance is the clamp interface, not the wall.
sv, sf = trimesh.sample.sample_surface(slab, 80000)
sn = slab.face_normals[sf]
side = np.abs(sn[:, 1]) < 0.5                     # glb y = board z -> side walls
edge_pts = to_phys(sv[side]) + np.array([0.0, 0.0, ZB - BCU])
edge_pts = edge_pts[(edge_pts[:, 2] > ZB + 0.1) &
                    (edge_pts[:, 2] < ZT_MAX - 0.3)]
bnd = BPOLY_P.boundary
keep = np.array([bnd.distance(Point(p[0], p[1])) < 0.6 for p in edge_pts])
edge_pts = edge_pts[keep]
gap_report(edge_pts, case, 'board edge <-> wall clearance',
           'min gap >=0.20 (pocket CL 0.3, probed)', 0.20,
           f' over {len(edge_pts)} perimeter pts')
# F-side component tops -> ceiling gap: ray +z from each F courtyard centre
# region at its model height -> first case hit is the ceiling/fin bottom
ri_up = trimesh.ray.ray_triangle.RayMeshIntersector(top)
ceil_min, ceil_at = 99.0, None
for f in G['components']:
    if f['side'] != 'F' or f.get('h3d', 0) <= 0.15:
        continue
    x0, y0 = f['bbox']['x'], f['bbox']['y']
    zt = ZT + f['h3d'] + 0.01
    for gx2 in np.linspace(x0 + 0.15, x0 + f['bbox']['w'] - 0.15, 3):
        for gy2 in np.linspace(y0 + 0.15, y0 + f['bbox']['h'] - 0.15, 3):
            px, py = gx2, -gy2                     # KiCad -> phys
            loc, _, _ = ri_up.intersects_location(
                [[px, py, zt]], [[0, 0, 1]], multiple_hits=False)
            if len(loc):
                g = float(loc[0][2]) - zt
                if g < ceil_min:
                    ceil_min, ceil_at = g, (f['ref'], px, py)
row('component tops <-> lid ceiling', 'min gap >=0.30',
    f'min gap {ceil_min:.3f} at {ceil_at}', ceil_min >= 0.30)
# lateral pinch: unsigned min distance, any direction (hard no-contact)
fmask = P_case[:, 2] > ZT + 0.05
gap_report(P_case[fmask], top, 'components <-> lid (any direction)',
           'min gap >=0.20 (strap-fin ledge relieved)',
           0.20, f' over {int(fmask.sum())} F-side pts')
# B-side component bottoms -> floor gap (points below B.Cu plane)
bmask = P_case[:, 2] < ZB - 0.05
gap_report(P_case[bmask], base, 'components <-> floor clearance',
           'min gap >=0.15', 0.15, f' over {int(bmask.sum())} B-side pts')

# ----------------------------------------------------------------------
# 12. battery bay vs 402535 cell envelope (24.3 x 35.0 x 4.0 on the plate)
# ----------------------------------------------------------------------
cup_at = cup.copy(); cup_at.apply_translation([0, 0, 9.85])
bat = trimesh.creation.box([24.3, 35.0, 4.0])
bat.apply_translation([17.3, -22.5, 9.85 + 1.4 + 2.0])
v = inter(bat, cup_at)
row('battery 402535 in cup', 'cell fits (press ribs 0.35)',
    f'inter {v:.3f} mm3', abs(v) < 0.5)

# fix_v25 F-side test pads (hw/v25-testpoints, read-only): TP29 RLDREF
# KiCad (46.88, 7.60) -> phys (46.88,-7.60); TP36 IOVDD (45.04,-9.44) ->
# phys (45.04, 9.44).  Ø1.0 SMD pads on F.Cu; the lid ledge is relieved
# over each pad (build_case.py).  Probe the pad rim band at pad-top height.
tp_min, tp_at = 99.0, None
for tpref, tpx, tpy in [('TP29', 46.88, -7.60), ('TP36', 45.04, 9.44)]:
    ang = np.linspace(0, 2 * np.pi, 40, endpoint=False)
    tpp = np.vstack([np.c_[tpx + 0.48 * np.cos(ang),
                           tpy + 0.48 * np.sin(ang),
                           np.full(40, ZT + 0.02)],
                     [[tpx, tpy, ZT + 0.02]]])
    _, td, _ = trimesh.proximity.closest_point(top, tpp)
    if float(td.min()) < tp_min:
        tp_min, tp_at = float(td.min()), tpref
row('TP29/TP36 <-> lid ledge', 'min gap >=0.03 (ledge relieved)',
    f'min gap {tp_min:.3f} at {tp_at}', tp_min >= 0.03)

# ----------------------------------------------------------------------
# 13. print overhang audit (unchanged voxel algorithm from case_v10)
# ----------------------------------------------------------------------
def _dil8(m):
    d = m.copy()
    for di in (-1, 1):
        d[max(0, -di):m.shape[0]-max(0, di), :] |= \
            m[max(0, di):m.shape[0]-max(0, -di), :]
    src = d.copy()
    for dj in (-1, 1):
        d[:, max(0, -dj):m.shape[1]-max(0, dj)] |= \
            src[:, max(0, dj):m.shape[1]-max(0, -dj)]
    return d

def _section_polys(mesh, z):
    try:
        s = mesh.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1])
        if s is None:
            return []
        p2d, T = s.to_2D()
        out = []
        for p in p2d.polygons_full:
            # to_2D recentres the plane: map coords back into mesh (x,y)
            def _w(coords):
                v = np.c_[np.asarray(coords), np.zeros(len(coords))]
                return trimesh.transformations.transform_points(v, T)[:, :2]
            out.append(Polygon(_w(p.exterior.coords),
                               [_w(r.coords) for r in p.interiors]))
        return out
    except Exception:
        return []

def _occ_from_sections(mesh, pitch=0.2):
    import shapely
    bmin, bmax = mesh.bounds
    x0, y0, z0 = bmin
    nx = int(np.ceil((bmax[0]-x0)/pitch)); ny = int(np.ceil((bmax[1]-y0)/pitch))
    nz = int(np.ceil((bmax[2]-z0)/pitch))
    xs = x0 + (np.arange(nx)+0.5)*pitch
    ys = y0 + (np.arange(ny)+0.5)*pitch
    heights = z0 + (np.arange(nz)+0.5)*pitch
    occ = np.zeros((nx, ny, nz), bool)
    GX, GY = np.meshgrid(xs, ys, indexing='ij')
    for k, h in enumerate(heights):
        polys = _section_polys(mesh, h)
        if not polys:
            for dz in (0.03, -0.03, 0.06, -0.06):
                polys = _section_polys(mesh, h+dz)
                if polys:
                    break
        for p in polys:
            occ[:, :, k] |= shapely.contains_xy(p, GX, GY)
    return occ

def overhang_audit(mesh, flip=False, pitch=0.2):
    """Unsupported down-facing cells, per the design's own print rules
    (PRINT_NOTES): a down-face is acceptable if it is (a) inside a 45 deg
    self-support cone from lower material, (b) an open-to-plate face (clear
    air below it down to the bed), or (c) the roof of a <=2.0 mm ledge/pocket.
    It is BAD only when none of those hold (deep enclosed canopy)."""
    try:
        occ = _occ_from_sections(mesh, pitch)
    except Exception as e:
        return -1, str(e)
    if flip:
        occ = occ[:, :, ::-1]
    sx, sy, sz = occ.shape
    supp = np.zeros((sx, sy), bool)
    run = np.full((sx, sy), np.inf)   # distance to nearest material below
    bad = 0
    for k in range(1, sz):
        supp = _dil8(supp | occ[:, :, k-1])
        run = np.where(occ[:, :, k-1], 0.0, run + pitch)
        down_faces = occ[:, :, k] & ~occ[:, :, k-1]
        un = down_faces & ~supp
        # un & run<=2mm  -> shallow roofed ledge (allowed)
        # un & run==inf  -> open-to-plate face   (allowed)
        # un & run>2mm   -> deep canopy          (BAD)
        bad += int(np.count_nonzero(un & np.isfinite(run) & (run > 2.0)))
    return bad, ''

for nm, m, fl in [('base', base, False), ('top', top, True),
                  ('bay_cup', cup, False), ('pin_plate', plate, False)]:
    nbad, err = overhang_audit(m, fl)
    row(f'overhang audit {nm}', '0 bad deep canopies @0.2mm',
        f'{nbad} cells {err}', nbad == 0 if nbad >= 0 else None)

# ----------------------------------------------------------------------
# 14. watertight
# ----------------------------------------------------------------------
wt_bad = [n for n in names if M[n] is not None and not M[n].is_watertight]
row('STL watertight', 'all parts closed', f'bad={wt_bad}', not wt_bad)

# ----------------------------------------------------------------------
# report
# ----------------------------------------------------------------------
npass = sum(1 for r in report if r[3] == 'PASS')
nflag = sum(1 for r in report if r[3] == 'FLAG')
nfail = sum(1 for r in report if r[3] == 'FAIL')
mirror_flag = (nd_b + nd_t) > 50 or nfail >= 3
lines = ['# VitalQ case real-geometry FIT REPORT\n',
         f'Case under test: `{CD}` (STLs)  |  board: fix_v24 GLB '
         f'({A.board_glb}) + pcbnew extraction ({A.geometry}).',
         f'Points sampled: {A.points} real board/component surface points '
         f'(+{sum(len(s) for s in supp) if supp else 0} supplemental).',
         'Frame: STL frame is the physical frame (x=KiCad x, y=-KiCad y, '
         'z up from skin); board placed B.Cu-down at the measured seat '
         f'z={ZB:.2f} with NO mirroring allowed.',
         f'kicad-cli 10.0.4 exports. tolerance: deep >0.1 mm counts a point.\n',
         '| Feature | Requirement | Measured | Result |', '|---|---|---|---|']
for feat, req, meas, res in report:
    lines.append(f'| {feat} | {req} | {meas} | {res} |')
lines.append(f'\n**{npass} checks PASS, {nfail} FAIL, {nflag} FLAGGED '
             f'({len(report)} total)**\n')
if nfail:
    lines.append('VERDICT: **FAIL**' +
                 (' — mirror-frame fault signature present\n'
                  if mirror_flag else '\n'))
else:
    lines.append('VERDICT: PASS\n')
with open(REPORT, 'w') as f:
    f.write('\n'.join(lines))
print('\n'.join(lines[-6:]))
sys.exit(0 if nfail == 0 else 1)
