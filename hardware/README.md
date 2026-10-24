# Hardware

## `pcb/` — VitalQ fix_v23 board (8-layer)

fix_v23 board built around the **ESP32-S3-WROOM-1-N16R8**. Design complete;
KiCad 10.0.4 measured: DRC 0 errors / 0 unconnected / 0 shorts / 0 parity,
ERC 0 errors. Not yet fabricated — pending fab/bench review.

- `pcb/kicad/` — the KiCad project (`vitalq_v2.kicad_pro`, `.kicad_pcb`,
  `.kicad_sch`, `.kicad_dru` scoped rule + `vitalq.pretty`, `snap.pretty`,
  `wroom_official`, `vitalq.kicad_sym`; `fp-lib-table`/`sym-lib-table` resolve
  them via `${KIPRJMOD}`).
- `pcb/fab/` — fabrication package: `VitalQ_v2_fixv23_fabrication.zip`;
  `gerbers/` KiCad-named set, `jlc/` JLC-named set, `drill/` PTH/NPTH +
  report, `assembly/` BOM/CPL/pos + checks (corrected BOM — U1 = C2913202).
- `pcb/docs/` — design/verification/prompt docs (`VERIFY_REPORT.md` now
  describes fix_v23, plus earlier revision reports kept for history).
- `pcb/reports/` — `VERIFY_REPORT.md`, `DRC_STATEMENT.md`,
  `BOM_CROSSCHECK.md` (fix_v23), `drc_erc/` run outputs, `audits/`
  (pin audits, net/component CSVs, `pinmap.json`).
- `pcb/_prompt_pics/` — board renders.

## `case/` — VitalQ V10 enclosure

Designed for the current board outline. 141/141 CAD fit checks, watertight
STLs, USB-C corridor for J1 at x29.5, 20-pogo electrode scheme.
**Nothing printed; no physical fit validation.**

- `case/outputs/stl/` — 13 print parts (base, lid, bay cup, pin plate,
  pogos, gasket, door, flap, plug, cap, light pipe, slug, assy).
- `case/outputs/case_v10.step` — faceted assembly STEP.
- `case/renders/` — assembled/top/bottom/exploded/worn + section views
  and the openings overlay.
- `case/src/` — geometry/verify/render/export pipeline.
- `case/data/` — extracted board geometry + feature/feature maps.
- `case/docs/` — `CHECKS.md` (141-row results), `FIT_REPORT.md`,
  `SIM_V22.md`, `ASSEMBLY.md`, `PRINT_NOTES.md`, `STRAP_DESIGN.md`,
  `DELTA_V22.md`, `CONTACTS_BOM.md`, `PROGRESS.md`, prompt doc.
- `case/_prompt_pics/` — reference images.

Ordering caveat carried from the audit: U12 MLX90632 sits on the
1.8 V I2C bus while powered at 3.3 V — order the 1.8 V variant.
