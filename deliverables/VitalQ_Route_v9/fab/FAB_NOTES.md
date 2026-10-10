# VitalQ v2 — JLCPCB fabrication notes (R2, 8-layer)

**STATUS: NOT RELEASED FOR FABRICATION.** The saved board still has open connections and real DRC violations
(see `fab_check.json` → `drc_of_board`, and `REPORT.md` R2 section). These files are a review/quote set only.

## Board
- 8 copper layers, stack S/G/S/S/P/S/G/S:
  F.Cu signal · In1 "Ground Layer 2" GND plane · In2 "Layer 3" signal · In3 "Signal Layer 4" signal ·
  In4 "Power Layer 5" +3V3 / VBAT_SYS pours · In5 "Signal Layer 6" signal · In6 "Ground Layer 7" GND plane · B.Cu signal.
- Finished thickness 1.6 mm (board-setup stackup is approximate; use JLC's standard 8-layer impedance build at order time).
- Surface finish ENIG. Outline 50 × 75 mm (unchanged; no growth was needed/used).
- Cost note (user/supervisor): 8-layer with POFV ≈ USD 80 per 5 boards at JLC (quote to be confirmed).

## Rules (JLC multilayer capability, never loosened below JLC)
- Track width / gap: 0.1016 mm netclass; board minimum clearance 0.09 mm (= JLC minimum) used only by the custom rule
  "JLC 0.09mm intra-footprint pad gap" for pad-to-pad gaps inside one footprint (SOT-23 pads are 0.100 mm apart).
- Through vias only (no blind / buried / micro vias). Minimum via 0.25 mm / 0.15 mm hole.
  Via sizes used: 0.40/0.20 mm (772), 0.25/0.15 mm (33), 0.30/0.15 mm (12, pre-existing).
- Via hole to any foreign copper ≥ 0.20 mm; drilled hole to hole ≥ 0.20 mm (custom rules in `vitalq_v2.kicad_dru`).

## Via-in-pad — POFV MANDATORY
Order **epoxy-filled, copper-capped, planar via-in-pad (POFV)** (JLC default process for 6+ layers; confirm on the order form).
- 49 vias carry filled+capped flags (`../pofv_vias_R2.json`): BGA/WLP ball vias on U7 (AD5940), U6 (AFE4900), U22 (MAX86178),
  the U11/U20 sensor-island +3V3 stitch (0.40/0.20 in U11.5 under U20.5) and pre-existing filled vias.
- **POFV mandatory (planar, under Tag-Connect J8 landing):** every U6 ball via inside the J8 TC2030 no-via area. Each has its own
  notch in the J8 rule area of exactly annulus + 0.002 mm (SUPERVISOR_R2_05); list in the latest `candidates/*/j8_permitted_vias_R2.json`.

## Files (`fab/`)
- `vitalq_v2_gerbers.zip` — 8 copper layers, F/B mask, F/B paste, F/B silk, Edge.Cuts, Excellon PTH + NPTH drills and drill maps
  (exported with kicad-cli, drill/aux origin).
- `vitalq_v2_BOM.csv` (Comment, Designator, Footprint, LCSC) and `vitalq_v2_CPL.csv` (Designator, Mid X, Mid Y, Layer, Rotation),
  both generated from the board so the reference sets are identical (255 placed parts; test points, fiducials, H1/H2,
  J8/J12/J13 are excluded by footprint attributes).
- **LCSC part numbers are missing for 247 parts** (the schematic has no LCSC field). They must be filled in before ordering assembly.
- `fab_check.json` — file list, copper-layer count, BOM/CPL reference cross-check, DRC summary of the exported board.
