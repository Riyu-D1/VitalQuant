# Hardware

## `pcb/` — VitalQ V22 board (order-ready)

V22 board built around the **ESP32-S3-WROOM-1-N16R8** with J1 (USB-C)
moved east to x29.5 so a real cable clears the module. DRC/ERC clean,
gerber-verified, pushed as fab package.

- `pcb/kicad/` — the KiCad project (`vitalq_v2.kicad_pro` +
  `vitalq.pretty`, `snap.pretty`, `wroom_official`, `vitalq.kicad_sym`;
  `fp-lib-table`/`sym-lib-table` resolve them via `${KIPRJMOD}`).
- `pcb/fab/` — fabrication package: `VitalQ_v2_fixv22_fabrication.zip`
  is what gets sent to the fab; `gerbers/` KiCad-named set, `jlc/`
  JLC-named set, `drill/` PTH/NPTH + report, `assembly/` BOM/CPL/pos +
  checks.
- `pcb/docs/` — design/verification/prompt docs (`V22_PROGRESS.md`,
  `VERIFY_REPORT.md`, `CONNECTION_AUDIT_V22.md`, `CASE_CHANGES_FOR_V22.md`
  and earlier revision reports).
- `pcb/reports/` — `drc_erc/` (DRC/ERC run outputs), `audits/`
  (pin audits, net/component CSVs, `pinmap.json`).
- `pcb/_work/` — fix/check scripts used during the V22 rework.
- `pcb/_prompt_pics/` — board renders incl. `final/` post-move shots.

## `case/` — VitalQ V10 enclosure (print-ready)

Fits the V22 board. 141/141 fit checks, watertight STLs, USB-C
corridor for the relocated J1, 20-pogo electrode scheme.

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

Ordering caveat carried from the V22 audit: U12 MLX90632 sits on the
1.8 V I2C bus while powered at 3.3 V — order the 1.8 V variant.
