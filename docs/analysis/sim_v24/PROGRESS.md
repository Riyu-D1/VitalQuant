# sim_v24 progress log

Job: power budget + SPICE + RF keep-out + thermal analysis of fix_v24 (reports only).
KiCad CLI `/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli` — `kicad-cli version` = **10.0.4** (checked, used for netlist + gerber exports below).
ngspice-47 at /usr/local/bin/ngspice (checked). Python 3.12, numpy 2.4.2 / scipy 1.17.1 / matplotlib 3.10.8 (checked).
Labels: checked / inferred / guess throughout.

Board under analysis: `fix_v24/vitalq_v2.kicad_pcb` + `vitalq_v2.kicad_sch` — READ ONLY.
No existing file modified. New work only inside `sim_v24/` (scratch in `sim_v24/_work/`).

## Step 1 — setup + RF geometry (done)
- Created `sim_v24/{scripts,decks,models,results,plots,data,_work}`.
- Parsed PCB (own s-expr parser, `scripts/parse_board.py`):
  - U1 = ESP32-S3-WROOM-1-N16R8 at (11.5, −6.5), rot 0. Module body y −22.25..3.25 (checked vs fp silk ±9 / −15.75..9.75 rel).
  - Board north edge y = −16.55 → **antenna end overhangs board edge 5.70 mm** (checked).
  - Keepout "antenna keepout" (in U1 fp): x 2.5..20.5, y −22.25..−16.3 (18×5.95 mm), all 8 Cu layers, tracks/vias/pads/pours/footprints not_allowed (checked).
  - Second strip keepout "antenna_keepout": x −2.5..25.5, y −16.55..−15.25 (28×1.3 mm, in-board band below feed) (checked).
  - PCB-level scan: 0 track, 0 via, 0 pad, 0 non-U1 footprint inside keepout. 2 pour OUTLINE edges cross boundary ≤0.1 mm (outlines only).
  - Nearest copper to antenna end: GND pour outline at y=−16.4 → **5.85 mm** (checked; = 5.70 overhang + 0.15 edge setback).
- Gerber verify (`kicad-cli pcb export gerbers` into `_work/gerbers/`, 8 Cu layers):
  - NOTE: KiCad gerber Y is negated vs .kicad_pcb Y (verified via known via at pcb (18.80794, 21.909891) → gerber Y−21909891).
  - Filled copper inside keepout rect: **0 hits on all 8 layers** (checked, `scripts/gerber_keepout.py`).
  - Strip keepout: 0 real hits; only zone-fill boundary strokes flush AT the keepout edge (correct clipping).
  - Drill file: 0 holes inside keepout (checked).
- Antenna plot: `plots/antenna_keepout.png`.
- RF verdict: **0 violations**; overhang placement = Espressif's recommended option (HDG §1.4.7: antenna outside base board; ≥15 mm clearance-in-housing note applies to case, not board copper).
- Repo cloned (shallow, depth 1) into `_work/repo/`. Full clone timed out at ~2.4 GB; shallow OK for a docs PR.
  - Baseline shasum: JOURNAL.md `27f28099f771a8d30d9878323bf5df7af5255188`, LICENSE `fa6093088c1db813ca018427cd9754a2b76228a8` (must remain identical).
  - Report destination per repo layout: `docs/analysis/sim_v24/` (docs/ holds numbered reports; hardware/pcb/docs holds board reports — analysis folder chosen per prompt example).

## Step 2 — power architecture extraction (done)
- Fresh netlist `kicad-cli sch export netlist` → `_work/v24.net` → `data/netlist.json`, `data/all_nets.txt` (checked).
- Power tree (checked from netlist):
  - VBUS(J1 USB-C) → U2 BQ25170 charger → VBAT(J2 LiPo pads); VBUS → Q6(AO3401)+D33(1N5819WS) power-path OR-ing → VBAT_SYS.
  - VBAT → R93 0R → VBAT_SYS → U3 TPS63802 (FB R23 560k/R24 100k → 3.3 V) → +3V3.
  - VBAT_SYS → U15 TPS61240 (fixed 5 V) → TX_5V_RAW → R96 0R → TX_5V (+ R114 0R spur).
  - +3V3 → U4 TPS7A2018 → +1V8_LDO → R97 0R → +1V8.
  - +3V3 → R94 0R → +3V3_ESP (U1); +3V3 → R95 0R → +3V3_ANA (U6 AFE4900, U7 AD5940, U8 ADS1292R); +3V3_ANA → R12 10R → IOVDD (AD5940 IO).
  - +1V8: U10 AS7341, U22 MAX86141 VDD, U26 SN74AXC2T245 VCCA, U9 PCA9306 VREF1, I2C_1V8 pull-ups R21/R22.
  - TX_5V: U16 SFH7072 anodes ×4 (cathodes→U6 TX1-4 sinks), U22 VLED, U6 TX_SUP, off-board via J12 FFC (LED1-3_K, PD_A/PD_K).
  - VDD_CP2102: U5 internal, from VBUS only (no battery path).
  - D11 IR via R47 100R + Q1 sink (IR_GATE=U19.P); D12 NIR via R109 100R + Q4 sink (NIR730_GATE=U7.E1 AD5940 GPIO); D10 white anode→+3V3, cathode→U10.4 LDR sink; D25 status via R88 150R.
  - Charger: R3 1.5k ISET → 200 mA CC; R75 27.0k VSET → 4.20 V CV (datasheet decode below).
- Earlier figures to re-derive: "~900 mA LED pulse" (FIRMWARE_NOTES §2 / fix_v22 CTO prompt: AFE4900 ~200 + MAX86141 ~200 + SFH7072 ~500 mA) vs "TPS61240 ~400 mA".

## Step 3 — datasheet figures (in progress)
- TPS61240 SLVS806D (TI, fetched to `_work/datasheets/`): EC table "Output current 200 mA" (VIN 2.3–5.5); Device Options "up to 250 mA loading down to 3.2 V input"; headline "up to 450 mA" (high VIN); switch valley current limit 500–600 mA; 30 µA Iq; 1.5 µA shutdown. → earlier "~400 mA" is optimistic; guaranteed 200–250 mA at VBAT 3.2 V.
- ESP32-S3-WROOM-1 DS v1.8 (local `verify/datasheets_v20/`): Wi-Fi TX peak 355 mA (11b 20.5 dBm), BLE TX 344 mA @20 dBm, RX ~95 mA; modem-sleep 33–108 mA; light-sleep 240 µA (+140 µA octal PSRAM); deep-sleep 8 µA.
- Espressif HDG §1.4.7 (local PDF): antenna outside base board preferred; ≥15 mm clearance in all directions inside housing.
- Prior `verify/power_budget.py` (v18-era) found — same rail topology; per-load currents cited there; re-deriving against fix_v24 netlist + datasheet txt library `verify/datasheets_v18_txt/`.

## Next
- Extract per-IC currents from `verify/datasheets_v18_txt/*.txt` → `data/rail_budget.csv`.
- ngspice decks: TX_5V pulse, +3V3 Wi-Fi burst, +1V8 step; runtime deck.
- REPORT.md, SIM_README.md, branch docs/sim-v24, PR.

## Step 4 — DRC/ERC re-check on copies (done)
- `_work/drc/`: fix_v24 pcb/sch + fix_v19 `.kicad_pro` rules + fix_v23 `.kicad_dru` (same basename → auto-loaded).
- `kicad-cli pcb drc --severity-error --schematic-parity --format json` → `drc_v24.json`:
  **0 violations, 0 unconnected, 0 shorts (subset of violations), 0 parity issues.** kicad-cli 10.0.4.
- `kicad-cli sch erc --severity-error --format json` → `erc_v24.json`: **0 violations**.
- Matches fix_v24/VERIFY_REPORT.md (0/0/0/0) — now re-verified in this workspace (checked).

## Step 5 — datasheet extraction (done)
- All load currents pulled from local text extracts `verify/datasheets_v18_txt/*.txt` + TI PDFs in `_work/datasheets/`:
  ESP32-S3 (TX 355mA/RX 95/deep 8uA/light 240+140uA PSRAM), AFE4900 (200mA/TX ch, ~30uA avg HRM),
  MAX86141 (3x124mA LED, 1.2mA VDD), AD5940 (ADC 1.5mA, ~6.5uA hib), ADS1292R (205+75uA),
  AS7341 (300uA act/5uA slp), MLX90632 (1.4mA/2.5uA), BME280 (3.6uA avg), SHT45 (500uA meas),
  TMP117 (220uA/3.5uA duty), LSM6DSV320X (0.8mA HP), IM69D130 (1.3mA), RV-3028 (60nA),
  TCA6408A (1uA sby), SN74AXC2T245 (23uA comb), W25Q512 (5mA rd/40mA pgm/<1uA pd),
  CP2102N (9.5-13.7mA), MAX17048 (40uA), SFH7072 (300mA-1A pulsed), SFH4053 (70mA DC),
  TPS63802 (2A, 11uA Iq), TPS61240 (500mA valley lim), TPS7A20 (300mA, VDO 200mV@300mA),
  BQ25170 (KISET=300 A·ohm, VSET table 27k→4.20V).
- `scripts/power_budget.py` → `data/power_budget.csv` (per-rail loads, typ+peak, label+source) + `data/runtime.csv`.

## Step 6 — SPICE decks run (done)
- ngspice-47, all decks in `decks/`, results+logs+csvs in `results/`:
  - `v24_tx5v_vendor.sp`  VENDOR SLVMAT6 macro — **does not boost in ngspice** (Vout tracks Vin); evidence kept.
  - `v24_tx5v_A_200mA.sp` BEHAVIOURAL averaged boost — 200mA×60us pulses: dip ~11mV, all VIN corners.
  - `v24_tx5v_B_900mA.sp` same model — ~900mA pulse: **rail collapses to 2.54–2.87V** < 3.0V AFE4900 TX_SUP min → STAGGER REQUIRED.
  - `v24_3v3_loadstep.sp` BEHAVIOURAL TPS63802 — 15→355mA Wi-Fi bursts: 3.23V min (−2%), in ±3% band.
  - `v24_1v8_loadstep.sp` VENDOR TPS7A20 — 5→20mA step: 1.7974V min, ≥1.2V headroom.
  - `v24_bq25170_charge.sp` BEHAVIOURAL — 200mA CC → 4.20V CV at ~80min, taper to ~28mA at 90min, SOC≈99%.
- Plots: `plots/tx5v_pulse.png`, `v33_loadstep.png`, `v18_loadstep.png`, `bq25170_charge.png`, `antenna_keepout.png`.

## Next
- Thermal guess-note, REPORT.md, SIM_README.md, git branch + PR, final line.

## Step 7 — deliverables + repo (done)
- REPORT.md (summary-first, every claim labelled), SIM_README.md, data/ CSVs, plots/, decks/, models/, results/ — all inside sim_v24/ only; no fix_* folders touched.
- Repo: `docs/sim-v24` branched from `main`; only `docs/analysis/sim_v24/` added (41+1 files).
- JOURNAL.md / LICENSE SHA-1 verified identical before AND after commit+push.
- Pushed; PR opened: https://github.com/Riyu-D1/VitalQuant/pull/20 — never merged.
- Final numbers for status line: rails=11 named supply nets, runtime 3.5h (BLE) / 1423h (deep sleep), STAGGER REQUIRED, 6/6 spice runs, 0 RF violations.
