# hw_v2 BOM addendum

Addendum to `vitalq_hw_v1_bom.csv` — the regenerated merged BOM is
produced by the schematic step, not this file. Per `HW_V2_SPEC.md`.
Designators are provisional where the spec did not assign one; the
schematic agent's numbering wins. Status column: **add** = new on-board
part, **replace** = in-place upgrade, **dnp** = on the schematic, do not
populate, excluded from the JLC BOM by the hw_v1 convention. LCSC
numbers are not assigned here — stock and basic/extended tier go through
the usual BOM pass.

Research prototype. Nothing here is a medical-device or sourcing claim.

| Refdes | Value / MPN-class | Footprint | Side | Bus / addr | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| U1 | ESP32-S3-MINI-1U-N4R2 | Module, 15.4 × 15.4 mm, U.FL | top (hw_v1 site) | — | replace | Replaces ESP32-WROOM-32E-N8R2. Native USB GPIO19/20, UART0 GPIO43/44 → CP2102. 4 MB flash + 2 MB PSRAM. Pin map vs symbol is a verification gate |
| U22 | MAX86178 (ECG/PPG/BioZ AFE) | WLP-49 | TBD | SPI, CS GPIO38, INT GPIO39 | add | Likely consign/global-source — check JLC tier early; escape probably needs POFV. Local decoupling set below |
| U23 | SHT45-AD1B | DFN-4, 1.5 × 1.5 mm | bottom, skin face | I2C3V3 0x44 | add | Shares R19/R20 pull-ups; C76 100 nF local decoupler fitted |
| D12 | 730 nm LED (SFH 4735-class — emitter VERIFY) | LED_0402 | bottom, optical cluster | — | add | Cathode to Q4 drain; anode via R109 to +3V3. Confirm emitter before fab |
| Q4 | CSD13380F3T (Q1 clone) | CSP-3-class (same land as Q1/Q2) | TBD | gate = NIR730_GATE | add | AD5940 GPIO2 (ball E1) drives the gate |
| R109 | 100 Ω 0402 | R_0402 | TBD | — | add | D12 series R, +3V3 → NIR_AN; confirm vs chosen LED Vf |
| R110 | 100 kΩ 0402 | R_0402 | TBD | — | add | NIR730_GATE hold-down |
| C79, C81 | 100 nF 0402 | C_0402 | with U22 | — | add | MAX86178 AVDD (+3V3_ANA) and DVDD (+3V3) HF bypass |
| C80 | 1 µF 0402 | C_0402 | with U22 | — | add | MAX86178 AVDD bulk |
| C82 | 1 µF 0402 | C_0402 | with U22 | — | add | MAX86178 LED_DRV_SUP (VBAT) bypass |
| C83 | 1 µF 0402 | C_0402 | with U22 | — | add | MAX86178 MX_VREF bypass — VERIFY value vs NDA datasheet |
| R113 | 10 kΩ 0402 | R_0402 | with U22 | — | add | MAX86178_INT open-drain pull-up to +3V3 |
| R114 | 0 Ω 0402 | R_0402 | by U22/J10 | — | add | TX_5V → LED_AN anode link for the J10 shared-anode rail — moved off VBAT (no headroom for ~3 V Vf + driver compliance); time-shared with AFE4900 TX under TX5_EN |
| R116 | 10 kΩ 0402 | R_0402 | with U6 | — | add | CS_AFE4900 pull-up to +3V3_ANA (the AFE's own rail) |
| R117 | 10 kΩ 0402 | R_0402 | with U22 | — | add | CS_MAX86178 pull-up to +3V3 — same role as R98/R7/R52 |
| R106 | 0 Ω 0402 | R_0402 | debug band | — | add | MISO_MX → SPI_MISO isolation link (fifth in the R89–R92 pattern) |
| R107, R108 | 100 kΩ 0402 | R_0402 | by U1 | — | dnp | Logic-level VBAT-present divider into SPARE_IN (GPIO42, digital-only) — never an analog sense tap |
| U24 | RV-3028-C7 or equiv RTC | TBD | TBD | I2C3V3 0x52 | dnp | Integrated 32.768 kHz claimed for C7 — verify land + crystal integration against the Micro Crystal datasheet. + C77 100 nF VDD bypass, R112 10 kΩ on RTC_INT |
| U25 | IM69D130-class PDM mic | TBD | TBD | PDM CLK/DAT GPIO40/41 | dnp | Footprint + firmware optional. + C78 100 nF VDD bypass |
| J9 | 4 × SMD pads (3V3/SDA/SCL/GND) | Pads_SMD | TBD | tail device 0x4A | add | Bare pads, not an assembled part — same treatment as J5/J6/J7 (excluded from JLC BOM) |
| J10 | 6 × SMD pads | Pads_SMD | TBD | LED1/2/3_K, LED_AN, PD_K/PD_A | add | Bare pads as above |
| J11 | 3 × SMD pads (WE/RE/CE) | Pads_SMD | TBD | AD5940 mux | add | Bare pads. RESEARCH-GRADE label required on schematic notes |
| J12 | FH12-14S-0.5SH FFC, 14-position | TBD | TBD | carries J9+J10+J11 (13 nets), pin 14 open | dnp | 13 signals needed the 14-pos class, not the spec's 12-pos; assembly option vs discrete tails |
| J13 | 2 × SMD pads | Pads_SMD | TBD | MX_ECG_INP / MX_ECG_INM | dnp | Optional MAX86178 ECG input pair; bare pads as J9–J11 |
| R118, R119 | 0 Ω (BOM: 25121WJ0000T4E / LCSC C2908946) | R_2512_6332Metric (FP_HV) | east column — R118 F (35.7, 44.3), R119 F (35.7, 56.5) | J13_INP/INM → MX_ECG_INP/INM | add | Series cut-points on the DNP J13 pair — same HV-boundary footprint pattern as R76–R83 (~4.7 mm pad span), not 0402. Removing one isolates the tail |
| R120, R121, R122 | 0 Ω (BOM: 25121WJ0000T4E / LCSC C2908946) | R_2512_6332Metric (FP_HV) | east column B — R120 (35.0, 48.5), R121 (34.3, 39.7), R122 (34.3, 58.0) | J11_WE/RE/CE → SWEAT_WE/RE/CE | add | Series cut-points on the RESEARCH-GRADE J11 sweat site, same FP_HV land. RESEARCH-GRADE label required on schematic notes |
| D26, D27, D28 | TPD1E10B06DPYR | vitalq:TPD1E10B06_DPY | with the J11 chain (IC side) | SWEAT_WE/RE/CE → GND | dnp | Clamp footprints on the IC side of R120–R122 — same pad → series → clamp order as the ladder |
| D29, D30 | TPD1E10B06DPYR | vitalq:TPD1E10B06_DPY | with the J13 chain (IC side) | MX_ECG_INP/INM → GND | dnp | Clamp footprints on the IC side of R118/R119 |
| C84 | 1 µF 0402 | C_0402 | regulator decoupling row | — | add | +3V3 → GND; +3V3 is the input rail of U4 |
| R115 | 10 kΩ 0402 | R_0402 | with U24 | — | dnp | RTC_VBK → GND — RV-3028-C7 requires unused VBACKUP to reach VSS through 10 kΩ, not a dead short |
| TP28 | test pad, 1.0 mm | TestPoint_Pad_D1.0mm | top (5.02, 37.5) | RTC_INT | add | 28th test point; excluded from JLC BOM/CPL like TP1–TP27 |
| R102, R103 | 0 Ω 0402 links | R_0402 | top, near J1 | — | add | Default strap, **fitted**: J1 D+ → USB_DP_S3, D− → USB_DM_S3 → S3 native USB (GPIO20/19). BOM maps value 0 → 25121WJ0000T4E (LCSC C2908946, value-keyed — also covers the 0402 links) |
| R104, R105 | 0 Ω 0402 links | R_0402 | top, near J1 | — | dnp | Alternate strap: J1 D± → USB_DP_CP/USB_DM_CP → CP2102 USB. **Never fit both pairs — two USB devices cannot share one D+/D− pair** |

## Off-board / assembly items — not in the PCBA BOM

| Item | Source class | Notes |
| --- | --- | --- |
| Tail TMP117 / TMP119 | J9 flex-mounted | Must be strapped to **0x4A** (TMP117 ADD0 → SDA). Soldered to the tail, not the board |
| SFH7050A | J10 satellite PPG | Off-board optical sensor on the satellite tail; driven by MAX86178 natively |
| Ag/AgCl snap electrodes | J5/J6/J7 tails | Non-sterile single-use consumables; user-supplied |
| ZHF insulation dome | over U20 | Closed-cell foam dome, adhesive-carrier assembly note; not a part |
| U.FL antenna + coax | U1 | External antenna is now required — the module has no PCB antenna |

## Carried over unchanged

Everything in `vitalq_hw_v1_bom.csv` not listed above stays: the defib
ladder (R32–R36, R76–R83, D1–D9, D21–D24), TP1–TP28, J8, the power
tree, W25Q512, J5/J6/J7 pads, all hw_v1 sensors, and both TMP117s.
DNP handling, consign flagging, and the JLC BOM/CPL exclusions follow
the hw_v1 convention. Note the BOM maps every 0 Ω value to
25121WJ0000T4E / LCSC C2908946 (the 2512 jumper) — that lands on the
0402-footprint 0 Ω links too; only the 0603 links R93/R94 keep
0603WAF0000T5E (C21189).
