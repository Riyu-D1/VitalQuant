# VitalQ v2 fix_v25 — first-power-on bring-up procedure

All rail/debug test points are on the **top (lid) side**, `TestPoint_Pad_D1.0mm`.

Rail TPs: TP1 VBUS · TP2 VBAT · TP3 +3V3 · TP4 +1V8 · TP5 TX_5V ·
TP6 VDD_CP2102 · TP7/TP8 GND · TP29 RLDREF · TP30 RLDOUT · TP31 VBAT_SYS ·
TP32 +3V3_ANA · TP33 TX_5V_RAW · TP34 RTC_VBK · TP35 +3V3_ESP · TP36 IOVDD ·
TP37 CHG_ISET · TP38 CHG_VSET · TP39 VBUS_DET · TP40 TS.

Debug TPs: TP9 ESP_EN · TP10 ESP_IO0 · TP11/TP12 ESP_TX/ESP_RX ·
TP13/TP14 I2C_SDA/SCL (3V3 bus) · TP15/TP16 I2C_SDA_1V8/SCL_1V8 ·
TP17–19 SPI · TP20–23 chip-selects · TP24/TP25 DRDY/ADC_RDY ·
TP26 EXP_INT · TP27 CHG_STAT · TP28 RTC_INT.

Labels: **checked** = verified against the v25 schematic/PCB or prior audit
files; **inferred** = reasoned from checked facts/datasheet behaviour;
**guess** = unverified — confirm on the bench.

## Step 1 — visual inspection and resistance-to-GND (unpowered)

- [ ] Inspect soldering under magnification: U3 WSON orientation, U6/U22 BGA
  voids, J1 USB-C tongue, all J2–J12 pad lands, TP29–TP40 pads clean and not
  shorted to adjacent copper (visual — **checked**: pads verified DRC-clean).
- [ ] BOM check: R104/R105 (USB D+/D− to CP2102), R93, R107/R108, D29/D30,
  R118/R119, J12, J13 are **DNP** (**checked** — v24 BOM).
- [ ] Resistance to GND (TP7/TP8) at each rail TP — expected rough values,
  power off, no battery, no USB:

| TP | Rail | Expect to GND | Label |
|---|---|---|---|
| TP1 | VBUS | >50 kΩ (input caps only) | guess |
| TP2 | VBAT | >50 kΩ (charger OUT + caps) | guess |
| TP31 | VBAT_SYS | >50 kΩ (VBUS=0 → R123 pulls Q6 gate low → Q6 on, but VBAT floats too) | guess |
| TP3 | +3V3 | >10 kΩ | inferred (v4 bring-up measured similar) |
| TP32 | +3V3_ANA | ≈TP3 (0 Ω link R95) | checked |
| TP35 | +3V3_ESP | ≈TP3 (0 Ω link R94) | checked |
| TP4 | +1V8 | >10 kΩ (LDO unpowered) | inferred |
| TP5/TP33 | TX_5V(_RAW) | >50 kΩ (boost disabled) | guess |
| TP36 | IOVDD | ≈TP3 via R12 10 Ω | checked |
| TP34 | RTC_VBK | ≈10 kΩ (R115 to GND) | checked |

- [ ] **TS must see exactly ONE pull-down to GND — never two.** The TS net
  carries BQ25170 pin 3, J2 pin 3 (pack NTC), R61 (on-board 10 kΩ NTC
  B3380, C77131), the Q2 CHG_DIS pull-down drain and TP40 (**checked** —
  schematic). R61 is **fitted** per the v25 BOM/CPL (**checked** — listed
  in BOM_JLC.csv and CPL.csv, no DNP flag) even though the schematic note
  says "R61 stays DNP" — **whether R61 should be fitted is an OPEN
  DECISION for Riyansh** (board/BOM question; do not resolve it by
  editing the board/BOM here). If the fitted pack also has a 10 kΩ NTC on
  J2 pin 3, the two sit in parallel → ≈5 kΩ → TS ≈0.19 V ≈ the hot
  threshold → charge suspends (see Step 3 TS note). Valid configs: pack
  NTC with R61 depopulated, **or** R61 alone with a 2-wire (no-NTC) pack.

## Step 2 — current-limited bench supply: VBAT first, then USB VBUS

- [ ] Bench supply **3.7 V, current limit 150 mA** → TP2 (VBAT) / TP7 (GND).
  Idle draw expected <30 mA (**inferred** — same power tree as v4).
- [ ] If current pegs at the limit, power off immediately and find the short
  (Step 8).
- [ ] Remove bench supply; fit the LiPo on J2 (polarity: pin1=VBAT, pin2=GND,
  pin3=NTC — **checked** against J2 pad nets).
- [ ] Then apply **USB VBUS: 5.0 V, current limit 300 mA** (USB-C breakout or
  host port) at TP1 / TP8. VBUS feeds the BQ25170 (ICHG=200 mA by R3 1.5 kΩ —
  **checked**) so 300 mA covers charge + system idle.
- [ ] TP39 VBUS_DET should read ≈3.0 V when VBUS is present (R50 100 kΩ /
  R51 150 kΩ divider → 0.6 × VBUS; 2.85–3.15 V across the 4.75–5.25 V
  USB range — **checked** against schematic), ≈0 V without it; sensed by
  the U19 expander input (**inferred** pin read).

## Step 3 — expected rail voltages and where to measure

| TP | Rail | Expected | Tolerance basis | Label |
|---|---|---|---|---|
| TP1 | VBUS | 4.75–5.25 V | USB-C source spec | inferred |
| TP2 | VBAT | 3.0–4.2 V cell; ≈4.20 V while charging | BQ25170 VSET R75=27 kΩ → 4.20 V exact (Table 7-1) | checked |
| TP31 | VBAT_SYS | ≈VBAT on battery only; ≈4.5–4.7 V with USB | USB present: Q6 off (G=VBUS, R123 pulls G low otherwise) and D33 1N5819WS feeds VBAT_SYS = VBUS − Vf | topology checked; Vf ≈0.3–0.5 V at bring-up currents inferred |
| TP3 | +3V3 | 3.30 V, accept 3.23–3.37 V | TPS63802, FB 560 kΩ/100 kΩ × 0.5 V ref | checked ratio; ±2 % inferred |
| TP32 | +3V3_ANA | ≈TP3 − 0 Ω drop | R95 0 Ω link | checked |
| TP35 | +3V3_ESP | ≈TP3 − 0 Ω drop | R94 0 Ω link | checked |
| TP4 | +1V8 | 1.80 V, accept 1.77–1.83 V | TPS7A2018 fixed 1.8 V | ±1.5 % inferred |
| TP5 | TX_5V | 0 V until enabled; 4.9–5.1 V enabled | TPS61240 fixed 5 V | ±2 % inferred |
| TP33 | TX_5V_RAW | = TP5 (boost output node) | — | checked |
| TP6 | VDD_CP2102 | ≈3.3 V only when VBUS present | CP2102 is VBUS-fed; silent on USB (R104/105 DNP) | checked |
| TP34 | RTC_VBK | ≈0 V | R115 10 kΩ → GND, unused-backup handling | checked |
| TP36 | IOVDD | ≈3.2–3.3 V | R12 10 Ω + C19 from +3V3 | inferred |
| TP29 | RLDREF | ≈1.65 V when ADS1292 on | RLD mid-supply reference | inferred |
| TP30 | RLDOUT | ≈1.65 V when RLD enabled | RLD amplifier output | inferred |
| TP37 | CHG_ISET | ≈0.5–1 V | BQ25170 ISET pin (200 mA set by 1.5 kΩ) | guess |
| TP38 | CHG_VSET | ≈0.5–1.5 V, sampled at charger start | BQ25170 VSET config pin | guess |
| TP40 | TS | ≈0.38 V with ONE 10 kΩ NTC at 25 °C (≈0.26 V at ~35 °C skin); ≈0.19 V = two NTCs in parallel → suspended | ITS_BIAS = 38 µA × R_TS; charge window VHOT 188 mV < V_TS < VCOLD 1.04 V | thresholds checked (datasheet); voltages inferred |

- [ ] +3V3 missing or ≈VBAT → suspect U3 direction or a shorted rail link
  (R93/R94/R95 area). +1V8 missing with +3V3 present → U4 EN/OUT.
- [ ] **TS charge window (checked — BQ25170 datasheet):** ITS_BIAS = 38 µA
  (36.5–39.5); charge is allowed only while VHOT (188 mV typ, 176–200,
  falling ≈45 °C) < V_TS < VCOLD (1.04 V typ, ≈0 °C); hot-exit ≈220 mV
  (208–232); below VTS_ENZ ≈50 mV the charger is disabled (what Q2
  CHG_DIS does). Datasheet wants **one** 10 kΩ β=3435 NTC (103AT-2) or a
  fixed 10 kΩ resistor to disable TS.
- [ ] **Expected V_TS for a single 10 kΩ β≈3435 NTC (inferred — V_TS =
  ITS_BIAS × R_TS, R(T) = 10 kΩ · exp(β·(1/T − 1/298.15 K))):** 25 °C → 10.00 kΩ →
  **≈380 mV** (mid-window); ~35 °C skin → ≈6.88 kΩ → **≈260 mV**.
  **Two 10 kΩ NTCs in parallel (R61 + pack):** 25 °C → 5.00 kΩ →
  **≈190 mV**, sitting on the 176–200 mV VHOT spread — may or may not
  start; ~35 °C → ≈3.44 kΩ → **≈131 mV**, below VHOT even at its minimum
  → suspended; and recovery needs >≈208 mV, which 5 kΩ never reaches at
  ≥25 °C → stays suspended. A 0.19 V reading is therefore the two-NTC
  fault signature, not a healthy rail.
- [ ] TS far outside the window → NTC open/missing (V_TS → VTS_CLAMP
  ≈2.6 V) or Q2/CHG_DIS held low (V_TS <50 mV) → the charger suspends
  (expected behaviour — **inferred**).

## Step 4 — USB enumeration, flashing, boot mode

- [ ] Plug USB-C to the host. **Native USB-Serial-JTAG (ESP32-S3, IO19/IO20)
  should enumerate**: host log shows a "USB JTAG/serial debug unit" device
  (VID 0x303A); on macOS a `cu.usbmodem*` node appears (**checked**: R104/R105
  DNP, so the CP2102 path is disconnected — **only** the native interface
  enumerates; TP6 shows CP2102 is powered but silent).
- [ ] Flash via `esptool` on the usbmodem port. Download-boot: hold
  **IO0 (TP10) low** while releasing **EN (TP9)**; SW1/IO0 button or J8
  Tag-Connect pins can also drive these (**checked**: TP9/TP10 + J8 = EN, IO0,
  3V3, GND, TX, RX).
- [ ] UART console alternative: J8 / TP11 (ESP_TX) / TP12 (ESP_RX), 115 200 8N1
  (**inferred** — standard ESP boot log baud).
- [ ] UART TPs were chosen over USB D+/D− for probe points: D+/D− would gain
  ~1 mm² of pad capacitance on a 480 Mb/s pair and UART already exposes the
  console (**inferred**; USB D+/D− TPs skipped — see REPORT.md).

## Step 5 — I²C scan

- [ ] Run the bus at **100 kHz** — the 4.7 kΩ/2.2 kΩ pull-ups on ~200–270 pF
  give ~0.8–1.0 µs rise, over the 400 kHz spec (**checked** — audit finding
  I29).
- [ ] Scan on the 3V3 bus (TP13 SDA / TP14 SCL). Expected 7-bit ACKs:

| Addr | Device | Addr | Device |
|---|---|---|---|
| 0x20 | TCA6408A U19 | 0x52 | RV-3028-C7 U24 |
| 0x36 | MAX17048 U17 | 0x6A | LSM6DSV U14 |
| 0x44 | SHT45 U23 | 0x76 | BME280 U13 |
| 0x48 | TMP117 U11 | 0x49 | TMP117 U20 |

(**checked** — address pins per v22 connection audit.)

- [ ] The 1V8 bus devices appear through the PCA9306 (U9) on the same scan:
  **0x39** AS7341 U10, **0x3A** MLX90632 U12 (**checked**). If missing: U9
  EN/VREF2 should be ≈3.3 V (R18 200 kΩ pull-up — **checked**); probe TP15/16.
- [ ] No collisions expected (**checked** — audit). A missing ACK = that
  device, its rail, or its pull-up — not the bus master.

## Step 6 — first LED / boost test

- [ ] Enable TX_5V: set TX5_EN high via the TCA6408 (P3 output, R25 100 kΩ
  pull-down keeps it off at boot — **checked**). Expect TP5/TP33 ≈5.0 V;
  inrush <150 mA (**inferred**).
- [ ] Scope TX_5V and one LED channel at a time, **low current first**
  (smallest drive code). A single ~150 mA pulse should keep TX_5V >4.8 V
  (**inferred** — sim_v24 measured an 11 mV droop for a 200 mA × 60 µs
  pulse). Boost figures, stated per source: datasheet EC table guarantees
  only **200 mA** output across VIN (**checked**); "up to **450 mA**" is
  the feature headline at favourable VIN and the **500 mA** spec is the
  *input* valley current limit, not output (**checked**); usable output at
  a **3.2 V battery is ≈250–270 mA** (**inferred** — datasheet Figure 1
  max-output curve and the sim_v24 behavioural boost, PR #20).
- [ ] **Avoid overlapping AFE4900 TX and MAX86141 LED pulses** — the
  ~900 mA coincident bound far exceeds the ≈250–270 mA boost capability
  and collapses TX_5V (sim_v24: dips to 2.5–2.9 V, below the AFE4900
  TX_SUP minimum — **inferred**). Firmware gives a hard non-overlap
  guarantee only for the LED chains `led_sched` pulses directly via
  cb_on/cb_off while owning TX_5V (**checked** — led_sched.h/vq_led.c).
  The AFE4900 and MAX86141 time their own LEDs internally — AFE4900's
  engine only runs while a scheduler grant is active, and MAX86141 is
  fully autonomous (its on/off callbacks are empty no-ops,
  `mx_led_cb_on/off`, and its GPIO1 sync input is unconnected —
  **checked** in firmware source) — so per-pulse non-overlap between the
  two AFE chains is **NOT enforced** (**inferred**). Safe today only
  because programmed currents are low: ≈5 mA AFE4900
  (`AFE_LED_CODE_RUN`) + ≈15–23 mA MAX86141 (`MX_LED*_PA`) versus
  ≈250–270 mA usable TX_5V at a 3.2 V battery (**inferred**).
- [ ] AS7341 illuminator D10: start at LED_DRIVE code 4, verify <20 mA,
  cap at code 8 (**checked** — audit limit).
- [ ] IR gate / D12 730 nm via AD5940 GPIO2: expect 12–16 mA (**inferred** —
  v4 bring-up value).

## Step 7 — battery cut-off

- [ ] **≈3.2 V low-battery shut-down is a guess** — confirm against the
  MAX17048 alert/empty configuration and firmware before relying on it.
  The BQ25170 charges to 4.20 V; the cell's own protection FET (~2.5 V) is
  the last resort, not the design cut-off (**guess / inferred**).
- [ ] While verifying, watch TP2 down through discharge and note where the
  firmware reports empty (0x36 MAX17048 SOC via I²C).

## Step 8 — if anything fails

| Symptom | First check |
|---|---|
| Current limit hit on VBAT | Short on VBAT_SYS/Q6, U3 orientation, R94/R95 links |
| No +3V3 | TP31 VBAT_SYS present? U3 EN (=VBAT_SYS, always-on), L1 |
| +3V3 but no +1V8 | U4 EN (=+3V3) and R97 0 Ω link |
| No USB enumeration | J1 CC resistors 5.1 kΩ, IO19/IO20 pair, try download boot (IO0+EN) |
| I²C dead / missing ACKs | 100 kHz speed; pull-ups; U9 VREF2≈3.3 V; TP26 EXP_INT |
| AFE silent on SPI | Device still in reset via U19 — recheck expander writes (R9/R11/R14 pulls) |
| TX_5V won't rise | TX5_EN (U19 P3) low; R25 pull-down; U15 soldering; enable before pulsing LEDs |
| Won't charge | TP40 ≈0.19 V → R61 + pack NTC in parallel → at/below VHOT → suspended (see Step 3 TS note); CHG_DIS (U19 P7 → Q2) pulling TS <50 mV also disables; CHG_STAT on TP27 |
| TS out of window | ONE 10 kΩ NTC → ≈0.38 V at 25 °C is healthy; ≈0.19 V means two NTCs in parallel — resolve the R61-vs-pack question (OPEN DECISION above); don't "fix" a correctly suspended charger |
| Rail OK, board hot | Isolate: power down, resistance-check that rail, reflow suspects |

Power off between probe moves on BGA/regulator areas; never poke +3V3 nodes
with the board energised by a non-current-limited source.

## Corrections (30 Oct 2026)

- **TS / NTC (Steps 1, 3, 8) — most important.** Re-derived the TS node
  from the schematic: BQ25170 pin 3 + J2 pin 3 (pack NTC) + R61 (10 kΩ
  B3380, **fitted** per v25 BOM_JLC/CPL, no DNP flag — despite the
  "R61 stays DNP" schematic text note) + Q2 CHG_DIS pull-down + TP40.
  The earlier text told the operator to confirm a pack NTC *in addition
  to* R61 and not to "fix" a correct 0.19 V. In fact two 10 kΩ NTCs in
  parallel give ≈190 mV at 25 °C — on top of the 188 mV VHOT threshold —
  and ≈131 mV at ~35 °C, so the charger suspends. Rule now stated:
  **exactly ONE pull-down on TS** (datasheet: one 10 kΩ NTC, or a fixed
  10 kΩ resistor if TS is unused). Whether R61 should be fitted is an
  **OPEN DECISION for Riyansh** — board/BOM question, not resolved here.
- **TP31 VBAT_SYS with USB (Step 3).** Was "≈4.2 V"; corrected to
  ≈4.5–4.7 V — with VBUS present Q6 is off and D33 (1N5819WS) feeds
  VBAT_SYS from VBUS minus its ≈0.3–0.5 V drop at bring-up currents.
- **TP39 VBUS_DET (Step 2).** Was "≈+3V3"; corrected to ≈3.0 V —
  R50 100 kΩ / R51 150 kΩ divide VBUS by 0.6 (2.85–3.15 V over the USB
  4.75–5.25 V range).
- **Step 6 LED overlap claim.** The "firmware interleaves channels
  (checked — audit I02)" claim was wrong for the AFE chains: `led_sched`
  serialises only the chains it pulses directly; the AFE4900 and
  MAX86141 time their own LEDs internally (MAX86141 callbacks are empty
  no-ops, sync input unconnected), so per-pulse non-overlap between them
  is **not** enforced — relabelled **inferred**. Still safe today at the
  programmed ≈5 mA (AFE4900) and ≈15–23 mA (MAX86141) currents.
- **Step 6 boost figure.** "≈400–450 mA worst case" corrected: 450 mA is
  the datasheet's headline at favourable VIN and 500 mA is the *input*
  valley limit; the EC table guarantees 200 mA output and the usable
  figure at a 3.2 V battery is ≈250–270 mA (datasheet Figure 1 /
  sim_v24, PR #20).
- **Step 1 resistance table.** TP31 note corrected: with VBUS=0, R123
  pulls Q6's gate low so Q6 is **on** (was "Q6 off when VBUS=0?").
