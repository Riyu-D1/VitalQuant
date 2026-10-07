"""
VitalQ chest-worn enclosure - initial parametric concept (build123d 0.13).

Run:  case/.venv/bin/python case/case.py
Outputs in case/: base/cover STEP+STL, assembly STEP, SVG renders + PNGs.

Case coords: X = width (across chest), Y = length, Z = up (skin side down).
Skin surface apex is z=0 and dips toward the edges (chest curvature, R_SKIN).
KiCad board coords are x 0..46, y -8..62  ->  case_x = board_x-23,
case_y = board_y-27. All mm. Everything parametric - board layout is not
final (Quilter), so tune the constants, not the code.
"""

import math
import subprocess
from pathlib import Path

from build123d import *
from build123d.exporters import Drawing, ExportSVG

OUT = Path(__file__).resolve().parent

# ------------------------------ board ------------------------------------
PCB_W, PCB_L, PCB_T = 46.0, 70.0, 1.6      # outline x:0..46  y:-8..62
PCB_CLR = 0.35                             # per-side cavity clearance
PCB_Y_OFF = 27.0                           # board-y centre in case coords
BOARD_R = 1.5                              # corner radius, model board only

# ------------------------------ shell ------------------------------------
WALL = 1.8          # side-wall thickness (FDM: ~4 perimeters @ 0.4 nozzle)
FLOOR_MIN = 1.4     # skin-floor thickness at centre (thinnest point)
TOP_WALL = 1.8      # cover thickness over the battery bay
CORNER_R = 5.0      # outer corner radius
R_SKIN = 150.0      # chest curvature radius across width (sternum ~140-180)
R_TOP = 150.0       # cover dome radius - same R -> near-uniform shell

# ------------------------------ stack -------------------------------------
STANDOFF_H = 1.8    # board bottom face height over the floor (sensor zone)
DECK_H = 3.5        # cavity headroom over board top, general area
SHELF_W = 1.7       # perimeter board-support ledge width
SHELF_Z0 = 2.4      # ledge bottom (leaves floor 1.4 + rib zone 1.0)

# ------------------------------ battery -----------------------------------
# 452535 pouch ~4.5x25x35 mm, ~350-400 mAh. A 502535 (5.0 mm, ~450 mAh)
# also fits - apex grows ~0.5 mm. Change BATT_T only.
BATT_T, BATT_W, BATT_L = 4.5, 25.0, 35.0
BATT_X0, BATT_Y0 = 10.5, 15.0      # board coords of cell lower-left corner
BATT_PAD = 0.4                     # foam/clearance inside the battery pocket

# ------------------------------ strap --------------------------------------
STRAP_SLOT_W = 27.0   # slot length along Y - for a 25 mm elastic band
STRAP_SLOT_H = 3.4    # band clearance
STRAP_RING_T = 1.7    # loop frame thickness
STRAP_PROJ = 2.6      # how far the loop stands off the wall
STRAP_Y = 0.0
STRAP_Z0 = 0.2        # slot bottom - band rides in the sag hollow

# ------------------------------ USB-C --------------------------------------
USB_X = 26.0          # board-x of J1 receptacle centreline (top -Y edge)
USB_W, USB_H = 10.0, 4.2
USB_Z0 = 0.3          # opening starts this far above board top

# --------------------------- skin openings ---------------------------------
# Rectangles in BOARD coords (x0,y0,x1,y1), cut clean through the floor.
SENSOR_WIN = (5.0, 42.0, 31.0, 57.5)     # optics + AFE cluster
# Electrode pads (J3 FSR, J5 ECG, J6 EDA, J7 BIOZ) + right-edge tail pads
# (J9-J13) all sit at/along the board's far edge: one open slot in the
# +Y end wall bottom edge carries them to the skin / cable exits.
END_NOTCH = (4.0, 45.0, -4.0, 2.2)      # x0, x1 (board), z0, z1 (case)

# ---------------------------- board mounting -------------------------------
# v1 board M2 holes (board coords) -> locating pins on standoff pegs.
PEG_POS = [(2.6, 0.3125), (1.85, -4.6844)]
PEG_D, PEG_PIN_D, PEG_PIN_H = 4.6, 1.9, 1.7

# ------------------------------ screws -------------------------------------
# 4x M2 self-tapping screws through corner ears (cover: clearance + cbore).
EAR_D = 7.0
EAR_R_OUT = 5.0       # ear-centre distance out along the corner diagonal
SCREW_PILOT_D = 1.7   # self-tap pilot in the base ear
SCREW_CLEAR_D = 2.5   # through-hole in cover ear
SCREW_BORE_D = 4.6    # M2 head counterbore
SCREW_BORE_H = 2.2

# ------------------------------ misc ---------------------------------------
RIB_Y = (-20.0, 30.0)  # case-y positions of cover ribs pressing the board
RIB_T = 2.0

# ------------------------------ derived ------------------------------------
CW = PCB_W + 2 * PCB_CLR                     # cavity width   46.7
CL = PCB_L + 2 * PCB_CLR                     # cavity length  70.7
OW = CW + 2 * WALL                           # outer width    50.3
OL = CL + 2 * WALL                           # outer length   74.3
CR_IN = max(CORNER_R - WALL, 1.0)

Z_FLOOR = FLOOR_MIN                          # flat inner floor face   1.4
Z_BRD_BOT = Z_FLOOR + STANDOFF_H             # board bottom            3.2
Z_SEAM = Z_BRD_BOT + PCB_T                   # board top / split line  4.8
Z_DECK = Z_SEAM + DECK_H                     # deck ceiling            8.3
BAY_H = BATT_T + BATT_PAD                    # bay pocket depth above board
Z_BAY = Z_SEAM + BAY_H                       # bay ceiling             9.7
Z_APEX = Z_BAY + TOP_WALL                    # cover apex             11.5

SAG = R_SKIN - math.sqrt(R_SKIN**2 - (OW / 2) ** 2)   # ~2.13 edge dip


def bcx(x):
    """board x -> case x"""
    return x - PCB_W / 2


def bcy(y):
    """board y -> case y"""
    return y - PCB_Y_OFF


def rounded_box(w, l, r, z0, z1):
    return Pos(0, 0, z0) * extrude(RectangleRounded(w, l, r), amount=z1 - z0)


def y_cyl(radius, z_axis, length=3 * OL):
    """Cylinder with its axis along Y, centred at height z_axis."""
    return Pos(0, 0, z_axis) * Rot(90, 0, 0) * Cylinder(radius=radius, height=length)


# Ear centres sit on each corner's 45-deg diagonal, EAR_R_OUT from the
# corner-arc centre (ear bulges ~EAR_R_OUT+EAR_D/2-CORNER_R outside the arc).
EAR_POS = [
    (sx * (OW / 2 - CORNER_R + EAR_R_OUT / math.sqrt(2)),
     sy * (OL / 2 - CORNER_R + EAR_R_OUT / math.sqrt(2)))
    for sx in (-1, 1) for sy in (-1, 1)
]


# =========================== BASE (skin half) ==============================
def build_base():
    # outer body + screw ears, then cut by the chest-curvature cylinder:
    # the cylinder's upper nappe leaves the skin face dipping ~SAG at edges.
    base = rounded_box(OW, OL, CORNER_R, -SAG - 0.6, Z_SEAM)
    for ex, ey in EAR_POS:
        base += Pos(ex, ey, -SAG - 0.6, ) * Cylinder(
            EAR_D / 2, Z_SEAM + SAG + 0.6, align=(Align.CENTER, Align.CENTER, Align.MIN))
    base -= y_cyl(R_SKIN, -R_SKIN)

    # interior cavity up to the seam
    base -= rounded_box(CW, CL, CR_IN, Z_FLOOR, Z_SEAM + 0.05)

    # perimeter shelf the board rests on (top face = board bottom plane)
    shelf = rounded_box(CW, CL, CR_IN, SHELF_Z0, Z_BRD_BOT) - rounded_box(
        CW - 2 * SHELF_W, CL - 2 * SHELF_W,
        max(CR_IN - SHELF_W, 0.5), SHELF_Z0, Z_BRD_BOT)
    # relieve shelf under the right-edge tail pads and far-edge electrode pads
    shelf -= Pos(bcx(45.0), bcy(37.5), Z_BRD_BOT - 0.4) * Box(3.2, 50.0, 1.8)
    shelf -= Pos(bcx(18.0), bcy(59.0), Z_BRD_BOT - 0.4) * Box(28.0, 7.0, 1.8)
    base += shelf

    # standoff pegs + locating pins into the board's M2 mounting holes
    for px, py in PEG_POS:
        base += Pos(bcx(px), bcy(py), Z_FLOOR) * Cylinder(
            PEG_D / 2, Z_BRD_BOT - Z_FLOOR, align=(Align.CENTER, Align.CENTER, Align.MIN))
        base += Pos(bcx(px), bcy(py), Z_BRD_BOT) * Cylinder(
            PEG_PIN_D / 2, PEG_PIN_H, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # sensor window through the skin floor
    x0, y0, x1, y1 = SENSOR_WIN
    base -= Pos(bcx((x0 + x1) / 2), bcy((y0 + y1) / 2), -SAG - 0.6) * Box(
        x1 - x0, y1 - y0, Z_FLOOR + SAG + 0.8, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # electrode / tail exit notch through the +Y end wall bottom edge
    nx0, nx1, nz0, nz1 = END_NOTCH
    base -= Pos((bcx(nx0) + bcx(nx1)) / 2, OL / 2 - 2.5, nz0) * Box(
        bcx(nx1) - bcx(nx0), 6.0, nz1 - nz0, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # strap loops: closed rings on both side walls, band passes behind case
    zc = STRAP_Z0 + STRAP_SLOT_H / 2          # slot centre z
    ring_out = Box(STRAP_PROJ, STRAP_SLOT_W + 2 * STRAP_RING_T,
                   STRAP_SLOT_H + 2 * STRAP_RING_T)
    slot = Box(STRAP_PROJ + 1.0, STRAP_SLOT_W, STRAP_SLOT_H)
    for sx in (-1, 1):
        ring = Pos(sx * (OW / 2 + STRAP_PROJ / 2), STRAP_Y, zc) * ring_out
        ring -= Pos(sx * (OW / 2 + STRAP_PROJ / 2 + 0.5), STRAP_Y, zc) * slot
        base += ring

    # M2 self-tap pilots in the ears
    for ex, ey in EAR_POS:
        base -= Pos(ex, ey, Z_SEAM - 6.0) * Cylinder(
            SCREW_PILOT_D / 2, 6.1, align=(Align.CENTER, Align.CENTER, Align.MIN))

    return base


# ============================ COVER (top half) =============================
def build_cover():
    cover = rounded_box(OW, OL, CORNER_R, Z_SEAM, Z_APEX + 0.6)
    for ex, ey in EAR_POS:           # ears first so the dome trims them too
        cover += Pos(ex, ey, Z_SEAM) * Cylinder(
            EAR_D / 2, Z_APEX + 0.6 - Z_SEAM, align=(Align.CENTER, Align.CENTER, Align.MIN))
    cover &= y_cyl(R_TOP, Z_APEX - R_TOP)      # domed top face (dips at edges)

    # cavity: shallow deck over the whole board + deeper battery pocket
    cover -= rounded_box(CW, CL, CR_IN, Z_SEAM - 0.01, Z_DECK)
    bx0, by0 = bcx(BATT_X0), bcy(BATT_Y0)
    bx1, by1 = bcx(BATT_X0 + BATT_W), bcy(BATT_Y0 + BATT_L)
    cover -= Pos((bx0 + bx1) / 2, (by0 + by1) / 2, Z_SEAM) * Box(
        (bx1 - bx0) + 2 * BATT_PAD, (by1 - by0) + 2 * BATT_PAD,
        BAY_H + 0.02, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # flush seam - the four M2 corner screws locate the cover
    # (skirt/lip deliberately omitted: side walls carry the strap rings)

    # ribs pressing the board top (clear of the battery pocket)
    for ry in RIB_Y:
        cover += Pos(0, ry, Z_SEAM + 0.05) * Box(
            CW - 0.4, RIB_T, Z_DECK - Z_SEAM - 0.05,
            align=(Align.CENTER, Align.CENTER, Align.MIN))

    # USB-C opening through the -Y end wall
    cover -= Pos(bcx(USB_X), -OL / 2 + 1.0, Z_SEAM + USB_Z0) * Box(
        USB_W, WALL + 2.0, USB_H, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # cover screw holes + counterbores in the ears (dome lowers ear tops)
    ear_top = Z_APEX - (R_TOP - math.sqrt(R_TOP**2 - (EAR_POS[0][0]) ** 2))
    for ex, ey in EAR_POS:
        cover -= Pos(ex, ey, Z_SEAM - 0.05) * Cylinder(
            SCREW_CLEAR_D / 2, ear_top - Z_SEAM + 0.3,
            align=(Align.CENTER, Align.CENTER, Align.MIN))
        cover -= Pos(ex, ey, ear_top - SCREW_BORE_H) * Cylinder(
            SCREW_BORE_D / 2, SCREW_BORE_H + 1.0,
            align=(Align.CENTER, Align.CENTER, Align.MIN))

    return cover


# ========================= dummies for renders ============================
def build_pcb():
    return Pos(0, 0, Z_BRD_BOT) * extrude(
        RectangleRounded(PCB_W, PCB_L, BOARD_R), amount=PCB_T)


def build_battery():
    return Pos(bcx(BATT_X0 + BATT_W / 2), bcy(BATT_Y0 + BATT_L / 2),
               Z_SEAM + 0.05) * Box(BATT_W, BATT_L, BATT_T,
                                    align=(Align.CENTER, Align.CENTER, Align.MIN))


# ------------------------------ renders ------------------------------------
def render(shapes_layers, name, look_from, look_up=(0, 0, 1), hidden=False):
    """HLR-project -> SVG -> PNG via qlmanage. shapes_layers: [(shape,color)]."""
    svg = ExportSVG(scale=1.0, margin=4.0)
    for i, (shape, color) in enumerate(shapes_layers):
        layer = f"p{i}"
        svg.add_layer(layer, line_color=color, line_weight=0.25)
        d = Drawing(shape, look_from=look_from, look_up=look_up,
                    with_hidden=hidden)
        svg.add_shape(d.visible_lines, layer=layer)
    svg_path = OUT / f"{name}.svg"
    svg.write(svg_path)
    subprocess.run(["qlmanage", "-t", "-s", "1600", "-o", str(OUT),
                    str(svg_path)], capture_output=True)
    png = OUT / f"{name}.svg.png"
    if png.exists():
        png.rename(OUT / f"{name}.png")


def main():
    base = build_base()
    cover = build_cover()
    pcb = build_pcb()
    batt = build_battery()

    export_step(base, OUT / "case_base.step")
    export_step(cover, OUT / "case_cover.step")
    export_stl(base, OUT / "case_base.stl")
    export_stl(cover, OUT / "case_cover.stl")
    export_step(Compound(children=[base, cover, pcb, batt]),
                OUT / "case_assembly.step")

    asm = Compound(children=[base, cover, pcb, batt])
    layers = [(asm, (0.25, 0.25, 0.25))]
    render(layers, "case_iso", (0.9, -1.0, 0.55))
    render(layers, "case_top", (0.01, -0.02, 1))
    render(layers, "case_bottom", (0.01, -0.02, -1), look_up=(0, 1, 0))
    half = asm & Pos(-100, 0, -50) * Box(200, 200, 200)
    render([(half, (0.25, 0.25, 0.25))], "case_section", (1, 0, 0.08))

    print(f"outer: {OW:.1f} x {OL:.1f} mm   apex z={Z_APEX:.1f}  sag={SAG:.2f}")


if __name__ == "__main__":
    main()
