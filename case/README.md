# VitalQ chest-worn enclosure — initial concept

Parametric build123d concept for the VitalQ PCB (46 × 70 × 1.6 mm, 6-layer,
skin-facing sensors on the B/bottom side). Board placement is **not final**
(Quilter AI is laying it out) — every dimension lives in the constants block
at the top of `case.py`; window/connector positions are in *board
coordinates* so they track the layout, not the case.

## Overall shape

- **Two-part shell**: skin-side **base** + domed **top cover**, flush seam at
  the board's top surface (z = 4.8), closed by **4× M2 self-tapping screws**
  through corner ears (Ø1.7 pilots in the base, Ø2.5 clear + Ø4.6 × 2.2
  counterbore in the cover).
- **Size**: ~50.3 × 74.3 mm body; **55.5 × 78.4 mm** over strap loops and
  screw ears; **~11.5 mm** shell height at the apex (fits the 10–12 mm
  low-profile target with a 452535 cell; a 502535 5 mm cell pushes to ~12).
- **Curved skin face**: cylindrical chest-following arc, `R_SKIN = 150 mm`
  across the width → ~2.1 mm edge dip. Cover top uses the same radius so the
  shell keeps near-uniform thickness.
- **Rounded everything**: 5 mm plan corners, no sharp edges.

## What's inside

- **PCB** rests on a 1.7 mm perimeter shelf (top = board bottom, sensor
  clearance 1.8 mm) and is located by **2 standoff pegs** with Ø1.9 pins into
  the board's M2 holes (`PEG_POS`, v1 positions — parametric since Quilter
  may move them). Two cover ribs press the board down.
- **Battery bay**: deeper pocket in the cover over the board, sized for a
  **452535 LiPo pouch (~4.5 × 25 × 35 mm, ~350–400 mAh)** + 0.4 mm foam
  clearance. Default position board-x 10.5–35.5, y 15–50 — keeps clear of
  the ESP32 antenna end and USB-C.
- **USB-C opening** in the −Y end wall, aligned to J1 (board x = 26):
  10 × 4.2 mm, starting 0.3 mm above board top.
- **Skin-side openings (placeholders)**:
  - `SENSOR_WIN` — window under the optics/AFE cluster (board 5–31 × 42–57.5:
    AS7341, SFH7072, MLX90632, TMP117, MAX86141, LEDs) so sensors sit flush.
  - `END_NOTCH` — open slot in the +Y end-wall bottom edge carrying the
    electrode pads (J3 FSR, J5 ECG, J6 EDA, J7 BIOZ) and the tail pads
    (J9–J13) to skin/cables.
- **Strap loops**: closed rings on both long walls, 27 × 3.4 mm slots for a
  ~25 mm elastic chest band; the band runs behind the case in the curvature
  hollow so strap tension presses the patch to the chest.

## Stack-up (centreline, z)

    skin apex      0.0
    floor top      1.4   (FLOOR_MIN; edges are ~2.1 mm thicker from the arc)
    board bottom   3.2   (standoff 1.8 for B-side sensors, ~1.9 mm packages OK)
    board top/seam 4.8
    deck ceiling   8.3   (3.5 mm component headroom)
    bay ceiling    9.7   (4.5 mm cell + 0.4 pad)
    cover apex    11.5   (1.8 mm top wall)

## Debossed emblem (cover)

The cover top face carries the chosen VitalQ mark — the **QRST monogram**
(~/Downloads/VitalQ_Logo_v1 `c1_qrst`, reversed style: Q ring + "key-like"
PQRST tail) — cut **into** the dome (deboss, not emboss).

- Params: `EMBLEM_SIZE = 18 mm` overall, `EMBLEM_X/Y = (0, -23)` — centred
  in X, shifted toward the USB end so the whole footprint clears the
  battery pocket (pocket starts at y = -12.4).
- `EMBLEM_DEPTH = 0.6 mm`, uniform over the curved dome: the groove floor
  is built by intersecting the emblem footprint column with a copy of the
  cover offset down by the depth, so every point sits 0.6 mm under the
  local surface rather than at a flat depth.
- Min wall under the grooves: **~2.4 mm** (deck ceiling z=8.3 below;
  pocket never closer than ~0.3 mm in plan) — verified by probe columns
  along the groove centrelines; spec ≥ 1.0 mm.
- Min feature width: tail stroke 1.5 mm, ring 3.1 mm — well over the
  0.5 mm FDM floor. Prints rim-down, no supports (grooves face up).
- Note: boolean quirk documented in `case.py` — footprint prisms must stay
  plain CCW solids (a holed `Part` silently fails `&`; a CW polygon
  extrudes downward).

## FDM notes

- Walls 1.8 mm (~4×0.4 perimeters), floor 1.4 mm at centre — all ≥ 3 shells.
- **Print base skin-face down** with a brim (the face is gently curved);
  cavity features face up, no supports. Strap rings print vertically.
- **Print cover rim-down** (dome up, shallow 2°-class slopes — no supports);
  battery pocket is a vertical recess — self-supporting.
- Fits: board–cavity 0.35 mm/side, battery 0.4 mm around, band slot 27 × 3.4,
  USB opening 10 × 4.2. Screws: M2×~8 self-tapping.

## Known placeholders / next rev

- Sensor/electrode openings are plain cutouts — replace with gasketed
  windows + electrode contact geometry once Quilter's layout lands.
- The end notch is one wide slot; split into per-pad slots or add a dust lip.
- No display/LED/button openings yet (SW1/SW2 parked in current layout).
- Curvature is width-only (cylindrical); sternum longitudinal curve could
  be added the same way on Y.
- Ear counterbores land on the domed surface — flat-bottomed to 2.2 mm at
  the lowest dome point; verify screw head flush after shrink wrap.

## Files

`case.py` (parametric source), `case_base.*`, `case_cover.*` (STEP+STL),
`case_assembly.step`, `case_{iso,top,bottom,section,emblem}.{svg,png}`,
`vitalq_pcb.step` (reference board, exported from VitalQ_Quilter_v6).

Run: `case/.venv/bin/python case/case.py` — rebuilds everything in `case/`.
