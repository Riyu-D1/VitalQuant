# VitalQ v2 — fix_v21 fabrication package

Generated from `fix_v21/vitalq_v2.kicad_pcb` after the FULLROUTE repair
(41 U1 connections routed; DRC 0 violations / 0 unconnected / 0 shorts;
zones refilled).

Contents:
- Gerbers (X2): 8 copper layers + F/B mask, silk, paste, Edge.Cuts
- `drill/vitalq_v2.drl` — Excellon drill
- `BOM.csv` (kicad sch export bom), `BOM_JLC.csv` (JLCPCB grouped)
- `CPL.csv`/`CPL_JLC.csv`, `positions_kicad.csv`, `vitalq_v2-pos.csv`
- `ASSEMBLY_CHECKS.json` — placement/BOM consistency summary
- `vitalq_v2-job.gbrjob` — gerber job file

Post-verify fixes applied to this package:
- BOM.csv regenerated: U1 = ESP32-S3-WROOM-1-N16R8 (was stale MINI-1-N4R2)
- Gerbers/drill regenerated after coincident-via cleanup (5 same-net via pairs resolved: 3 duplicates removed, 2 merged; drill hits 918->913)
