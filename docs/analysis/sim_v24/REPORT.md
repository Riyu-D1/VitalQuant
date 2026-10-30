# sim_v24 — power, SPICE, RF keep-out & thermal notes for fix_v24 (reports only)

**Scope:** analysis of the **unchanged** `fix_v24` board (`fix_v24/vitalq_v2.kicad_pcb`/`.kicad_sch`). No board, firmware or repo-file edits. All outputs live in `sim_v24/`.

**Summary (all pass criteria):**

| Item | Result | Label |
|---|---|---|
| Rails identified | 11 named board supply nets + 12 IC-internal ref/bias nets (all schematic power nets covered) | checked |
| Runtime estimate | deep sleep ≈ **1423 h** (~59 d); BLE streaming ≈ **3.5 h**; 1-in-10 sampling ≈ **6.6 h** (conservative) / ~22 h (PPG-only duty) | inferred |
| BQ25170 charge | 200 mA CC (R3=1.5 kΩ), 4.20 V CV (R75=27 kΩ); ~1.5 h CC + ~0.7 h taper | checked |
| Boost headroom | **STAGGER REQUIRED** — ~900 mA simultaneous LED pulse collapses TX_5V to 2.5–2.9 V | checked (sim) |
| LDO dropout @3.2 V battery | all rails in regulation (+3V3 buck-boost 3.30 V, +1V8 1.797 V) | checked (sim) |
| SPICE runs | **6/6** decks completed (5 analysis + 1 vendor-evidence) | checked |
| RF keep-out violations | **0** (interior copper; 2 intentional/edge findings documented) | checked |
| DRC/ERC | 0 errors / 0 unconnected / 0 shorts / 0 parity; ERC 0 | checked |
| `JOURNAL.md`/`LICENSE` | byte-identical (hashes below) | checked |

Tool versions: kicad-cli **10.0.4**, ngspice **47** (KLU), Python 3.12, numpy 2.4.2, matplotlib 3.10.8.

---

## (a) Power budget

### Rail map (checked — fresh `kicad-cli sch export netlist` of fix_v24)

```
J1 USB-C ──VBUS──┬─ U2 BQ25170 ──OUT── VBAT ── J2 LiPo (402535)
                 ├─ U5 CP2102N (VBUS only)      │
                 └─ D33 ─┐              Q6 AO3401A ─┘
                         ├── VBAT_SYS ──┬─ U3 TPS63802 ── +3V3 ─┬─ R94→ +3V3_ESP → U1 ESP32-S3
                        L2              │                     ├─ R95→ +3V3_ANA → U6/U7/U8
                         └─ U15 TPS61240 ── TX_5V_RAW ─R96─ TX_5V ├─ U4 TPS7A2018 → +1V8_LDO ─R97─ +1V8
                                                            └─ D10/D11/D12 LEDs, sensors, flash…
```

`+1V8` loads: AS7341, MAX86141 VDD, SN74AXC2T245, PCA9306, I²C pull-ups.
`TX_5V` loads: SFH7072 anodes ×4 (→AFE4900 TX1–4 sinks), MAX86141 VLED (LED1–3_K → J10 tail), AFE4900 TX_SUP.
IC-internal (not board rails): AD5940 AVDD_REG/DVDD_REG/VBIAS*/VCAP1/2/VREFP, ADS1292R IOVDD, MAX86141 MX_VREF, RV-3028 RTC_VBK, VDD_CP2102.

### Per-rail budget (full table in `data/power_budget.csv`; sources are datasheet page/section refs)

| rail | regulator & rating | I_typ | I_peak (all-on worst case) | headroom |
|---|---|---|---|---|
| +3V3 | TPS63802 buck-boost, **2 A** @ VIN≥2.3 V | ~94 mA | ~490 mA | 4× margin — OK |
| +1V8 | TPS7A20 LDO, **300 mA** | ~1.6 mA | ~5 mA | 60× margin — OK |
| TX_5V | TPS61240 boost, **~250–450 mA** out | ~0.5 mA | ~1170 mA (if all LEDs fire) | **negative → stagger** |
| VDD_CP2102 | CP2102N int. LDO | 9.5 mA | 13.7 mA | USB-only, OK |
| VBAT_SYS | Q6/D33 OR-ing | ~30 mA | ~950 mA input side | limited by boost limit |
| VBUS | host 5 V | ~60 mA | ~864 mA | charger 200 mA + system |

Key per-load figures (checked, `data/power_budget.csv` has citations):
ESP32-S3-WROOM-1: Wi-Fi TX peak 355 mA, RX ~95 mA, deep-sleep 8 µA (+140 µA PSRAM in light-sleep).
AFE4900: TX sinks ≤200 mA/ch ×4 = 800 mA worst. MAX86141: LED drivers ≤124 mA/ch ×3 = 372 mA.
Sum ≈ 1170 mA theoretical; the earlier design figure of ~900 mA simultaneous is the planning bound.
SFH7072 emitters rated 300 mA (green/red) / 1 A (IR) pulsed at D=0.005 — silicon is fine; the **supply** is not.

### Runtime (cell assumed **300 mAh**, 402535 pouch — assumption; usable ≈90%)

| scenario | battery draw | runtime | assumptions |
|---|---|---|---|
| Deep sleep (RTC on) | ~0.21 mA | **≈1423 h (~59 d)** | ESP32 8 µA + PSRAM 140 µA (light-sleep figure used as bound), sensors asleep, boost+LDO off (guess) |
| BLE streaming, continuous | ~87 mA | **≈3.5 h** | ESP32 BLE-active ~80 mA avg (inferred), IMU 0.8 mA, sensors idle, LEDs off |
| 1-min-in-10 sampling | ~45 mA | **≈6.6 h** | 10 % duty of ~110 mA system + **20 mA avg** LED allowance (conservative guess) |
| same, PPG-only LED duty | ~13.5 mA | **≈22 h** | LEDs 200 mA×60 µs×25 Hz×4 = 1.2 mA avg (checked pulse math) |

The honest range for periodic monitoring is **~7–22 h** depending on LED duty; both ends shown because duty cycle is the dominant unknown (inferred).

### BQ25170 charge (checked)

- `R3 = 1.5 kΩ` on ISET → `ICHG = KISET/RISET = 300/1.5k = ` **200 mA** (datasheet KISET 270–330).
- `R75 = 27 kΩ` on VSET → **4.20 V** Li-ion CV (datasheet Table 7-1).
- Charge time ≈ 300/200 = **1.5 h CC** + ~0.5–1 h CV taper → **~2 h total** (inferred; behavioural sim: 200 mA flat to 4.2 V at ~80 min, taper to <30 mA by ~90 min — `results/v24_bq25170_charge.csv`).
- USB budget check: 200 mA charge + ~60 mA system < 500 mA USB2 default — OK **if** VBUS isn't also asked to run full LED pulses simultaneously (inferred — gate TX_5V on VBUS by firmware, already the design rule).

### Boost headroom → STAGGER REQUIRED (checked)

TPS61240 guarantees only **200 mA** output across VIN (datasheet EC "output current"), ~250 mA at 3.2 V input (device-options note), up to 450 mA at high VIN; valley switch limit 500–600 mA.
Worst simultaneous demand ~900–1170 mA ≫ ~250 mA ⇒ **LED chains must be staggered** (one chain per slot, ≤1% duty for 400 mA-class pulses) — already the firmware rule; this analysis independently confirms it is **mandatory**, not optional. Simulated: 200 mA×60 µs → 11 mV droop; 900 mA×60 µs → TX_5V falls to 2.5–2.9 V < AFE4900's 3.0 V TX_SUP minimum.

### LDO dropout @3.2 V battery (checked, sim + datasheet)

- `+3V3`: TPS63802 input range 2.3–5.5 V — at VBAT_SYS=3.2 V output holds **3.297 V** (sim). ✓
- `+1V8`: LDO input is +3V3 (not battery); VDO max 200 mV@300 mA but load is <5 mA → headroom **1.5 V**. Holds even if +3V3 sags to ~2.1 V. ✓
- `TX_5V`: boost from 3.2 V → 5.0 V holds for loads ≤~270 mA. ✓ (for staggered pulses)
- `VDD_CP2102`: USB-only — not applicable on battery (checked).
- Battery floor: at 3.0 V cell, +3V3 still 3.297 V (sim corner) — the limit is the cell, not the regulators. ✓

---

## (b) SPICE (ngspice-47) — `decks/`, `results/*.csv|.log`, `plots/*.png`

| deck | model | result |
|---|---|---|
| `v24_tx5v_vendor.sp` | TI SLVMAT6 (vendor) | **does not boost in ngspice-47** — Vout tracks VIN at every corner (same finding as fullsim_v20/b06); evidence kept, not used for conclusions |
| `v24_tx5v_A_200mA.sp` | **behavioural** averaged boost (datasheet-bounded) | 200 mA×60 µs pulses: droop **11 mV**, recovers; Iin ≤0.37 A < 0.5 A limit, all VIN 3.2/3.7/4.2 |
| `v24_tx5v_B_900mA.sp` | same behavioural | ~900 mA pulse: TX_5V dips to **2.54–2.87 V** — below AFE4900 TX_SUP min (3.0 V) ⇒ simultaneous firing fails |
| `v24_3v3_loadstep.sp` | **behavioural** TPS63802 (vendor macro non-convergent, inherited finding re-verified) | 15→355 mA Wi-Fi bursts: rail 3.297 V, dips **3.229 V** (−2.1%, inside ±3%), VIN 3.0–4.35 |
| `v24_1v8_loadstep.sp` | **vendor** TPS7A20 (SBVM961) | 5→20 mA step: 1.7974–1.7998 V, ≥1.2 V headroom |
| `v24_bq25170_charge.sp` | **behavioural** BQ25170 + 402535 cell | 200 mA CC → 4.20 V at ~80 min, taper ~28 mA @90 min, SOC≈99% |

6 decks ran to completion: 5 analysis decks pass; the vendor-check deck documents non-regulation — **6/6 runs complete**. Behavioural models are bounded by datasheet EC values (switch limits, VFB dividers, VDO) — labelled in each deck header.

## (c) RF / antenna keep-out (checked — fresh parse + Gerber audit)

- U1 `ESP32_S3_WROOM_1` at (11.5, −6.5) mm, antenna end at y = −22.25 mm; board north edge y = −16.55 mm → **overhang 5.70 mm** past the board edge ✓ (Espressif's preferred option, HDG §1.4.7).
- Main keep-out zone (checked from `.kicad_pcb`): x 2.5–20.5, y −22.25–−16.30 mm, all 8 copper layers. **Gerber audit: 0 copper operations inside it on all 8 layers.**
- In-board antenna strip keep-out x −2.5–25.5, y −16.55–−15.25: **0 interior copper**; six fill-boundary strokes land exactly flush on the edge — clipping artifacts, not violations.
- Antenna-end → nearest copper distance: **5.85 mm** (GND pour at ~22.3, −16.4). With the overhang, the "≥15 mm keep-out or edge overhang" guidance is satisfied by the overhang; remaining copper gap >5 mm.
- PCB-parser findings (3): U1 footprint overlaps the keep-out **by design** (module body carries the antenna), plus 2 duplicate pour-edge records → **actual violation count = 0**. Plot: `plots/antenna_keepout.png`.

## (d) Thermal sanity (guess-level throughout)

- Worst-case LED pulse: 200 mA×5 V = 1.0 W per 60 µs pulse → **60 µJ/pulse**; at ≤1% duty ≈ **≤10 mW average** — thermally trivial (guess).
- Boost conversion loss during 200 mA pulse ≈ Pout×(1/η−1) ≈ 0.18 W instantaneous, ≤2 mW averaged at 1% duty (guess).
- LED copper area near U16/L2 is a few mm² of pour — enough for these micro-second pulse trains; only concern would be sustained >400 mA (which §(a) already forbids) (guess).
- TPS63802 worst-case dissipation (355 mA out × ~8% loss) ≈ 0.1 W — fine for its thermal pad layout (guess).

## Verification commands

```sh
# netlist → rails         python3 scripts/parse_netlist.py (input: _work/v24.net)
# power budget            python3 scripts/power_budget.py   → data/power_budget.csv, data/runtime.csv
# ngspice (run in decks/) ngspice -b v24_tx5v_A_200mA.sp  (etc.; logs in results/)
# plots                   python3 scripts/plot_sims.py      → plots/*.png
# RF                      python3 scripts/parse_board.py + gerber_keepout.py + plot_antenna.py
# DRC (on _work/drc copies)  kicad-cli pcb drc --severity-error --schematic-parity --format json vitalq_v2.kicad_pcb -o drc_v24.json
# ERC                        kicad-cli sch erc --severity-error --format json vitalq_v2.kicad_sch -o erc_v24.json
```

## Hash check (checked before & after)

- local `JOURNAL.md`: `27f28099f771a8d30d9878323bf5df7af5255188`
- local `LICENSE`:    `fa6093088c1db813ca018427cd9754a2b76228a8`
- repo copies: see git log / `shasum` in `_work/repo/` — identical.

## Open / caveats

- 300 mAh cell capacity is an **assumption** (402535 class; not verified against a part number on the board — the BOM shows a LiPo pad footprint only).
- ESP32-S3 "80 mA BLE-active average" and the 20 mA LED allowance in the 6.6 h figure are **inferred/guess**; duty-cycle-dominated estimates.
- TPS61240 & TPS63802 sims use behavioural averaged models because the TI transient macros do not converge/regulate in ngspice-47 (**checked** — evidence deck + log kept); behavioural bounds are datasheet-derived (**inferred**).
- TPS7A20 uses the TI vendor macro (**checked**).
- Thermal numbers are **guess**-level by design of the task.
- No medical/safety/regulatory claims are made anywhere in this analysis.
