# ATOMIC-LEVEL PCB VERIFICATION — fix_v21

Exhaustive failure-mode, wiring-trap, and stress analysis of `vitalq_v2.kicad_pcb`.
Method: pin-level netlist audit (946 pin-net pairs, all `pinfunction`/`pintype` data),
per-component orientation checks, extended ngspice stress corners, single-fault FMEA,
per-skin-land leakage model, and end-to-end sepsis signal-chain mapping.
Sources: `atomic/nets_nodes.json`, `atomic/comps.json`, sims `b01–b20` + `stress/` reruns.

## Verdict summary

| Class | Count |
|---|---|
| Wiring/orientation defects found | **0** |
| Real design notes (non-defect) | 5 |
| Single-point failures enumerated | 25 (all expected single-bias topology) |
| Sim blocks re-run + extended | 20 + 3 stress variants — all consistent |

**No wiring error or polarity fault was found anywhere in the design.** Every interface
direction, every polarized part, every rail feed, and every pin map checked against
datasheets passed. The two closest calls — the MAX86141 3.3V-input/1.8V-core question and
the MLX90632 rail/bus mismatch — both resolve as *correct by design* (6V-tolerant inputs;
the `-100` SKU is specifically the 1V8-I²C variant).

## A. Wiring-trap audit — all PASS

| Check | Evidence |
|---|---|
| UART crossing | U1.37 `TXD0`→R72→U5.25 `RXD`; U5.26 `TXD`→R73→U1.36 `RXD0` — both directions correct |
| USB pair | J1 `D+`(A6/B6)→R102→U1.14 `IO20` and R104→U5.4 `D+`; `D-`(A7/B7)→R103→U1.13 `IO19` and R105→U5.5 `D-`; USBLC6 IO1↔D+, IO2↔D- |
| SPI direction | MOSI→`DI/SDI/DIN/MOSI/I2C_DAT` slave pins; SCK→`SCLK/CLK/I2C_CLK`; MISO slaves (MISO_AFE/MX/FL/ADS/AD) join through series R89–92/R106 |
| Chip selects | each `CS_*` reaches exactly one slave CS pin; CS_MAX86178 also drives U26 `OE` (asserted only during MAX transactions — correct) |
| I2C | SCL pins on SCL nets, SDA on SDA; PCA9306 VREF1=+1V8, VREF2/EN=+3V3 via R18 200k |
| Interrupts | every OC/open-drain alert (CHG_STAT/PG, GAUGE_ALRT, TMP117_ALERT, EXP_INT, MAX86178_INT, ADS1292_DRDY) has a pull-up + reaches U1/U19 |
| Battery | J2: pad1=VBAT(+), pad2=GND(−), pad3=TS |
| Diodes | D1–D9/D21–D30 TPD1E10B06 bidirectional ESD: signal→GND everywhere correct; D33 Schottky `A`→VBUS, `K`→VBAT_SYS (feeds rail); D10 LED A→+3V3, K→AS7341 `LDR` sink; D11/D12 A→IR_AN/NIR_AN, K→FET drains; D25 A→LED_A→R88→U1.10, K→GND |
| FETs | Q1/Q4 N-FET low-side LED sinks (G=gate, S=GND, D=cathode); Q2 G=CHG_DIS pulls battery TS low to disable charge; Q6 P-FET G=VBUS/S=VBAT_SYS/D=VBAT — conducts when USB absent, body-diode orientation correct for ideal-OR |
| Q3 auto-reset | emitters on CP_DTR/CP_RTS, collectors → ESP_EN/ESP_IO0, bases via R70/R71 — esptool-style auto-program circuit |
| Level shifter | U26 SN74AXC2T245: VCCA=+1V8 ≤ VCCB=+3V3 (required), DIR1 high=A→B (1.8V SDO→3.3V MISO), unused ch2 input B2 tied to GND, output left open — textbook-correct |
| Power ICs | U2 BQ25170, U3 TPS63802, U4 TPS7A20, U15 TPS61240, U17 MAX17048, U18 W25Q512 — all pin maps verified vs datasheets incl. EP=GND |
| ADS1292R | CLKSEL=+3V3_ANA (internal clock), CLK=GND, START=GND (SPI-command start required — firmware note), RLD loop complete (RLDOUT→R69/D3→RLD_CLAMP→R34→RLD_PAD→J5.3, RLDINV/RLDREF feedback) |
| AD5940 | all internal-rail decoupling present (VREF_1V82/2V5, AVDD/DVDD_REG, VBIAS_CAP, VZERO0, RC0_0/1, AIN4_LPF0); RCAL=R13; electrode mux pins map to EDA/BIOZ chains |
| AFE4900 | ECG INP/INM chains complete (pad→R35/36→ESD→R44/46→C56/57→pin, biased via R43/45 to shared RLDOUT); PD/TX channels wired |
| Conflicts | 0 output-vs-output; 0 no_connect pins on named nets; all 64 single-pin nets are intentional NCs |

## B. Design notes (verified OK, worth recording)

1. **AFE_CLK**: net has only U6.F1 input + R10 pull-down — *not* a missing driver. AFE4900 has an internal 4 MHz oscillator; pull-down is the correct termination for internal-clock mode (TI datasheet: "clocked by external or internal clock"). Firmware must select internal clock.
2. **RTC_INT**: U24 INT → R112 pull-up + TP28 only — never reaches U1. RTC alarm cannot wake the ESP32; VBACKUP is tied to GND (no backup supply → time lost on power removal). Likely intentional; flag as a design-review note.
3. **Net naming**: `CS_MAX86178`/`MAX86178_INT` actually connect to U22 = **MAX86141ENP+** — no MAX86178 exists on the board. Stale net labels only; connectivity correct.
4. **MAX86141 inputs at 3.3V with 1.8V core**: verified safe — datasheet abs-max for SCLK/SDO/SDI/CSB/INT is −0.3…+6.0V, VIH min 1.4V. Only SDO needed shifting (U26) since its 1.8V swing wouldn't reach ESP32 VIH.
5. **U12 MLX90632 on 1V8 bus**: `-DCB-100-SP` = the 1V8-I²C ordering variant — correct SKU (VIH=0.7·VI2C≈1.26V ≪ 1.8V pull-up rail).

## C. FMEA — 25 single-bias points

Every net whose only DC bias is one resistor. If it fails open, the net floats:

| Part | Rail→Net | Failure impact |
|---|---|---|
| R93 (0Ω) | VBAT→VBAT_SYS | **Total battery loss** (USB-only operation) |
| R94 (0Ω) | +3V3→+3V3_ESP | **ESP32 unpowered — dead board** |
| R5 (10k) | +3V3→ESP_EN | **Boot failure** (EN floats) |
| R6 (10k) | +3V3→ESP_IO0 | Wrong boot mode possible |
| R19/R20 (2.2k) | +3V3→I2C_SDA/SCL | **All 3V3 I²C sensors dead** (TMP117×2, BME280, SHT45, LSM6, gauge, RTC, TCA6408) |
| R21/R22 (2.2k) | +1V8→I2C_1V8 | MLX90632 + AS7341 + PCA9306 1V8 side dead |
| R12 (10Ω) | +3V3_ANA→IOVDD | AD5940 digital I/O dead |
| R18 (200k) | +3V3→VREF2 | PCA9306 disabled → 1V8 bus isolated |
| R7/R52/R98/R116/R117 (10k) | →CS_AD5940/FLASH/ADS1292/AFE4900/MAX86178 | Floating CS → that slave dead, possible SPI corruption |
| R49/R60/R53/R65/R74/R112/R113/R99 | →CHG_STAT/PG, EXP_INT, ALERTs, DRDY | Status/interrupt lost for that line only |
| R47/R109 (100Ω) | +3V3→IR_AN/NIR_AN | That emitter dead |

Mitigation: none needed electrically (standard topologies); relies on assembly quality + ATE. The I²C pull-up quad (R19–R22) is the largest fan-out risk.

## D. Stress results (beyond standard corners)

| Test | Result |
|---|---|
| b05 TPS63802 @ VIN 2.5/2.8/3.0V | +3V3 = 3.298V ss, 3.24V dip at all corners — regulates through deep discharge |
| b02 power path @ VBAT 2.5V, SOC 0 | settles 3.08V battery-backed; ~2.56V transient dip on USB hot-plug (momentary, marginal) |
| b18 land fault @ 12V | 231µA through 51k barrier into clamp path; patient-side (IC-faulted-to-rail) = 66µA |
| b18 land fault @ 5V (corners 0/25/45°C) | patient-side ≈ 66–100µA model range |
| Full b01–b20 suite | all OK (VBUS 4.75/5/5.5, VBAT 3.0/3.7/4.2, T 0/25/45 baked in) |

Numeric comparison only (not a compliance claim): IEC 60601-1 BF figures are NC 100µA / SFC 500µA. IC-side fault path (66µA) is within NC bounds; the 12V-into-land fault (231µA) exceeds NC but is within SFC — single-fault charger-malfunction scenario, risk-file item.

## E. Sepsis signal-chain verification

Every required measurand of `sepsis_v11` (sens 0.98 / spec 0.853, seed 7) maps to a verified hardware path:

| Measurand | Path (verified end-to-end) |
|---|---|
| HR / RMSSD / SDNN (ECG) | J5.1/2 → R32/33 → ECG_P/N → D1/D2 ESD → U8 IN2P/IN2N (ADS1292R ch2) + J5.4/5 → AFE4900 ECG pair (redundant) |
| Respiration rate | ADS1292R RESP_MODP/N (impedance pneumo via R30/31 on ECG lines) + EDR + PPG-derived — triple-source |
| SpO2 | PPG red/IR: SFH7072(U16) emitters ← TX1–4 drivers (U6) → PD returns → U6 INP/INM + U22 MAX86141 PD_K → LED1–3_K |
| Core temp | TMP117 (U11, contact) + MLX90632 (U12, IR) → algo.core_temp fusion; +U20 second TMP117 |
| EDA | J6 CE/SE/RE/DE → R76–79 → *_SURGE → ESD D6–D9 → U7 SE0/CE0/RE0/DE0 (AD5940) |
| BioZ | J7 FP/FN/SP/SN → R80–83 → ESD D21–D24 → U7 AIN0–3 |
| Motion artifact gate | LSM6DSV80X (U14) → LSM6_INT1→U1.4 |
| Electrochem/sweat | J11 CE/RE/WE → R120–122 → U7 AFE4/3/AIN6 |
| Control/PSRAM | U1 N16R8 (octal PSRAM on reserved pads) — firmware buffers OK |

Noise budget (from crosstalk model): worst coupling I2C_SCL_1V8→EDA_DE_PAD ≈0.36% (~12mV est.) — within AD5940 EDA range, firmware-filterable; ECG inputs see ≤2mV model coupling with >50k CMRR (b12) — signal margins hold. All sample rates the EWS model needs (ECG ~1kHz, PPG 25–100Hz, temp/EDA/BioZ slow) are inside each AFE's verified range.

## F. Remaining residual risks (not verifiable in EDA)

Assembly/tombstoning of the 25 single-bias resistors; real RF performance in-case; actual EMC/leakage on hardware; solder-joint quality (POFV on U1 EP requested); case-fit items from the mechanical check; firmware correctness (AFE internal-clock select, START command for ADS1292, LED interleave, octal-PSRAM config).

---

## Round-3: full-battery sweep (tooling survey: MCP brain/supabase = file/DB only, pinokio pterm not installed → full KiCad+ngspice+python stack used)

### G. DFM geometry census (board file, 901 vias / 8719 track segs / 1005 pads)
| Metric | Result | Verdict |
|---|---|---|
| Min track width | 0.1016 mm | OK vs 8L 3.5mil fab floor |
| Via sizes | 741× 0.20/0.40mm, 68× 0.20/0.45, 81× **0.15/0.25**, 11× 0.15/0.30 | 0.15mm drills at JLC advanced-process boundary — confirm at order |
| Min annular ring (vias) | 0.05 mm | borderline-minimum; acceptable |
| Min hole→edge | 0.381 mm | OK (≥0.3mm) |
| **Copper→edge** | **−0.637 mm at R122.1** (pad1 spans y 61.79–65.14, bottom edge y=64.5) | **MINOR**: pad overhangs outline; fab clips copper at edge. Pad retains ~2.7mm — solderable, but verify intentional / expect DFM query |
| Pad-to-pad gaps <0.09mm | 36 bbox-level flags | All resolve as opposite-side-EP/pad bbox artifacts (U2/U8/U19/QFN); DRC 0-shorts confirms no real contact |
| Orphan zone fragments | **0** of 10 fill fragments — every pour contains a same-net pad/via | PASS |
| Courtyard bbox overlaps | 128 (bbox incl. silk extent) | informational only — DRC clean, no body collisions expected on a dense wearable board |
| Dangling stubs | 56 real (DRC class) | cosmetic, all at deliberate route landings |
| Silkscreen | min-height/thickness warnings ×10, silk-over-copper ×8, edge-clearance ×15 | cosmetic; JLC auto-clips silk at mask — standard |

### H. Via-in-pad census — **64 pads**, broader than the U1-EP 9
- U1.41 EP: 9 GND vias (known, POFV noted)
- **U6 (AFE4900) EP sub-pads**, **U7 (AD5940) EP** incl. one **+3V3_ANA via inside U7.F3**
- Passive/connector pads: C89.2, C62.2, J2.2, U25 pads, others
→ **Fab note expanded**: specify via-in-pad (POFV/plugging) not just for U1 but all EP/pad-sited vias, or accept solder-wicking/tombstone risk on the passive pads.

### I. Power-net track capacity (IPC-2221, 10°C rise, 1oz outer)
| Net | min width | I-cap thin seg | peak need | Note |
|---|---|---|---|---|
| VBAT / VBAT_SYS | 0.145 mm | ~0.59 A | ~1.0 A burst (b-boost inrush at low VIN) | OK — rails ride plane pours; thin segs are pad-entry stubs; at 30°C rise ~1 A available |
| +3V3 / +3V3_ESP | 0.102 mm | ~0.46 A | ~0.5 A Wi-Fi burst | same — pours carry bulk |
| TX_5V | 0.102 mm | ~0.46 A | 0.45 A design burst / 0.9 A if unleashed | interleave required regardless |
| VBUS | 0.102 mm | ~0.46 A | 0.7 A worst | 1.2mm thin stub only; pour handles |

### J. Interface deep-checks (all verified from netlist + pinfunctions)
- **I²C 3V3 bus** (14 members): addresses 0x20(TCA6408 A0=GND), 0x36(MAX17048), 0x44(SHT45), **0x48(U11 TMP117 ADD0=GND)**, **0x49(U20 TMP117 ADD0=+3V3)** — no collision, 0x52(RV3028), 0x6x(LSM6 SA0 pin strapped), 0x76(BME280 SDO=GND, CSB=+3V3=I²C mode). **No duplicates.**
- **I²C 1V8 bus**: 0x39(AS7341) + 0x3A(MLX90632) via PCA9306 (VREF1=1V8, VREF2/EN→200k→3V3 per TI spec).
- **U26 level shifter**: DIR1=+1V8 → A1→B1 (MX_SDO_1V8→MISO_MX), **OE gated by CS_MAX86178** — MISO only driven during MAX transactions (proper shared-MISO hygiene). DIR2=GND, unused input B2 tied off. VCCA(1V8)<VCCB(3V3) per spec.
- **U19 TCA6408**: RESET←ESP_EN (expander resets with SoC — outputs Hi-Z during boot, check FW re-init), P-ports drive AFE_RESETZ/ADS_PWDN/AD5940_RESET/TX5_EN/IR_GATE/CHG_DIS, P5 reads CHG_STAT, P6 reads VBUS_DET — expander used to off-load scarce ESP32 GPIOs. Sensible.
- **U25 IM69D130 PDM mic**: SELECT=GND (left slot), CLOCK/DATA to IO40/41.
- **U1 pad map complete**: 41 pads decoded; all 34 used nets correct; IO35/36/37/45/46/47/48 NC — **octal-PSRAM-reserved GPIOs correctly unconnected**; straps IO0/IO3/IO45/IO46 sane; USB=IO19/20.
- **Connector pinouts**: J1 USB-C (CC1/CC2→R56/R57, SBU NC, symmetric DP/DM pairs ✓ reversible); J10/J12 optical tails map identically (LED1-3_K, LED_AN, PD_K/PD_A — cross-consistent); J2 LiPo 1=VBAT/2=GND/3=TS; J5 ECG 5-land; J6 EDA 4; J7 BioZ 4; J11 sweat 3; J13 MX ECG pair; J8 TC2030 Tag-Connect (TX/RX/EN/IO0+power — full SWD-less programming path); J3 FSR; J9 I²C tail.
- **unconnected-*() nets**: zero live multi-pin — all honest NCs.

### K. Component value math (from netlist + BOM)
| Item | Values | Result |
|---|---|---|
| BQ25170 ISET | R3=1.5k → I=300/1.5k | **200 mA** charge — matches sims |
| BQ25170 VSET | R75=27k | **4.20 V** regulation — LiPo correct |
| TPS63802 FB | R23/R24 = 560k/100k → 0.5V×6.6 | **3.30 V** exact |
| VBUS_DET | R50/R51 = 100k/150k → 0.4·VBUS | 2.0V@5V → safe into TCA6408 input |
| Status LED | R88=150Ω on IO17 | ~9mA — within GPIO drive |
| ECG bias | R43/R45 = 5.11M to RLDOUT | high-Z AFE input bias ✓ |
| Electrode barriers | 17 lands, all 51k (R34 et al, 2512 pkg) | uniform — deliberate HV-rated package |
| Inductors | L1=0.47µH (TPS63802), L2=1.0µH (TPS61240) DFE201612 | within vendor ranges; Isat ~1.9–2.6A adequate for interleaved design loads |
| TPS7A20 | fixed 1.8V, IN=+3V3 | 1.5V headroom ≫ dropout; thermal note: ~0.15–0.3W at 100–200mA 1V8 load → warm but b19 sim OK |

### L. Extra stress sims (new this round)
| Deck | Result |
|---|---|
| **b06_overload** — all 3 LED chains simultaneous 900mA, worst VIN | TX_5V droops to **4.62V**; modeled L peak **7.0–7.7A** ≫ Isat → **simultaneous mode is genuinely unsustainable; interleave mandatory** |
| **b01_500ma** — strict 500mA source vs 700mA (200chg+500sys) | VBUS foldback → **~0.49V**: source collapses. **But** power-path OR auto-hands-off to battery (verified in b02) → device keeps running, just won't charge on weak ports |

### Round-3 verdicts
- New real issues: **2 MINOR** — R122 pad-over-edge (−0.64mm), expanded via-in-pad census (64 vs known 9). Neither blocks function; both are order-time notes.
- Everything else — **PASS or confirmed-design**: addresses, GPIO reservations, pinouts, dividers, protections, pours, drills, silks.
- Hard requirements that remain firmware: LED interleave (sim now proves overload is unsustainable), AFE internal-clock select, ADS1292 START, expander re-init after reset.
