# VitalQ – current board (fix_v19)

This folder holds the order-ready VitalQ board, **fix_v19**, built on fix_v18.

- **DRC:** 0 errors / 0 unconnected at error severity, checked with the board's own `vitalq_v2.kicad_pro` rules.
- **Open it with** `fix_v19/vitalq_v2.kicad_pro`, so that the 8-layer design rules load. Opening the `.kicad_pcb` on its own falls back to KiCad's default rules and shows false errors.
- **Order file:** `fix_v19/fab/VitalQ_v2_fixv19_fabrication.zip`
  - SHA-256: `5f976845f422c6288c8b449167d16541d53471016cee02109fcbd1cccfbefd52`
- **D10 and D12 (LEDs) must be supplied separately.** No LCSC part fits them, so they are not in the assembly order.
- `fix_v19/FULLSIM_V19_REPORT.md` is the simulation and verification report for this board.

Everything in `archive/` is superseded. Use it for history only.

fix_v20 (the ESP32-S3-WROOM-1 upgrade) is work in progress and is not uploaded.

Research prototype only; not a medical device.
