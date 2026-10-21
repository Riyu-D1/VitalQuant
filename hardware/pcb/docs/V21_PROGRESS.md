# V21 progress (Devin writes here)

## Final state — ROUTING COMPLETE

- **DRC (error severity): 0 violations / 0 unconnected / 0 shorts** — verified by
  `kicad-cli pcb drc --severity-error` after zone refill (`_work/drc_final2.txt`).
- All **41** initially-unconnected U1 connections routed and verified by DRC.
- All 108 zones refilled after routing (`_work/refill.py`).

## What was done

### Pre-fix
- C64 moved −0.15 mm X (1.60→1.45,−11.30) for pad clearance; C8 moved −0.15 mm X
  (0.60→0.45,−11.40) to fix the resulting courtyard overlap. Stubs re-landed.
- Verified layer map: F.Cu=Top, In1–In6.Cu inner, B.Cu="Bottom Layer" (id2).

### Routing (all 41 unconnected U1 items)
Hand-authored per prompt topology; every batch geometrically preflighted
(`_work/geo.py`) then committed. Track width 0.102–0.127 mm, vias 0.45/0.20 mm.
Escape stubs on Top into vias inside the U1 courtyard (intentional, DRC-legal),
then inner-layer runs (mostly In1.Cu) landing on the destination net's island
track/via. `In6.Cu` (nearly empty) used for the hardest crossings.

- Nets 1–16 (short/non-U1 links + west/east pad escapes): GND J2 link, +3V3_ESP,
  USB_DM/DP hop-backs around the ESP_EN horseshoe (In5.Cu via-to-via),
  TMP117_ALERT, LSM6_INT1 (In1 x3.6), STATUS_LED (L4 x9.2 → B.Cu island diag via
  (5.5,18.46) west of north keepout), ESP_IO0, ESP_RX/TX, EXP_INT, SPI_MOSI/MISO/
  SCK, CS_FLASH, CS_AFE4900, I2C_SDA/SCL, AD5940_GPIO0 ×2, AFE4900_ADC_RDY,
  GAUGE_ALRT, CHG_PG, CHG_VSET, MIC_CLK/DAT, SPARE_IN, ADS1292_DRDY, FSR_ADC,
  MAX86178_INT (In4 x27.0), CS_ADS1292 (In2 south lane), CS_AD5940.
- **CS_MAX86178** (hardest, ~15 candidates): Top stub → via (17.8,−3.33) →
  In1.Cu east → vert x21.5 → via (21.45,10.24) → In6.Cu west → via (13.9,8.5)
  onto the island's B.Cu diagonal (13.82,8.42)→(14.92,9.52).

### Ripped/rerouted neighbour copper
- No neighbour tracks were ripped in the final state — the USB_DP closure was
  made via an In5.Cu hop (R102-pad via → via on the B island) around the intact
  ESP_EN horseshoe.
- CS_MAX86178's own stale In1 stubs at (21.8,−3.33)→(21.8,11.72)→(24,11.72)
  removed and replaced (same-net rework).

### Board size / U1 moves
- **None.** Board outline unchanged; U1 stays at (11.5,−6.5) rot 0.

### Passive moves (CASE IMPACT)
- C64: (1.60,−11.30)→(1.45,−11.30), Δx −0.15 — pad clearance.
- C8: (0.60,−11.40)→(0.45,−11.40), Δx −0.15 — courtyard overlap fix.
- Neither is a pogo/sensor-window part; J2/test pads/sensor windows untouched.

### Pin audit
- U1 = `ESP32_S3_WROOM_1` land (fix_v20 module swap, see WROOM_REPORT.md).
- Every board pad's net lands on the GPIO pinmap.json intends for that net
  (e.g., CS_MAX86178→IO38, ESP_RX→IO44/RXD0, ESP_TX→IO43/TXD0,
  USB_DM/DP_S3→IO19/20). **Audit: PASS** — no pin swaps needed vs firmware
  intent; `FIRMWARE_NOTES.md` §13 already documents the MINI→WROOM mapping.
- Pads 16 (IO46), 24 (IO47), 25 (IO48), 26 (IO45), 28/29/30 (IO35/36/37,
  PSRAM-reserved on N16R8) correctly unconnected.
- Antenna keep-out (x −2.5…25.5, y −16.55…−15.25, all layers): 0 items inside.

### Fab outputs (regenerated into `fab/`, old moved to `fab/_stale/`)
- Gerbers: 8 copper + F/B mask, silk, paste, Edge.Cuts; Excellon drill;
  BOM.csv/BOM_JLC.csv, CPL.csv/CPL_JLC.csv, positions, ASSEMBLY_CHECKS.json.
- `fab/VitalQ_v2_fixv21_fabrication.zip`
  SHA-256 `697629405d8658674ae8def36cbcd12dd9c114a4fab712231d4aed62d3ba2ac5`
- Renders: `_prompt_pics/final_render_{top,bottom}.png`,
  `_prompt_pics/final/U1zoom_*.png`, `layer_<L>.svg(.png)` refreshed.

### Checkpoints
- `_work/ckpt_all_routed.kicad_pcb` (all 41 committed, pre-refill)
- `_work/ckpt_prefinal.kicad_pcb`
