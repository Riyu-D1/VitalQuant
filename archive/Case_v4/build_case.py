"""Build the VitalQ case: case_base.stl, case_top.stl + small parts.
Coordinates: KiCad board XY (mm), z=0 at skin side of base.
"""
import numpy as np
import trimesh
from case_lib import *

FEATS = {}   # cutout/feature centers for the fit report

# =====================================================================
#  BASE (skin-side tray): z 0 .. ZSEAM
# =====================================================================
def build_base():
    outer = rrect(OX0, OY0, OX1, OY1, RO)
    inner = rrect(IX0, IY0, IX1, IY1, RI)
    shell = prism(outer, 0, ZSEAM)
    # interior cavity
    shell = D(shell, prism(inner, ZFLOOR, ZSEAM+0.1))
    # board edge support ledge (1.5 mm ring, top flush with B surface ZB)
    ledge_ring = inner.difference(rrect(IX0+1.5, IY0+1.5, IX1-1.5, IY1-1.5, RI))
    shell = U([shell, prism(ledge_ring, ZB-0.8, ZB)])

    sub = []   # subtract list
    # --- corner reliefs for the sharp board corners ---
    for cx, cy in [(BX0,BY0),(BX1,BY0),(BX0,BY1),(BX1,BY1)]:
        sub.append(cyl(cx, cy, 1.5, ZSEAM+0.05, 1.7))
    # --- J12 pocket + FFC exit (connector overhangs left edge to x=-3.6) ---
    sub.append(rbox(-4.25, 45.4, 1.05, 4.6, 59.7, ZSEAM+0.05))
    FEATS['J12_slot'] = (52.55, 1.05, ZSEAM)
    # --- tail slots for J9 / J10 (top-shell covers; here: shallow notch) ---
    for name, ya, yb in [('J9', 12.6, 22.4), ('J10', 38.8, 53.0)]:
        sub.append(rbox(46.0, ya, 3.25, OX1+0.1, yb, ZSEAM+0.05))
        FEATS[name+'_notch_base'] = ((ya+yb)/2, 3.25, ZSEAM)
    # --- LiPo wire channel: floor groove + vertical riser through seam ---
    sub.append(rbox(-2.6, 3.4, 1.0, 14.2, 6.6, 1.62))
    sub.append(rbox(OX0-0.1, 3.6, 1.5, -1.5, 7.0, ZSEAM+0.05))
    FEATS['J2_wire'] = (5.2, 1.0, 1.62)
    # --- skin electrode apertures (pad +0.55 margin each axis -> merged rows) ---
    def padrow(ref, ax='x'):
        ps = pads(ref)
        if ax == 'x':
            xs = [p[0] for p in ps]; ws = [p[2] for p in ps]
            a0 = min(x-w/2 for x, w in zip(xs, ws))-0.28
            a1 = max(x+w/2 for x, w in zip(xs, ws))+0.28
            y0 = min(p[1]-p[3]/2 for p in ps)-0.28
            y1 = max(p[1]+p[3]/2 for p in ps)+0.28
            return (a0, y0, a1, y1)
        else:
            ys = [p[1] for p in ps]; hs = [p[3] for p in ps]
            a0 = min(y-h/2 for y, h in zip(ys, hs))-0.28
            a1 = max(y+h/2 for y, h in zip(ys, hs))+0.28
            x0 = min(p[0]-p[2]/2 for p in ps)-0.28
            x1 = max(p[0]+p[2]/2 for p in ps)+0.28
            return (x0, a0, x1, a1)
    for ref, ax in [('J5','x'), ('J6','x'), ('J13','x'), ('J11','y'), ('J7','x'), ('J3','x')]:
        x0, y0, x1, y1 = padrow(ref, ax)
        sub.append(rbox(x0, y0, -0.1, x1, y1, ZSEAM+0.05))
        FEATS[ref+'_aperture'] = ((x0+x1)/2, (y0+y1)/2)
    # --- optical apertures ---
    for name, cx, cy, w, h in [
        ('U16', 13.4, 56.0, 6.6, 4.6), ('U10', 20.0, 55.5, 3.6, 3.6),
        ('D10', 24.75, 57.5, 4.0, 4.0), ('D11_12', 15.2, 49.7, 4.6, 4.2),
        ('U12', 10.3, 50.8, 4.2, 4.6), ('U23', 7.6, 47.0, 2.4, 2.4)]:
        sub.append(rbox(cx-w/2, cy-h/2, -0.1, cx+w/2, cy+h/2, ZSEAM+0.05))
        FEATS[name+'_aperture'] = (cx, cy, w, h)
    # --- U11 TMP117 thermal contact: stepped bore for Ni-Cu slug ---
    sub.append(cyl(20.2, 50.4, -0.1, 1.7, 1.5))       # Ø3.0 slug bore
    sub.append(cyl(20.2, 50.4, -0.1, 0.68, 1.82))     # Ø3.64x0.68 flange seat
    FEATS['U11_slug'] = (20.2, 50.4, 3.0)
    # --- mic acoustic port: Ø1.6 hole + gasket recess ---
    sub.append(cyl(28.936, 36.154, -0.1, 1.65, 0.8))
    sub.append(cyl(28.936, 36.154, 1.6, 2.3, 1.9))
    FEATS['U25_port'] = (28.936, 36.154)
    # --- mounting bosses H1/H2: pad + locating pin (hole Ø2.2) ---
    add = []
    for hx, hy in [(0.6, -2.1875), (-0.15, -7.1844)]:
        add.append(cyl(hx, hy, ZFLOOR, ZB, 2.2))
        add.append(cyl(hx, hy, ZB, ZB+1.3, 0.95))
    # --- extra standoff bosses at verified-clear spots ---
    for sx, sy in [(38.0, -6.0), (44.0, -6.0), (40.0, 63.0), (33.0, -6.0)]:
        add.append(cyl(sx, sy, ZFLOOR, ZB, 2.0))
    # --- ledge relief where B components reach the board edge (R122) ---
    sub.append(rbox(31.8, 62.6, 2.5, 37.6, 64.95, 3.45))
    # screw pilot bosses handled in top (screws clamp through H1/H2):
    # M2 self-tap -> pilot Ø1.7 into base boss
    for hx, hy in [(0.6, -2.1875), (-0.15, -7.1844)]:
        pass  # boss already there; screw goes into same axis
    # --- snap groove on exterior wall face (0.75 deep x 1.1 high, z 1.9..3.0) ---
    groove_poly = rrect(OX0-0.05, OY0-0.05, OX1+0.05, OY1+0.05, RO).difference(
                  rrect(OX0+0.75, OY0+0.75, OX1-0.75, OY1-0.75, RO))
    shell = D(shell, prism(groove_poly, 1.9, 3.0))
    shell = U([shell]+add)
    shell = D(shell, U(sub))
    return shell

# =====================================================================
#  TOP SHELL: z ZSEAM .. ZLID (+ blister)
# =====================================================================
def build_top():
    outer = rrect(OX0, OY0, OX1, OY1, RO)
    inner = rrect(IX0, IY0, IX1, IY1, RI)
    shell = prism(outer, ZSEAM, ZLID)
    # interior cavity to ZFCAV
    shell = D(shell, prism(inner, ZSEAM-0.1, ZFCAV))
    # F-side pressure ledge ring (top at 5.7, bottom on board top 5.0)
    ledge_ring = inner.difference(rrect(IX0+1.5, IY0+1.5, IX1-1.5, IY1-1.5, RI))
    shell = U([shell, prism(ledge_ring, ZT, ZT+0.7)])

    # antenna bulge: thin-wall blister on -y wall over the off-board antenna
    shell = U([shell, rbox(4.4, OY0-2.2, 4.0, 19.9, OY0+0.05, 8.2)])
    sub = []
    # --- corner reliefs for the sharp board corners ---
    for cx, cy in [(BX0,BY0),(BX1,BY0),(BX0,BY1),(BX1,BY1)]:
        sub.append(cyl(cx, cy, ZSEAM-0.1, ZT+0.3, 1.7))
    # --- USB-C: window through -y wall + body overhang pocket ---
    sub.append(rbox(21.2, OY0-0.1, 4.9, 30.8, -10.0, 8.7))         # plug window
    sub.append(rbox(20.5, -12.0, 4.35, 31.5, -10.0, 8.35))        # J1 body pocket
    sub.append(rbox(20.4, -11.0, ZFCAV, 31.6, -3.3, 8.75))        # J1 ceiling recess
    FEATS['J1_window'] = (26.0, 6.8, 9.6, 3.8)
    # --- antenna pocket (inside bulge; 0.9 mm thin wall, no metal) ---
    sub.append(rbox(3.7, -13.95, 4.35, 20.7, -10.55, 7.9))
    FEATS['antenna_pocket'] = (12.4, -12.0)
    # ledge-ring relief over antenna + J1 edge + SW + J7/J3 pad zones
    sub.append(rbox(3.9, -10.9, 4.9, 20.6, -9.2, 5.8))
    sub.append(rbox(20.5, -10.9, 4.9, 31.5, -9.2, 5.8))
    sub.append(rbox(43.0, 31.0, 4.9, 47.6, 38.2, 5.8))
    sub.append(rbox(-0.4, 54.6, 4.9, 4.3, 59.7, 5.8))
    sub.append(rbox(10.5, 61.4, 4.9, 18.7, 64.8, 5.8))
    sub.append(rbox(24.2, 61.4, 4.9, 28.8, 64.8, 5.8))
    # --- SW plunger guide holes through lid ---
    for nm, x, y in [('SW1', 45.30024, 34.61124), ('SW2', 2.02538, 57.16984)]:
        sub.append(cyl(x, y, ZFCAV-0.5, ZLID+0.1, 1.75))
        FEATS[nm+'_plunger'] = (x, y)
    # --- D25 light pipe hole ---
    sub.append(cyl(4.0, 42.9, ZFCAV-0.5, ZLID+0.1, 1.4))
    FEATS['D25_pipe'] = (4.0, 42.9)
    # --- J8 Tag-Connect window + plug recess ---
    sub.append(rbox(22.7, 49.9, ZFCAV-0.5, 30.2, 57.0, ZLID+0.1))
    sub.append(rbox(22.0, 49.2, ZLID-0.6, 30.9, 57.7, ZLID+0.1))
    FEATS['J8_window'] = (26.45, 53.4)
    # (case_v4 vs fix_v9: J7 and J3 both moved to B.Cu skin pads -> base
    #  apertures + pogo contacts; both old lid windows are removed.)
    # --- BME280 vent ---
    sub.append(cyl(32.393, 31.970, ZFCAV-0.5, ZLID+0.1, 1.25))
    FEATS['U13_vent'] = (32.393, 31.97)
    # --- B-side edge notches (tails J9/J10 + J2 wire + FFC, through skirt) ---
    for ya, yb in [(12.6, 22.4), (38.8, 53.0)]:
        sub.append(rbox(46.0, ya, 2.3, OX1+1.2, yb, 4.4))
    sub.append(rbox(OX0-1.2, 3.6, 2.3, -1.5, 7.0, 4.4))
    sub.append(rbox(OX0-1.2, 47.0, 1.9, -1.5, 58.0, 4.5))
    # --- battery blister: raised pocket on lid ---
    bx0, by0, bx1, by1 = 4.4, 4.6, 30.2, 40.4
    blister = rbox(bx0-WALL, by0-WALL, ZLID-0.2, bx1+WALL, by1+WALL, ZBAY_OUT, r=3.0)
    blister = D(blister, rbox(bx0, by0, ZLID, bx1, by1, ZBAY_HI))
    shell = U([shell, blister])
    FEATS['battery_bay'] = ((bx0+bx1)/2, (by0+by1)/2, bx1-bx0, by1-by0)
    # wire hole blister->cavity
    sub.append(cyl(6.6, 8.2, ZFCAV-0.1, ZBAY_IN+0.6, 1.75))
    # --- skirt cutout at the USB-C window (plug overmould approach) ---
    sub.append(rbox(19.0, OY0-1.3, 2.3, 33.0, OY0+0.9, 4.6))
    # --- strap lugs (belt loops) on LEFT+RIGHT top-shell walls ---
    lugs = []
    def lug(xside, ya, yb):
        if xside == 'L':
            f = [rbox(OX0-4.4, yy-1.5, 4.2, OX0, yy+1.5, 8.6) for yy in (ya, yb)]
            f.append(rbox(OX0-4.4, ya-1.5, 8.0, OX0, yb+1.5, 9.4))
        else:
            f = [rbox(OX1, yy-1.5, 4.2, OX1+4.4, yy+1.5, 8.6) for yy in (ya, yb)]
            f.append(rbox(OX1, ya-1.5, 8.0, OX1+4.4, yb+1.5, 9.4))
        return f
    lugs += lug('L', 8.0, 26.0)    # slot ~14.5 x 4.4 mm (main strap side)
    lugs += lug('L', 59.5, 66.0)   # below FFC slot
    lugs += lug('R', -6.0, 10.0)   # above J9 tail slot
    lugs += lug('R', 24.0, 37.0)   # between J9 and J10 tail slots
    shell = U([shell]+lugs)
    # --- exterior skirt + segmented snap beads ---
    skirt_poly = rrect(OX0-0.85, OY0-0.85, OX1+0.85, OY1+0.85, RO)
    skirt = D(prism(skirt_poly, 2.45, ZSEAM+0.35),
              prism(rrect(OX0-0.15, OY0-0.15, OX1+0.15, OY1+0.15, RO), 2.3, ZSEAM+0.5))
    shell = U([shell, skirt])
    # snap beads: 6 segments (x2 per long edge, 1 top, 1 bottom)
    beads = []
    for (xa, xb, ya, yb) in [
        (OX0-0.85, OX0+0.45, 14.0, 30.0),   # -x edge bead1
        (OX0-0.85, OX0+0.45, 36.0, 52.0),   # -x edge bead2
        (OX1-0.45, OX1+0.85, 14.0, 30.0),   # +x edge
        (OX1-0.45, OX1+0.85, 36.0, 52.0),   # +x edge
        (8.0, 24.0, OY1-0.45, OY1+0.85),    # +y edge
        (24.0, 40.0, OY1-0.45, OY1+0.85)]:  # +y edge
        beads.append(rbox(xa, ya, 2.62, xb, yb, 3.12))
    # beads sit on skirt interior -> protrude inward 0.45 under base ledge/groove
    shell = U([shell]+beads)
    # --- screw clearance holes at H1/H2 through lid ---
    for hx, hy in [(0.6, -2.1875), (-0.15, -7.1844)]:
        sub.append(cyl(hx, hy, ZFCAV-0.6, ZLID+0.2, 1.25))
        sub.append(cyl(hx, hy, ZLID-0.8, ZLID+0.2, 2.3))   # head recess M2
    shell = D(shell, U(sub))
    return shell

# =====================================================================
#  SMALL PARTS
# =====================================================================
def button_plunger():
    """Plunger pin for SW1/SW2. tip at z0 -> sits at z~6.95 assembled."""
    tip = cyl(0, 0, 0, 0.5, 0.95)
    shaft = cyl(0, 0, 0.5, 3.35, 1.3)
    cap = cyl(0, 0, 3.35, 4.55, 2.75)
    return U([tip, shaft, cap])

def light_pipe():
    """Ø2.6 light pipe D25 -> lid."""
    stem = cyl(0, 0, 0, 4.5, 1.3)
    dome = trimesh.creation.icosphere(radius=1.55).apply_translation([0, 0, 4.5])
    dome = D(dome, rbox(-2, -2, -3, 2, 2, 4.5))
    return U([stem, dome])

def plug():
    """Reusable TPU plug for J8 / J3 windows.
    Print flat; sized to the J8 recess (9.0x8.6)."""
    core = rbox(-3.6, -3.6, 0, 3.6, 3.6, 1.35, r=0.6)
    lip = rbox(-4.5, -4.5, 1.35, 4.5, 4.5, 2.15, r=0.9)
    pull = cyl(0, 0, 2.15, 3.0, 1.5)
    return U([core, lip, pull])

# ---------------- skin-contact inserts ----------------
# Electrode pads are on B.Cu (z=ZB, faces skin through the base floor).
# Gap pad->skin plane = 3.4 mm -> spring-loaded contact soldered per pad.
# Chosen: Harwin P70-5100045 SMD pogo, Ø1.14 body, 4.8 free / 4.0 working
# height, gold. Tip at working height reaches ZB-4.0 = -0.6 (0.6 mm proud
# under rated load) — always across the skin plane; at 1.7 mm pad pitch the
# Ø1.14 body keeps >=0.56 mm isolation between adjacent contacts.
POGO_FREE, POGO_WORK = 4.8, 4.0

def pogo_pin(comp=0.0):
    """P70-5100045 approx model; origin = solder plane (z0), tip hangs -z.
    comp = plunger compression (0 free -> 0.8 at 4.0 mm working height)."""
    tip = trimesh.creation.icosphere(radius=0.45)
    tip.apply_translation([0, 0, -POGO_FREE+comp])
    plunger = cyl(0, 0, -POGO_FREE+comp, -POGO_FREE+comp+1.1, 0.45)
    barrel = cyl(0, 0, -3.9, 0.0, 0.57)
    tail = cyl(0, 0, -0.15, 0.05, 0.66)
    return U([tip, plunger, barrel, tail])

def thermal_slug():
    """Ni-plated C110 slug for U11: Ø3.55x0.62 flange + Ø2.95 shaft, z0..2.42.
    +0.3 mm TIM pad (3M 5590H-05) compresses to package face at z=2.4."""
    flange = cyl(0, 0, 0.0, 0.62, 1.775)
    shaft = cyl(0, 0, 0.55, 2.42, 1.475)
    return U([flange, shaft])

def electrode_pads():
    """[(ref,x,y)] for every skin electrode pad (J5/J6/J7/J13/J11/J3)."""
    out = []
    for f in G['components']:
        if f['ref'] in ('J5', 'J6', 'J7', 'J13', 'J11', 'J3'):
            for p in f['pads']:
                out.append((f['ref'], p['pos'][0], p['pos'][1]))
    return out

def contacts_assembly():
    """All contacts placed in assembled position (for verify/renders)."""
    parts = []
    for ref, x, y in electrode_pads():
        p = pogo_pin(comp=POGO_FREE-POGO_WORK)
        p.apply_translation([x, y, ZB])
        parts.append(p)
    s = thermal_slug(); s.apply_translation([20.2, 50.4, 0.0])
    parts.append(s)
    return U(parts)

if __name__ == '__main__':
    import json as _j
    base = build_base(); top = build_top()
    for name, m in [('case_base', base), ('case_top', top),
                    ('button_plunger', button_plunger()),
                    ('light_pipe', light_pipe()), ('plug', plug()),
                    ('pogo_pin', pogo_pin()), ('thermal_slug', thermal_slug()),
                    ('contacts_assy', contacts_assembly())]:
        m.export(name+'.stl')
        print(name, 'watertight=', m.is_watertight, 'vol=', round(m.volume, 1))
    _j.dump(FEATS, open('features.json', 'w'), indent=1)
    print('done')
