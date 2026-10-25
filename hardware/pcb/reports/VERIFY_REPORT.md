# VERIFY_REPORT — fix_v24

Date: 2026-10-25. **KiCad CLI 10.0.4**
(`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`).

Reviewer-equivalent setup (same as fix_v23): fix_v19 project rules copied
into a temporary work copy + fix_v23's narrow `vitalq_v2.kicad_dru`
(Q1/Q2/Q4 internal-pad clearance 0.09 mm). All numbers **checked** (measured
today).

## DRC — `kicad-cli pcb drc --severity-error --schematic-parity`

| Metric | Baseline (v24 pre-edit) | fix_v24 |
|---|---|---|
| violations (errors) | 0 | **0** |
| unconnected items | 0 | **0** |
| shorts | 0 | **0** |
| schematic parity issues | 0 | **0** |

Severity-all: 154 warnings, **identical class breakdown** to baseline
(lib_footprint_issues 52, lib_footprint_mismatch 5, silk_edge_clearance 15,
silk_over_copper 8, silk_overlap 3, text_height 9, text_thickness 1,
track_dangling 59, via_dangling 2). No new warning classes.
Reports: `verify/drc_v24.json|.txt`, baseline in `verify/baseline/`.

## ERC — `kicad-cli sch erc --severity-error`

| Metric | Baseline | fix_v24 |
|---|---|---|
| violations (errors) | 0 | **0** |

Severity-all: 564 warnings, identical class breakdown to baseline
(lib_symbol_issues 506, footprint_link_issues 53, pin_to_pin 5). No new
warning classes. Reports: `verify/erc_v24.json|.txt`.

## Copper / geometry equivalence vs fix_v23

- All **16 gerber layers** (8 Cu + mask + silk + paste + Edge.Cuts)
  byte-identical modulo creation-date headers.
- Drill files (merged, PTH, NPTH) identical modulo creation-date lines.
- **302 footprints**: 0 geometry diffs (pads/position/rotation; property
  and attr tokens excluded by design).
- **8,898** board primitives (segments, vias, zones, drawings):
  identical.
- Detail: `verify/COPPER_EQUIV.md`.

## Edit accounting

- 67 schematic designators changed (fields + flags), all listed in
  `CHANGES_V24.md`.
- PCB footprint sync: only `property "Value"/"LCSC"` and `attr` tokens;
  86 real line diffs; no attr flag ever removed (J8's existing exclusions
  preserved).
- Cross-check vs `_inputs/BOM_rebuilt_JLC.csv`: every applicable row
  applied; the only differences are the owner-mandated deviations (D10
  consigned/blank, U8/U12/U14 swap targets, DNP'd D29/D30/R118/R119) and
  the D3-fitted J12/U24/U25.

## Export consistency (regenerated fab/)

- `BOM.csv` == fresh `kicad-cli sch export bom` output (257 rows).
- `BOM_JLC.csv` (79 rows) and `CPL_JLC.csv` (247 rows): designator sets
  identical, both directions.
- No DNP ref (D29, D30, R118, R119, J13, R93, R104, R105, R107, R108) in
  BOM_JLC or CPL_JLC.
- No J2–J11 pad refs anywhere in BOM/position outputs.
- J12, U24, U25 present in BOM_JLC and CPL_JLC (fitted, D3).
- D10: consigned row, blank LCSC, MPN NF2W757G-F1 — same convention as
  D12 (XL0603NRC730).
- `ASSEMBLY_CHECKS.json`: 247 placed, 79 groups, 0 missing-LCSC
  references, 0 missing positions, consigned = {D10, D12}.
- `SHA256SUMS.txt` covers the gerber + drill set; `VitalQ_v2_fixv24_
  gerbers.zip` inside `fab/gerbers/`; fabrication zip at `fab/` root.

## LCSC/JLC verification

105 LCSC product pages + 89 JLC partdetail pages fetched and parsed
2026-10-25. Every changed part's MPN, value, package, tolerance and
rating cross-checked against the page; stock and price recorded in
`BUY_NOTES.md` and `BOM_FULL.csv` (USD→GBP 0.748, ECB 2026-10-23).
Delisted/404 LCSC codes noted: C5221601 (U23), C536262 (U25) — both still
stocked at JLC; flagged.
