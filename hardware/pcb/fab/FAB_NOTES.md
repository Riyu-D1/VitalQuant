# VitalQ v2 — fix_v22 fabrication package

Generated from `fix_v22/vitalq_v2.kicad_pcb` after the J1 (USB-C) move:
J1 (29.5,-7.8) rot 180; R2 (27.55,-1.4); TP11 (36.3,-8.474); TP20 (34.3,-0.4);
R26 (32.0,-1.9). DRC 0 violations / 0 unconnected / 0 shorts / 0 parity; ERC 0;
zones refilled.

Contents:
- Gerbers (X2, netlist attributes): 8 copper layers + F/B mask, silk, paste,
  Edge.Cuts — JLC extensions in fab/ root; .gbr copies in fab/gerbers/
- drill/vitalq_v2-PTH.drl + -NPTH.drl (separate) and fab/vitalq_v2.drl (merged);
  917 plated holes
- BOM.csv (kicad-cli sch export bom; U1 = ESP32-S3-WROOM-1-N16R8),
  BOM_JLC.csv (JLCPCB grouped; J1 = C165948)
- CPL.csv / CPL_JLC.csv, positions_kicad.csv, vitalq_v2-pos.csv
- ASSEMBLY_CHECKS.json — placement/BOM consistency summary
- vitalq_v2-job.gbrjob — gerber job file

Checks:
- J1 CPL: 29.5,7.8 Top rot180 — same orientation as v21, XY only changed
- R2 CPL: 27.55,1.4 rot270; R26: 32.0,1.9 rot180 (TP11/TP20 are no-mount
  test pads — not in CPL)
- Gerber-only continuity (sims/fullsim_v20/gerber_check): 0 shorts, 0 opens
  on real nets; J1 pads all tagged with correct nets
