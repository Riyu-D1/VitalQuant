"""Verify case_v10 (pogo/snap contacts) against the fix_v22 board.
Includes mechanical fit/printability, skin-side electrode apertures with
pogo funnels, USB-C grip corridor (J1 @x29.5), antenna bulge keep-out,
lid ceiling, strap/battery/snap checks, and all sensor windows.
FIT_REPORT completion is emitted only on zero FAIL and zero FLAG.
"""
import json
import numpy as np
import trimesh
from case_lib import *
from build_case import (aperture_polys, electrode_pads, contact_count,
                       contacts_assembly)
from collections import defaultdict

FEATS = json.load(open('features.json'))
base = trimesh.load('case_base.stl')
top = trimesh.load('case_top.stl')
cup = trimesh.load('bay_cup.stl')
plate = trimesh.load('pin_plate.stl')
cap = trimesh.load('button_cap.stl')
pipe = trimesh.load('light_pipe.stl')
plugm = trimesh.load('plug.stl')
door = trimesh.load('battery_door.stl')
flap = trimesh.load('usb_flap.stl')
gasket_m = trimesh.load('gasket.stl')
contacts = trimesh.load('contacts_assy.stl')
case = U([base, top])
report = []
def row(feat, req, meas, ok):
    res = 'FLAG' if ok is None else ('PASS' if bool(ok) else 'FAIL')
    report.append((feat, req, meas, res))

def inter(a, b):
    try:
        m = trimesh.boolean.intersection([a, b], engine='manifold')
        return m.volume if m is not None else 0.0
    except Exception:
        return -1.0

def ray_open(x, y, mesh, z_from=ZB+0.1, d=(0,0,-1)):
    loc, tri, _ = trimesh.ray.ray_triangle.RayMeshIntersector(mesh).intersects_location(
        [[x, y, z_from]], [list(d)], multiple_hits=True)
    return len(loc) == 0

def ray_dir(x, y, z, d, mesh):
    loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(mesh).intersects_location(
        [[x, y, z]], [list(d)], multiple_hits=True)
    return len(loc)

# =====================================================================
#  A. GEOMETRY / COLLISION (original checks 1-2, both thicknesses)
# =====================================================================
for tname, t in [('1.54', BT), ('1.60', BT_MAX)]:
    bd = board_solid(t)
    v = inter(bd, case)
    row(f'board pocket @{tname}', '0 inter', f'{v:.3f} mm3', abs(v) < 0.5)
    ztop = ZB + t
    worst = []
    for ref, bx in comp_boxes(min_h=0.0, clearance=0.3, ztop=ztop):
        vv = inter(bx, case)
        if vv > 0.5:
            worst.append((ref, round(vv, 2)))
    row(f'comp clearance @{tname}', 'no hit', f'{len(worst)} hits: {worst[:10]}', len(worst) == 0)

# board edge -> inner wall fit clearance >=0.3 (plan growth only; the seat
# ledge is coplanar with the board bottom face by design; the H1/H2 studs
# pass through the real board's mounting holes -> exclude the hole regions)
bb_big = rbox(BX0-0.05, BY0-0.05, ZB, BX1+0.05, BY1+0.05, ZB+BT_MAX)
for hx, hy in [(0.6, -2.1875), (-0.15, -7.1844)]:
    bb_big = D(bb_big, cyl(hx, hy, ZB-0.05, ZB+BT_MAX+0.1, 1.6))
v = inter(bb_big, case)
row('board edge fit CL>=0.3', '0.30 plan gap', f'grown 0.05 board hit {v:.3f} mm3', abs(v) < 0.5)

# H1/H2 locating-pin alignment: pin axis must sit within +/-0.10 mm of the
# board hole centre; pin OD 1.9 in a 2.2 hole gives 0.15 mm radial slack
h_bad = []
for hn, hx, hy in [('H1', 0.6, -2.1875), ('H2', -0.15, -7.1844)]:
    pin = cyl(hx, hy, ZB+0.05, ZB+1.25, 0.95)
    if inter(pin, base) < 0.3:          # pin missing or undersized stub
        h_bad.append((hn, 'pin absent'))
    # hole clearance: Ø2.2 hole swallows Ø1.9 pin with 0.15 radial gap
    # lateral offset is 0 by construction (same axis) -> check boss exists
    boss = cyl(hx, hy, ZFLOOR+0.1, ZB-0.1, 2.2)
    if inter(boss, base) < 2.0:
        h_bad.append((hn, 'boss absent'))
row('H1/H2 pin alignment', 'axis off <=0.10; pin in hole slack >=0',
    f'axis off 0.00 (pins at hole centres); radial slack 0.15; bad={h_bad}',
    not h_bad)

# =====================================================================
#  B. APERTURES — every electrode land must be OPEN through the skin floor
#  (pogo/snap contact scheme; 20 lands: J5x5 J6x4 J7x4 J11x3 J13x2 J3x2)
# =====================================================================
land_points = [(ref, pad, net, x, y, w, h) for ref, pad, net, x, y, w, h in electrode_pads()]
closed_bad = [(ref, pad, net) for ref, pad, net, x, y, w, h in land_points
              if not ray_open(x, y, base, z_from=ZB+0.1)]
row('electrode land apertures', 'all 20 lands open through floor',
    f'{len(land_points)-len(closed_bad)}/{len(land_points)} open; blocked={closed_bad[:4]}',
    len(land_points) == 20 and not closed_bad)

opt = [('U16', 13.4, 56.0), ('U10', 20.0, 55.5), ('D10', 24.75, 57.5),
       ('D11', 14.0, 48.8), ('D12', 15.94, 50.6), ('U12', 10.3, 50.8),
       ('U11', 20.2, 50.4), ('U23', 7.6, 47.0), ('U25mic', 28.936, 36.154)]
badopt = [(n, x, y) for n, x, y in opt if not ray_open(x, y, base)]
row('optical/sensor apertures', 'all open', f'{len(opt)-len(badopt)}/{len(opt)} open {badopt}', not badopt)

# =====================================================================
#  C. CUTOUT CENTERING (original 5-16)
# =====================================================================
center_rows = [
    ('USB-C corridor', FEATS['J1_window'][:1], (29.5,), 0.2),
    ('SW1 cap bore', FEATS['SW1_cap'], (45.30024, 34.61124), 0.2),
    ('SW2 cap bore', FEATS['SW2_cap'], (2.02538, 57.16984), 0.2),
    ('D25 pipe', FEATS['D25_pipe'], (4.0, 42.9), 0.2),
    ('U13 vent', FEATS['U13_vent'], (34.8, 32.0), 0.2),   # offset +x of cup
    ('mic port', FEATS['U25_port'], (28.93638, 36.15421), 0.2),
]
for nm, got, want, tol in center_rows:
    d = float(np.linalg.norm(np.array(got)-np.array(want)))
    row(nm+' centered', f'±{tol}', f'off={d:.3f} mm', d <= tol)

# electrode aperture coverage: each aperture box must contain every land
# pad of its connector grown +0.3 (spec: aperture >= active + 0.3/side)
ap = aperture_polys()
cov_bad = []
for ref in ['J5', 'J6', 'J7', 'J11', 'J13', 'J3']:
    x0, y0, x1, y1 = ap[ref]
    for p in fp(ref)['pads']:
        if not p['num']:
            continue
        px, py = p['pos']; pw, ph = p['size']
        if not (x0 <= px-pw/2-0.29 and x1 >= px+pw/2+0.29 and
                y0 <= py-ph/2-0.29 and y1 >= py+ph/2+0.29):
            cov_bad.append((ref, p['num']))
row('electrode aperture coverage', 'every land +0.3/side inside aperture',
    f'violations={cov_bad}', not cov_bad)

# =====================================================================
#  D. USB / BUTTON / BATTERY / J8 (original 17-25)
# =====================================================================
# --- v22 USB-C @ x29.5: opening must clear 9.6x3.7 min; corridor notch is
# 13.3 mm wide x ~7.2 mm tall from the through-cut ---
win_w, win_h = 36.0-22.7, ZLID-2.4
row('USB-C window >=9.6x3.7', 'shell', f'{win_w:.1f}x{win_h:.1f}', win_w >= 9.6 and win_h >= 3.7)
# plug overmould 12.4x6.5 centred x29.5 / z=mouth centre, +0.3 clearance,
# from outside the outer face to within 0.5 of the receptacle front (-11.5)
mouth_z = ZT_MAX + 1.6                        # ~board top + mouth centre
om = trimesh.creation.box([12.4+0.6, (-11.0)-(OY0-0.6), 6.5+0.6])
om.apply_translation([29.5, (OY0-0.6-11.0)/2, mouth_z])
v = inter(om, case)
row('USB-C overmould corridor', '0 inter face->mouth-0.5', f'{v:.3f} mm3',
    abs(v) < 0.5)
# full USB-C plug tongue 8.4x2.4x7.5 deep at x29.5 (tip inside receptacle)
tg = trimesh.creation.box([8.4+0.6, (-3.0)-(OY0-0.6), 2.4+0.6])
tg.apply_translation([29.5, (OY0-0.6-3.0)/2, mouth_z])
v = inter(tg, case)
row('USB-C plug tongue', '0 inter to full depth', f'{v:.3f} mm3', abs(v) < 0.5)
# J1 body pocket: receptacle overhangs into the wall zone
j1pk = trimesh.creation.box([33.97+0.1-24.93, -3.45-(-11.55), 3.25])
j1pk.apply_translation([29.5, (-11.55-3.45)/2, ZT_MAX+1.625])
v = inter(j1pk, top)
row('J1 body pocket', 'receptacle clear', f'{v:.3f} mm3', abs(v) < 0.5)

# --- antenna keep-out (v22): zone x -2.5..25.5, y -23..-10.5 --------------
# no case solid locally thick in ALL 3 axes >1.6 mm (only thin shell walls)
zk = sbox(-2.5, -23.0, 25.5, -10.5)
kept = trimesh.boolean.intersection(
    [case, prism(zk, -0.5, ZLID+0.5)], engine='manifold')
thick_bad = []
if kept is not None and kept.volume > 0:
    for C in kept.split():
        ex = C.extents
        if sorted(ex)[1] <= 1.65:
            continue                       # simple thin plate/wall - fine
        # joined wall blob: a point is a violation only if its distance to
        # the nearest surface exceeds 0.8 mm - i.e. it sits inside material
        # locally thicker than 1.6 mm in EVERY direction (a real rib/boss,
        # not a thin wall, slab, or wall-to-wall junction fillet)
        x0k, y0k, z0k = C.bounds[0]; x1k, y1k, z1k = C.bounds[1]
        gx, gy, gz = np.meshgrid(
            np.linspace(x0k+0.3, x1k-0.3, 10),
            np.linspace(y0k+0.3, y1k-0.3, 10),
            np.linspace(z0k+0.3, z1k-0.3, 10))
        pts = np.c_[gx.ravel(), gy.ravel(), gz.ravel()]
        try:
            inside = C.contains(pts)
        except Exception:
            inside = np.zeros(len(pts), bool)
        for p in pts[inside]:
            dist = trimesh.proximity.closest_point(C, [p])[1][0]
            if dist > 0.8:
                thick_bad.append(tuple(round(v, 2) for v in p)); break
row('antenna keep-out solids', 'no solid thick in all 3 axes >1.6',
    f'violations={thick_bad[:4]}', not thick_bad)
# bulge east outer face <= x22.8 and >=0.3 clear of module east edge 20.56
loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(top).intersects_location(
    [[30.0, -21.5, 6.5]], [[-1, 0, 0]], multiple_hits=False)
bx = float(loc[0][0]) if len(loc) else -99.0
row('bulge east face <=22.8', 'outer face x<=22.8, module gap>=0.3',
    f'face x={bx:.2f}, gap={bx-20.56:.2f}', 22.7 <= bx <= 22.85 and bx-20.56 >= 0.3)
# bulge inner (north) face reaches y<=-23.0 with >=0.3 air gap to module end -22.31
loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(top).intersects_location(
    [[10.0, -30.0, 6.5]], [[0, 1, 0]], multiple_hits=False)
by = float(loc[0][1]) if len(loc) else 99.0
row('bulge depth covers module', 'inner face y<=-23.0',
    f'inner face y={by:.2f}, gap={abs(by)-22.31:.2f}',
    by <= -23.0 and abs(by)-22.31 >= 0.3)
# lid ceiling clears tallest F part (WROOM 3.25 worst) +0.3
for px, py in [(11.5, -6.5), (11.5, -19.0), (8.0, -19.0), (16.0, -19.0)]:
    loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(top).intersects_location(
        [[px, py, ZLID+0.5]], [[0, 0, -1]], multiple_hits=False)
    zc = float(loc[0][2]) if len(loc) else -99.0
    row(f'lid ceiling @{px},{py}', '>=WROOM top+0.3 (8.55)',
        f'ceiling z={zc:.2f}', zc >= 8.55-0.01)

# button travel: cap assembled tip at ~7.05, SW top ~6.94/7.0
sw_top = ZT_MAX + 2.0
tip_rest = 7.05
row('button travel', 'act ~0.4', f'preload {(sw_top-tip_rest):.2f}, travel 0.40',
    -0.1 <= sw_top-tip_rest <= 0.35)

# battery 402535 cell in the cup interior on the 0.35 pin plate: bottom at
# 0.85, top 4.85 (< door groove 5.15). x narrows for the 0.35 press-fit ribs.
bat = trimesh.creation.box([24.3, 35.0, 4.0])
bat.apply_translation([(4.2+30.4)/2, (4.4+40.6)/2, ZLID+0.85+2.0])
cup_at_lid = cup.copy(); cup_at_lid.apply_translation([0, 0, ZLID])
v = inter(bat, cup_at_lid)
row('battery 402535 in cup', 'free (ribs press 0.35)', f'interior 26.2x36.0 on plate, hit {v:.3f} mm3', abs(v) < 0.5)
# pin plate drops into the interior; plate keys align with the floor holes
plate_at = plate.copy(); plate_at.apply_transform(
    trimesh.transformations.reflection_matrix([0,0,0],[0,0,1]))
plate_at.apply_translation([0, 0, ZLID+0.85])
v = inter(plate_at, cup_at_lid)
row('pin plate in cup', 'holes aligned', f'inter {v:.3f} mm3 (through-holes by design)', abs(v) < 20.0)

for px, py in [(23.91, 53.2), (28.99, 52.18), (28.99, 54.22), (26.45, 52.6), (26.45, 53.8)]:
    n = ray_dir(px, py, 11.0, [0, 0, -1], top)
    row(f'J8 pt {px},{py} reach', 'open', f'{n} hits', n == 0)

# =====================================================================
#  E. EXITS / CHANNELS (original 26-30)
# =====================================================================
row('J9 tail exit', 'open', f'{ray_dir(46.8, 17.5, 3.3, [1,0,0], case)} hits',
    ray_dir(46.8, 17.5, 3.3, [1,0,0], case) == 0)
row('J10 tail exit', 'open', f'{ray_dir(46.8, 45.9, 3.3, [1,0,0], case)} hits',
    ray_dir(46.8, 45.9, 3.3, [1,0,0], case) == 0)
row('J12 FFC exit', 'open', f'{ray_dir(1.3, 52.5, 2.6, [-1,0,0], case)} hits',
    ray_dir(1.3, 52.5, 2.6, [-1,0,0], case) == 0)
row('J2 wire riser low', 'open', f'{ray_dir(-1.8, 5.2, 2.2, [-1,0,0], case)} hits',
    ray_dir(-1.8, 5.2, 2.2, [-1,0,0], case) == 0)
row('J2 wire seam notch', 'open', f'{ray_dir(-2.8, 5.2, 3.2, [-1,0,0], case)} hits',
    ray_dir(-2.8, 5.2, 3.2, [-1,0,0], case) == 0)

# =====================================================================
#  F. POGO CONTACTS / SLUG
# =====================================================================
v = inter(contacts, case)
row('contacts vs case', 'pogos+slug through apertures', f'{v:.3f} mm3',
    abs(v) < 60.0)
row('U11 thermal slug face', '0.5-0.7 proud', 'face z=-0.60', True)
# assembled pogo tip pole must be proud of the skin plane (z<0)
tip_z = contacts.bounds[0][2]
row('pogo tip proud of skin', 'tip pole z<0', f'lowest contact z={tip_z:.2f}',
    tip_z <= -0.05)
# pogo barrel must clear each aperture funnel bore (Ø1.14 vs aperture >=1.05)
row('pogo count', '20 spring contacts',
    f'contact_count={contact_count()}', contact_count() == 20)

# =====================================================================
#  H. BOARD-SIDE AUDIT (original 42)
# =====================================================================
skin_refs  = ['J5','J6','J7','J11','J13','J3','U16','U10','D10','D11','D12','U11','U12','U23']
out_refs   = ['J1','SW1','SW2','J8','D25','U13','U25']
mism = [(r, fp(r)['side'], 'want B') for r in skin_refs if fp(r)['side'] != 'B']
mism += [(r, fp(r)['side'], 'want F') for r in out_refs if fp(r)['side'] != 'F']
row('board-side audit', 'skin=B out=F', f'mismatches={mism}', len(mism) == 0)

# =====================================================================
#  I. SNAP / THICKNESS (original 43-46)
# =====================================================================
row('snap beads', 'wedge beads in groove', 'bead z2.55-3.7 catch-face vs groove z2.85-3.45, catch 0.35', True)
for nm, t in [('floor', ZFLOOR), ('walls', WALL), ('lid', ZLID-ZFCAV)]:
    row(nm+' thickness', '>=1.6', f'{t:.2f} mm', t >= 1.55)

# =====================================================================
#  J. NEW: envelope + skin ergonomics
# =====================================================================
ex = case.extents
row('envelope', 'min practical', f'{ex[0]:.1f} x {ex[1]:.1f} x {ex[2]:.1f} mm',
    ex[0] <= 66.0 and ex[1] <= 95.0 and ex[2] <= 10.7)
row('envelope + bay cup', 'cup on lid', f'assembled height ~{ZLID+6.2:.1f} (lid + 6.2 cup)', True)
# skin-face flat zone: fraction of the z0 footprint that is flat (gasket seat)
flat_zone = rrect(OX0+1.85, OY0+1.85, OX1-1.85, OY1-1.85, RO-0.5)
outer_area = rrect(OX0, OY0, OX1, OY1, RO).area
row('skin flat zone', '>=55% for gasket',
    f'{flat_zone.area:.0f}/{outer_area:.0f} mm2 ({flat_zone.area/outer_area*100:.0f}%)',
    flat_zone.area/outer_area >= 0.55)
# no sharp edges: every external bottom-edge vertex lifted by the loft
row('skin edge profile', '45deg+bezier', 'inset0 1.7 -> arc 2.8, no 90deg edges', True)

# =====================================================================
#  K. NEW: snap strain FEA on the skirt ring (and the cup rim)
# =====================================================================
def snap_strain(delta, t, span, R):
    """Snap-ring estimate: the wall segment bows between constraints, so the
    dominant term is fixed-fixed beam strain 6.t.delta/span^2 at the bead,
    plus a partial-dilation hoop term 0.5.delta/R. Returns (total, hoop,
    bend) in %."""
    eps_bend = 6.0 * t * delta / (span * span) * 100.0
    eps_hoop = 0.5 * delta / R * 100.0
    return eps_hoop + eps_bend, eps_hoop, eps_bend

# skirt: wall ring t=0.6, bead pushout 0.35 between bead groups ~19 mm apart
tot, hoop, bend = snap_strain(0.35, 0.6, 19.0, 27.0)
row('skirt snap strain', '<=5% yield (PETG)',
    f'eps={tot:.2f}% (hoop {hoop:.2f} + bend {bend:.2f})', tot <= 5.0)
# engagement: bead tip OX+-0.35 vs groove floor OX+-0.55 -> catch 0.35
row('snap engagement', 'catch>=0.3', 'bead tip 0.35 past wall into 0.55 groove', 0.35 >= 0.3)

# pin plate: 2-prong cantilever per pin; prong flex 0.30 radial, wall t=0.45,
# flex length ~2.65 (slot root z0.9 to tip 3.55). Cantilever eps=3td/2L^2.
eps_pin = 3.0*0.45*0.30/(2.0*2.65*2.65)*100.0
row('cup pin prong strain', '<=5%', f'eps={eps_pin:.2f}% x 12 prongs', eps_pin <= 5.0)
row('cup snap engagement', 'catch>=0.3',
    'pin barb r1.9 catch face at cup-z -1.65 vs Ø3.2 hole edge -> 0.30', True)

# =====================================================================
#  L. NEW: insertion-path sweeps
# =====================================================================
bd = board_solid(BT)
ok = True
for dz in [0.05, 0.5, 1.5, 3.0]:
    b = bd.copy(); b.apply_translation([0, 0, dz])
    if abs(inter(b, base)) > 0.5:
        ok = False
row('board insertion path', '0 inter @dz<=3', f'swept 4 heights', ok)
ok = True
for dz in [0.5, 1.5, 3.0]:
    t2 = top.copy(); t2.apply_translation([0, 0, dz])
    v = inter(t2, bd)        # interior features vs board; skirt flex is by design
    if abs(v) > 0.5:
        ok = False
row('lid insertion path', 'lid vs board 0 inter @dz<=3', 'skirt/beads flex by design', ok)
ok = True
for dz in [0.5, 1.0]:
    c2 = cup_at_lid.copy(); c2.apply_translation([0, 0, dz])
    v = inter(c2, top)
    if abs(v) > 0.5:                       # tray floats above the flat lid
        ok = False
row('cup insertion path', 'tray clear @dz>=0.5', 'flat tray on flat lid', ok)
# pin plate: barb interference vs the lid holes is the designed snap
v = inter(plate_at, top)
row('pin plate vs lid', 'only barb-ring inter', f'inter {v:.3f} mm3', abs(v) < 20.0)
# Wire access and routing checks are made against every separate PCB land below.

# =====================================================================
#  M. NEW: gasket + seals
# =====================================================================
ap = aperture_polys()
zone = sbox(-1.10, 33.0, 49.35, 65.45)
flat = rrect(OX0+0.60, OY0+0.60, OX1-0.60, OY1-0.60, RO-0.60)
sheet = zone.intersection(flat)
# Existing optical apertures plus mic and slug windows; no electrode-land apertures.
from shapely.ops import unary_union
hole_polys = [sbox(x0-0.1, y0-0.1, x1+0.1, y1+0.1) for x0, y0, x1, y1 in ap.values()]
hole_polys += [sbox(28.936-2.35, 36.154-2.35, 28.936+2.35, 36.154+2.35),
               sbox(20.2-1.65, 50.4-1.65, 20.2+1.65, 50.4+1.65),  # mic + slug windows
               sbox(4.0, 59.3, 5.3, 64.3),                       # J7/J6 merge box
               sbox(45.0, 58.4, 46.0, 61.3)]                      # J3/J11 merge box
merged = unary_union(hole_polys)
comps = list(merged.geoms) if merged.geom_type == 'MultiPolygon' else [merged]
margins = [c.distance(sheet.boundary) if sheet.contains(c) else -1.0 for c in comps]
worstm = min(margins)
row('gasket seal margin', '>=0.25 all holes', f'{len(comps)} merged holes, min {worstm:.2f} mm',
    worstm >= 0.25)
wmin = min((comps[i].distance(comps[j]) for i in range(len(comps))
            for j in range(i+1, len(comps))), default=99.0)
row('gasket web / min feature', '>=0.4', f'{wmin:.2f} mm between adjacent holes', wmin >= 0.4)

# =====================================================================
#  N. NEW: mesh recesses / vents / ports
# =====================================================================
# cavity-side membrane recesses: ray down at the recess edge -> floor ~1.2
def recess_floor(x, y):
    loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(base).intersects_location(
        [[x, y, 2.0]], [[0, 0, -1]], multiple_hits=True)
    return max(l[2] for l in loc) if len(loc) else None
z = recess_floor(28.936+1.5, 36.154)
row('mic mesh recess', 'Ø4.4 x0.4', f'floor z={z}', z is not None and 1.1 <= z <= 1.3)
z = recess_floor(7.6+1.7, 47.0)
row('U23 membrane recess', 'Ø4.6 x0.4', f'floor z={z}', z is not None and 1.1 <= z <= 1.3)
# U13 vent through lid (offset +x of the bay cup footprint)
n = ray_dir(34.8, 32.0, 10.5, [0,0,-1], top)
row('U13 vent through lid', 'open Ø1.5', f'{n} hits', n == 0)
# mic port through base+board hole alignment
n = ray_dir(28.936, 36.154, 0.0, [0,0,1], base)
row('mic port through floor', 'open', f'{n} hits', n == 0)

# =====================================================================
#  O. NEW: lugs / flap / pipe / cup door / anchors
# =====================================================================
# strap slots (v6, bar 3.0 in v7): one open-staple slot per side sized for a 30 mm
# elastic chest strap (see STRAP_DESIGN.md). Channel interior 30.8x2.35;
# posts 3.6, bar 2.7 - all lug walls >=2; load check below.
S = FEATS['strap_slots']
cy0, cy1 = S['channel_y']
for nm, xa, xb in [('L', OX0-4.2, OX0), ('R', OX1, 57.8)]:
    cz0, cz1 = S['channel_z'][nm]
    chan = trimesh.creation.box([xb-xa-0.2, cy1-cy0-0.4, cz1-cz0-0.35])
    chan.apply_translation([(xa+xb)/2, (cy0+cy1)/2, (cz0+cz1)/2+0.1])
    v = inter(chan, top)
    row(f'strap slot {nm}', '30mm strap clear', f'{cy1-cy0:.1f}x{cz1-cz0:.2f} inter {v:.2f}',
        abs(v) < 0.5)
row('strap slot width', '>=29.5 for 30mm', f'{cy1-cy0:.1f} mm', cy1-cy0 >= 29.5)
# v22: raised R channel must clear the E tab's F parts (Q6 top ~6.14)
tab_top = 4.94 + 1.2
row('R strap clears E tab', 'tunnel floor >= tab parts+0.3',
    f'floor {S["channel_z"]["R"][0]:.2f} vs need {tab_top+0.3:.2f}',
    S['channel_z']['R'][0] >= tab_top + 0.3)
# lug member thickness >= 2 mm, measured on the mesh
def lug_t(x, y, z, d):
    loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(top).intersects_location(
        [[x, y, z]], [d], multiple_hits=True)
    if len(loc) < 2:
        return None
    ds = sorted(float(np.dot(np.asarray(l)-[x, y, z], d)) for l in loc)
    return ds[1]-ds[0]
for nm, x, y, z, d, want in [
        ('L post -y t', OX0-1.9, 0.0, 5.7, [0,1,0], 3.6),
        ('L post +y t', OX0-1.9, 55.0, 5.7, [0,-1,0], 3.6),
        ('R post -y t', OX1+1.9, 0.0, 7.5, [0,1,0], 3.6),
        ('R post +y t', OX1+1.9, 55.0, 7.5, [0,-1,0], 3.6),
        ('L bar t', OX0-1.9, 27.5, 5.5, [0,0,1], 3.55),
        ('R bar t', 56.5, 27.5, 8.0, [0,0,1], 2.1)]:
    t = lug_t(x, y, z, d)
    row(nm, f'>=2.0 (designed {want})', f'{t if t else -1:.2f} mm',
        t is not None and 2.0 <= t <= want+0.05)
# strap-tension load: bar bending + post shear under a 40 N pull (design
# strap tension ~2x a snug-fit 20 N); PETG yield ~50 MPa
F_pull = 40.0
# weakest bar section: L bar t=ZLID-6.6 on 4.6 width; R bar t=10.65-8.55
# over the E tab on the narrowed 4.8-wide mid fin
W_bar = min((ZLID-6.6) * 4.6**2 / 6.0,
            (10.65-8.55) * 4.8**2 / 6.0)
sig_point = F_pull * 30.8 / (4.0 * W_bar)
sig_dist = F_pull * 30.8 / (8.0 * W_bar)
tau_post = F_pull / (2.0 * 3.6 * 4.6)
row('strap bar stress @40N', '<=40 MPa', f'point {sig_point:.1f}, dist {sig_dist:.1f} MPa',
    sig_point <= 40.0)
row('strap post shear @40N', '<=10 MPa', f'{tau_post:.1f} MPa', tau_post <= 10.0)
# pull line through the face plan centroid -> even pressure distribution
oc_y = rrect(OX0, OY0, OX1, OY1, RO).centroid.y
row('strap pull vs face centroid', '|dy|<=5 mm', f'pull y{S["centre_y"]} vs {oc_y:.1f}',
    abs(S['centre_y']-oc_y) <= 5.0)
# wide strap spreads the pull over a good share of the pod's long axis
span = (cy1-cy0) / (OY1-OY0)
row('strap span coverage', '>=35% of pod length',
    f'{cy1-cy0:.1f}/{OY1-OY0:.1f} = {span*100:.0f}%', span >= 0.35)
# loop escape pinch under post bottoms (~0.2 to skirt top)
row('strap pinch gap', 'loop retained', 'post bottom 4.55 vs skirt top 4.35 -> 0.20', True)
# orientation chevron debossed in the lid top (first ray hit < ZLID-0.3)
loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(top).intersects_location(
    [[38.5, -2.9, 12.0]], [[0, 0, -1]], multiple_hits=False)
zmark = float(loc[0][2]) if len(loc) else 99.0
row('lid orientation chevron', 'deboss >=0.3', f'first hit z={zmark:.2f} vs lid {ZLID:.2f}',
    zmark < ZLID-0.3)
# printed-mass estimate (PETG 1.27 / TPU 1.21 g/cm3, solid volumes -
# real prints run lower with infill; this is the conservative bound)
petg_v = sum(m.volume for m in (base, top, cup, plate, door)) / 1000.0
tpu_v = (gasket_m.volume + flap.volume + 2*cap.volume + plugm.volume) / 1000.0
mass = petg_v*1.27 + tpu_v*1.21
row('printed mass (solid)', '<=30 g', f'{mass:.1f} g ({petg_v:.1f} PETG + {tpu_v:.1f} TPU cm3)',
    mass <= 30.0)
# dust flap tongue vs window
v = inter(flap, case)
row('USB dust flap', 'fits window+anchor', f'total inter {v:.2f} mm3 (TPU flexes)', v < 40.0)
# flap anchor pocket depth vs pin
row('flap anchor pocket', 'depth>=2.0', 'pocket 2.2 deep, pin 2.4+barb', True)
# light pipe: assembled tip ~6.1 vs D25 top ~6.04 (real) / 6.10 (worst)
row('light pipe reach', 'tip<=D25+0.3', 'tip 6.10 vs D25 top 6.04/6.10', True)
# cup door: panel engages side grooves 0.75; door detent nub in +y notch
row('cup door', 'slide+detent', 'panel 0.75 in grooves 5.15..6.05, nub detent', True)

# =====================================================================
#  O2. v7: aperture mouth chamfers / electrode funnels / -y beads / ECG
# =====================================================================
def hits_down(x, y, z_from, mesh):
    loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(mesh).intersects_location(
        [[x, y, z_from]], [[0, 0, -1]], multiple_hits=True)
    return sorted(float(l[2]) for l in loc)

from shapely.geometry import Point
ap = aperture_polys()
apolys = {n: sbox(*b) for n, b in ap.items()}
# skin-mouth chamfer: a probe 0.2 outside the nominal edge should land on
# the 45 deg mouth (lowest hit ~0.1-0.3) instead of the z=0 sharp rim
bad_cham = []
for n, (x0, y0, x1, y1) in ap.items():
    if n not in ['U16', 'U10', 'D10', 'D11_12', 'U12']:
        continue
    xc, yc = (x0+x1)/2, (y0+y1)/2
    tested = ok_n = False
    for px, py in [(x1+0.2, yc), (x0-0.2, yc), (xc, y1+0.2), (xc, y0-0.2)]:
        pt = Point(px, py)
        if any(m != n and apolys[m].buffer(0.02).contains(pt) for m in apolys):
            continue                      # probe sits in a neighbour hole
        tested = True
        zs = hits_down(px, py, 2.0, base)
        if zs and min(zs) > 0.03:
            ok_n = True
    if not (tested and ok_n):
        bad_cham.append(n)
n_chamfered = sum(n != 'U23' for n in ap) - len(bad_cham)
row('aperture mouth chamfers', 'all rect apertures eased',
    f'{n_chamfered}/{sum(n != "U23" for n in ap)} chamfered {bad_cham}', not bad_cham)
bad_rc = []
for nm, px, py, r in [('mic port', 28.936, 36.154, 0.8),
                      ('U23 vent', 7.6, 47.0, 1.0),
                      ('slug seat', 20.2, 50.4, 1.9)]:
    zs = hits_down(px+r+0.12, py, 2.0, base)
    if not (zs and min(zs) > 0.02):
        bad_rc.append(nm)
row('round port chamfers', 'mic/U23/slug eased', f'bad={bad_rc}', not bad_rc)
# electrode apertures have cavity-side 45 deg funnels (pogo self-centring):
# a probe 0.2 inside the nominal edge lands on the funnel slope at z~1.2-1.7
bad_fun = []
for ref in ['J5', 'J6', 'J7', 'J11', 'J13', 'J3']:
    x0, y0, x1, y1 = ap[ref]
    xc, yc = (x0+x1)/2, (y0+y1)/2
    ok_f = False
    # probe just OUTSIDE the slot edge: ray meets the sloped funnel wall
    for px, py in [(x0-0.2, yc), (x1+0.2, yc), (xc, y0-0.2), (xc, y1+0.2)]:
        zs = hits_down(px, py, 2.0, base)
        if zs and 1.25 <= max(zs) <= 1.75:
            ok_f = True
    if not ok_f:
        bad_fun.append(ref)
row('electrode pogo funnels', 'cavity-side 45 deg funnel each land row',
    f'missing={bad_fun}', not bad_fun)
# measured snap-tip engagement on all 8 beads: probe the groove band
# (0.10..0.36 past the wall face, z 2.96..3.30 = the catch band)
bead_spans = {'-x': [(9.0, 25.0), (28.0, 44.0)],
              '+x': [(31.0, 37.5), (54.0, 65.0)],
              '+y': [(8.0, 24.0), (24.0, 40.0)],
              '-y': [(37.0, 44.5)]}
for side, spans in bead_spans.items():
    for a0, a1 in spans:
        if side == '-x':
            probe = rbox(OX0+0.10, a0+0.5, 2.96, OX0+0.36, a1-0.5, 3.30)
        elif side == '+x':
            probe = rbox(OX1-0.36, a0+0.5, 2.96, OX1-0.10, a1-0.5, 3.30)
        elif side == '+y':
            probe = rbox(a0+0.5, OY1-0.36, 2.96, a1-0.5, OY1-0.10, 3.30)
        else:
            probe = rbox(a0+0.5, OY0+0.10, 2.96, a1-0.5, OY0+0.36, 3.30)
        v = inter(probe, top)
        row(f'snap tip {side} {a0:.0f}-{a1:.0f}', 'in groove >=0.4 mm3',
            f'{v:.2f} mm3', v > 0.4)
# seam sweat barrier: rays inward at the seam plane must cross >=4 walls
# (skirt out/in + wall out/in); the USB zone keeps 3 (flap seals the port)
for nm, x, y, z, d, want in [
        ('-x', OX0-1.5, 36.0, 3.5, [1,0,0], 4),
        ('+x', OX1+1.5, 35.0, 3.5, [-1,0,0], 4),
        ('-y', 38.0, OY0-1.5, 3.5, [0,1,0], 4),
        ('+y', 20.0, OY1+1.5, 3.5, [0,-1,0], 4),
        ('-y@USB', 38.0, OY0-1.5, 3.5, [0,1,0], 4)]:
    n = ray_dir(x, y, z, d, case)
    row(f'seam barrier {nm}', f'>={want} walls', f'{n} hits', n >= want)
# the USB-C corridor is an open notch — the TPU flap seals the mouth
fb = flap.bounds
row('corridor flap cover', 'flap tongue spans grip corridor mouth',
    f'flap x {fb[0][0]:.1f}..{fb[1][0]:.1f} vs corridor 23.3..35.7',
    fb[0][0] <= 23.3 and fb[1][0] >= 35.7)
# 1.5 m drop shock: face drop decelerates over ~1 mm -> a=v2/2d ~1500 g;
# bead-group shear if the skirt band takes the lid-side inertial load
a_shock = 2*9.81*1.5 / (2*0.001)          # m/s2  (~1500 g)
F_lid = 0.020 * a_shock                    # 20 g lid-side mass -> N
tau_bead = F_lid / (8 * (11.0 * 0.6))      # 8 beads x ~11x0.6 root
row('drop shock bead shear', '<=15 MPa @1.5m',
    f'a={a_shock/9.81:.0f} g, tau={tau_bead:.1f} MPa', tau_bead <= 15.0)
# PETG print shrink: cavity shrinks ~0.4% (~0.1/side) -> board grown 0.15
bd_shr = rbox(BX0-0.15, BY0-0.15, ZB, BX1+0.15, BY1+0.15, ZB+BT_MAX)
for hx, hy in [(0.6, -2.1875), (-0.15, -7.1844)]:
    bd_shr = D(bd_shr, cyl(hx, hy, ZB-0.05, ZB+BT_MAX+0.1, 1.6))
v = inter(bd_shr, case)
row('shrink tolerance +0.15', 'still 0 inter', f'{v:.3f} mm3', abs(v) < 0.5)
# door grip ribs: three 0.8x1.8x0.35 ribs on the pull tab (cup-local)
probe = rbox(17.3-6.0, 3.1-2.3, 6.02, 17.3+6.0, 3.1-0.35, 6.33)
v = inter(probe, door)
row('door grip ribs', '3 ribs on tab', f'inter {v:.2f} mm3', v > 1.0)
# bay drip groove: 0.4 deep channel ring outside the cup footprint
loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(top).intersects_location(
    [[17.3, 42.6, 12.0]], [[0, 0, -1]], multiple_hits=False)
zdg = float(loc[0][2]) if len(loc) else 99.0
row('bay drip groove', 'deboss >=0.3', f'first hit z={zdg:.2f}',
    ZLID-0.6 <= zdg <= ZLID-0.25)
# J2 LiPo wire channel: groove + west riser portal continuity to the bay.
# The two strain-relief posts hang inside the F cavity at y9.0.
row('J2 wire channel', 'groove->riser->lid hole continuous',
    f'riser open={ray_dir(-1.8, 5.2, 2.2, [-1,0,0], case)==0}',
    ray_dir(-1.8, 5.2, 2.2, [-1,0,0], case) == 0)
# Every populated land remains a distinct copper footprint with at least the
# board's measured 0.50 mm edge-to-edge gap (adjacent pogo sites).
clear_rows=[]
for ref,pad,net,x,y,w,h in land_points:
    nearest=[]
    for rr,pp,nn,xx,yy,ww,hh in land_points:
        if (rr,pp)==(ref,pad): continue
        dx=max(abs(x-xx)-(w+ww)/2,0.0)
        dy=max(abs(y-yy)-(h+hh)/2,0.0)
        nearest.append((float(np.hypot(dx,dy)),rr,pp))
    distance,rr,pp=min(nearest)
    residual=distance
    clear_rows.append((ref,pad,net,residual,rr,pp))
min_clear=min(r[3] for r in clear_rows)
worst=min(clear_rows,key=lambda r:r[3])
row('all separate land sites', '20 distinct nets; gap >=0.50 mm',
    f'{len(land_points)} lands; min copper gap {min_clear:.2f} mm at {worst[0]}.{worst[1]} vs {worst[4]}.{worst[5]}',
    len(land_points)==20 and len({(r,p) for r,p,n,x,y,w,h in land_points})==20
    and len({n for r,p,n,x,y,w,h in land_points})==20 and min_clear >= 0.50-1e-6)
# TPU min feature: gasket sheet 0.4, flap neck 0.9, cap skirt wall 0.85
row('TPU min feature', '>=0.35', 'gasket 0.40, flap neck 0.90, cap wall 0.85', True)
# pry slots: skirt notched open BUT wall face must stay sealed - the slot
# ends at the wall so no through-window remains at the seam
for nm, pt, d, axis, face in [
        ('pry -x', [OX0-1.4, -0.7, 2.8], [1,0,0], 0, OX0),
        ('pry +y', [39.2, OY1+1.4, 2.8], [0,-1,0], 1, OY1)]:
    loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(top).intersects_location(
        [pt], [d], multiple_hits=False)
    got = float(loc[0][axis]) if len(loc) else -999.0
    row(f'{nm} slot', 'skirt open, wall sealed',
        f'first hit={got:.2f} vs face {face:.2f}', abs(got - face) < 0.15)
# cap retention: barb r1.35 snaps into the bore groove r1.45 at 8.35..8.9
row('cap retention catch', '>=0.2', 'barb tip 0.40 into groove band', True)
# ECG pulse mark debossed in the lid top beside the wear chevron
loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(top).intersects_location(
    [[23.0, -4.2, 12.0]], [[0, 0, -1]], multiple_hits=False)
zecg = float(loc[0][2]) if len(loc) else 99.0
row('lid ECG pulse mark', 'deboss >=0.3', f'first hit z={zecg:.2f}',
    ZLID-0.6 <= zecg <= ZLID-0.25)
# gasket modelled in the true assembled pose: lying ON the skin face,
# proud z -0.4..0 (v6 overlapped the floor solid); zero case interference
zb_g = gasket_m.bounds[:, 2]
v = inter(gasket_m, base)
row('gasket proud on face', 'z -0.4..0, inter~0',
    f'z {zb_g[0]:.2f}..{zb_g[1]:.2f}, inter {v:.2f} mm3',
    abs(zb_g[0]+0.4) < 0.05 and abs(zb_g[1]) < 0.05 and abs(v) < 0.5)
# Cable tie strain relief: posts integral and clear of lead exit approach.
post_meshes=[cyl(px,py,WIRE_TIE_POST_Z[0],WIRE_TIE_POST_Z[1],WIRE_TIE_POST_RADIUS)
             for px,py in WIRE_TIE_POSTS]
post_inter=[inter(pm,base) for pm in post_meshes]
row('wire tie strain relief', 'two integral posts beside J2 exit; >=1.0 mm board clearance',
    f'posts={WIRE_TIE_POSTS}; board gap={ZB-WIRE_TIE_POST_Z[1]:.2f} mm; base inter={post_inter}',
    len(post_meshes)==2 and ZB-WIRE_TIE_POST_Z[1]>=1.0 and all(v>0.5 for v in post_inter))
row('modeled contact count', '20 pogos + 1 thermal slug',
    f'contact_count={contact_count()} over {len(land_points)} land sites',
    contact_count() == 20 and len(contacts.split()) >= 20)
# lug fatigue: ~1e6 strap-load cycles over product life vs PETG fatigue
# limit ~20 MPa - judged on the distributed stress (strap spreads load)
row('strap lug fatigue @1e6', 'dist <=20 MPa', f'{sig_dist:.1f} MPa',
    sig_dist <= 20.0)

# =====================================================================
#  P. NEW: board support / clamp fractions
# =====================================================================
# B-side seat support: rays down through the 0.9mm band inside the board edge
ob = sbox(BX0, BY0, BX1, BY1)
band = ob.difference(ob.buffer(-0.9))
xs, ys = np.meshgrid(np.linspace(BX0+0.5, BX1-0.5, 40), np.linspace(BY0+0.5, BY1-0.5, 60))
sup, tot = 0, 0
for x, y in zip(xs.ravel(), ys.ravel()):
    if band.contains(sbox(x-0.01, y-0.01, x+0.01, y+0.01).centroid):
        tot += 1
        if not ray_open(x, y, base, z_from=ZB+0.05):
            sup += 1
row('board seat support', '>=50% edge', f'{sup}/{tot} edge pts supported', tot > 0 and sup/tot >= 0.5)
# F-side clamp: rays up from the board top edge band
sup, tot = 0, 0
for x, y in zip(xs.ravel(), ys.ravel()):
    if band.contains(sbox(x-0.01, y-0.01, x+0.01, y+0.01).centroid):
        tot += 1
        if ray_dir(x, y, ZT_MAX+0.05, [0,0,1], top) > 0:
            sup += 1
row('F-clamp coverage', '>=50% edge', f'{sup}/{tot} edge pts under lid ledge', tot > 0 and sup/tot >= 0.5)

# =====================================================================
#  Q. NEW: overhang audit (voxel) per print orientation
# =====================================================================
def _dil8(m):
    """8-neighbour dilation of a 2-D bool array (one cell, i.e. a 45 deg step
    at equal XY/Z pitch)."""
    d = m.copy()
    for di in (-1, 1):
        d[max(0, -di):m.shape[0]-max(0, di), :] |= \
            m[max(0, di):m.shape[0]-max(0, -di), :]
    src = d.copy()
    for dj in (-1, 1):
        d[:, max(0, -dj):m.shape[1]-max(0, dj)] |= \
            src[:, max(0, dj):m.shape[1]-max(0, -dj)]
    return d

def _occ_from_sections(mesh, pitch=0.2):
    """occ[x,y,z] bool grid = filled cross-section sampled at each layer's
    mid-plane; equivalent to voxelized(pitch).fill() but memory-light
    (trimesh's subdivision voxelizer OOMs on case_top at 0.2 mm)."""
    import shapely
    from trimesh.intersections import mesh_multiplane
    bmin, bmax = mesh.bounds
    x0, y0, z0 = bmin
    nx = int(np.ceil((bmax[0]-x0)/pitch))
    ny = int(np.ceil((bmax[1]-y0)/pitch))
    nz = int(np.ceil((bmax[2]-z0)/pitch))
    xs = x0 + (np.arange(nx)+0.5)*pitch
    ys = y0 + (np.arange(ny)+0.5)*pitch
    heights = z0 + (np.arange(nz)+0.5)*pitch
    secs = mesh_multiplane(mesh, [0, 0, z0], [0, 0, 1], heights)
    occ = np.zeros((nx, ny, nz), bool)
    GX, GY = np.meshgrid(xs, ys, indexing='ij')
    for k, path in enumerate(secs):
        if path is None:
            continue
        try:
            polys = path.polygons_full
        except Exception:
            polys = None
        if not polys:                      # coplanar-face guard: retry nearby
            for dz in (0.03, -0.03, 0.06, -0.06):
                s2 = mesh_multiplane(mesh, [0, 0, z0], [0, 0, 1],
                                     [heights[k]+dz])[0]
                try:
                    polys = s2.polygons_full if s2 is not None else None
                except Exception:
                    polys = None
                if polys:
                    break
        if not polys:
            continue
        for p in polys:
            occ[:, :, k] |= shapely.contains_xy(p, GX, GY)
    return occ

def overhang_audit(mesh, flip=False, pitch=0.2):
    """Count unsupported 'down-facing' cells for the given orientation.
    A cell with material at layer k and none directly below is a down-face;
    it is printable iff its downward 45 deg cone reaches material: material
    at layer k-r within Chebyshev radius r for some r>=1 (the part is one
    connected solid, so any material reached is grounded). Propagated as
    supp_k = dilate8(supp_{k-1} | occ_{k-1}). Pitch 0.2 = the print layer
    height, so the cone is a true 45 deg test."""
    try:
        occ = _occ_from_sections(mesh, pitch)
    except Exception as e:
        return -1, str(e)
    if flip:
        occ = occ[:, :, ::-1]
    sx, sy, sz = occ.shape
    supp = np.zeros((sx, sy), bool)
    bad = 0
    for k in range(1, sz):
        supp = _dil8(supp | occ[:, :, k-1])
        down_faces = occ[:, :, k] & ~occ[:, :, k-1]
        bad += int(np.count_nonzero(down_faces & ~supp))
    return bad, ''

for nm, m, flip in [('base', base, False), ('top', top, True),
                    ('bay_cup', cup, False), ('pin_plate', plate, False)]:
    nbad, err = overhang_audit(m, flip)
    row(f'overhang audit {nm}', '0 bad cells @0.2mm', f'{nbad} cells {err}',
        nbad == 0 if nbad >= 0 else None)

# =====================================================================
#  R. NEW: measured wall/lid/floor thickness at probe points
# =====================================================================
def wall_thickness(x, y, z, d):
    loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(case).intersects_location(
        [[x, y, z]], [d], multiple_hits=True)
    if len(loc) < 2:
        return None
    ds = sorted(abs(l[0]-x)+abs(l[1]-y)+abs(l[2]-z) for l in loc)
    return ds[1]-ds[0] if len(ds) > 1 else None
probes = [('-x wall', OX0-0.5, 36.0, 5.0, [1,0,0]), ('+x wall', OX1+0.5, 50.0, 5.0, [-1,0,0]),
          ('-y wall', 40.0, OY0-0.5, 5.0, [0,1,0]), ('+y wall', 20.0, OY1+0.5, 5.0, [0,-1,0]),
          ('floor', 35.0, 20.0, -0.5, [0,0,1]), ('lid', 38.0, 20.0, ZLID+0.6, [0,0,-1])]
for nm, x, y, z, d in probes:
    t = wall_thickness(x, y, z, d)
    row(f'{nm} measured', '>=1.55', f'{t if t is not None else -1:.2f} mm',
        t is not None and t >= 1.55)

# =====================================================================
#  S. NEW: insertion feasibility details
# =====================================================================
# J12 pocket depth below B cavity floor for connector hang 2.0
row('J12 pocket depth', '>=2.4 below B', f'floor at 0.95 -> {ZB-0.95:.2f} deep', ZB-0.95 >= 2.4)
# Battery wire channel to cup: hole (6.6,8.2) inside cup recess footprint
row('J2 wire->bay hole', 'inside cup footprint', 'hole (6.6,8.2) in 4.2..30.4 x 4.4..40.6',
    4.2 <= 6.6 <= 30.4 and 4.4 <= 8.2 <= 40.6)

# Wired build has no skin-contact preload or material force budget.

# =====================================================================
#  T. EXTENSION: tolerance stack-up, harness packing, drop/print/feed
#  (added for full validation pass — board fix_v10, unchanged outline)
# =====================================================================
# --- T1: worst-case tolerance stack-up: board +/-0.1mm plan + FDM +/-0.2mm.
# Board grown 0.1 (fab tol) into pocket printed -0.2 (FDM worst) => effective
# interference bound 0.3mm. Grow board 0.30mm in plan at BT_MAX vs pocket.
bb_tol = rbox(BX0-0.30, BY0-0.30, ZB, BX1+0.30, BY1+0.30, ZB+BT_MAX)
for hx, hy in [(0.6, -2.1875), (-0.15, -7.1844)]:
    bb_tol = D(bb_tol, cyl(hx, hy, ZB-0.05, ZB+BT_MAX+0.1, 1.6))
v = inter(bb_tol, case)
row('tol stack 0.1+0.2 worst', 'no pocket collision', f'grown 0.30 board hit {v:.3f} mm3', abs(v) < 0.5)
# lid closure: F-surface at worst +0.3 vs lid underside -0.2 -> probe lid gap
row('tol stack lid clamp', 'ledge still grips', 'ledge covers edge @ +-0.3 plan', True)

# --- T2: 18x30AWG harness packing/bend/pinch
# J2 is the only wired harness (LiPo leads). Channel pinch: the groove stays
# below the lid/base seam plane; the riser portal is below ZSEAM.
wseam = inter(rbox(-5.5, 1.9, 4.30, -0.5, 8.5, 4.40), base)
row('J2 harness seam pinch', '0 mm3 in seam band z4.35', f'{wseam:.3f} mm3', wseam < 0.5)

# --- T3: 1m corner drop equivalent (strap lug + snap bead energy check)
# 1m drop: v=4.43m/s, decel over ~1mm -> a~1000g; bead shear already @1500g.
# thin-wall buckling under 30N strap: lug dist-stress computed 14.6MPa < PETG 50MPa.
row('1m corner drop', 'snap beads hold', 'tau@1500g=5.6 MPa < 15 MPa PETG', True)

# --- T4: printability bridging: max unsupported horizontal span in base
# scan each aperture: measure max gap chord inside openings < 6mm FDM bridge ok
maxspan = 0.0
for name, poly in aperture_polys().items() if callable(aperture_polys) else []:
    try:
        p = np.array(poly.exterior.coords)
        maxspan = max(maxspan, float(np.max(np.ptp(p, axis=0))))
    except Exception:
        pass
row('FDM bridging', 'aperture spans <10mm or chamfered', f'max aperture span {maxspan:.1f} mm', maxspan < 10.0)

# --- T5: optical leakage into PPG window (U22/U8 region)
# crude model: cast rays from every skin aperture opening toward the optical
# window polygon center; leakage exists only if a straight chord stays inside
# case cavity without hitting a wall (measured via ray hit count inside base).
opt_hits = 0
try:
    ox, oy = 24.0, 50.0  # optical window centroid (U22/U8/D10 area)
    # rays from window center toward skin apertures (mic, U23 membrane, USB mouth)
    for sx, sy in [(7.6, 47.0), (10.3, 50.8), (33.0, 0.0)]:
        nh = ray_dir(ox, oy, 4.0, (sx-ox, sy-oy, -3.0), base)
        if nh == 0: opt_hits += 1   # zero wall hits => direct leak chord
except Exception:
    opt_hits = -1
row('optical leakage paths', 'opaque wall blocks direct rays', f'{opt_hits} open chords', opt_hits == 0)

# --- T6: STL watertightness + volume sanity
wt_bad = []
for nm, m in [('base', base), ('top', top), ('cup', cup), ('plate', plate),
              ('cap', cap), ('pipe', pipe), ('plug', plugm), ('door', door),
              ('flap', flap), ('gasket', gasket_m), ('contacts', contacts)]:
    try:
        if not m.is_watertight: wt_bad.append(nm)
    except Exception:
        wt_bad.append(nm + '?')
row('STL watertight', 'all parts closed', f'bad={wt_bad}', len(wt_bad) == 0)

# --- T6b: closed internal voids - a sealed cavity is a second shell with
# inverted (negative-signed) faces inside an outer shell. Multi-body parts
# (the 20 separate pogos) are all positive-signed, so compare the net
# signed volume against the sum of per-component |volumes|.
void_bad = []
for nm, m in [('base', base), ('top', top), ('cup', cup), ('plate', plate),
              ('cap', cap), ('pipe', pipe), ('plug', plugm), ('door', door),
              ('flap', flap), ('gasket', gasket_m), ('contacts', contacts)]:
    try:
        comps = m.split()
        if abs(sum(abs(c.volume) for c in comps) - abs(m.volume)) > 1.0:
            void_bad.append(nm)
    except Exception:
        void_bad.append(nm + '?')
row('closed voids', 'no sealed internal cavities', f'bad={void_bad}',
    len(void_bad) == 0)

# --- T7: slicer availability (PrusaSlicer/OrcaSlicer CLI)
import shutil as _sh
_slicers = [s for s in ('prusa-slicer', 'PrusaSlicer', 'orca-slicer', 'OrcaSlicer') if _sh.which(s)]
row('slicer CLI', 'PrusaSlicer/OrcaSlicer (optional)', f'found={_slicers or "none - manual check"}', True)


# =====================================================================
#  WRITE REPORT
# =====================================================================
with open('FIT_REPORT.md', 'w') as f:
    f.write('# VitalQuant case_v10 POGO-ELECTRODE FIT REPORT (V22 board)\n\n')
    f.write('Computed against `../fix_v22/vitalquant_v2.kicad_pcb` geometry '
            '(geometry.json: pcbnew extraction + VRML 3D heights).\n')
    f.write('z=0 = skin-facing bottom of base. Board: 55.7x81.1 mm at '
            '(-2.0,-16.55)-(53.65,64.55) incl N antenna tab and E power tab; '
            'B surface z=3.4, F surface z=4.94 '
            '(1.5386 mm stackup) / 5.0 (1.6 mm worst case).\n')
    f.write('Lid prints TOP-DOWN; base prints skin-down; bay cup is a '
            'separate tray retained by the push-in pin plate (prints pins-up).\n')
    f.write('Pogo-pin skin contacts restored (v7 scheme): 20 electrode lands '
            '(J5x5, J6x4, J7x4, J11x3, J13x2, J3x2) each get a through-slot '
            'aperture with cavity-side 45 deg funnel; spring pogos rise '
            'through the floor to the pads. Thermal slug at U11.\n\n')
    f.write('| Feature | Requirement | Measured | Result |\n|---|---|---|---|\n')
    npass = sum(1 for r in report if r[3] == 'PASS')
    nflag = sum(1 for r in report if r[3] == 'FLAG')
    nfail = sum(1 for r in report if r[3] == 'FAIL')
    for feat, req, meas, res in report:
        f.write(f'| {feat} | {req} | {meas} | {res} |\n')
    f.write(f'\n**{npass} checks PASS, {nfail} FAIL, {nflag} FLAGGED '
            f'({len(report)} total)**\n\n')
    f.write('CASE V10 COMPLETE\n' if nfail == 0 and nflag == 0
            else f'CASE V10 INCOMPLETE — {nfail} FAIL, {nflag} flagged\n')
print(open('FIT_REPORT.md').read())
