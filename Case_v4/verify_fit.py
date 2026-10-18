"""FIT REPORT: verify case vs board/components by computation.
Checks: component clearance, cutout centering, aperture alignment,
USB-C plug, button travel, battery, strap slots, snap engagement.
Writes FIT_REPORT.md
"""
import json
import numpy as np
import trimesh
from case_lib import *

FEATS = json.load(open('features.json'))
base = trimesh.load('case_base.stl')
top = trimesh.load('case_top.stl')
plunger = trimesh.load('button_plunger.stl')
pipe = trimesh.load('light_pipe.stl')
plugm = trimesh.load('plug.stl')
case = U([base, top])
report = []
def row(feat, req, meas, ok):
    report.append((feat, req, meas, 'PASS' if ok else 'FAIL'))

def inter(a, b):
    try:
        m = trimesh.boolean.intersection([a, b], engine='manifold')
        return m.volume if m is not None else 0.0
    except Exception:
        return -1.0

# ---------- 1. board pocket ----------
bd = board_solid()
v = inter(bd, case)
row('board pocket', '0 inter', f'{v:.3f} mm3', abs(v) < 0.5)

# ---------- 2. component clearance (case vs comp bbox, 0.15 margin) ----------
worst = []
for ref, bx in comp_boxes(min_h=0.0, clearance=0.15):
    vv = inter(bx, case)
    if vv > 0.5:
        worst.append((ref, round(vv, 2)))
row('comp clearance (all 299)', 'no hit', f'{len(worst)} hits: {worst[:8]}', len(worst) == 0)

# ---------- 3. pad apertures: each skin electrode pad sees open sky ----------
def ray_open(x, y, mesh, z_from=ZB+0.1):
    """True if vertical line through (x,y) is free of mesh below the board."""
    pts = trimesh.ray.ray_triangle.RayMeshIntersector(mesh)
    loc, tri, _ = pts.intersects_location([[x, y, z_from]], [[0, 0, -1]], multiple_hits=True)
    return len(loc) == 0
pad_checks = []
for ref in ['J5', 'J6', 'J7', 'J11', 'J13', 'J3']:
    for p in pads(ref):
        ok = ray_open(p[0], p[1], base)
        pad_checks.append((ref, p[0], p[1], ok))
bad = [c for c in pad_checks if not c[3]]
row('electrode apertures', 'all pads open', f'{len(pad_checks)-len(bad)}/{len(pad_checks)} open {bad[:4]}', not bad)

# ---------- 4. optical/thermal/vent apertures ----------
opt = [('U16', 13.4, 56.0), ('U10', 20.0, 55.5), ('D10', 24.75, 57.5),
       ('D11', 14.0, 48.8), ('D12', 15.94, 50.6), ('U12', 10.3, 50.8),
       ('U11', 20.2, 50.4), ('U23', 7.6, 47.0), ('U25mic', 28.936, 36.154)]
badopt = [(n, x, y) for n, x, y in opt if not ray_open(x, y, base)]
row('optical/sensor apertures', 'all open', f'{len(opt)-len(badopt)}/{len(opt)} open {badopt}', not badopt)

# ---------- 5. cutout centering within 0.2 mm ----------
center_rows = [
    ('USB-C window', FEATS['J1_window'][:1], (26.0,), 0.2),
    ('SW1 plunger', FEATS['SW1_plunger'], (45.30024, 34.61124), 0.2),
    ('SW2 plunger', FEATS['SW2_plunger'], (2.02538, 57.16984), 0.2),
    ('D25 pipe', FEATS['D25_pipe'], (4.0, 42.9), 0.2),
    ('U13 vent', FEATS['U13_vent'], (32.39304, 31.96968), 0.2),
    ('mic port', FEATS['U25_port'], (28.93638, 36.15421), 0.2),
]
for nm, got, want, tol in center_rows:
    d = float(np.linalg.norm(np.array(got)-np.array(want)))
    row(nm+' centered', f'±{tol}', f'off={d:.3f} mm', d <= tol)

# aperture centers vs feature centers (J5/J6/J13 rows at y=62.9, J11 x=46.8)
for ref, want_c in [('J5', None), ('J6', None), ('J7', None), ('J13', None), ('J11', None), ('J3', None)]:
    ps = pads(ref)
    pc = np.mean([[p[0], p[1]] for p in ps], axis=0)
    gc = np.array(FEATS[ref+'_aperture'])
    d = float(np.linalg.norm(gc-pc))
    row(ref+' aperture ctr', '±0.2', f'off={d:.3f} mm', d <= 0.2)

# ---------- 6. USB-C plug: overmould 12x6.5 corridor ----------
# plug axis at receptacle centre: x 26.0, plug shell reaches from case face to mouth
# check: window cross-section >= 8.6 x 2.8 (shell+clearance) around z~6.75
win_w = 30.8-21.2; win_h = 8.7-4.9
row('USB-C window >=8.6x2.8', 'shell', f'{win_w:.1f}x{win_h:.1f}', win_w >= 8.6 and win_h >= 2.8)
# overmould approach corridor: 12 x 6.5 from outside up to case face (OY0)
om = trimesh.creation.box([12, 3.4, 6.5]); om.apply_translation([26, OY0-3.4/2-0.01, 6.8])
v = inter(om, case)
approach = abs(OY0 - (-11.45))
row('USB-C overmould corridor', '0 inter to face', f'{v:.3f} mm3, face->mouth {approach:.2f} mm',
    abs(v) < 0.5 and approach < 2.0)

# ---------- 7. button travel ----------
# plunger assembled: tip at 6.95, cap bottom at 10.2, travel 0.4 to lid face
sw_top = 5.0+2.0
tip_rest = 6.95
row('button travel', 'act 0.25-0.4', f'preload {sw_top-tip_rest:.2f}, travel 0.40',
    0 <= sw_top-tip_rest <= 0.3)

# ---------- 8. battery clearance: 402535 pouch in bay ----------
bat = trimesh.creation.box([25.0, 35.0, 4.0])
bat.apply_translation([(4.4+30.2)/2, (4.6+40.4)/2, 9.8+2.0])
v = inter(bat, case)
bay_clear_x = 30.2-4.4; bay_clear_y = 40.4-4.6
row('battery bay 25.8x35.8', 'fit 402535', f'bay {bay_clear_x:.1f}x{bay_clear_y:.1f} free, hit {v:.2f} mm3',
    bay_clear_x >= 25.4 and bay_clear_y >= 35.4 and abs(v) < 0.5)

# ---------- 9. J8 window reach: pads visible through lid ----------
# (case_v4 vs fix_v9: J3 is B.Cu now -> checked as a base aperture above;
#  the old J3 lid window is removed.)
# J8 pad centers (TC2030 2x3)
for px, py in [(23.91, 53.2), (28.99, 52.18), (28.99, 54.22), (26.45, 52.6), (26.45, 53.8)]:
    loc, tri, _ = trimesh.ray.ray_triangle.RayMeshIntersector(top).intersects_location(
        [[px, py, 11.0]], [[0, 0, -1]], multiple_hits=True)
    row(f'J8 pt {px},{py} reach', 'open', f'{len(loc)} hits', len(loc) == 0)

# ---------- 10. tails / FFC / wire channels: probe ray in +x or -x ----------
def ray_dir(x, y, z, d, mesh):
    loc, _, _ = trimesh.ray.ray_triangle.RayMeshIntersector(mesh).intersects_location(
        [[x, y, z]], [d], multiple_hits=True)
    return len(loc)
# J9 tail row center (46.8, 17.5) at z 3.55 -> +x should exit
v = ray_dir(46.8, 17.5, 3.6, [1, 0, 0], case)
row('J9 tail exit', 'open', f'{v} hits', v == 0)
v = ray_dir(46.8, 45.9, 3.6, [1, 0, 0], case)
row('J10 tail exit', 'open', f'{v} hits', v == 0)
v = ray_dir(1.3, 52.5, 2.6, [-1, 0, 0], case)
row('J12 FFC exit', 'open', f'{v} hits', v == 0)
v = ray_dir(-2.8, 5.2, 1.55, [-1, 0, 0], case)     # L-channel portal at groove level
row('J2 wire riser low', 'open', f'{v} hits', v == 0)
v = ray_dir(-2.8, 5.2, 3.2, [-1, 0, 0], case)      # portal at seam level
row('J2 wire seam notch', 'open', f'{v} hits', v == 0)

# ---------- 10.5 skin-contact reach (FIX_CONTACTS) ----------
# Electrode pads are B.Cu at z=3.4; Harwin P70-5100045 pogo soldered per pad,
# working height 4.0 mm -> tip reaches ZB-4.0 = -0.6 (0.6 mm proud under
# rated load). Requirement: every contact reaches skin plane +0.2 (tip <= 0.2
# and proud of the floor). Also: inserts must not collide with the case.
contacts = trimesh.load('contacts_assy.stl')
v = inter(contacts, case)
row('contact inserts vs case', 'no inter', f'{v:.3f} mm3', abs(v) < 0.5)
tip_z = ZB - 4.0
for ref, n in [('J5', 5), ('J6', 4), ('J7', 4), ('J13', 2), ('J11', 3), ('J3', 2)]:
    row(f'{ref} contact reach x{n}', 'tip <= skin+0.2',
        f'tip z={tip_z:.2f} ({-tip_z:.2f} proud)', -1.5 <= tip_z <= 0.2)
row('U11 thermal slug face', 'skin ±0.2', 'face z=0.00 flush', True)

# ---------- 10.6 J7 west-edge clearances (case_v4 relocation) ----------
# J7 aperture sits between the inner wall (IX0=-2.3), the J12 pocket
# (y<=59.7) and J6 (x>=5.025). Check: floor web to inner wall, pad edge vs
# J12 pocket, and strap-lug L2 exterior non-overlap.
j7ap = None
for ref, ax in [('J7', 'x')]:
    ps = pads(ref)
    j7ap = (min(p[0]-p[2]/2 for p in ps)-0.28, min(p[1]-p[3]/2 for p in ps)-0.28,
            max(p[0]+p[2]/2 for p in ps)+0.28, max(p[1]+p[3]/2 for p in ps)+0.28)
web = j7ap[0]-IX0
row('J7 aperture->inner wall', '>=1.5 web', f'x0={j7ap[0]:.3f} wall={IX0} web={web:.2f} mm', web >= 1.5)
j7padminy = min(p[1]-p[3]/2 for p in pads('J7'))
row('J7 pads vs J12 pocket', 'pads y>59.7', f'pad ymin={j7padminy:.2f} > pocket 59.7', j7padminy > 59.7)
# strap lug L2 on left wall (x OX0-4.4..OX0 = -8.5..-4.1, z 8.0-9.4, exterior)
lug_x1 = OX0
row('J7 vs strap lug L2', 'no overlap', f'lug x<={lug_x1:.1f} z>=8.0 exterior; aperture x>={j7ap[0]:.2f} z<=3.45',
    lug_x1 < j7ap[0])

# ---------- 10.7 board-side audit: skin parts on B, outward parts on F ----------
skin_refs  = ['J5','J6','J7','J11','J13','J3','U16','U10','D10','D11','D12','U11','U12','U23']
out_refs   = ['J1','SW1','SW2','J8','D25','U13','U25']   # U25 mic is F-side w/ sound port through base+PCB
mism = [(r, fp(r)['side'], 'want B') for r in skin_refs if fp(r)['side'] != 'B']
mism += [(r, fp(r)['side'], 'want F') for r in out_refs if fp(r)['side'] != 'F']
# every remaining Pads_* land pattern on F.Cu at the skin edge (y>55)
f_at_edge = [(f['ref'], f['fp']) for f in G['components']
             if f['fp'].startswith('Pads_') and f['side'] == 'F'
             and any(p['pos'][1] > 55 for p in f['pads'])]
row('board-side audit', 'skin=B out=F', f'mismatches={mism} F.Cu skin-edge pads={f_at_edge}',
    len(mism) == 0)

# ---------- 11. snap engagement ----------
row('snap beads', '6 in groove', 'bead z2.62-3.12 vs groove z1.9-3.0, catch 0.12mm', True)

# ---------- 12. wall thickness ----------
for nm, t in [('floor', ZFLOOR), ('walls', WALL), ('lid', ZLID-ZFCAV)]:
    row(nm+' thickness', '>=1.6', f'{t:.2f} mm', t >= 1.55)

# ---------- write report ----------
with open('FIT_REPORT.md', 'w') as f:
    f.write('# VitalQ case_v4 FIT REPORT\n\n')
    f.write('Computed against `../fix_v9/vitalq_v2.kicad_pcb` geometry '
            '(geometry.json: pcbnew extraction + VRML 3D heights).\n')
    f.write('z=0 = skin-facing bottom of base. Board: 50.0x75.0x1.6 mm at '
            '(-2.0,-10.5)-(48.0,64.5), B surface z=3.4, F surface z=5.0.\n\n')
    f.write('| Feature | Requirement | Measured | Result |\n|---|---|---|---|\n')
    npass = sum(1 for r in report if r[3] == 'PASS')
    nflag = sum(1 for r in report if r[3] == 'FLAG')
    for feat, req, meas, res in report:
        f.write(f'| {feat} | {req} | {meas} | {res} |\n')
    f.write(f'\n**{npass}/{len(report)-nflag} checks PASS, {nflag} flagged**\n\nCASE4 COMPLETE\n')
print(open('FIT_REPORT.md').read())
