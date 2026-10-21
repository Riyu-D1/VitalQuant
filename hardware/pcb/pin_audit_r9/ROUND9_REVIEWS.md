# ROUND 9 — full-board review via three persona perspectives + Gerber-level checks

## Method (new this round)
1. **Gerber-level independent check** — parse Gerbers + Excellon directly (no pcbnew): 867 flashes/copper layer, 863 via drill sites covered on all 8 cu layers, 4 G85 slot sites = through pads (correct); C63 new site: pad1 flash + pad2 stroke-drawn copper present at (28.6,3.42/4.38), old site (26.3,12.0) clean.
2. **Persona reviews** — assembly technician, test engineer, EMC engineer.

## Assembly-technician review
- All SMD refs have unique silkscreen designators; checked refs don't overlap pads (DRC silk-over-pad clean except accepted board-edge clips).
- Polarized parts: D1/D2/D21-24 (ESD arrays), D25/D12 (LEDs), U16 (optical), all ICs pin-1 marked via pad-1 chamfer/dot in footprint; J1 keyed USB-C — OK.
- 0402 density highest around U6/U8 BGA fanouts — hand rework impractical; noted for assembly note (machine-only rework zones listed in DFM doc).
- No fiducial within 5mm of board edge (JLC rule); 6 fids OK.
- Panel: single board 50x75, no v-cut needed.
- **Finding: none.**

## Test-engineer review
- Coverage: TP1-TP28 exist; mapping checked — all power rails (+3V3/+1V8/VBUS/VBAT/TX_5V), all I2C/SPI buses, all AFE resets, RTC_INT, and sensor interrupt lines reachable. U2 STAT/PG on TP27/R60-TP. J8 Tag-Connect gives SWD-less USB/UART programming via ESP32 native USB.
- Boundary-scan n/a. Bed-of-nails: 28 TPs on bottom, single-side probing OK.
- **Finding: RTC_INT only reaches TP28 (test point) — carried-open item (FW polls); this IS the documented exception, counted as known-open not new.**

## EMC-engineer review
- USB-C: D+/D- have U21 USBLC6 clamp + R104/R105 DNP disconnect (data unused); VBUS clamp U21 + C63 1µ@9.7mm + C1/C10 bulk @15-18mm — J1 edge was never closer-bypassed (pre-existing geometry, unchanged by R7/R8).
- RF: U1 antenna keepout copper-free all layers (verified fix_v4); RF ground via-fence present around module edge.
- Decap: every IC has local 100n within 5mm except documented-accepted cases; all bulk caps plane-adjacent.
- Return paths: sensor AFE grounds share single GND plane (L2/L7) — split-plane review: none needed (mixed-signal parts are self-contained AFEs).
- Clocks: no high-speed clocks off-board; ESP32 SPI ≤10MHz short runs; 32.768kHz inside U24 package (I2C RTC) — no crystal EMC issue.
- **Finding: none new.**

## Re-verification this round
- DRC: 0 errors / 48 warnings / 0 unconnected (fix_v16). ERC: 0 errors / 13 warnings.
- Gerber↔drill: 863/863 via sites annular-covered all 8 layers.
- Case fit: unchanged (C63 value text is F.Fab-only; placement geometry identical) — 136/136 carried.
- Sepsis: deterministic `run.py && score_v11.py` → ews_v11_4 0.98/0.853 (capability unchanged).

## ROUND 9 RESULT: **0 new issues** — CLEAN ROUND 1/2
