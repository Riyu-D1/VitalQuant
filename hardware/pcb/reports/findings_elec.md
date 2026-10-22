# VitalQ v2 — Electrical Adequacy Audit (fab-gate)

**Target**: `VitalQ_v2_fixv19_fabrication.zip` (fix_v19) @ commit `b9002188`, branch `feature/vitalq-firmware-v2`.
**Method**: pure S-expression extraction of `vitalq_v2.kicad_pcb` (302 footprints, 1,027 pads, 8 NPTH, 45 unconnected pads) → `06_tool_output/01_netlist_pinmap_fixed.csv`; Euclidean pad-to-pad distances; BOM values from `BOM.csv`, MPNs/dielectrics from `BOM_JLC.csv`; datasheet limits from `reports/datasheets/`. Computations in `tools/elec_audit.py`.
**Limitation**: the board source cannot be cryptographically proven to be the exact source of the fab ZIP; netlist taken from board at b9002188. No `PMID`-named net exists on this design — the system rail is `VBAT_SYS`.

---

## 1. Rail map (full data: `04_rails.csv`, 80 load-pin rows, 30 nets)

| Net | Nominal / range | Source | Loads (margin) |
|---|---|---|---|
| `VBUS` | 5.0 V, 4.75–5.25 | J1 USB-C A4/A9/B4/B9 | U2.BQ25170 IN (op 3.0–6.6, abs 7.0 ✓); U5.CP2102N VREGIN pin7 (op 3.6–5.25, abs 5.8 — **margin 0.00 V at USB ceiling**, abs still OK); dividers R63/R64, R50/R51; Q6 gate |
| `VBAT` | 3.0–4.2 V | J2 / U2 OUT pin8 (VSET R75=27k → 4.20 V) | U17.MAX17048 pads 2/3 (op 2.5–4.5 ✓, margin ~0.3 V incl. charger tol); Q6 drain; R107 **DNP** |
| `VBAT_SYS` | 3.0–4.7 V (USB: 5.0−~0.35 V Schottky; BAT: VBAT−I·Rds(on)) | OR: D33 1N5819 (VBUS→SYS) ‖ Q6 AO3401A (VBAT→SYS, gate=VBUS); R93 0R **DNP** | U3.TPS63802 VIN pin10 (1.3–5.5 ✓); U15.TPS61240 VIN A1 (2.3–5.5 ✓) |
| `+3V3` | 3.3 V, 3.234–3.366 (VFB=0.5 V, R23/R24=560k/100k 1%) | U3 TPS63802 pin6 | U1-domain loads, U12 (3.0–3.6 ✓), U13, U14, U18 (2.7–3.6 ✓), U19 (1.65–5.5 ✓), U20/U11 (1.7–5.5 ✓), U23, U24, U25, U4.IN (1.6–6.0 ✓), U3.EN |
| `+3V3_ESP` | 3.3 V | +3V3 via R94 0R (0603, fitted) | U1 ESP32-S3 VDD33 pin3 (3.0–3.6, abs 3.6; hi margin 0.234 V) |
| `+3V3_ANA` | 3.3 V | +3V3 via R95 0R (**no ferrite/RC**) | U6 AFE4900 (2.0–3.6/5.25 ✓), U7 AD5940 (2.8–3.6 ✓), U8 ADS1292R AVDD (2.7–3.6, abs 5.5 ✓) |
| `IOVDD` | 3.3 V | +3V3_ANA via R12 10R | U7 AD5940 IOVDD |
| `+1V8_LDO` | 1.8 V, 1.773–1.827 (±1.5%) | U4 TPS7A2018 OUT pin5 | U4 self, R97 |
| `+1V8` | 1.8 V | +1V8_LDO via R97 0R | U9.VREF1 (1.2–5.5 ✓), U10 AS7341 (op 1.7–2.0, abs 2.2 — **hi margin 0.173 V**), U22 VDD ×2 (op 1.7–2.0, abs 2.2 — **0.173 V**), U26 VCCA (0.65–3.6 ✓), R21/R22 pull-ups |
| `TX_5V_RAW` | 5.0 V, 4.9–5.1 (±2%) | U15 TPS61240 B2/C2 | R96 |
| `TX_5V` | 5.0 V | TX_5V_RAW via R96 0R | U6.TX_SUP F5 (op→5.25: **hi margin 0.15 V**), U22.VLED A1 (3.1–5.5, abs 6.0 ✓), R114 |
| `LED_AN` | 5.0 V | TX_5V via R114 0R | U16 SFH7072 anodes ×4, J12 pin8 flex tail |
| `VDD_CP2102` | 3.3 V | U5 internal LDO | U5 VDD pins 1/2/6/23/27 (3.0–3.6 ✓) |
| `CP_VBUS` | 3.41 V typ, 3.24–3.58 | VBUS · 47.5k/(22.1+47.5k) | U5 pin8 VBUS sense: VIH=VIO−0.6=2.7 V < 3.24 ✓; abs VIO+2.5=5.8 ✓ |
| `VBUS_DET` | 3.00 V, 2.85–3.15 | VBUS · 150k/(250k) | U19 TCA6408 P input (VIH 2.31 < 2.85 ✓, abs 6.5 ✓) |
| `VREF2` | 3.3 V | +3V3 via R18 200k | U9 pins 7/8 (VREF2/EN — TI-recommended tie, no cap fitted) |
| Internal refs | — | U7 (AVDD_REG, DVDD_REG, VREF_1V82, VREF_2V5, VBIAS0/CAP, VZERO0), U8 (VREFP=2.42 V, VCAP1/2), U22 (MX_VREF) | bypass caps only |

**Result: 0 pins fed above absolute maximum; 0 pins above operating maximum** (script flags both classes). Tightest operating margins: CP2102 VREGIN 0.00 V at VBUS ceiling; AFE4900 TX_SUP 0.15 V; +1V8 loads 0.173 V; MAX17048 ~0.3 V.

---

## 2. Decoupling distances (`06_tool_output/decap_distances.csv`, 76 pins)

>10 mm hard flags: **1** — `U9.2` VREF1 → nearest +1V8 cap C90 at **14.61 mm**.
Missing-cap flags: `U9.7` VREF2 — **no capacitor on net at all** (PCA9306 app note recommends 100 pF at VREF2); `U24.6` VBACKUP and `U5.8` VBUS-sense correctly have none (sense/pulled nodes, not decoupling targets).

Notable 5–10 mm supply-pin distances: U18.8 VCC 9.47 mm (C91); U22 C2/D2 VDD 8.69/8.44 mm (C87), A1 VLED 8.53 mm (C89); U8.9 VREFP 6.49 mm (C30); U8.11/27 VCAP1/2 5.66/5.82 mm; U1.3 VDD33→bulk C7 5.93 mm (at-pin C8 100 nF = 7.31 mm); U6.F5 TX_SUP 5.56 mm; U4.1 IN→C60 100 nF 5.28 mm (nearest ≥1 µF: C45 at **9.42 mm**); U3.6 VOUT→nearest ≥1 µF C45 at **15.95 mm** (only C38 100 nF within 4.96 mm).

Good (<3 mm): U12 0.39, U23 1.21, U10 1.44, U13 1.62, U17 2.21, U19 2.25, U15 2.09–2.26, U2.IN→C63 2.87, U25 2.89, U14 2.81 mm.

---

## 3. I2C domains (PCA9306 U9, VSSOP-8: 1=GND, 2=VREF1, 3/4=side-1, 5/6=side-2, 7/8=VREF2/EN)

| Domain | Nets | Pull-ups | Pull-up rail | Devices (7-bit addr) |
|---|---|---|---|---|
| 3V3 | `I2C_SDA`/`I2C_SCL` | R19, R20 = **2.2 kΩ** | +3V3 | U1 ESP32 master; U9 side-2 (5,6); U11 TMP117 **0x48**; U20 TMP117 **0x49**; U13 BME280 0x76; U14 LSM6DSV80X 0x6A; U17 MAX17048 0x36; U19 TCA6408 **0x20** (ADDR=GND ✓); U23 SHT45 0x44; U24 RV-3028 0x52; J9 tail pins 2/3 |
| 1V8 | `I2C_SDA_1V8`/`I2C_SCL_1V8` | R21, R22 = **2.2 kΩ** | +1V8 | U9 side-1 (3,4); U10 AS7341 0x39; U12 MLX90632 0x3A |

- Collisions: **none** — 3V3 set {0x20,0x36,0x44,0x48,0x49,0x52,0x6A,0x76} unique; 1V8 set {0x39,0x3A} unique. Domains separated by U9 anyway.
- Pull-up strength: 3V3 side Isink = 3.3/2200 = **1.5 mA**; 1V8 side 1.8/2200 = **0.82 mA** (both ≪3 mA VOL budget); estimated tr ≈0.85·2200·60 pF ≈ **112 ns** < 300 ns @400 kHz. R21/R22 ≥ TMP117's 2 kΩ floor.
- PCA9306 wiring per TI reference: VREF1=+1V8 (pin2), VREF2=EN→+3V3 through R18=200 kΩ, each side pulled to its own rail. Translation direction/leveling **correct**.
- `MLX90632SLD-DCB-100` option digit "1" = **1V8 I2C variant** (datasheet ordering legend) → correctly placed on 1V8 bus; VIH=0.7·1.8=1.26 V, bus high 1.8 V → 0.54 V margin. VDD=+3V3 ✓.

## 4. 0402 high-µF dielectric / DC-bias

| Ref | Value/Pkg | MPN | Dielectric | Applied | %rated | Est. eff. |
|---|---|---|---|---|---|---|
| C2 | 4.7 µF/0402 | CL05A475**M**P5NRNC | X5R 10 V | VBAT 4.2 V | 42% | ~2.3 µF |
| C20 | 4.7 µF/0402 | CL05A475MP5NRNC | X5R 10 V | VREF_1V82 1.8 V | 18% | ~4.2 µF |
| C9 | 10 µF/0402 | GRM155R61A106ME44 | X5R 10 V | VDD_CP2102 3.3 V | 33% | ~7 µF (>4.7 µF req ✓) |
| C30 | 10 µF/0402 | CL05A106MQ5NUNC | X5R 6.3 V | VREFP 2.42 V | 38% | ~6 µF |
| C87 | 10 µF/0402 | CL05A106MQ5NUNC | X5R 6.3 V | +1V8 1.8 V | 29% | ~7.5 µF |
| C46 | 2.2 µF/0402 | CL05A225KP5NSNC | X5R 10 V | VBAT_SYS ≤4.7 V | 47% | ~1.2 µF |
| C7, C45 | 22 µF/0603 | CL10A226MP8NUNE | X5R 6.3 V | 3.3 V | 52% | ~12 µF |
| C44, C89 | 10 µF/0603 | CL10A106KP8NNNC | X5R 10 V | 4.7/5.0 V | 47–50% | ~7 µF |
| C85 | 47 µF/0603 | CL10A476MQ8QRNC | X5R 6.3 V | 4.2 V | 67% | ~20 µF |
| C1, C47 | 4.7 µF/0603 | CL10A475KA8NQNC | X5R 25 V | 5 V | 20% | ~4.3 µF |

`CL05A475MP` confirmed **X5R** ("A" dielectric code). All high-µF MPNs resolved — no ambiguity at fab level; generic `BOM.csv` alone would be ambiguous (values only). Worst effective-cap risk: C7/C45 22 µF→~12 µF (ESP32 bulk ≥10 µF still met), C30 10 µF→~6 µF at VREFP.

## 5. ESP32-S3 rail & TPS63802 capacity

Path: `VBAT_SYS` → U3.VIN(10) → U3.VOUT(6) = `+3V3` → R94 0R → `+3V3_ESP` → U1 pin3.
- ESP32-S3: VDD 3.0–3.6 V, **≥500 mA burst** required (Espressif HW guideline).
- TPS63802: 2 A @VO=3.3 V for VI≥2.3 V; worst-case VI = VBAT_SYS ≈ 3.0 V (battery, Q6 on) → 2 A available; USB case VI≈4.6 V → 2 A. **≥4× margin on burst requirement.** EN tied to VIN (always-on); inductor L1 = 0.47 µH DFE201612E = datasheet-recommended value.
- VBAT_SYS on battery = 3.0–4.2 V crosses VOUT=3.3 V → buck-boost required; TPS63802 is buck-boost ✓.

## 6. VBUS→charger→SYS continuity (no `PMID` net exists)

`J1.VBUS` → {U2.IN(1), D33 anode, Q6 gate, R123 100 k→GND bleed (fitted — ~50 µA, legal)}.
`U2.OUT(8)` → `VBAT` → {J2.1, U17, Q6 drain, R93 **DNP**}.
`VBAT_SYS` = D33(VBUS) OR Q6(VBAT) → {U3.VIN, U15.VIN, C44/C46}.
Path is continuous; the only DNP-dependent path is R93 (VBAT↔VBAT_SYS redundant feed — not needed, Q6 covers it). No breaks found; system rail works on USB (via D33) or battery (via Q6, VGS = 0−VBAT_SYS < Vth when VBUS absent).

## 7. Findings

| Sev | ID | Finding |
|---|---|---|
| HIGH | E1 | **U3 TPS63802 output bulk cap far**: VOUT(6)→nearest ≥1 µF (C45 22 µF) = **15.95 mm**; only C38 100 nF within 4.96 mm. Datasheet: COUT "as close as possible", CO,eff ≥7 µF. Adds loop inductance/ripple on the main 3V3 rail that feeds every sensor + ESP32. |
| HIGH | E2 | **U4 TPS7A2018 has no ≥1 µF input cap nearby**: IN(1)→nearest 100 nF 5.28 mm; nearest ≥1 µF (C45) = **9.42 mm**. Datasheet wants input cap ≥OUT cap nominal, close to IN/GND. The +1V8 rail (AS7341/MAX86141 VDD, 0.173 V margin) sits behind this. |
| MEDIUM | E3 | **PCA9306 VREF2 has no 100 pF cap at all** (net `VREF2` = R18+U9 pins 7/8 only); VREF1(2)→nearest +1V8 cap **14.61 mm** (>10 mm flag). Datasheet recommends 100 pF on VREF2. |
| MEDIUM | E4 | **U22 MAX86141 decoupling borderline**: VDD pins C2/D2 → C87 at 8.69/8.44 mm, nearest 100 nF C88/C86 ≈ 9.7–9.9 mm; VLED A1 → C89 10 µF at 8.53 mm. Pulsed-LED AFE, high di/dt. |
| MEDIUM | E5 | **U18 W25Q512 VCC(8)→C91 = 9.47 mm**; QSPI flash at high clock; borderline. |
| MEDIUM | E6 | **ESP32 decoupling layout inverted**: at-pin 100 nF (C8) is at **7.31 mm** while bulk C7 22 µF is closer at 5.93 mm; guideline wants 100 nF at the pin. Effective bulk ~12 µF (derated) still ≥10 µF. |
| MEDIUM | E7 | **ADS1292R internal-rail bypasses 5.7–6.5 mm away**: VCAP1 C32 5.66, VCAP2 C33 5.82, VREFP C30 10 µF 6.49 mm (C30 derates to ~6 µF at 2.42 V vs datasheet 10 µF settling condition). |
| MEDIUM | E8 | **`+3V3_ANA` is a 0R link to the buck-boost rail** (R95), no ferrite/RC filtering for ADS1292R AVDD, AFE4900, AD5940 analog domains; only IOVDD gets R12=10R. Functional, but analog PSNR relies entirely on local 100 nF caps. |
| MEDIUM | E9 | **U6 AFE4900 TX_SUP margin thin**: TX_5V hi 5.10 V vs op max 5.25 V → **0.15 V**; TPS61240 ±2% + tolerance stackup leaves no headroom beyond spec. |
| MEDIUM | E10 | **+1V8 loads at 0.173 V margin** (1.827 V hi vs 2.0 V op max on AS7341 + MAX86141 VDD); pass, but thin for a lithium-powered product seeing cable/reg tolerances. |
| COSMETIC | E11 | R123 = **100 kΩ fitted** GND↔VBUS — a legal bleed (~50 µA), not a short; earlier suspicion of a 0R VBUS-GND bridge is **disproven** (BOM line 43/238). |
| COSMETIC | E12 | R107 100k VBAT→SPARE_IN is **DNP**; if ever populated it would put 4.2 V on an ESP32 GPIO (abs max 3.6 V) — needs a divider, do not fit as-is. Advisory only. |
| COSMETIC | E13 | CP2102N VREGIN margin = **0.00 V** at USB-spec ceiling (5.25 V = op max; abs 5.8 V OK). Legal but at boundary. |
| PASS | — | No over-abs-max on any of 80 load rows; I2C domains/addresses/pull-ups all legal; MLX90632 is the 1V8 variant on the 1V8 bus; TPS63802 covers 500 mA burst with ≥4× margin; Q6/D33 power-path topology correct; R93/R104/R105/R107 are DNP and so marked. |
