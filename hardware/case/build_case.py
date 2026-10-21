"""Build the VitalQ case_v8: case_base.stl, case_top.stl + small parts.
Coordinates: KiCad board XY (mm), z=0 at skin side of base.

case_v8 wired-electrode revision:
  * No in-case conductive skin contacts: the skin face remains closed except
    for existing optical, microphone, humidity and thermal-slug windows.
  * Each populated board land is wired separately. Insulated 30 AWG lead wires
    route in shallow raceways to a single 4 mm edge exit and cable-tie relief.
  * Raceway and land clearances are checked against all J5/J6/J7/J11/J13 pads.
  * External disposable snap-on gel electrodes are used; see POGO_CHANGE.md.
  * All unrelated enclosure features are retained.
"""
import numpy as np
import trimesh
from case_lib import *

FEATS = {}   # feature info for the fit report

# pin positions shared by the lid holes, cup floor holes and pin plate
PIN_XY_CUP = [(7.0, 12.5), (27.0, 12.5), (7.0, 24.5),
              (27.0, 24.5), (7.0, 38.5), (27.0, 38.5)]

# =====================================================================
#  BASE (skin-side tray): z 0 .. ZSEAM   [prints skin-down, as drawn]
# =====================================================================
def padrow(ref, ax='x', m=0.45):
    ps = pads(ref)
    if ax == 'x':
        a0 = min(p[0]-p[2]/2 for p in ps)-m
        a1 = max(p[0]+p[2]/2 for p in ps)+m
        y0 = min(p[1]-p[3]/2 for p in ps)-m
        y1 = max(p[1]+p[3]/2 for p in ps)+m
        return (a0, y0, a1, y1)
    a0 = min(p[1]-p[3]/2 for p in ps)-m
    a1 = max(p[1]+p[3]/2 for p in ps)+m
    x0 = min(p[0]-p[2]/2 for p in ps)-m
    x1 = max(p[0]+p[2]/2 for p in ps)+m
    y0 = min(p[1]-p[3]/2 for p in ps)-m
    y1 = max(p[1]+p[3]/2 for p in ps)+m
    return (x0, a0, x1, y1)

APERTURES = {}
def aperture_polys():
    """{name: (x0,y0,x1,y1)} for every skin-side opening."""
    if APERTURES:
        return APERTURES
    ap = {}
    # v22: pogo/snap electrode apertures restored (no separate ECG lead wires).
    for ref, ax in [('J5','x'), ('J6','x'), ('J13','x'), ('J11','y'),
                    ('J7','x'), ('J3','x')]:
        ap[ref] = padrow(ref, ax)
    ap['U16'] = (9.0, 53.3, 17.65, 58.7)
    ap['U10'] = (18.3, 52.6, 22.8, 58.4)
    ap['D10'] = (22.5, 54.9, 26.8, 60.0)
    ap['D11_12'] = (13.0, 48.0, 17.2, 51.6)
    ap['U12'] = (7.4, 47.9, 13.2, 53.7)
    ap['U23'] = (6.4, 45.8, 8.8, 48.2)     # vent hole inside the membrane recess
    APERTURES.update(ap)
    return ap

def build_base():
    outer, inner = OPOLY, IPOLY
    # ergonomic skin edge: 45 deg chamfer + bezier arc loft z0..2.8
    shell = U([edge_profile_loft(outer, inset0=1.7, chamfer=1.3, arc_z=2.8),
               prism(outer, 2.8, ZSEAM)])
    shell = D(shell, prism(inner, ZFLOOR, ZSEAM+0.1))       # interior cavity
    shell = U([shell, wedge_ring(inner, ZB-1.0, ZB, 1.2)])  # 45 deg board seat

    sub, add = [], []
    # corner reliefs for the sharp board corners
    for cx, cy in [(BX0,BY0),(BX1,BY0),(BX0,BY1),(BX1,BY1),
                   (TAB_N[0],TAB_N[1]),(TAB_N[2],TAB_N[1]),
                   (TAB_E[0],TAB_E[1]),(TAB_E[0],TAB_E[3]),(TAB_E[2],TAB_E[1]),(TAB_E[2],TAB_E[3])]:
        sub.append(cyl(cx, cy, 1.5, ZSEAM+0.05, 1.7))
    # J12 pocket + FFC exit (connector overhangs the west edge)
    sub.append(rbox(OX0-0.1, 45.4, 0.95, 4.7, 59.7, ZSEAM+0.05))
    FEATS['J12_slot'] = (52.55, 0.95, ZSEAM)
    # tail slots J9 / J10 (below-board, open at the tray rim)
    for name, ya, yb in [('J9', 12.6, 22.4), ('J10', 38.8, 53.0)]:
        sub.append(rbox(46.0, ya, 2.9, OX1+0.1, yb, ZSEAM+0.05))
        FEATS[name+'_notch_base'] = ((ya+yb)/2, 2.9, ZSEAM)
    # LiPo wire: floor groove + west riser portal (gabled ceiling)
    sub.append(rbox(-2.6, 3.4, 1.0, 14.2, 6.6, 1.62))
    sub.append(gabled_slot_cutter(OX0-0.1, 3.6, 1.9, -1.5, 7.0, 3.4, axis='x'))
    FEATS['J2_wire'] = (5.2, 1.0, 1.62)
    # USB-C grip corridor (v22): the -y wall must stay open x22.7..36.0 down
    # to the plug's lower lip (z3.05) — cut the base wall band above the
    # seat wedge so the overmould passes the seam unimpeded.
    sub.append(rbox(22.7, OY0-0.3, 2.85, 36.0, -10.4, ZSEAM+0.1))
    # the antenna tab's east wall band (x22.55..24.45) still clips the
    # overmould's west edge -> open it into the corridor notch
    sub.append(rbox(22.55, -18.6, 2.85, 24.7, -10.4, ZSEAM+0.1))
    # Skin electrode apertures: through-slot + 45 deg skin-mouth chamfer
    # (w0.48 over z -0.12..0.36) + 45 deg cavity-side funnel (w0.50 over
    # z 1.20..1.70) self-centring the pogo/snap tips on insertion (v7 rule).
    ap = aperture_polys()
    for ref in ['J5', 'J6', 'J13', 'J11', 'J7', 'J3']:
        x0, y0, x1, y1 = ap[ref]
        sub.append(rbox(x0, y0, -0.1, x1, y1, ZSEAM+0.05))
        sub.append(rect_cone_cut(x0, y0, x1, y1, -0.12, 0.36, 0.48, 0.0))
        sub.append(rect_cone_cut(x0, y0, x1, y1, 1.20, 1.70, 0.0, 0.50))
        FEATS[ref+'_aperture'] = ((x0+x1)/2, (y0+y1)/2)
    # optical apertures: same mouth chamfer, no funnel needed (no contact)
    for name in ['U16', 'U10', 'D10', 'D11_12', 'U12']:
        x0, y0, x1, y1 = ap[name]
        sub.append(rbox(x0, y0, -0.1, x1, y1, ZSEAM+0.05))
        sub.append(rect_cone_cut(x0, y0, x1, y1, -0.12, 0.36, 0.48, 0.0))
        FEATS[name+'_aperture'] = ((x0+x1)/2, (y0+y1)/2, x1-x0, y1-y0)
    x0, y0, x1, y1 = ap['U23']
    FEATS['U23_aperture'] = ((x0+x1)/2, (y0+y1)/2, x1-x0, y1-y0)
    # U23 humidity vent: Ø2.0 bore inside a Ø4.6 membrane recess on the
    # cavity side (recess floor z1.2); 45 deg skin-mouth chamfer r1.0->1.35
    sub.append(cyl(7.6, 47.0, -0.1, 1.65, 1.0))
    sub.append(cyl(7.6, 47.0, 1.2, 1.65, 2.3))
    sub.append(cyl_cone_cut(7.6, 47.0, -0.15, 0.20, 1.35, 1.0))
    FEATS['U23_vent_recess'] = (7.6, 47.0, 4.6)
    # U11 TMP117 thermal slug: stepped bore, slug face 0.6 proud; the
    # Ø3.8 seat mouth gets a 0.15 mm 45 deg rim ease (seat keeps 0.15 wall)
    sub.append(cyl(20.2, 50.4, 0.25, 1.7, 1.5))        # Ø3.0 shaft bore
    sub.append(cyl(20.2, 50.4, -0.1, 0.30, 1.9))       # Ø3.8 flange seat 0.30
    sub.append(cyl_cone_cut(20.2, 50.4, 0.0, 0.15, 2.05, 1.9))
    FEATS['U11_slug'] = (20.2, 50.4, 3.0)
    # mic port Ø1.6 + cavity-side acoustic-mesh recess Ø4.4x0.4; 45 deg
    # skin-mouth chamfer r0.8->1.15
    sub.append(cyl(28.936, 36.154, -0.1, 1.65, 0.8))
    sub.append(cyl(28.936, 36.154, 1.2, 1.65, 2.2))
    sub.append(cyl_cone_cut(28.936, 36.154, -0.15, 0.20, 1.15, 0.8))
    FEATS['U25_port'] = (28.936, 36.154)
    # mounting bosses H1/H2 + standoffs
    for hx, hy in [(0.6, -2.1875), (-0.15, -7.1844)]:
        add.append(cyl(hx, hy, ZFLOOR, ZB, 2.2))
        add.append(cyl(hx, hy, ZB, ZB+1.3, 0.95))
    # v22: dropped (33,-6) standoff — sits under J1's shield-peg NPTH field
    for sx, sy in [(38.0, -6.0), (44.0, -6.0), (40.0, 63.0)]:
        add.append(cyl(sx, sy, ZFLOOR, ZB, 2.0))
    # wedge-ring reliefs where B parts reach the board edge
    sub.append(rbox(31.8, 62.6, 2.5, 37.6, 64.95, 3.45))          # R122
    sub.append(rbox(45.9, 54.5, 2.5, 48.4, 60.4, 3.45))           # J11 tail pads
    sub.append(rbox(26.0, 63.0, 2.5, 33.4, 64.95, 3.45))          # J13 pads +y edge
    # snap groove on the exterior wall: z2.85..3.45, 0.55 deep, 45 deg edges
    gp = outer.difference(outer.buffer(-0.55))
    sub.append(prism(gp, 2.95, 3.35))
    for i in range(3):   # 45 deg top edge
        w = 0.55*(1-(i+1)/3.0)
        sub.append(prism(outer.difference(outer.buffer(-max(w, 0.02))),
                         3.35+i*0.033, 3.35+(i+1)*0.033))
    for i in range(3):   # 45 deg bottom edge
        w = 0.55*(i/3.0)
        sub.append(prism(outer.difference(outer.buffer(-max(w, 0.02))),
                         2.85+i*0.033, 2.85+(i+1)*0.033))
    shell = U([shell]+add)
    shell = D(shell, U([m for m in sub if m.is_volume]))
    return shell

# =====================================================================
#  TOP SHELL: wall ring + lid plate, z 2.55 .. ZLID   [prints TOP-DOWN]
# =====================================================================
def build_top():
    outer, inner = OPOLY, IPOLY
    # lid top-edge rounding (mirror of the base loft; flat face = plate side)
    # v22: pre-mirror the outline about y-centre - the pi-rotation below
    # would otherwise flip the N antenna tab's outer bulge to +y72.
    from shapely.affinity import scale as _sc
    ycm = (OY0+OY1)/2.0
    outer_m = _sc(outer, xfact=1.0, yfact=-1.0, origin=(0.0, ycm))
    cap = edge_profile_loft(outer_m, inset0=1.5, chamfer=1.1, arc_z=2.5)
    cap.apply_transform(trimesh.transformations.rotation_matrix(
        np.pi, [1, 0, 0], point=[0, ycm, ZLID/2]))
    shell = U([prism(outer, ZSEAM, ZLID-2.5), cap])
    # antenna bulge: tail embedded deep in the wall; the cavity cut below
    # removes whatever pokes inside, so no slivers can survive. v22: module
    # overhangs to y=-22.31 -> inner face -23.3 (1.0 air gap), outer -24.9.
    # East outer face capped at 22.8 so the bulge never enters the USB-C
    # grip corridor (x 23.3..35.7). Flat top at ZLID -> plate-side print.
    bulge = rbox(0.1, -24.9, 4.0, 22.8, OY0+2.1, ZLID, r=2.0)
    shell = U([shell, bulge.copy()])
    shell = D(shell, prism(inner, ZSEAM-0.1, ZFCAV))        # interior cavity
    # F-side clamp ledge: 45 deg wedge reaching the board top edge at ZT_MAX
    shell = U([shell, wedge_ring_down(inner, ZT_MAX+0.7, ZT_MAX, 1.2)])

    sub = []
    # corner reliefs (board corners, top side)
    for cx, cy in [(BX0,BY0),(BX1,BY0),(BX0,BY1),(BX1,BY1),
                   (TAB_N[0],TAB_N[1]),(TAB_N[2],TAB_N[1]),
                   (TAB_E[0],TAB_E[1]),(TAB_E[0],TAB_E[3]),(TAB_E[2],TAB_E[1]),(TAB_E[2],TAB_E[3])]:
        sub.append(cyl(cx, cy, ZSEAM-0.1, ZT_MAX+0.3, 1.7))
    # --- USB-C (v22 J1 @x29.5): open grip corridor through the -y wall ---
    # Plug overmould 12.4x6.5 centred x29.5 / z=mouth centre must slide home:
    # case open x23.0..36.0 (12.4 + margins), z from below the mouth band up
    # through the lid top, from the outer face to within 0.5 of the
    # receptacle front (y=-11.5). Through-notch: wall+skirt absent there.
    sub.append(rbox(22.7, -18.8, 2.4, 36.0, -10.4, ZLID+0.1))
    sub.append(rbox(23.4, -12.1, 4.35, 35.6, -2.4, 8.45))         # J1 body pocket
    sub.append(rbox(23.4, -11.0, ZFCAV, 35.6, -2.4, 8.85))        # J1 ceiling recess
    FEATS['J1_window'] = (29.5, 13.0, 6.65)
    # antenna pocket: v22 module overhangs to -22.31; pocket inner face -23.3
    # (1.0 mm air gap goal met), x keeps >=0.54 gap to module body (2.44..20.56)
    sub.append(gabled_slot_cutter(1.55, -23.3, 4.35, 21.45, -10.45, 8.55,
                                  apex=0.6, axis='x'))
    FEATS['antenna_pocket'] = (11.5, -17.0)
    # ledge reliefs over edge F parts
    for (xa, ya, xb, yb) in [(3.9, -10.9, 20.6, -9.2),   # U1 edge
                             (23.5, -10.9, 35.5, -9.2),   # J1 edge (v22)
                             (43.0, 31.0, 47.6, 38.2),    # SW1
                             (-0.4, 54.6, 4.3, 59.7),     # SW2
                             (10.5, 61.4, 18.7, 64.8),    # +y edge parts
                             (24.2, 61.4, 28.8, 64.8)]:   # +y edge parts
        sub.append(rbox(xa, ya, ZT_MAX-0.1, xb, yb, ZT_MAX+0.8))
    # --- SW cap guide bores + cap-seat counterbores in the lid top ---
    for nm, x, y in [('SW1', 45.30024, 34.61124),
                      ('SW2', 2.02538, 57.16984)]:
        sub.append(cyl(x, y, ZFCAV-0.5, 8.35, 0.95))          # guide bore
        sub.append(cyl(x, y, 8.35, 8.9, 1.45))                # barb groove
        sub.append(cyl(x, y, 8.9, ZLID+0.1, 0.95))            # guide bore
        FEATS[nm+'_cap'] = (x, y)
    # --- D25 light pipe pocket + seat ---
    sub.append(cyl(4.0, 42.9, ZFCAV-0.5, ZLID+0.1, 1.4))
    sub.append(cyl(4.0, 42.9, ZLID-0.5, ZLID+0.1, 1.85))
    FEATS['D25_pipe'] = (4.0, 42.9)
    # --- J8 Tag-Connect window + plug recess ---
    sub.append(rbox(22.7, 49.9, ZFCAV-0.5, 30.2, 57.0, ZLID+0.1))
    sub.append(rbox(22.0, 49.2, ZLID-0.6, 30.9, 57.7, ZLID+0.1))
    FEATS['J8_window'] = (26.45, 53.4)
    # --- U13 BME280 vent: Ø1.5 + membrane recess on interior ceiling ---
    #     offset +x of the bay cup footprint (cup covers the sensor's x)
    sub.append(cyl(34.8, 32.0, ZFCAV-0.5, ZLID+0.1, 0.75))
    sub.append(cyl(34.8, 32.0, ZFCAV-0.35, ZFCAV+0.05, 2.1))
    FEATS['U13_vent'] = (34.8, 32.0)
    # --- edge notches through skirt+wall: tails J9/J10, J2 wire, FFC ---
    for ya, yb in [(12.6, 22.4), (38.8, 53.0)]:
        sub.append(gabled_slot_cutter(46.0, ya, 2.4, OX1+1.2, yb, 4.5, axis='x'))
    sub.append(gabled_slot_cutter(OX0-1.2, 3.6, 1.9, -1.5, 7.0, 4.4, axis='x'))
    sub.append(gabled_slot_cutter(OX0-1.2, 47.0, 1.9, -1.5, 58.0, 4.5, axis='x'))
    # v22: R strap tunnel bores through the E-tab bulge wall + ledge at
    # z6.4..8.6, all the way from the cavity edge x49.4 to the fin face
    sub.append(rbox(49.4, 17.0, 6.4, 57.8, 33.0, 8.55))
    # --- battery bay: flat lid top + 6 hollow snap pins on the cup floor ---
    # (a raised ring/plinth would leave the lid face floating in the flipped
    # print). Pins press through these Ø3.2 holes; barbs catch the cavity-side
    # face. Mouths get a 45 deg lead chamfer.
    for px, py in PIN_XY_CUP:
        sub.append(cyl(px, py, ZFCAV-0.2, ZLID+0.2, 1.6))
        sub.append(cyl(px, py, ZLID-0.15, ZLID+0.2, 1.75))   # 45 deg mouth chamfer
        sub.append(cyl(px, py, ZLID-0.30, ZLID-0.15, 1.675)) # (2 steps)
    FEATS['snap_pins'] = PIN_XY_CUP
    rcx0, rcy0, rcx1, rcy1 = 2.9, 3.1, 31.7, 41.9
    FEATS['battery_bay'] = ((rcx0+rcx1)/2, (rcy0+rcy1)/2,
                             rcx1-rcx0, rcy1-rcy0)
    # wire hole lid-top -> cavity (inside the bay footprint), 45 deg lead
    sub.append(cyl(6.6, 8.2, ZFCAV-0.1, ZLID+0.3, 1.6))
    sub.append(cyl(6.6, 8.2, ZLID-0.3, ZLID+0.3, 2.1))
    # --- J2 strain-relief posts hanging from the F-cavity ceiling ---
    for px, py in [(7.6, 9.0), (10.9, 9.0)]:   # y9.0 clears U1's courtyard
        shell = U([shell, cyl(px, py, ZT_MAX+1.0, ZFCAV, 1.1)])
    FEATS['J2_posts'] = (9.25, 9.0)
    # --- USB dust-flap tether anchor pocket (blind, lid top) ---
    # v22: anchor moved east of the grip corridor (x>=36.5 required)
    sub.append(cyl(37.5, -11.3, ZLID-2.2, ZLID+0.1, 1.3))
    FEATS['flap_anchor'] = (37.5, -11.3)
    # --- skirt cutout at the USB-C window (plug overmould approach) ---
    sub.append(rbox(22.2, OY0-1.3, 2.3, 36.5, OY0+0.9, 4.6))
    # --- strap lugs (v6 shape, v7 bar): ONE 30 mm open-staple slot per
    #     side. A single solid fin minus the channel void -> two 3.6 mm
    #     posts + a 3.0 mm top bar the strap loops (v7: bar 2.7 -> 3.0,
    #     channel 2.35 -> 2.05 clear, still >=1.5 for a folded elastic).
    #     Post bottoms pinch 0.2 mm to the skirt top so the wrapped loop
    #     can't slip off the ends. Plan corners R1.2; outboard edges get
    #     45 deg chamfers so no knife edge frays the elastic. See
    #     STRAP_DESIGN.md. ---
    def lug(xside):
        if xside == 'L':
            xa, xb, ftop = OX0-4.2, OX0+0.4, ZLID
            fin = rbox(xa, 8.5, 4.55, xb, 46.5, ftop, r=1.2)
            chan = rbox(xa-0.1, 12.1, 4.4, xb+0.1, 42.9, 6.6, r=0.8)
            edges = (4.55, 6.6, ftop)
            ch_top = 6.6
        else:
            # v22 R lug: the east power tab bulges the wall to x55.5 over
            # y20..29.9 and carries Q6/D33 to z~6.14. The strap band cannot
            # step sideways, so the whole R channel is raised to z6.5..8.5:
            # it crosses the tab airspace (floor 6.5 > comp top 6.14 +0.3),
            # bored through the bulge wall, and the fin grows to cover.
            xa, xb, ftop = OX1-0.4, 55.0, 10.65
            # over the E-tab zone (y17..33) the fin stays outboard of the
            # pocket edge (x>=53.0, clear of Q6/D33 boxes ending x52.53) and
            # its bottom rides up to z5.3 = tab F surface 5.0 +0.3 air gap
            fin = U([rbox(xa, 8.5, 4.55, xb, 20.0, ftop, r=1.2),
                     rbox(xa, 30.0, 4.55, xb, 46.5, ftop, r=1.2),
                     rbox(53.0, 17.0, 5.3, 57.8, 33.0, ftop)])
            chan = rbox(xa-0.1, 12.1, 6.5, 57.8, 42.9, 8.5, r=0.8)
            edges = (4.55, 8.5, ftop)
            ch_top = 8.5
        f = D(fin, chan)
        xo = xa if xside == 'L' else xb     # 45 deg chamfers on ALL three
        for ze in edges:                    # strap-bearing outboard edges:
            bev = trimesh.creation.box([2.0, 39.0, 2.0])  # fin bottom, bar
            bev.apply_transform(trimesh.transformations.rotation_matrix(
                np.pi/4, [0, 1, 0]))                    # bottom + bar top
            bev.apply_translation([xo, 27.5, ze])
            f = D(f, bev)
        # v7: same ease on the channel's inboard top edge - the strap's
        # inner edge slides over that corner while threading
        xi = xb if xside == 'L' else xa
        bev = trimesh.creation.box([2.0, 39.0, 2.0])
        bev.apply_transform(trimesh.transformations.rotation_matrix(
            np.pi/4, [0, 1, 0]))
        bev.apply_translation([xi, 27.5, ch_top])
        f = D(f, bev)
        return f
    lugs = [lug('L').copy(), lug('R').copy()]
    shell = U([shell]+lugs)
    # orientation chevron debossed 0.5 into the lid top near the -y chin
    # (points +y = wear direction; on the plate face in the flipped print)
    for cx, rot in ((37.2, 45.0), (41.8, -45.0)):
        bar = trimesh.creation.box([5.4, 1.4, 0.6])
        bar.apply_transform(trimesh.transformations.rotation_matrix(
            np.radians(rot), [0, 0, 1]))
        bar.apply_translation([cx, -4.2, ZLID-0.25])
        shell = D(shell, bar)
    # ECG pulse mark debossed 0.4 into the lid top on the -y chin, between
    # the antenna bulge and the wear chevron (branding + orientation)
    from shapely.geometry import LineString
    ecg = LineString([(21.8, -4.2), (24.3, -4.2), (25.0, -3.3), (25.8, -5.8),
                      (26.6, -2.0), (27.4, -4.2), (29.5, -4.2), (30.6, -3.5),
                      (31.6, -4.2), (33.2, -4.2)]).buffer(0.42, cap_style='flat')
    shell = D(shell, prism(ecg, ZLID-0.4, ZLID+0.1))
    FEATS['ecg_mark'] = (27.5, -4.2)
    # drip groove: 0.4 deep channel ring just outside the bay-cup footprint -
    # sweat pooling at the cup seam sheds sideways instead of sitting at the
    # joint line (purely exterior, plate-face print); keepout around D25
    dg = sbox(2.0, 2.0, 32.6, 43.0).difference(sbox(2.8, 2.8, 31.8, 42.2))
    dg = dg.difference(sbox(1.5, 40.5, 6.5, 44.5))   # D25 pipe keepout
    shell = D(shell, prism(dg, ZLID-0.4, ZLID+0.1))
    FEATS['drip_groove'] = (17.3, 42.6)
    FEATS['strap_slots'] = {'centre_y': 27.5, 'channel_y': [12.1, 42.9],
                            'channel_z': {'L': [4.4, 6.6], 'R': [6.5, 8.5]},
                            'bar_z': {'L': [6.6, ZLID], 'R': [8.5, 10.65]},
                            'post_t': 3.6, 'fin_x': 4.6, 'strap_w': 30.0}
    # --- exterior skirt + segmented snap beads ---
    # one ring: outer -0.85, inner -0.25 -> 0.25 clear of the base wall face,
    # covers the seam + groove zone; the snap beads provide the retention
    skirt = D(prism(roff(-0.85), 2.55, 4.35), prism(roff(-0.25), 2.40, 4.50))
    lead_rings = []   # 45 deg lead-in cone at the skirt bottom
    for i in range(4):
        off1 = 0.55 - (0.55-0.12)*((i+1)/4.0)
        lead_rings.append(D(prism(roff(-0.85), 2.55+i*0.075, 2.55+(i+1)*0.075),
                            prism(roff(off1), 2.4, 3.0)))
    # v22: the rectangular skirt ring would cross both board tabs as a
    # floating band inside the cavity -> open it over the tab spans
    skirt_tab = U([rbox(TAB_N[0]-0.6, TAB_N[1]-4.0, 2.3, TAB_N[2]+0.6,
                        OY0+0.6, 4.7),
                   rbox(46.0, TAB_E[1]-0.6, 2.3, TAB_E[2]+4.0,
                        TAB_E[3]+0.6, 4.7)])
    skirt = D(skirt, skirt_tab)
    lead_rings = [D(l, skirt_tab) for l in lead_rings]
    shell = U([shell, skirt.copy()]+lead_rings)
    # snap beads: wedge cross-section (45 deg top AND bottom slopes -> both
    # faces printable), protrude 0.45 inward of the skirt inner face
    beads = []
    # Barb profile in (across,z): tip 0.35 inside the wall face (in the base
    # groove zone), flat catch top at 3.30 under the groove's top ramp, 45 deg
    # lead wedge exits the skirt bottom, back anchored inside the skirt band.
    def bead_poly(t, b, f):
        sgn = 1.0 if t < b else -1.0      # tip direction across the wall
        return Polygon([(t, 2.95), (t+0.4*sgn, 2.55), (b, 2.55),
                        (b, 3.70), (f, 3.70), (f, 3.30), (t, 3.30)])
    for side, t, b, f, a0, a1 in [
        ('-x', OX0+0.35, OX0-0.75, OX0-0.25, 9.0, 25.0),
        ('-x', OX0+0.35, OX0-0.75, OX0-0.25, 28.0, 44.0),
        # v22: +x bead must not float inside the E-tab cavity (y20..29.9)
        # or cross the J9/J10 tail notches -> short bead y31..37.5 only
        ('+x', OX1-0.35, OX1+0.75, OX1+0.25, 31.0, 37.5),
        ('+x', OX1-0.35, OX1+0.75, OX1+0.25, 54.0, 65.0),
        ('+y', OY1-0.35, OY1+0.75, OY1+0.25, 8.0, 24.0),
        ('+y', OY1-0.35, OY1+0.75, OY1+0.25, 24.0, 40.0),
        # v22: only ONE -y bead remains, east of the USB grip corridor
        # (x23..36 must stay open) and outside the antenna keep-out
        # (x<=25.5 no thickened solids). West half has no bead — the
        # bulge/corridor leave no legal x for it (logged in PROGRESS.md).
        ('-y', OY0+0.35, OY0-0.75, OY0-0.25, 37.0, 44.5)]:
        bb = trimesh.creation.extrude_polygon(bead_poly(t, b, f), a1-a0)
        # extrusion axis -> along the wall (y for x-side beads, x for +/-y)
        bb.apply_transform(np.array(
            [[1.,0.,0.,0.],[0.,0.,1.,0.],[0.,1.,0.,0.],[0.,0.,0.,1.]]))
        if side in ('+y', '-y'):
            bb.apply_transform(np.array(
                [[0.,1.,0.,0.],[1.,0.,0.,0.],[0.,0.,1.,0.],[0.,0.,0.,1.]]))
            bb.apply_translation([a0, 0, 0])
        else:
            bb.apply_translation([0, a0, 0])
        beads.append(bb)
    shell = U([shell]+beads)
    # pry slots for disassembly: notch the SKIRT only (v7 fix - v5/v6 cut
    # 0.9 into the wall too, leaving a 2.4 mm through-window at the seam:
    # a sweat path straight into the interior). Wall face stays sealed;
    # a tool under the skirt lip still levers the snap band free.
    sub.append(rbox(OX0-0.9, -1.0, 2.55, OX0-0.05, 1.4, 3.1))
    sub.append(rbox(38.0, OY1+0.05, 2.55, 40.4, OY1+0.9, 3.1))
    sub_vol = [m for m in sub if m.is_volume]
    shell = D(shell, U(sub_vol))
    return shell

# =====================================================================
#  BATTERY CUP (separate snap-on part; prints bottom-down)
# =====================================================================
def bay_cup():
    """Battery bay cup: open tray pressing flat onto the lid top; retained by
    the separate pin plate (pins pass through the floor holes + lid, barb on
    the cavity face). Prints bottom-down. Local z=0 = the lid top face."""
    cx0, cy0, cx1, cy1 = 2.65, 2.85, 31.95, 42.15     # outer bounds
    ix0, iy0, ix1, iy1 = cx0+1.3, cy0+1.3, cx1-1.3, cy1-1.3   # inner 26.2x36.0
    ring = D(rbox(cx0, cy0, 0.0, cx1, cy1, 6.2, r=2.0),
             rbox(ix0, iy0, 0.5, ix1, iy1, 6.3))
    sub = []
    # six Ø3.2 pin holes through the floor (plate pins pass through + lid)
    for px, py in PIN_XY_CUP:
        sub.append(cyl(px, py, -0.1, 0.6, 1.6))
    # door grooves on the x-side walls, above the cell-on-plate top (4.85)
    sub.append(gabled_slot_cutter(cx0+0.15, cy0-0.2, 5.15, cx0+1.15,
                                  cy1+0.2, 6.05, apex=0.45, axis='y'))
    sub.append(gabled_slot_cutter(cx1-1.15, cy0-0.2, 5.15, cx1-0.15,
                                  cy1+0.2, 6.05, apex=0.45, axis='y'))
    # door entry slot through the -y cup wall
    sub.append(gabled_slot_cutter(cx0-0.1, cy0-0.2, 5.15, cx1+0.1,
                                  cy0+1.0, 6.05, apex=0.45, axis='y'))
    # door detent notch in the +y interior wall (at groove level)
    sub.append(rbox(15.6, cy1-0.4, 5.35, 19.0, cy1+0.1, 5.95))
    # wire notch through the floor, covering the Ø3.2 lid hole at (6.6,8.2)
    sub.append(rbox(5.0, 6.6, -0.1, 8.2, 9.8, 0.62))
    # interior retention ribs x0.35 (below the cell, beside the pin plate)
    ribs = [rbox(ix0+0.05, iy0+6.0, 0.5, ix0+0.4, iy0+14.0, 4.0),
            rbox(ix1-0.4, iy0+6.0, 0.5, ix1-0.05, iy0+14.0, 4.0),
            rbox(ix0+0.05, iy1-14.0, 0.5, ix0+0.4, iy1-6.0, 4.0),
            rbox(ix1-0.4, iy1-14.0, 0.5, ix1-0.05, iy1-6.0, 4.0)]
    return D(U([ring]+ribs), U(sub))

def pin_plate():
    """Push-in snap pin plate: 0.35 plate + six hollow Ø3.2 pins (two prongs
    each, outward wedge barb). Prints pins-UP; flipped at assembly so the pins
    pass through the cup floor holes + lid holes, barb catch face landing at
    cup-z -1.65 (just under the lid's cavity face). In STL coords pins +z.
    Assembled: plate 0.5..0.85, pin tip at cup-z -2.5."""
    # plate sits between the cell ribs (ribs occupy x4.25..4.6 / 30.05..30.35)
    plate = rbox(4.7, 4.5, 0.0, 29.95, 40.5, 0.35, r=1.0)
    pins = []
    for px, py in PIN_XY_CUP:
        tube = D(cyl(px, py, 0.35, 3.55, 1.6), cyl(px, py, 0.45, 2.9, 1.15))
        # catch band: plate-facing annulus at STL 2.50 -> cup-z -1.65
        barb = [cyl(px, py, 2.50, 3.15, 1.9)]
        for i in range(3):                                   # 45 deg lead tip
            barb.append(cyl(px, py, 3.15+i*0.133, 3.15+(i+1)*0.133,
                            1.9-0.10*(i+1)))
        pin = U([tube]+barb)
        for sx in (-1, 1):                                   # slots -> 2 prongs
            ya, yb = py+sx*1.00, py+sx*2.05
            pin = D(pin, rbox(px-0.35, min(ya, yb), 0.9,
                              px+0.35, max(ya, yb), 3.6))
        pins.append(pin)
    # finger pull tab at the -y edge
    tab = rbox(15.0, 4.3, 0.0, 19.6, 5.6, 1.0, r=0.4)
    p = U([plate, tab]+pins)
    # edge notch over the floor's wire notch (wire rises through both)
    return D(p, rbox(4.5, 6.4, -0.1, 8.4, 10.0, 0.6))

# =====================================================================
#  SMALL PARTS
# =====================================================================
def button_cap():
    """TPU cap for SW1/SW2: dome + flex skirt + pin + retention barb."""
    tip = cyl(0, 0, 0, 0.4, 0.8)
    pin = cyl(0, 0, 0.4, 1.3, 0.85)
    barb = cyl(0, 0, 1.3, 1.85, 1.35)
    neck = cyl(0, 0, 1.85, 2.95, 0.85)
    skirt = cyl(0, 0, 2.95, 3.75, 3.3)
    dome = trimesh.creation.icosphere(radius=3.3)
    dome.apply_transform(np.diag([1.0, 1.0, 0.42, 1.0]))
    dome.apply_translation([0, 0, 3.664])
    return U([tip, pin, barb, neck, skirt, dome])

def light_pipe():
    stem = cyl(0, 0, 0, 4.0, 1.35)
    dome = trimesh.creation.icosphere(radius=1.7).apply_translation([0, 0, 4.0])
    dome = D(dome, rbox(-2, -2, -3, 2, 2, 4.0))
    return U([stem, dome])

def plug():
    core = rbox(-3.6, -3.6, 0, 3.6, 3.6, 1.35, r=0.6)
    lip = rbox(-4.5, -4.5, 1.35, 4.5, 4.5, 2.15, r=0.9)
    pull = cyl(0, 0, 2.15, 3.0, 1.5)
    return U([core, lip, pull])# ---- case_v8 WIRED: fine insulated leads from individual PCB lands ----
# No foam pads or direct skin contacts are used.

def electrode_pads():
    """Every separate J5/J6/J7/J11/J13 land with its exact PCB XY and net."""
    out = []
    for ref, site in WIRE_NETS.items():
        for pad_num, net in site.items():
            pad = next(p for p in fp(ref)['pads'] if str(p['num']) == pad_num)
            out.append((ref, pad_num, net, pad['pos'][0], pad['pos'][1],
                        pad['size'][0], pad['size'][1]))
    return out

# ---- case_v10 POGO: spring/snap contacts through skin-side apertures ----
# v22 restores the v7 contact scheme: no separate ECG lead wires; each land
# is reached by a pogo through its floor aperture (prompt §3).

POGO_FREE, POGO_WORK = 4.8, 4.0   # Harwin P70-5100045 (min working 3.85)

def pogo_pin(comp=POGO_FREE-POGO_WORK):
    """Pogo pin in assembled pose at a land XY; call site translates by
    (pad_x, pad_y, ZB). Rounded tip POLE lands at the nominal datum."""
    tip = trimesh.creation.icosphere(radius=0.45)
    tip.apply_translation([0, 0, -POGO_FREE+comp+0.45])
    plunger = cyl(0, 0, -POGO_FREE+comp+0.45, -POGO_FREE+comp+1.55, 0.45)
    barrel = cyl(0, 0, -3.9, 0.0, 0.57)
    tail = cyl(0, 0, -0.15, 0.05, 0.66)
    return U([tip, plunger, barrel, tail])

def contact_count():
    return len(electrode_pads())

def contacts_assembly():
    parts = []
    for ref, pad_num, net, x, y, _, _ in electrode_pads():
        p = pogo_pin()
        p.apply_translation([x, y, ZB])
        parts.append(p)
    s = make_slug(); s.apply_translation([20.2, 50.4, -0.6])
    parts.append(s)
    return U(parts)

def contact_force_n(): return 0.0

def base_contact_force_n(): return 0.0




def make_slug():
    f = cyl(20.2, 50.4, 0.0, 0.9, 1.85)
    f = U([f, cyl(20.2, 50.4, 0.9, 2.55, 1.45)])
    f = U([f, cyl(20.2, 50.4, 2.55, 2.7, 1.2)])
    return f


# ---------- TPU skin gasket ----------
def gasket():
    """0.4 mm TPU sheet over the sensor zone; holes at each aperture.
    v7: modelled in the true assembled pose - lying ON the skin face,
    proud z -0.4..0 (v5/v6 overlapped the floor solid by ~500 mm3)."""
    ap = aperture_polys()
    zone = sbox(-1.10, 33.0, 49.35, 65.45)  # extends past J13 pad row for edge margin
    flat = rrect(OX0+0.60, OY0+0.60, OX1-0.60, OY1-0.60, RO-0.60)
    sheet = zone.intersection(flat)
    holes = []
    for name, (x0, y0, x1, y1) in ap.items():
        holes.append(rbox(x0-0.1, y0-0.1, -0.5, x1+0.1, y1+0.1, 0.1))
    holes.append(cyl(28.936, 36.154, -0.5, 0.1, 2.25))   # mic port
    holes.append(cyl(20.2, 50.4, -0.5, 0.1, 1.65))       # slug (Ø3.0 face +0.3)
    # v22: J7/J6 apertures land 0.05 mm apart (x4.625|4.675) — merge the gap,
    # a 0.05 mm TPU web is unprintable. J3/J11 leave a 0.35 web — merge too.
    holes.append(rbox(4.0, 59.3, -0.5, 5.3, 64.3, 0.1))
    holes.append(rbox(45.0, 58.4, -0.5, 46.0, 61.3, 0.1))
    g = D(prism(sheet, -0.4, 0.0), U([m for m in holes if m.is_volume]))
    return g, sheet

# ---------- USB dust flap ----------
def usb_flap():
    """TPU dust flap in the assembled pose.
    v22: covers the widened grip notch (x23..36); tether relocated east of
    the corridor to the anchor at (37.5,-11.3)."""
    tongue = rbox(23.2, OY0-0.9, 5.0, 35.8, OY0+0.9, 8.3, r=0.4)
    lip = rbox(22.5, OY0-2.3, 4.4, 36.5, OY0-0.9, 8.9, r=0.5)
    neck = rbox(35.8, OY0-1.4, 6.0, 37.5, OY0-0.5, 7.6, r=0.4)
    tether = rbox(36.9, OY0-1.4, 6.0, 38.1, OY0-0.5, 7.6, r=0.4)
    pin = cyl(37.5, -11.3, ZLID-1.9, ZLID+0.5, 1.15)
    barb = cyl(37.5, -11.3, ZLID-2.3, ZLID-1.8, 1.45)
    return U([tongue, lip, tether, neck, pin, barb])

# ---------- battery door ----------
def battery_door():
    """PETG slide-in door for the bay cup: panel + pull tab + edge detent.
    v7: three grip ribs on the tab top for a sure finger pull."""
    cx0, cy0, cx1, cy1 = 2.65, 2.85, 31.95, 42.15
    cx = (cx0+cx1)/2
    # panel: engages the side grooves (z 5.15..6.05 in cup-local coords)
    panel = rbox(cx0+0.25, cy0-0.6, 5.25, cx1-0.25, cy1+0.4, 6.00, r=0.5)
    tab = rbox(cx-4.0, cy0-2.4, 5.35, cx+4.0, cy0-0.2, 6.00, r=0.5)
    ribs = [rbox(cx-3.0+i*3.0, cy0-2.2, 6.00, cx-2.2+i*3.0, cy0-0.4, 6.35)
            for i in range(3)]
    nub = cyl(cx+0.0, cy1+0.15, 5.4, 5.9, 0.6)   # leading-edge detent nub
    return U([panel, tab, nub]+ribs)

if __name__ == '__main__':
    import json as _j
    base = build_base(); top = build_top(); cup = bay_cup()
    gk, gzone = gasket()
    parts = [('case_base', base), ('case_top', top), ('bay_cup', cup),
             ('pin_plate', pin_plate()),
             ('button_cap', button_cap()), ('light_pipe', light_pipe()),
             ('plug', plug()), ('pogo_pin', pogo_pin()),
             ('contacts_assy', contacts_assembly()),
             ('thermal_slug', make_slug()), ('gasket', gk),
             ('usb_flap', usb_flap()), ('battery_door', battery_door())]
    for name, m in parts:
        m.export(name+'.stl')
        print(name, 'watertight=', m.is_watertight, 'vol=', round(m.volume, 1))
    _j.dump(FEATS, open('features.json', 'w'), indent=1)
    print('done')

