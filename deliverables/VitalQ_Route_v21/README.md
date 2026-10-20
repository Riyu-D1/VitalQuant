# VitalQ Route v21 — fabrication-ready board

Routed and fully verified KiCad design for the VitalQ wearable, superseding Route_v9.

- `vitalq_v2.kicad_pcb/.kicad_sch/.kicad_pro/.kicad_dru` — design files (KiCad 10)
- `vitalq.kicad_sym`, `vitalq.pretty/` — project libraries
- `fab/` — gerbers, drill, BOM (WROOM-1 corrected), CPL, `VitalQ_v2_fixv21_fabrication.zip`, `SHA256SUMS.txt`
- `renders/` — top / bottom / iso / U1 zoom
- `VERIFY_REPORT.md` — full verification pass (DRC 0/0/0, 20/20 sims, sepsis-model check)
- `ATOMIC_REPORT.md` — atomic-level wiring/FMEA/stress audit
- `DATASHEET_AUDIT.md` — per-component datasheet-vs-wiring check (26 ICs, 0 defects)
- `PROMPT_DEVIN_V21_*.md`, `V21_PROGRESS.md` — work record

Fab notes: request POFV (via-in-pad plugging — U1 EP + passive pads); R122 pad
overhangs bottom edge by design intent or accepts fab clip; order against JLC 8L.
