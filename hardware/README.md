# Hardware

`pcb/` contains the order-ready VitalQ board, **fix_v19**, built around the
ESP32-S3-MINI-1-N4R2.

- `pcb/fab/` holds the fabrication package — the gerber/BOM/CPL zip
  (`VitalQ_v2_fixv19_fabrication.zip`) is what gets sent to the fab.
- Project-local KiCad libraries (`vitalq.pretty`, `snap.pretty`,
  `vitalq.kicad_sym`) live alongside the project; `fp-lib-table` and
  `sym-lib-table` resolve them via `${KIPRJMOD}`.

The WROOM-1-N16R8 variant (**v22**) is in progress and will replace fix_v19
once verified.
