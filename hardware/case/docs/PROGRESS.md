# case_v10 progress — COMPLETE

**State: 140/140 checks PASS. FIT_REPORT.md ends with CASE V10 COMPLETE.**

Baseline: `case_v9` scripts (unverified v21 adaptation). Board:
`../fix_v22/vitalq_v2.kicad_pcb` — 55.7×81.1 mm incl. the north antenna
tab (y −16.55) and east power tab (x 53.65); J1 USB-C moved east to
(29.5, −7.8). Contact scheme: **v7-style pogo electrodes restored**
(20 spring pogos through floor apertures + U11 thermal slug); the v8
wired-electrode raceways are gone.

## Geometry pipeline

- `extract_geometry.py` repointed at a /tmp copy of fix_v22 →
  `geometry.json` (bbox exactly matches prompt §2).
- VRML unit bug fixed in `parse_vrml.py` (v22 exports 0.1-in children
  with a 0.1 root scale; mm scale forced).
- `merge_heights.py` centre offset now derived from the board bbox
  (was hard-coded for the v18 outline) → 0 unmatched 3D models.
- `DELTA_V22.md` documents the outline/feature deltas vs case_v8.

## v22 changes vs the v8 baseline

- Outline constants re-derived (OX/OY, tabs): envelope
  64.8 × 92.15 × 10.65 mm.
- **J1 east move**: window centred x 29.5; grip corridor opened through
  the −y wall x 22.7..36.0, through-notch in base+lid; verified 12.4×6.5
  overmould slides fully home (0.000 mm³ at final check).
- Antenna bulge extended to y −24.9 (module overhangs to −22.31 +
  1.0 mm air gap); east face capped x 22.8 (corridor edge 23.3); pocket
  widened to x 1.55..21.45 for the module's 0.3 clearance band.
- Dust-flap anchor relocated out of the corridor; flap tongue covers
  the wider mouth.
- Snap beads re-placed: +x bead span y31..37.5 (clears the E-tab
  cavity); −y beads moved clear of the corridor.
- **R strap lug redesign**: E-tab bulge (wall to x55.5) + tall tab
  comps (Q6/D33 top ~6.14) block the staple → strap channel raised to
  z 6.5..8.5 as a closed tunnel bored through the bulge wall + ledge
  (x 49.4..57.8). Fin mid-section stays x≥53.0 (outboard of the pocket
  edge, clears comp boxes); bar 10.65−8.55 = 2.1 mm on a 4.8 width →
  σ=38.2/19.1 MPa @40 N.
- Top-shell outline pre-mirrored before the π-rotation loft (fixes a
  phantom +y bulge that cost 5.3 mm of envelope).
- Lid deboss checks (chevron/drip/ECG) now reference ZLID=10.15
  dynamically.
- Gasket hole merges at J7↔J6 and J3↔J11 (0.05/0.35 mm webs → merged;
  min printable feature 0.40).
- Pogo scheme: 20 apertures (J5×5, J6×4, J7×4, J11×3, J13×2, J3×2) +
  cavity-side 45° funnels; funnel probes sit just outside the slot
  mouths; `contact_count()==20`; Harwin P70-5100045 pins 0.6 mm proud.
- Antenna keep-out check measures true local thickness (nearest-surface
  distance >0.8 ⇒ >1.6 mm material) — thin walls/junction fillets pass.
- H1/H2 boss+pin alignment check added (axis off 0.00, radial slack
  0.15).
- Base corridor cut set back 0.15 mm (x 22.55) to keep coplanar faces
  from producing non-manifold edges.

## Verification history

- First full run: 122 PASS / 17 FAIL (stale v8 checks + tab collisions).
- After skirt/tab cuts, R-lug raise, gasket merges, probe fixes:
  132 PASS / 8 FAIL.
- Final round (pocket widen, tunnel extension, fin x-limits, envelope):
  **140 PASS / 0 FAIL / 0 FLAGGED**.

## Outputs

- 13 STLs, all watertight (manifold3d): base, top, bay_cup, pin_plate,
  button_cap, light_pipe, plug, pogo_pin, contacts_assy, thermal_slug,
  gasket, usb_flap, battery_door.
- `case_v10.step` — 13-solid faceted assembly (board placed).
- `renders/` — assembled/top/bottom/exploded/worn + sections:
  y62.9 & y59.85 electrode rows, y55.5 optics, y25 battery bay,
  y−19 antenna bulge, y27.5 lug, x29.5 USB-C+plug, openings overlay.
- `features.json`, `geometry.json`, `FIT_REPORT.md`, `CHECKS.md`.
