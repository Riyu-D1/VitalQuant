# Copper equivalence: fix_v23 vs fix_v24

Date: 2026-10-25. KiCad CLI 10.0.4
(`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`).

Method: `kicad-cli pcb export gerbers` (X2) on both boards into separate
dirs; all 16 layers compared byte-for-byte after stripping
`TF.CreationDate` header lines and `G04` comments. Drill via
`kicad-cli pcb export drill` (merged + `--excellon-separate-th`).
Board-level check: S-expression parse comparing every footprint's
geometry-bearing children (pads, at-position, rotation — excluding
`property`/`attr`/`uuid`) and all top-level copper/edge primitives.

## Result — IDENTICAL

| Check | Result |
|---|---|
| 8 copper layers (F.Cu, In1–In6.Cu, B.Cu) | byte-identical (modulo creation date) |
| Edge.Cuts | byte-identical |
| F/B.Mask, F/B.Silkscreen, F/B.Paste | byte-identical (no DNP text on these layers) |
| Merged drill file | identical except `DRILL file ... date` + `TF.CreationDate` lines |
| PTH + NPTH drill files | identical except creation-date lines |
| Footprints (302): pads, position, rotation | 0 diffs (property/attr/uuid excluded by design) |
| Board primitives (segment/via/zone/gr_*): 8898 | identical |

Silkscreen and paste also identical — the v24 edits touched Value/LCSC
fields (F.Fab layer) and attr flags only, none of which reach the silk or
paste layers.

Conclusion: **copper, Edge.Cuts and drills are identical to fix_v23.**
Only metadata changed (schematic/PCB fields + fabrication exports).
