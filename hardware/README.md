# Hardware

`pcb/` contains the VitalQuant board, **fix_v24**, built around the
ESP32-S3-WROOM-1-N16R8 (KiCad 10.0.4; DRC 0 errors / 0 unconnected /
0 shorts / 0 schematic-parity issues, ERC 0 errors).

- `pcb/fab/` holds the fabrication package — the gerber/BOM/CPL zip
  (`VitalQ_v2_fixv24_fabrication.zip`) is what gets sent to the fab.
- Project-local KiCad libraries (`vitalquant.pretty`, `snap.pretty`,
  `vitalquant.kicad_sym`) live alongside the project; `fp-lib-table` and
  `sym-lib-table` resolve them via `${KIPRJMOD}`.
- fix_v24 is a BOM/metadata revision of the fix_v23 design: stock-driven
  part swaps, LCSC corrections, D10 consigned (Nichia NF2W757G-F1) and the
  dead MX-ECG chain marked DNP. Copper, geometry, positions and drills are
  unchanged from fix_v23 — see `pcb/reports/CHANGES_V24.md` and
  `pcb/reports/COPPER_EQUIV.md`.
