# VitalQ case_v10 — PRINT NOTES

## Parts

| File | Part | Material | Print orientation / notes |
|---|---|---|---|
| `case_base.stl` | Skin-side base shell | PETG (or TPU 95A for skin comfort) | **skin-down** on plate, as drawn |
| `case_top.stl` | Top shell + skirt + strap lugs | PETG | **top-down** (lid face on plate, interior up) |
| `bay_cup.stl` | Battery tray (snaps onto lid) | PETG | **bottom-down** (flat floor on plate) |
| `pin_plate.stl` | Snap-pin retainer for bay cup | PETG | **pins-up** (plate on plate) |
| `battery_door.stl` | Slide-in door for bay cup | PETG | flat side down |
| `gasket.stl` | Skin-side seal sheet over sensor zone | TPU 95A | flat (0.4 mm — 2 layers @0.2) |
| `usb_flap.stl` | USB-C dust flap + tether pin | TPU 95A | flat, tongue face down |
| `button_cap.stl` | SW1/SW2 caps (print ×2) | TPU 95A (PETG OK) | tip-down on plate |
| `light_pipe.stl` | D25 status light pipe | Clear PETG / clear resin | dome-up |
| `plug.stl` | Reusable plug for the J8 window | TPU | flat |
| `contacts_assy.stl` | 20 spring pogos + thermal slug, assembled pose | — | verification/render only |
| `pogo_pin.stl` | single pogo reference model | — | verification/render only |
| `thermal_slug.stl` | U11 thermal insert | C110 copper, Ni/Au plated | machining model, not printed |
| `case_v10.step` | whole assembly (faceted solids) | — | CAD exchange, not printed |

All meshes are watertight (manifold3d checked) and dimensioned in mm.

## Print settings (FDM)

- Layer height: **0.2 mm** max (0.12–0.15 preferred for the snap beads,
  barbs and gasket). The overhang audit is run at 0.2 mm pitch.
- Walls: 4 perimeters (1.6 mm nozzle-width equivalent), top/bottom 6 layers
- Infill: 20–30 % gyroid for the shells; strap lugs benefit from 40 %
- Nozzle: 0.4 mm standard; min feature is 0.4 mm (gasket webs 0.45)
- **Supports: none required** — the voxel overhang audit (0.2 mm layers,
  45° cone) reports 0 unsupported cells for every printed part in the
  orientations above. Overhangs are either ≤2 mm ledges, ≥45° sloped
  faces (V-floors, wedge beads, gabled slot ceilings, chamfered mouths)
  or open-to-plate faces in the flipped lid print.
- Brim: optional 3 mm on `case_top` (large plate contact — not needed).
- Material: **PETG** for the shells/cup/plate/door (slight flex for the snap fits);
  **TPU 95A** for gasket, flap and button caps (print TPU slow, 20 mm/s,
  tuned retraction).

## Snap fits (designed + strain-verified)

- **Lid→base:** 8 wedge snap beads on the lid skirt (2 extra flank the
  USB opening on −y), 45° top+bottom slopes, bead tip 0.35 mm into the
  0.55 mm base groove; skirt ring t≈0.6 flexes — worst strain **1.0 %**
  (0.65 hoop + 0.35 bend) vs PETG's ~5 % yield. Per-bead engagement is
  ray-verified (≥0.55 mm³ in-groove each). Pry notches at the −x edge
  (y≈0.2) and +y edge (x≈39.2) cut the skirt only — the wall behind is
  sealed; lever the lip to reopen.
- **Cup→lid:** `pin_plate` — six Ø3.2 hollow pins (2 prongs each) press
  through the cup floor holes + lid holes; wedge barbs catch the lid's
  cavity face (cup-z −1.65) with 0.30 mm engagement. Prong strain
  **2.9 %**, 12 prongs total. Reusable; pull the plate's -y tab to
  release all six pins at once.
- **Battery door:** slides in x-side wall grooves (cup-z 5.15–6.05)
  from the −y entry slot; leading-edge Ø1.2 nub detents into the +y
  interior notch.
- **Button caps:** TPU barb clicks into the 8.35–8.9 counterbore;
  ~0.4 mm travel.

## Tolerances / fits (designed)

- Board ↔ pocket clearance: **0.30 mm** per side (grown-board check: 0 hit)
- H1/H2 locating pins Ø1.9 through the PCB's Ø2.2 mounting holes
- Button cap pin: ~0.4 mm travel, −0.05 preload vs SW top
- The skin face carries **20 pogo apertures** (J5×5, J6×4, J7×4, J11×3,
  J13×2, J3×2) — each a through-slot with a 45° skin-mouth chamfer and a
  cavity-side 45° self-centring funnel for the spring pins
  (Harwin P70-5100045 class, tip 0.6 mm proud). Optical, microphone,
  humidity and thermal-slug openings are unchanged in function. No
  loose-lead routing exists; J2's battery pair still uses its floor
  groove, lid feed-through and two tie posts.
- The U11 thermal slug face independently sits 0.6 mm proud (floor 1.6 mm;
  B surface at z = 3.4 mm).
- Corner reliefs Ø3.4 at all four board corners (sharp-cornered PCB in a
  R2-radius pocket)
- FDM process capability is typically ±0.1–0.2 mm; all designed clearances
  absorb this. If your printer is badly calibrated, test the snap fit on a
  20 mm section first (slice the top 6 mm off both shells).

## Post-processing

- Deburr skin-side aperture edges lightly (240 grit); preserve the closed floor
  and inspect the optical/slug openings for burrs.
- The gasket is 0.4 mm TPU: wipe the plate, use a brim if corner lift is
  seen, and don't stretch the webs (0.45 mm min between holes).
- Mesh/membrane patches (Ø4.4 mic, Ø4.6 U23) drop into the cavity-side
  recesses; U13's lid vent is an open Ø1.5 bore — keep it clear.
- Light pipe: press-fit, add a drop of clear CA if loose.

## Assumptions

- Board stackup **1.5386 mm** real (checked) / **1.6 mm** worst case —
  clearances pass at both. Board outline is the V22 Edge.Cuts shape:
  55.7 × 81.1 mm incl. the north antenna tab (y −16.55) and east power
  tab (x 53.65), R1.7 corner reliefs at all eight corners.
- Battery: **402535 LiPo pouch** (≤24.3 × 35 × 4.0 mm, ~300–350 mAh) on
  the pin plate inside the 26.2 × 36.0 mm cup interior; door caps the top.
- Antenna keepout: the −y wall bulge over the ESP32 trace antenna
  (module overhangs to y −22.31) is a thin-wall pocket reaching
  y −24.9 / x 0.5–22.8 — no metal fasteners or paint there. Its east
  face is capped at x 22.8 to keep the USB-C grip corridor clear.
- **USB-C grip corridor**: the −y wall is a through-notch over
  x 22.7–36.0 (J1 moved east to x 29.5 in V22) — the 12.4 × 6.5 mm plug
  overmould slides fully home with 0.000 mm³ interference. The dust
  flap anchors east of the corridor.
- All external radii ≥ 2 mm (corners R3.0); lug fins: R1.2 plan corners +
  45° chamfers on all strap-bearing edges (fin bottom, bar bottom, bar
  top, channel inboard top) + R0.8 channel end walls so the elastic
  can't fray; skin edge is a 45° chamfer + bezier loft — no sharp edges.
  A 0.5 mm debossed chevron plus an ECG pulse mark on the lid's −y chin
  marks the +y wear direction; a 0.4 mm drip-groove ring around the
  bay-cup footprint sheds sweat off the cup seam (all plate-face grooves
  in the flipped print).
- Skin-side finish: every aperture mouth is chamfered (rect openings +
  mic/U23/slug). The 20 electrode apertures add cavity-side 45°
  self-centring funnels. Spring pogos seat in the bores — confirm tail
  contact method and electrical isolation on hardware.
- Envelope: **64.8 × 92.2 × 10.7 mm** shell (+ 6.2 mm bay cup → ~16.4 mm
  assembled on-chest, ≤26.6 g of printed plastic at solid volume —
  ~20 g typical with infill).
