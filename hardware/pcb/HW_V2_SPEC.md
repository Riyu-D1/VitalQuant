# hw_v2 Upgrade Specification (authoritative coordination spec)

Base: `devinpcb` @ 54da3295 (hw_v1 placement snapshot). Branch: `hw_v2`.
This file is the single source of truth for the hw_v2 hardware revision.
All design work must match this spec. If a detail conflicts with this file,
fix the detail — not this file — unless the spec is provably wrong
(datasheet-verified error), in which case flag it in the change log below.

## Mandate

**Maximum functionality, zero compromise.** Keep EVERY hw_v1 component —
including the defib-protection ladder, CP2102N console path, all test
points (now TP1–TP28 — TP28 on RTC_INT was added), and both TMP117s.
ADD new capability; upgrade parts only where the
replacement is strictly better. No sensing function is ever dropped.

## Approved changes

### Upgrade (in-place replacement — strictly better)
1. **U1 ESP32-WROOM-32E → ESP32-S3-MINI-1U-N4R2**
   - 15.4×15.4 mm, U.FL external antenna (kills the x<6.5 mm keep-out strip)
   - Native USB (GPIO19=D−, GPIO20=D+ → J1 USB-C)
   - Vector/SIMD instructions for on-device TinyML sepsis inference
   - 4 MB flash + 2 MB PSRAM (W25Q512 64 MB stays for logging)
   - CP2102N **stays** wired to UART0 (GPIO43 TX / GPIO44 RX defaults);
     its USB side is selectable via 0Ω solder links (new SJ_A/SJ_B pair):
     default D+/D− → S3 native; alternate strap → CP2102 bridge for
     out-of-band console recovery.
   - Strapping: GPIO0 boot (SW1+TP24), GPIO3 JTAG-sel (EXP_INT — input w/
     existing R53 pull-up, fine), GPIO45/46 leave unstrapped (no loads).

### Additions (all I2C addresses verified free)
2. **MAX86141 WLP-20** — optical PPG AFE (hw_v2.1 swap, replacing the
   MAX86178 WLP-49). SPI + CS_MAX86178 + MAX86178_INT (net names kept).
   Drives the satellite PPG tail natively (3 LED cathodes + anode + 1 PD
   pair). SDO level-shifted to 3.3 V by new U26 SN74AXC2T245. ECG/BioZ
   channels dropped — J13's MX_ECG pads are unused (D29/D30, R118/R119
   left in place). Existing AFE4900/ADS1292R/AD5940 all stay — AD5940
   keeps EDA/bioZ-lactate duty.
3. **SHT45-AD1B** skin RH+temp, DFN-4 1.5×1.5 mm, bottom skin face,
   I2C3V3 0x44. Shares R19/R20 pull-ups.
4. **D12: 730 nm LED** (LED_0402 package) + 0402 series R from +3V3,
   cathode switched by new Q4 (CSD13380F3T clone of Q1) gated by
   **AD5940 spare GPIO** (net name NIR730_GATE — document which ball).
   Pairs with AS7341 NIR channel for tissue-StO₂ ratio.
5. **J9: distal-temp tail** — 4× Pads_SMD pads (3V3/SDA/SCL/GND) for a
   flex-mounted TMP117/TMP119 at I2C 0x4A.
6. **J10: satellite-PPG tail** — 6 pads wired to MAX86178 LED1/LED2/LED3
   cathodes, LED_ANODE, PD cathode/anode. Hosts an off-board SFH7050A.
   Same-site synchronous acquisition with main PPG.
7. **J11: sweat-electrode site** — 3 pads WE/RE/CE → AD5940 mux inputs
   (SE1/RE1/CE1-class spare channel; confirm ball numbers vs datasheet,
   document in PINMAP). Amperometric sweat-lactate research channel.
   MUST be labeled RESEARCH-GRADE in schematic notes.
8. **J12: unified FFC tail connector** (DNP, FH12-14S-0.5SH — J9+J10+J11
   carry 13 signals, so 12-pos is insufficient) — assembly option vs.
   discrete pads.
9. **RTC (DNP): RV-3028-C7 or equiv**, I2C3V3 0x52 — absolute wall-clock
   timestamps for multi-day logging / clock-model anchoring.
10. **PDM mic (DNP): IM69D130-class** — chest acoustic channel, I2S/PDM
    clock+data on S3 GPIOs. Footprint only; firmware optional.
11. **ZHF insulation dome** — mechanical/assembly note only (foam dome
    over U20 TMP117); documented in README + VERIFICATION, no part.

### Explicitly NOT changed
- Defib ladder R32–36/R76–83 + TVS D1–9/D21–24 + all creepage slots: KEPT
- TP1–TP28 + J8 TC2030: KEPT (TP28 on RTC_INT added after this spec)
- BQ25170 + MAX17048 + TPS63802 power tree: KEPT (no PMIC swap)
- W25Q512 WSON-8, all connector pads J5/J6/J7, passives stay 0402
  (the only non-0402 passives are the new J11/J13 0 Ω cut-points
  R118–R122 — R_2512_6332Metric FP_HV like the ladder)
- BME280, LSM6DSV80X, AS7341, MLX90632, SFH7072, FSR pads, buttons: KEPT

## Authoritative interface map (S3-MINI-1U)

| Net | S3 GPIO | hw_v1 note |
|---|---|---|
| FSR_ADC | GPIO1 | ADC1_CH0 |
| ADS1292_DRDY | GPIO2 | timestamped input |
| EXP_INT | GPIO3 | input; strap JTAG-sel — pull-up exists |
| LSM6_INT1 | GPIO4 | input |
| TMP117_ALERT | GPIO5 | input |
| GAUGE_ALRT | GPIO6 | input |
| CHG_PG | GPIO7 | input |
| I2C_SDA | GPIO8 | matches existing firmware profile |
| I2C_SCL | GPIO9 | " |
| CS_ADS1292 | GPIO10 | |
| SPI_MOSI | GPIO11 | shared SPI2 host |
| SPI_SCK | GPIO12 | " |
| SPI_MISO | GPIO13 | " |
| AD5940_GPIO0 | GPIO14 | |
| CS_AD5940 | GPIO15 | |
| CS_FLASH | GPIO16 | W25Q512 |
| STATUS_LED | GPIO17 | via R88 1 kΩ → D25 → GND (hw_v1 net unchanged) |
| CS_AFE4900 | GPIO18 | |
| USB_D− / USB_D+ | GPIO19 / GPIO20 | native USB → J1 (default strap) |
| AFE4900_ADC_RDY | GPIO21 | timestamped input |
| CS_MAX86178 | GPIO38 | new |
| MAX86178_INT | GPIO39 | new, timestamped input |
| MIC_CLK / MIC_DAT | GPIO40 / GPIO41 | DNP |
| SPARE_IN | GPIO42 | MTMS — digital-only, NO SAR ADC channel. Leave open as spare input (MAX17048 covers battery SoC; no VBAT_SENSE ADC tap in this rev) |
| UART0_TX / UART0_RX | GPIO43 / GPIO44 | → CP2102 (kept console path) |
| ESP_IO0 / ESP_EN | module pins | hw_v1 wiring preserved: SW1=EN, SW2=IO0; TP24 stays ADS1292_DRDY |

Expander P0–P7: **unchanged** — ground truth is hw_v1 PINMAP.md:
P0 AFE4900_RESETZ, P1 ADS1292_PWDN, P2 AD5940_RESET, P3 TX5_EN,
P4 IR_GATE, P5 CHG_STAT, P6 VBUS_DET, P7 CHG_DIS. (An earlier draft of
this spec listed a different port set — that list was wrong; PINMAP.md
is authoritative.)
SPI CSes: CS_ADS1292(10), CS_AFE4900(18), CS_AD5940(15), CS_FLASH(16),
CS_MAX86178(38) — 5 total on the shared SPI host.

I2C3V3 (GPIO8/9, R19/R20): 0x20 TCA6408, 0x36 MAX17048, 0x44 SHT45,
0x48/0x49 TMP117×2, 0x4A distal TMP117 (tail), 0x52 RTC (DNP),
0x76 BME280, 0x6A LSM6DSV80X.
I2C1V8 (behind PCA9306, GPIO8/9 shared bus): 0x39 AS7341, 0x3A MLX90632.

## Target size

Target was ~32 × 62 mm (module keep-out freed; everything else
retained). **As built: 40.0 × 62.0 mm** — the 32 mm width did not
close once the J11/J13 HV-boundary protection row (5 connector pads +
5 cut-points + 5 DNP clamps + pad-shadow keep-outs) was added, so the
outline grew 8 mm east; J10 and J11 moved to the new east edge while
J13/J5/J6/J7/J12 kept their sites. The 36.5×70 fallback was not
needed. 291 parts. One rigid 4-layer board
(JLC04161H-7628 stackup). No rigid-flex in this rev.

## File ownership (one writer per file — no shared edits)

- `design.py` schematic edits — Schematic agent
- `PINMAP.md`, power/current budgets — Pin/power agent
- `build_pcb.py`, placement plan, board outline — Layout agent
- `VERIFICATION.md`, `README.md`, `PASSIVES.md`, BOM note, this spec's
  change log — Docs agent
- `firmware/esp32/profiles/hw_v1.yaml`, `config/hardware.example.yaml`,
  `sensor_driver.h` contract comments — Firmware agent

## Rules

- Do NOT push. Do NOT commit. Leave changes in the working tree.
- Generated .kicad_sch/.kicad_pcb regeneration requires skidl + kicad-cli;
  if unavailable locally, deliver sources + document the regeneration step.
- VERIFICATION.md must honestly say placement/routing/DRC pending — the
  existing "not routed" posture is kept; no fabricated checks.
- Sweat-lactate channel labeled RESEARCH-GRADE everywhere it appears.

## Change log (append here)

- Coordinator fix: expander P0–P7 parenthetical corrected to hw_v1
  ground truth (PINMAP.md is authoritative).
- Coordinator fix: VBAT_SENSE on GPIO42 → SPARE_IN — GPIO42/MTMS is
  digital-only on S3 (no SAR-ADC channel; ADC1=GPIO1–10, ADC2=GPIO11–20
  and ADC2 is WiFi-conflicted). All ADC1 pins are allocated, so the VBAT
  analog tap concept is dropped; MAX17048 already provides battery
  voltage/SoC. R107/R108 stay DNP as a logic-level detect option only.
- Coordinator fix: J12 spec widened from 12-pos to 13/14-pos FFC —
  J9+J10+J11 carry 13 nets. Schematic chose FH12-14S-0.5SH (pin 14 spare).
- Coordinator fix: spec table corrections — STATUS_LED resistor is R88
  (not R54); TP24 is ADS1292_DRDY and SW1=EN/SW2=IO0 per hw_v1.
- PINMAP.md rewritten for hw_v2 (S3-MINI-1U map, full bus map, straps,
  J9–J12 tails); POWER_V2.md added. Flagged, not "fixed": GPIO42 has no
  SAR-ADC channel on S3 so VBAT_SENSE cannot be an analog tap as written;
  expander parenthetical (7 names) does not match the hw_v1 P0–P7 map it
  calls "unchanged"; TP24/SW1 association vs hw_v1 (TP24 was ADS1292_DRDY,
  SW1 was EN); J12 carries 13 signals vs a 12-pos connector class;
  "R54" for STATUS_LED vs hw_v1 R88; MAX86178 interface/supply/LED-driver
  details are NDA-datasheet dependent and marked VERIFY in PINMAP/POWER.
- 2026-09-29 (docs agent): `docs/10-hw-v2-upgrade.md` created (design
  doc: feature motivation, BOM delta, kept list, interfaces, deployment,
  power, risks). `BOM_V2_ADDENDUM.md` created. `VERIFICATION.md` gained
  a hw_v2 section — status honestly "not placed / not routed / not
  ERC- or DRC-checked" plus the pre-fab gate list and `check.sh`
  expectations. `README.md` gained a hw_v2 paragraph in the Board
  section and a branch pointer in the header. `PASSIVES.md` gained a
  hw_v2 additions table with provisional refdes. Provisional numbering
  (U22–U25, R102–R103, C7x, SJ_A/SJ_B) is documentation-only until the
  schematic assigns final designators.
- hw_v2 firmware/config: added `firmware/esp32/profiles/hw_v2.yaml` (full device
  inventory per this spec), mirrored `config/hardware.example.yaml`, documented
  the FIFO timestamp contract + per-device clock anchors in
  `firmware/esp32/core/sensor_driver.h`, registered new channels
  (`ppg.satellite`, `skin.rh`, `skin.temp`, `skin.temp_distal`, `core.temp_est`,
  `tissue.sto2`, `sweat.lactate` RESEARCH-GRADE) in `src/vitalq/core/channels.py`,
  and extended `src/vitalq/core/config.py` (new sensor kinds/models + `cs`,
  `int_pin`, `dnp`, `research_grade` fields) so the profile validates.
- 2026-09-29 (schematic agent): `design.py` — U1 swapped to
  ESP32-S3-MINI-1U-N4R2 (all nets remapped per the interface table),
  USB strap links R102/R103 fitted + R104/R105 DNP to CP2102, new sheet 9
  `max86178.kicad_sch` (U22 + J9/J10/J11 tails, J12 FFC DNP, J13 DNP),
  SHT45 U23, RV-3028-C7 U24 DNP, IM69D130 U25 DNP, D12/Q4 730 nm NIR
  branch on AD5940 GPIO2, R106 MISO link, VBAT_SENSE tap DNP. Emitted
  sheets verified; placement/route/DRC still pending.
- 2026-09-30 (docs): board placed and re-verified — **40.0 × 62.0 mm,
  291 parts** (grew east from the 32 mm target for the J11/J13
  HV-boundary protection row; J10/J11 on the new east edge). TP count
  is 28 (TP28 on RTC_INT). The J11/J13 0 Ω cut-points R118–R122 are
  R_2512_6332Metric (FP_HV), not 0402. LED_AN moved off VBAT to TX_5V
  through R114 (VBAT lacks emitter-Vf + driver-compliance headroom).
  New DNP: R115, D26–D30 clamps; J12/J13 stay DNP. J12 value confirmed
  FH12-14S on KiCad's official FH12-14S-0.5SH land. `check.sh` passes
  end-to-end: ERC 0, placement gates 0, DRC 499 unconnected + 9
  Q1/Q2/Q4 intra-package clearances waived-by-name, silk warnings
  report-only. Board still unrouted — not fab-ready.
