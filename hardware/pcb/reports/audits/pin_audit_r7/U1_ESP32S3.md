# U1 — ESP32-S3-MINI-1-N4R2 (MCU module, 65 pads) — Round 7 independent re-audit

Datasheet: ESP32-S3-MINI-1 (Espressif) — VDD 3.0–3.6 V; module integrates flash/PSRAM + RF decap; external requirement: EN RC (≥10 k + ~1 µF) and IO0/IO46 strap control; all GPIO ≤VDD+0.3.

## Pin summary (measured; full map in measure.json)
- **Power**: pin3 +3V3_ESP (C7 22µ@5.9 mm + C8 100n@7.3 mm; module adds on-module decap) — OK. Pins 1,2,42,43,46–65 GND/EP — all GND ✓.
- **Boot**: pin45 ESP_EN → R5 10k+C64 1µ+SW1+Q3 auto-reset — DS-compliant ✓. Pin4 IO0 → R6 10k+SW2+Q3 — boot strap legal ✓.
- **Buses**: I2C pins12/13 → R19/R20 2.2k; SPI pins14–17+19,20,22,34 CS; UART 39/40 →CP2102N via 10 k; USB 23/24 native D-/D+.
- **Strap-only/NC**: pins 26–33 (IO26/47/33/34/48/35/36/37) unconnected — flash/PSRAM-internal pins on N4R2, must float: ✓ correct. Pin41 IO46 NC, pin44 IO45 NC — OK (straps have internal defaults).
- **GPIO**: FSR_ADC(5), ADS1292_DRDY(6), EXP_INT(7), LSM6_INT1(8), TMP117_ALERT(9), GAUGE_ALRT(10), CHG_PG(11), AD5940_GPIO0(18), STATUS_LED(21), AFE4900_ADC_RDY(25), MAX86178_INT(35), MIC_CLK(36), MIC_DAT(37), SPARE_IN(38) — all 3.3 V-domain ✓.

## Abs-max, five states
- All GPIO nets are +3V3-domain except 1V8-side SPI to U22 (6 V-tolerant pins on the AFE side; U1 side drives 3.3 V ≤VDD+0.3 ✓) and I2C (U9 translation) ✓.
- Battery-absent/USB-only: VDD from U3 regardless ✓. Back-drive: none (all drivers powered with U1's rail or open-drain/pulled lines ≤3.3 V).

## Reference design / passives
- EN RC exactly per Espressif hardware guidelines; auto-reset Q3 on ESP_TX/ESP_RX? — Q3 sits on EN+IO0 per standard devkit reset ✓. Antenna keepout verified copper-free (prior fix).
- Deviation accepted: bulk decap 22 µ@5.9 mm slightly beyond module guideline (~"close"), mitigated by on-module caps + plane.

## Disagreements vs R5 audit
None functional; this audit adds per-pad measured data and confirms the 8 NC'd module pins are the correct N4R2 strap/internal pads (R5 listed several as "unconnected?" without the strap justification).
