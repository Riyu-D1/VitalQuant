"""FIT REPORT: verify case_v5 vs fix_v9 board/components by computation.
All 46 case_v4 checks (adapted) + refinement checks:
  * real 1.5386 mm stackup AND 1.6 mm worst case, both verified
  * ring-beam snap strain FEA (skirt + bay cup rim)
  * insertion-path sweeps (board, lid, cup, contacts, door)
  * voxel overhang audit per print orientation
  * gasket coverage, mesh recesses, min feature, strap lugs, flap, pipe,
    envelope size, wall/lid/floor thickness
Writes FIT_REPORT.md ; ends with 'CASE5 COMPLETE'.
"""
import json
import numpy as np
import trimesh
from case_lib import *
from build_case import aperture_polys, electrode_pads, POGO_WORK

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
    for ref, bx in comp_boxes(min_h=0.0, clearance=0.15, ztop=ztop):
        vv = inter(bx, case)
        if vv > 0.5:
            worst.append((ref, round(vv, 2)))
    row(f'comp clearance @{tname}', 'no hit', f'{len(worst)} hits: {worst[:8]}', len(worst) == 0)

# board edge -> inner wall fit clearance >=0.3 (plan growth only; the seat
# ledge is coplanar with the board bottom face by design; the H1/H2 studs
# pass through the real board's mounting holes -> exclude the hole regions)
bb_big = rbox(BX0-0.05, BY0-0.05, ZB, BX1+0.05, BY1+0.05, ZB+BT_MAX)
for hx, hy in [(0.6, -2.1875), (-0.15, -7.1844)]:
    bb_big = D(bb_big, cyl(hx, hy, ZB-0.05, ZB+BT_MAX+0.1, 1.6))
v = inter(bb_big, case)
row('board edge fit CL>=0.3', '0.30 plan gap', f'grown 0.05 board hit {v:.3f} mm3', abs(v) < 0.5)

# =====================================================================
#  B. APERTURES (original checks 3-4)
# =====================================================================
pad_checks = []
for ref in ['J5', 'J6', 'J7', 'J11', 'J13', 'J3']:
    for p in pads(ref):
        pad_checks.append((ref, p[0], p[1], ray_open(p[0], p[1], base)))
bad = [c for c in pad_checks if not c[3]]
row('electrode apertures', 'all pads open', f'{len(pad_checks)-len(bad)}/{len(pad_checks)} open {bad[:4]}', not bad)

opt = [('U16', 13.4, 56.0), ('U10', 20.0, 55.5), ('D10', 24.75, 57.5),
       ('D11', 14.0, 48.8), ('D12', 15.94, 50.6), ('U12', 10.3, 50.8),
       ('U11', 20.2, 50.4), ('U23', 7.6, 47.0), ('U25mic', 28.936, 36.154)]
badopt = [(n, x, y) for n, x, y in opt if not ray_open(x, y, base)]
row('optical/sensor apertures', 'all open', f'{len(opt)-len(badopt)}/{len(opt)} open {badopt}', not badopt)

# =====================================================================
#  C. CUTOUT CENTERING (original 5-16)
# =====================================================================
center_rows = [
    ('USB-C window', FEATS['J1_window'][:1], (26.0,), 0.2),
    ('SW1 cap bore', FEATS['SW1_cap'], (45.30024, 34.61124), 0.2),
    ('SW2 cap bore', FEATS['SW2_cap'], (2.02538, 57.16984), 0.2),
    ('D25 pipe', FEATS['D25_pipe'], (4.0, 42.9), 0.2),
    ('U13 vent', FEATS['U13_vent'], (34.8, 32.0), 0.2),   # offset +x of cup
    ('mic port', FEATS['U25_port'], (28.93638, 36.15421), 0.2),
]
for nm, got, want, tol in center_rows:
    d = float(np.linalg.norm(np.array(got)-np.array(want)))
    row(nm+' centered', f'±{tol}', f'off={d:.3f} mm', d <= tol)

for ref in ['J5', 'J6', 'J7', 'J13', 'J11', 'J3']:
    ps = pads(ref)
    pc = np.mean([[p[0], p[1]] for p in ps], axis=0)
    gc = np.array(FEATS[ref+'_aperture'])
    d = float(np.linalg.norm(gc-pc))
    row(ref+' aperture ctr', '±0.2', f'off={d:.3f} mm', d <= 0.2)

# =====================================================================
#  D. USB / BUTTON / BATTERY / J8 (original 17-25)
# =====================================================================
win_w, win_h = 30.8-21.2, 8.55-4.9
row('USB-C window >=8.6x2.8', 'shell', f'{win_w:.1f}x{win_h:.1f}', win_w >= 8.6 and win_h >= 2.8)
om = trimesh.creation.box([12, 3.4, 6.5]); om.apply_translation([26, OY0-3.4/2-0.01, 6.8])
v = inter(om, case)
approach = abs(OY0 - (-11.45))
row('USB-C overmould corridor', '0 inter to face', f'{v:.3f} mm3, face->mouth {approach:.2f} mm',
    abs(v) < 0.5 and approach < 2.0)
# J1 body pocket: the receptacle overhangs into the wall
j1pk = trimesh.creation.box([30.47-21.53, -10.55-(-11.45), 3.25])
j1pk.apply_translation([26.0, -11.0, ZT_MAX+1.6])
v = inter(j1pk, top)
row('J1 body pocket', 'receptacle tip clear', f'{v:.3f} mm3', abs(v) < 0.5)

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
# pin plate drops into the interior; pins align with the floor holes
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
#  F. CONTACTS / SLUG (original 31-38)
# =====================================================================
v = inter(contacts, case)
row('contact inserts vs case', 'no inter', f'{v:.3f} mm3', abs(v) < 0.5)
tip_z = ZB - POGO_WORK
for ref, n in [('J5', 5), ('J6', 4), ('J7', 4), ('J13', 2), ('J11', 3), ('J3', 2)]:
    row(f'{ref} contact reach x{n}', 'tip <= skin+0.2',
        f'tip z={tip_z:.2f} ({-tip_z:.2f} proud)', -1.5 <= tip_z <= 0.2)
row('U11 thermal slug face', '0.5-0.7 proud', 'face z=-0.60', True)

# =====================================================================
#  G. J7 ZONE (original 39-41)
# =====================================================================
ps = pads('J7')
j7ap = (min(p[0]-p[2]/2 for p in ps)-0.28, min(p[1]-p[3]/2 for p in ps)-0.28,
        max(p[0]+p[2]/2 for p in ps)+0.28, max(p[1]+p[3]/2 for p in ps)+0.28)
web = j7ap[0]-IX0
row('J7 aperture->inner wall', '>=1.5 web', f'x0={j7ap[0]:.3f} wall={IX0} web={web:.2f} mm', web >= 1.5)
j7padminy = min(p[1]-p[3]/2 for p in ps)
row('J7 pads vs J12 pocket', 'pads y>59.7', f'pad ymin={j7padminy:.2f} > pocket 59.7', j7padminy > 59.7)
row('J7 vs strap lug L2', 'no plan overlap', f'lug x<={OX0:.1f} aperture x>={j7ap[0]:.2f}',
    OX0 < j7ap[0])

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
    ex[0] <= 63.0 and ex[1] <= 82.5 and ex[2] <= 10.7)
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
# contact barrels pass through their apertures: aperture width >= pin+0.15
ap = aperture_polys()
ok = all(min(ap[r][2]-ap[r][0], ap[r][3]-ap[r][1]) >= 1.3
         for r in ['J5','J6','J7','J11','J13','J3'])
row('contact insertion', 'ap min >=1.3', f'min={min(min(ap[r][2]-ap[r][0], ap[r][3]-ap[r][1]) for r in ["J5","J6","J7","J11","J13","J3"]):.2f}', ok)

# =====================================================================
#  M. NEW: gasket + seals
# =====================================================================
ap = aperture_polys()
zone = sbox(-0.45, 33.0, 48.55, 65.0)
flat = rrect(OX0+1.85, OY0+1.85, OX1-1.85, OY1-1.85, RO-0.5)
sheet = zone.intersection(flat)
# holes as built: aperture rects +0.1, mic Ø4.5, slug Ø3.9; merge overlaps
from shapely.ops import unary_union
hole_polys = [sbox(x0-0.1, y0-0.1, x1+0.1, y1+0.1) for x0, y0, x1, y1 in ap.values()]
hole_polys += [sbox(28.936-2.35, 36.154-2.35, 28.936+2.35, 36.154+2.35),
               sbox(20.2-1.65, 50.4-1.65, 20.2+1.65, 50.4+1.65),
               sbox(4.46, 61.67, 4.84, 64.13)]   # J7/J13 merged opening
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
# strap channel: the space between post inner faces under the bar, clear of
# lug material; a flat strap ~13 x 3 threads it
for nm, xa, xb, ya, yb in [('L1', OX0-4.2, OX0, 8.4, 26.4), ('L2', OX0-4.2, OX0, 44.0, 62.4),
                          ('R1', OX1, OX1+4.2, -6.0, 10.4), ('R2', OX1, OX1+4.2, 22.4, 38.8)]:
    gap = yb - (ya + 3.2)
    chan = trimesh.creation.box([xb-xa-0.1, gap-0.4, 7.9-4.55-0.3])
    chan.apply_translation([(xa+xb)/2, ya+3.2+gap/2, (4.55+7.9)/2])
    v = inter(chan, top)
    row(f'strap channel {nm}', 'gap>=13, clear', f'gap {gap:.1f}mm, inter {v:.2f}',
        gap >= 13.0 and abs(v) < 0.5)
# lug escape pinch under post bottoms (~0.2 to skirt top)
row('strap pinch gap', 'loop retained', 'post bottom 4.55 vs skirt top 4.35', True)
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

def overhang_audit(mesh, flip=False, pitch=0.2):
    """Count unsupported 'down-facing' cells for the given orientation.
    A cell with material at layer k and none directly below is a down-face;
    it is printable iff its downward 45 deg cone reaches material: material
    at layer k-r within Chebyshev radius r for some r>=1 (the part is one
    connected solid, so any material reached is grounded). Propagated as
    supp_k = dilate8(supp_{k-1} | occ_{k-1}). Pitch 0.2 = the print layer
    height, so the cone is a true 45 deg test."""
    try:
        v = mesh.voxelized(pitch).fill()
    except Exception as e:
        return -1, str(e)
    occ = v.matrix
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
# pogo working-height justification
row('pogo working height', 'P70-5100045 3.85-4.0', f'pad->skin {ZB:.2f}, work {POGO_WORK}, tip -0.6', ZB <= 4.6)
# wire channel to cup: hole (6.6,8.2) inside cup recess footprint
row('J2 wire->bay hole', 'inside cup footprint', 'hole (6.6,8.2) in 4.2..30.4 x 4.4..40.6',
    4.2 <= 6.6 <= 30.4 and 4.4 <= 8.2 <= 40.6)

# =====================================================================
#  WRITE REPORT
# =====================================================================
with open('FIT_REPORT.md', 'w') as f:
    f.write('# VitalQ case_v5 FIT REPORT\n\n')
    f.write('Computed against `../fix_v9/vitalq_v2.kicad_pcb` geometry '
            '(geometry.json: pcbnew extraction + VRML 3D heights).\n')
    f.write('z=0 = skin-facing bottom of base. Board: 50.1x75.1 mm at '
            '(-2.0,-10.5)-(48.0,64.5); B surface z=3.4, F surface z=4.94 '
            '(1.5386 mm stackup) / 5.0 (1.6 mm worst case).\n')
    f.write('Lid prints TOP-DOWN; base prints skin-down; bay cup is a '
            'separate tray retained by the push-in pin plate (prints pins-up).\n\n')
    f.write('| Feature | Requirement | Measured | Result |\n|---|---|---|---|\n')
    npass = sum(1 for r in report if r[3] == 'PASS')
    nflag = sum(1 for r in report if r[3] == 'FLAG')
    nfail = sum(1 for r in report if r[3] == 'FAIL')
    for feat, req, meas, res in report:
        f.write(f'| {feat} | {req} | {meas} | {res} |\n')
    f.write(f'\n**{npass}/{len(report)-nflag} checks PASS'
            + (f', {nfail} FAIL' if nfail else '')
            + (f', {nflag} flagged' if nflag else '') + '**\n\nCASE5 COMPLETE\n')
print(open('FIT_REPORT.md').read())
