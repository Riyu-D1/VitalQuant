# case_v7 progress — COMPLETE

**State: 137/137 checks PASS. FIT_REPORT.md ends with CASE7 COMPLETE.**

Baseline: `case_v6` (106/106 at fork). Five refinement rounds, each with
new verifier coverage. Two latent v5/v6 defects found and fixed.

## Round 1 — comfort + retention + branding

- 45° mouth chamfers on all 11 rectangular skin-side apertures + round
  chamfers on mic port / U23 vent / slug seat (skin comfort).
- Cavity-side 45° self-centring funnels on the six electrode aperture
  groups — pins find the bore during board drop-in.
- Two extra snap beads flanking the USB opening on −y (8 total).
- Strap bar 2.7 → 3.0 mm (stress 32.3 → 29.1 MPa point load).
- Debossed ECG pulse mark beside the wear chevron.
- New checks: aperture/round/port chamfers, electrode funnels, ECG mark,
  per-bead snap-tip engagement (initially on the two −y beads).

## Round 2 — gasket pose fix + lug fatigue

- **Fix:** gasket was modelled at z 0..0.4 — 500 mm³ buried inside the
  base floor solid. Now modelled in the true assembled pose, proud on
  the skin face at z −0.4..0 (matches ASSEMBLY.md).
- Lug channel end-wall corners sharpened→R0.8 (fatigue).
- New checks: `gasket proud on face`, `tips vs gasket face` (0.20 mm),
  `strap lug fatigue @1e6` (distributed 14.6 ≤ 20 MPa).

## Round 3 — durability audit

- Full 8-bead snap engagement audit (probe volume in groove per bead):
  min 0.55 mm³ (the two −y beads are smallest by design — they flank
  the USB window).
- Sweat seam-barrier rays on all four walls + the USB wall (6–8 hits).
- 1.5 m drop-shock estimate: ~1500 g worst case → bead shear 5.6 MPa.
- PETG shrink/tolerance: +0.15 mm grown board still 0.000 mm³ inter.
- Battery-door pull tab: three grip ribs (1.34 mm³ verified).

## Round 4 — contact reliability + sweat shedding

- Bay-cup drip groove: 0.4 mm debossed ring outside the cup footprint
  (D25-pocket keepout) — sweat sheds sideways off the cup seam.
- **Fix:** pogo tip pole (skin-contact point) sat at −1.05 mm — the
  icosphere centre was placed on the −0.60 datum, so every tip protruded
  0.45 mm too far (more skin poke than spec'd). Pole now lands exactly
  at −0.60; measured per-pin via up-facing rays.
- New checks: `bay drip groove`, `pogo rim clearance` (≥0.30 all 20 —
  min 0.40 at J5), `pogo lift reserve` (free tip −1.40 → 0.80 reserve),
  `contact preload uniform` (all 21 poles −0.60 ± 0.05),
  `TPU min feature` (≥0.35).

## Round 5 — seal integrity + strap threading polish

- **Fix:** pry slots cut the skirt AND 0.9 mm of wall, leaving two
  2.4×0.55 mm through-windows into the interior at the seam — a real
  sweat-ingress path. Slots now notch the skirt only; the wall face
  stays sealed (verified: first ray hit lands on the wall face).
- 45° chamfer added on the lug channel's inboard top edge — the strap's
  inner edge no longer slides over a sharp corner while threading.
- New checks: `pry -x / +y slot` (skirt open + wall sealed),
  `cap retention catch`.

## Final state

- `verify_fit.py` — **137 checks, all PASS → CASE7 COMPLETE**
- `case_v7.step` — faceted-BREP STEP, 13 named solids, assembled pose
- 9 renders: assembled, top, bottom, exploded, worn (R95 chest +
  strap), sections lug / J7 / J3 / USB-C
- Docs: `ASSEMBLY.md`, `PRINT_NOTES.md`, `CONTACTS_BOM.md`,
  `STRAP_DESIGN.md` updated to v7

fix_v9 board untouched (read-only, SHA unchanged). No commits/pushes.
