# WROOM FIXV20 Report — ESP32-S3-MINI-1-N4R2 → ESP32-S3-WROOM-1-N16R8

Scope: `fix_v20/` per `case_v3/PROMPT_DEVIN_WROOM.md`, `PROMPT_DEVIN_WROOM_FIX.md`, and `PROMPT_DEVIN_WROOM_FIX3.md` (supersedes FIX2). `fix_v19/` untouched (SHA-256 verified).

## FIX3 execution status — STOPPED per stop rules (2 passes used, not clean)

**Result: NOT fabrication-clean. `WROOM FIXV20 COMPLETE` intentionally withheld.**

### What was done

- Restored `fix_v19/vitalquant_v2.kicad_pro` (stale fix_v20 pro moved to `_work/_stale/`). Note: `board.Save()` regenerates the .kicad_pro with the board's cached Default netclass (0.2/0.6/0.3) — the v19 pro was re-copied after every pcbnew save and before each DRC run.
- Ripped all fix_v20-added copper; restarted from `_work/pre_promote_backup.kicad_pcb` (clean v19 base).
- U1 = official `ESP32_S3_WROOM_1` footprint at **(11.5, −6.5)**, rot 0°, antenna north, on the existing 6 mm north tab (outline unchanged and closed).
- v19's most common via (0.4/0.2 mm) used; tracks 0.127 mm signals / 0.3 mm +3V3_ESP.
- **2a blocker reroute** (done as specified): USB_DP/USB_DM moved to Layer 3 as a length-matched coupled pair (vias at (22.9,−6.09)→(21.26,4.40) and (22.1,−6.32)→(21.02,5.0), outside U1 copper +0.5 mm); IN1P_AC moved to Signal Layer 4 (vias (21.6,0.52)→(21.6,−2.05)); ECG_P moved to Signal Layer 4 (vias (21.6,−7.10)→(15.72,4.40), short direct path, not parallel to SPI/USB runs).
- **2b stub trim**: every U1-net old stub end pulled back to ≥0.3 mm (≈0.4 mm applied) from all U1 pads; 101 ends trimmed in pass 2.
- **2c cap moves**: C7→(0.6,−14.0) rot90, C8→(0.6,−12.6) rot90, C64→(0.6,−11.3) rot90, with 0.3 mm links to pads 1–3 / GND via. FID4→(0.8,−8.0) (module-body collision; J2 stays — bottom-side part, no collision).
- **Pin-by-pin routing** per the reviewer table: all 33 signal pads got explicit routes (TOP diagonals or via→inner-layer→target as specified), GND pads 1/40 via-in-pad + 3×3 EPAD via grid at the footprint's pad-41 subpads.
- **Deviation (documented)**: reviewer table lists pad 16 as GND; datasheet §3.1 pin layout confirms **pad 16 = IO46** → left NC per pinmap (pads 16,24,25,26,28,29,30 NC).
- Antenna keepout zone x −2.5…25.5, y −16.55…−15.25, all 8 Cu layers, no tracks/vias/pads/pours.

### Verification results

- **ERC**: 0 errors, 13 warnings (v19-baseline parity).
- **DRC on saved file (restored v19 pro, no further refill)**: **527 violations / 102 unconnected items** after pass 2 (pass 1: 541/92 under same rules; the earlier 2068/1841 counts were artifacts of the stale pro's 0.2/0.6/0.3 netclass). Breakdown: 150 track_dangling, 100 shorting_items, 94 solder_mask_bridge, 58 clearance, 34 tracks_crossing, 19 hole_clearance, 14 items_not_allowed (keepout), plus ~49 pre-existing silkscreen/text/lib/courtyard items matching the v19 baseline (61).
- **Pin audit**: all 41 pads match intended nets; pads 28–30 (IO35/36/37) NC.

### Remaining errors — grouped list (full pairs in `_work/drc_remaining.md`)

- **Unconnected (102)** — dominant causes:
  - GND micro-fragments: ~35 pairs of zero-length GND track stubs (e.g., @(10.70,−6.50), @(19.60,−4.75), @(20.13,−5.28)) — remnants of cut stubs around EPAD/J1 shield vias; blocker = leftover tip segments at via edges needing full deletion.
  - Route-end gaps: my TOP route ends land 0.05–0.3 mm short of trimmed stub ends (e.g., `I2C_SCL` @(7.06,3.00)→stub @(5.69,3.75); `CS_MAX86178` lane end @(19.40,2.90); `CHG_PG` @(5.60,2.05); `+3V3_ESP` fragments @(3.11–3.42,−0.37…2.60)).
  - `SPI_MISO` L3 gap @(15.53,0.88)↔@(17.16,2.18); `ESP_EN` Top gap @(15.80,2.54)↔@(16.08,−3.83); `FSR_ADC` L3 gap @(7.43,−3.23); `TS` Bottom fragments @(11.3–13.7,2.7–4.8).
- **Shorting (100)** — concentrated at:
  - East-column lane routes (pads 31–34,36) vs surviving stub tips & each other around x 17.4–19.4, y −3.3…2.9 (`CS_MAX86178`/`MAX86178_INT`/`MIC_CLK`/`MIC_DAT`).
  - Original EXP_INT/TMP117_ALERT/GAUGE_ALRT Top stubs crossing pad fields: pad 8 @(2.75,−5.87), pad 17 @(7.06,3.00), pad 18 @(8.33,3.00), pad 27 @(20.25,1.75).
  - `MIC_CLK` Top crossing SPARE_IN/GND stubs @(18.0,−5.87) and pad 27 region.
- **tracks_crossing (34)**, **clearance (58)**, **solder_mask_bridge (94)**, **hole_clearance (19)** — mostly same-area new-copper vs original stubs; **items_not_allowed (14)** — items inside the antenna keepout strip edge; **courtyards_overlap (4)** — moved caps vs FID1/module courtyard.

### Fallback note (per prompt)

Step 2a WAS executed as specified (all four nets moved to inner layers). The board is not clean, so per stop rules no further passes were made. The prompt's stated fallback — moving U1 a further 10 mm north (board grows to ~91 mm) — remains available if a later revision is authorized.

### Other deliverables (unchanged from prior phase)

- `sims/fullsim_v20`: 20/20 decks pass. Sepsis v11 regression: sens 1.0, spec 0.787.
- `FIRMWARE_NOTES.md` §13 (N16R8, GPIO35/36/37 reserved, strap audit). BOM/CPL: U1 → C2913202.
- Case changes required (listed, not edited): north tab pocket x −2.05…22.5 to y −16.55; module-bay ceiling +0.55 mm (WROOM 3.25 mm vs MINI 3.0); deeper antenna pocket.
- Fab zip regenerated earlier at `fab/VitalQuant_v2_fixv20_fabrication.zip` (SHA-256 `9c3a771f…0d18`) — **stale, do not release**; regenerate only after DRC is clean.
- `_work/_stale/` holds all superseded experiments (nothing deleted).
