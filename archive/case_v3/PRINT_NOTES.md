# VitalQ case_v3 — PRINT NOTES

## Parts

| File | Part | Material | Notes |
|---|---|---|---|
| `case_base.stl` | Skin-side base shell | PETG (or TPU 95A for skin comfort) | print face-down (skin side on plate) |
| `case_top.stl` | Top shell + battery blister + strap lugs | PETG | print lid-down on plate (interior up) |
| `button_plunger.stl` | SW1/SW2 plungers (print ×2) | PETG or TPU | snap into top-shell holes |
| `light_pipe.stl` | D25 status light pipe | Clear PETG / clear resin / transparent filament | press-fit Ø2.8 pocket |
| `plug.stl` | Reusable TPU plug for J8/J7/J3 windows | TPU | press into lid openings |
| `pogo_pin.stl` | P70-5100045 reference model | — | NOT printed — buy the part |
| `thermal_slug.stl` | U11 thermal insert | C110 copper, Ni/Au plated | machining drawing, not printed |
| `contacts_assy.stl` | all contacts in position | — | verification/render only |

All meshes are watertight (manifold3d checked) and dimensioned in mm.

## Print settings (FDM)

- Layer height: **0.15 mm** (0.12–0.2 OK; keep ≤0.2 for the snap beads)
- Walls: 4 perimeters (1.6 mm nozzle-width equivalent), top/bottom 6 layers
- Infill: 20–30 % gyroid for the shells; strap lugs benefit from 40 %
- Nozzle: 0.4 mm standard
- **Supports:** none required — all overhangs ≤45° or bridged; the snap
  groove and skirt print cleanly with the orientations above.
- Brim: 3 mm brim on `case_top` recommended (tall blister walls).
- Material: **PETG** preferred (skin-safe, sweat/oil resistant, slight flex
  for the snap fit). TPU 95A is an option for the base if skin comfort is a
  priority; print TPU slower (20 mm/s) with retraction tuning.

## Tolerances / fits (designed)

- Board ↔ pocket clearance: **0.30 mm** per side
- Snap engagement: bead Ø2.5 × 0.5 catch depth, 6 segments, 0.12 mm nominal
  interference — flexes on assembly, stays closed without tools
- Screw bosses: H1/H2 locating pins Ø1.9 through the PCB's Ø2.2 NPTH
  mounting holes; optional M2 self-tap screws clamp top→boss
- Button plunger: 0.15 mm radial clearance in guide bore, 0.05 mm preload,
  0.40 mm available travel (SW actuation ~0.25–0.3 mm)
- Skin contact is by **insert parts** (CONTACTS_BOM.md): 14× Harwin
  P70-5100045 pogos reach z = −0.6 mm at working height through the floor
  apertures; the U11 thermal slug face is flush at z = 0
  (floor 1.6 mm; B surface at z = 3.4 mm)
- Corner reliefs Ø3.4 at all four board corners (sharp-cornered PCB in a
  R2-radius pocket)
- FDM process capability is typically ±0.1–0.2 mm; all designed clearances
  absorb this. If your printer is badly calibrated, test the snap fit on a
  20 mm section first (slice the top 6 mm off both shells).

## Post-processing

- Deburr aperture edges on the skin side (light sand 240 grit) — the
  electrode windows must sit flat against skin.
- The optical-well light barriers are thin (0.8 mm webs): inspect for stringing
  between D10/D11/D12 emitters and the U16/U10 detectors.
- Light pipe: press-fit, add a drop of clear CA if loose.
- Acoustic mic port Ø1.6 in the base must align with the PCB's Ø0.8 NPTH
  sound hole at (28.94, 36.15) — a foam gasket ring (Ø3.8 counterbore,
  0.7 mm deep) is moulded in the floor; use a Ø4/Ø0.5 adhesive acoustic
  gasket or a thin silicone dot.

## Assumptions

- Board thickness 1.6 mm nominal (fab drawing); board outline is exactly
  the Edge.Cuts rectangle (−2.0,−10.5)–(48.0,64.5) with sharp corners.
- Battery: **402535 LiPo pouch** (25 × 35 × 4 mm, ~300–350 mAh) — bay is
  25.8 × 35.8 mm (+0.4 mm clearance) × 4.6 mm deep in the top blister.
  A 502030 (30 × 20 × 5 mm) also fits diagonally but is not the reference.
- Antenna keepout: the wall over the ESP32 trace antenna (x 3.7–20.7,
  y −14.0–−10.55) is a **1.0 mm thin-wall blister** — no ribs, no snap
  bead, no screw boss inside it. Do not add metal fasteners or paint there.
- All interior radii ≥ 2 mm except the corner-relief drill (R1.7) and
  snap-bead edge (R0.6 functional).
- Total wall envelope: 50.0×75.0 board → 55.6×80.3×9.8 mm case
  (+4.6 mm battery blister locally), i.e. ~14.4 mm max thickness on-chest.

## Disk / tool footprint

- Python 3.12 + numpy/scipy/trimesh/manifold3d/matplotlib (~95 MB total,
  under the 400 MB budget — no CAD install needed).
- KiCad 10.0.4 (pre-installed) used read-only for extraction + VRML export.
- Free disk at build time: ~2.0 GB (logged each run via `df -h`).
