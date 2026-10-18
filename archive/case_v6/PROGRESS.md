# case_v6 progress — COMPLETE

**State: 106/106 checks PASS. FIT_REPORT.md ends with CASE6 COMPLETE.**

## Refinement round (post-strap)

- All three strap-bearing outboard edges of each lug are now
  45°-chamfered (fin bottom, bar bottom, bar top) — before, only the
  top edge was eased. Envelope tightened a hair to 62.1 mm.
- Debossed orientation chevron (0.5 mm) on the lid's −y chin points the
  +y wear direction — prints as grooves in the plate-facing layers.
- Strap render model upgraded to a realistic hairpin wrap (low leg
  through the channel, outboard riser, fold-back over the bar), plus a
  new `render_worn.png`: pod seated on an R95 chest cylinder with the
  strap tails drooping to the surface.
- New checks: `strap span coverage` (30.8/78.8 mm = 39%),
  `lid orientation chevron` (ray probe), `printed mass` (24.4 g solid
  bound).

## What changed vs case_v5

Chest-strap integration (see STRAP_DESIGN.md for the researched decision):

- Replaced the four narrow v5 staples (2 per side, ~13 mm channel) with
  **one open-staple slot lug per side sized for a 30 mm elastic chest
  strap** — the common Polar/Garmin-style replacement width.
- Each lug: solid fin (plan corners R1.2) minus the channel void →
  two 3.6 mm end posts + a 2.7 mm top bar the strap loops; 45° chamfer
  on the outboard top edge so the wrap edge can't fray the elastic;
  0.2 mm pinch gap (post bottom z4.55 vs skirt top z4.35) retains the
  loop. Envelope unchanged: 62.2 x 82.2 x 9.6 mm.
- Alternatives documented and rejected: 45 mm snap studs (needs metal
  hardware; studs only earn their keep when they double as electrodes —
  ours are pogo pins), closed slot ring (can't fit 2+3+2 mm chords in
  the 5.25 mm wall band), v5's narrow staples (no standard strap fits).
- Skin face stays flat by decision: a camber would unload the edge-row
  contacts (J7 x1–4, J3 x42–47); even pressure comes from the flat face
  + TPU gasket + 0.8 mm pogo stroke reserve. Pull line within 0.5 mm of
  the face plan centroid (verified).

## New verifier checks (all passing)

- `strap slot L/R` — channel interior 30.8 x 2.35 mm, 0.00 mm3 interference
- `strap slot width` — 30.8 ≥ 29.5 for a 30 mm strap
- `L/R post t` — ray-measured post thickness 3.60 mm (≥2 required)
- `L/R bar t` — ray-measured bar thickness 2.70 mm
- `strap bar stress @40N` — 32.3 MPa worst-case point load
  (16.2 distributed) vs 40 MPa limit
- `strap post shear @40N` — 1.2 MPa
- `strap pull vs face centroid` — |dy| = 0.5 mm
- `strap pinch gap` — loop retained
- `skin contacts proud` — all 21 contacts at z = −0.60 mm

## Deliverables

- `build_case.py` — new lug block (all other geometry unchanged)
- `verify_fit.py` — 103 checks, all PASS → **CASE6 COMPLETE**
- `case_v6.step` — faceted-BREP STEP, 13 named solids, assembled pose
- 8 renders: assembled (with strap), top, skin face, exploded,
  sections through the lug (y27.5), J7 (y62.9), J3 (y59.85), USB-C (x26)
- `STRAP_DESIGN.md` (new), `ASSEMBLY.md` (§5 strap threading),
  `PRINT_NOTES.md`, `CONTACTS_BOM.md` updated to v6

fix_v9 board untouched (read-only, SHA unchanged). No commits/pushes.
