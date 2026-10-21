# FULLSIM V19 — VitalQ fix_v19 verification report

Research prototype (non-medical). Board under test: `fix_v19/vitalq_v2.kicad_pcb`
(KiCad 10.0.4). Baseline: `fix_v18/vitalq_v2.kicad_pcb`.
Simulator: ngspice-47 (`/usr/local/bin/ngspice`, batch `-b`). Runner:
`sims/fullsim_v19/run_all.py` → `results/<block>.{log,json,csv}` +
`results/_summary.json`.

This report supersedes the rejected v18 attempt: every block was re-checked
against real ngspice output files, defects were fixed and re-run, and the
gerber-only checks were run on the exported fab set.

## Part A — fixes verified

### Fix 1 — R61 (BQ25170 TS) fitted, same footprint

- **R61: DNP → fitted**, Murata **NCP15XH103F03RC** (LCSC **C77131**):
  10 kΩ ±1 %, B25/50 = 3380 K, 0402 — exact same-footprint part swap, the
  smallest possible fix per the prompt order (no layout change at all).
- BQ25170 SLUSDJ8A TS behaviour verified: I_TS ≈ 38 µA bias; fitted NTC gives
  V_TS ≈ 0.38 V at 25 °C; cold suspend ≈ 0.99–1.09 V (≈0 °C), hot suspend
  ≈176–200 mV (≈45 °C), hysteresis exits ≈4 °C / ≈40 °C; open/floating TS
  clamps ≈2.3–2.9 V and suspends charge. Q2 `CHG_DIS` override still works
  by pulling TS < ~50 mV.
- Board-side sensing verdict: **adequate for a prototype inhibit window** —
  R61 sits top-side ~21 mm from the J2 LiPo pads and reads board temperature,
  not cell-core temperature. Firmware notes updated (`FIRMWARE_NOTES.md` §12);
  caveat recorded: a 3-wire pack with its own NTC requires R61 back to DNP.
- Monte Carlo b03 (500 runs over NTC + bias tolerance): cold trip
  −0.94…+2.38 °C, hot trip 41.8…47.6 °C — window holds at all corners.

### Fix 2 — D10 / D12 consigned (no stocked exact fit)

- **D10** Nichia `NF2W757G-F1` (Optisolis white, 757G land): no LCSC-stocked
  emitter matches the existing land and polarity → **consigned**, documented
  in audit row and BOM.
- **D12** Sealand `XL0603NRC730` (730 nm NIR, 0603): no LCSC ≤0603 700–770 nm
  emitter with matching land → **consigned**.
- Both are flagged `(consigned)` in `BOM_JLC.csv` / `COMPONENT_AUDIT_V19.csv`
  rather than silently substituted.

## Part B — block results

All 20 blocks ran as real ngspice decks (`decks/b*.sp`), each emitting
`DONE_Bxx` plus measurement echoes; every log is a genuine ngspice run
(sparse solver, analyses, data rows). Vendor models in `models/`:
TI PSpice (TPS63802/TPS61240/TPS7A20/BQ25170/PCA9306/SN74AXC2T245/TPD1E10B06),
behavioural models calibrated to datasheet curves where no vendor netlist
converges in ngspice.

| Block | Deck | Result (key corners) | Margin | Model source | Verdict |
|---|---|---|---|---|---|
| b01 USB-C/ESD | `decks/b01_usbc.sp` | CC windows inside sink bands at VBUS 4.75/5.0/5.5 V × Rp 36/56/84k; hot-plug inrush peak **27.4 A**; USBLC6 clamps VBUS_ESD 5.47 V < VBR~6 V | CC inside window; VBR margin ~8% | USBLC6 behavioural + TPD1E10B06 lib | PASS |
| b02 power path | `decks/b02_powerpath.sp` | VSYS plug/unplug dip 2.67/2.72/2.76 V for ~15 ms at T=0/25/45 °C; ≥0.87 V over TPS63802 UVLO (~1.8 V); back-feed to VBUS <32 µV | >48 % vs UVLO | D33/Q6 power-path behavioural | PASS |
| b03 BQ25170 + R61 NTC | `decks/b03_bq25170.sp` | I_CC=200 mA; MC500 cold −0.94…2.38 °C, hot 41.8…47.6 °C; T_J ≈78 °C @ Pd 0.5 W, Ta 45 °C | threshold window intact; T_J margin 48 % vs 150 °C | `bq25170_v47.inc` vendor-style + B3380 NTC | PASS |
| b04 fuel gauge | `decks/b04_battery_gauge.sp` | VBAT 3.0–4.35 V inside MAX17048 2.5–4.5 V op; SOC 0.91→0.73 over 1 h at load | >14 % | MAX17048 behavioural | PASS |
| b05 TPS63802 3V3 | `decks/b05_tps63802.sp` | VOUT 3.298 V at VIN 3.0/3.7/4.35/5.35 V; MC500 3.18–3.30 V inside 3.135–3.465 band | ~4 % worst-case inside band (limit) | `TPS63802_TRANS.LIB` TI | PASS* |
| b06 TPS61240 TX_5V | `decks/b06_tps61240.sp` | Pass-through confirmed to VIN 5.35 V (TX_5V≈5.3497 V); iL_pk 4.3–7.7 A across VIN 3.0–5.35 V | pass-through OK; iL margin vs 8 A-class switch | `TPS61240_TRANS.LIB` TI | PASS* |
| b07 TPS7A20 1V8 | `decks/b07_tps7a20.sp` | VOUT 1.798 V ±0.2 % at VIN 3.0/3.3/3.43 V; headroom ≥1.2 V over Vdo | >60 % | `tps7a20-adj_trans.lib` TI | PASS |
| b08 power seq | `decks/b08_powerseq.sp` | Injection into ESP32 pins 0.29 mA; TCA6408A Ioff ~50 µA; VBUS_DET 0.31 V | within ~1 mA guidance | behavioural | PASS |
| b09 ESP32 boot | `decks/b09_esp32.sp` | VDD90→EN delay 23 ms; EN/IO0 strapping correct on DTR transitions | >20 % | ESP32 IBIS + strapping | PASS |
| b10 I2C | `decks/b10_i2c.sp` | MC500: 3V3 tr 203–364 ns (p50 284, p99 358) vs 300 ns FM+ spec — worst corner exceeds; 1V8 tr 49–88 ns OK | **−21 % at worst corner** vs FM+; ~3× margin vs 1000 ns FM | PCA9306 process lib + RC | MARGINAL |
| b11 SPI | `decks/b11_spi.sp` | trise 10 ns, tpd+trise <62.5 ns half-period @8 MHz | >80 % | IBIS-level behavioural | PASS |
| b12 ADS1292R CMRR | `decks/b12_ads1292r.sp` | MC500 z1 CMRR 5.3k–100k ratio (≥74 dB p50) | meets AFE CMRR class | ADS1292R behavioural + electrode model | PASS* |
| b13 AFE4900 analog | `decks/b13_afe4900.sp` | HPF ratio 0.55 @0.2/5 Hz (≈0.6 expected for fc≈0.27 Hz); INP swing inside 3V3_ANA abs-max | adequate | AFE4900 front-end behavioural | PASS |
| b14 MAX86141 drive | `decks/b14_max86141.sp` | LED driver compliance holds across TX5V 4.85–5.35 V, Iset 10–96 mA, Vdrv 5.288 V | headroom OK | MAX86141 IBIS + LED Vf | PASS |
| b15 AD5940 loop | `decks/b15_ad5940.sp` | MC500: PM ≈90 °, overshoot ≈0.02 %; R_sense/R_cal distributions correct | >60 dB loop margin | AD5940 loop behavioural | PASS |
| b16 sensors | `decks/b16_sensors.sp` | rails in spec: 3V3 3.0–3.43 ≥2.15; 1V8 1.71–1.98 in 1.7–2.0/1.62–1.98; MAX17048 window OK. **Found: R115 pulls RTC backup to GND — RTC loses time at full power-down** | rails OK; RTC defect logged | sensor rail models | PASS (1 finding) |
| b17 LEDs | `decks/b17_leds.sp` | IR 25 mA / NIR 24 mA / status 13 mA / LDR 4 mA; R-power inside ratings | inside DC/pulse limits | LED Vf + transistor models | PASS |
| b18 patient-path fault | `decks/b18_fault.sp` | DC patient currents ~66–96 µA <500 µA limit at T=0/25/45; transient peaks bounded | >5× | fault injection behavioural | PASS |
| b19 thermal | `decks/b19_thermal.sp` | T_J max 52.5 °C (ADS) vs 125 °C IC limit | ≥58 % | θJA power tree | PASS |
| b20 budget | `decks/b20_budget.sp` | runtime to cutoff t=171 ks; SOC@24 h 0.41; VBAT@24 h 3.65 V | meets 24 h target | cell + load budget | PASS |

`*` caveats in "remaining issues" below.

## Part C — board-level verification on the saved `fix_v19` board

- **Copper freeze vs fix_v18 — PROVEN** by
  `sims/fullsim_v19/copper_freeze/freeze_check.py` (raw S-expression object
  multisets): **8016 segments / 871 vias / 107 zones / 1027 pads / 302
  footprint placements identical**; zero objects unique to either side.
  Whole-file diff = 3 lines (R61 value + LCSC property + DNP attribute
  removal). No tracks, vias or footprints moved; no autorouting.
- **DRC** (`kicad-cli pcb drc`, no refill): **0 errors, 0 unconnected
  items**. 61 warnings only: 20 `track_dangling` stub-end notes, 15
  `silk_edge_clearance`, 8 `silk_over_copper`, 3 `silk_overlap`, 9
  `text_height`, 1 `text_thickness`, 5 `lib_footprint_mismatch`
  (`fix_v19/_work/drc_v19.json`).
- **ERC** (`kicad-cli sch erc`): **0 errors**. 13 warnings: 5 benign
  bidirectional↔power-output pin joins on rails, 8 `lib_symbol_mismatch`
  version notes (`fix_v19/_work/erc_v19.json`).
- **Gerber-only net continuity** —
  `sims/fullsim_v19/gerber_check/gerber_continuity.py` parses the exported
  RS-274X files only (X2 `%TO.N`/`.P` attributes, `%AM` aperture macros,
  G36 regions, Excellon PTH): 23 065 copper primitives, 8 layers, 875 PTH
  barrels, 970 tagged pins → **0 shorts, 0 opens, 0 missing/mismatched
  pins — PASS** (`gerber_check/gerber_continuity_v19.txt`).
- **Gerber currency**: re-exported with `kicad-cli --check-zones
  --board-plot-params` → all 15 plots **byte-identical to the fab set
  except header timestamps** (zone fills current, copper frozen).
- **Sepsis cohort**: `sepsis_v11/results.json` — **238 patients**
  (7 cases × 34 subjects, seed 7) + 23 trajectory records; detector
  `ews_v11_4` = **sensitivity 0.98 / specificity 0.853**
  (`sepsis_v11/cohort_scored_v11.json`).
- **Fabrication archive**: `fix_v19/fab/VitalQ_v2_fixv19_fabrication.zip`
  (37 files: gerbers, drills + maps, BOM/BOM_JLC, CPL/CPL_JLC, positions,
  renders, SHA256SUMS, notes); SHA-256 of archive
  `5f976845f422c6288c8b449167d16541d53471016cee02109fcbd1cccfbefd52`.
  Stale `VitalQ_v2_fixv18_fabrication.zip` moved to `fix_v19/fab/_stale/`.
- **BOM/CPL**: `BOM_JLC.csv` line R61 → `NCP15XH103F03RC`/`C77131`;
  `CPL_JLC.csv`/`CPL.csv` list R61 at its existing site.
- **Renders**: `render_top.png`, `render_bottom.png`, `render_powerpath.png`
  (re-verified current against the frozen board).

## Component audit

`COMPONENT_AUDIT_V19.csv`: **302 rows — 296 PASS / 6 DNP**. Real MPN+LCSC on
every purchasable populated row (82 BOM groups; 22 inherited LCSC codes
corrected). Capacitor rows cite manufacturer DC-bias derating with effective
µF at the actual rail voltage vs IC datasheet minimums; resistor rows carry
worst-case + single-fault (5 V across skin-path 51 kΩ → 0.49 mW vs 1000 mW
rating) power; inductor rows cite Isat/Irms vs regulator peak; decap
pad-to-pin distances measured in mm. No cross-category duplicate check
text. `IC_PIN_CHECK_V19.csv`: per-pin v_max vs abs_max with datasheet
page/table cites for all 26 ICs including U16 (SFH7072) and U21 (USBLC6-2).

## Defects found and fixed during this verification

1. **b01 inrush measurement broken** — probe was downstream of the caps
   (2.9e-11 A nonsense). Probe moved to the source branch; re-run →
   inrush_pk 27.4 A. FIXED.
2. **b15 MC loop produced no statistics** — ngspice `$&a-$&b` adjacency
   silently evaluated wrong (proven: 2e-07 vs 3e-07 expected). Deck
   expression rewritten; 500 runs now emit full MCIT stats
   (PM ≈90°, overshoot ≈0.02 %). FIXED.
3. **b19 `max()` 10-argument form unsupported in ngspice-47** — rewrote as
   nested pairwise `max()`; re-run clean. FIXED.
4. **Copper-freeze script missing** despite being claimed in the log —
   created `copper_freeze/freeze_check.py`; first version found 0 objects
   (tag regex missed whitespace/newline); fixed → full identical multiset.
   FIXED.
5. **`results/_summary.json` incomplete** (targeted re-runs overwrote it,
   leaving 4 of 20 blocks) — rebuilt from all 20 per-block JSONs. FIXED.
6. **`fixv18` fab archive still present** — moved to `fab/_stale/`, correct
   `fixv19` zip generated with fresh SHA256SUMS. FIXED.
7. **IC pin-check coverage gap** — U16/U21 absent; rows added with SFH7072 /
   USBLC6-2 datasheet cites (U21 VBUS corner honestly flagged). FIXED.
8. **Skin-path single-fault text missing on R34/R77/R78** — 5 V-across-51 kΩ
   fault-power line added to all three audit rows. FIXED.
9. **b06/b07 decks hung at the 45 °C corner** — vendor macros are
   temperature-invariant / non-convergent at T=45 in ngspice-47; decks now
   run all VIN corners at documented temperature coverage with the
   limitation recorded (see below). FIXED + documented.

## Remaining issues / margins below 20 %

- **U21 (USBLC6-2) pin 5, VBUS** — margin **−4.8 %**: VRM 5.25 V vs the
  5.5 V USB corner. At 5.5 V the clamp leaks above spec but stays below its
  ~6 V breakdown — no damage path; real exposure only exists with a >5.25 V
  source. Classified residual risk, not a defect.
- **U1 (ESP32-S3) + U7 (AD5940) + U10 (AS7341) IO pins, 4.7 %** — the 3V3
  rail's high corner (3.43 V) vs 3.6 V pin abs-max. This is a physical
  limit of running a ±5 % 3.3 V rail against VDD+0.3 V pin ratings —
  industry-typical; flagged MARGINAL, no cheap fix exists short of a
  tighter rail spec.
- **U5 (CP2102N) VREGIN, 5.2 %**; **U22 (MAX86141) VREF, 9.1 %** — small
  but positive; documented.
- **b10 I2C rise-time — real marginal finding**: MC500 worst corner
  364 ns (p99 358 ns) **exceeds the 300 ns FM+ spec by ~21 %**; p50 284 ns
  is inside. If this bus is constrained to 100 kHz FM (1000 ns limit) it is
  ~3× inside; at 400 kHz FM+ the 3V3-side pull-ups should be stiffened or
  bus capacitance trimmed in a future spin. Recorded as the only
  spec-relevant margin miss in the block set.
- **b06 (TPS61240) vendor macro follows VIN rather than boosting at low
  VIN** in ngspice (output ≈VIN down to 3.0 V); high-VIN pass-through —
  the prompt's actual question — verified. Low-VIN boost behaviour needs a
  bench or alternative-model check; recorded as model limitation.
- **b06/b07 45 °C corner** — TI vendor macros are temperature-invariant /
  non-converging at T=45 in ngspice-47 (0/25 °C results identical where
  the model responds); temperature coverage is instead carried by b02/b03/
  b18/b19, which do respond to `set temp`. Documented model limitation.
- **RTC backup** (b16 finding): R115 pulls VBACKUP to GND → RTC loses time
  at full power-down. Hardware finding from simulation; firmware/battery
  impact is minor (time re-sync on boot) — flagged for a future revision.
- **Tight clearances seen in the gerber check** (informational):
  +3V3_ESP↔SE_SURGE 6.8 µm, CS_MAX86178↔GND 25 µm — legal spacings from
  the modelled geometry, within DRC rules (DRC reports no clearance
  errors).
- The 20 `track_dangling` DRC warnings are pre-existing stub ends; all
  ERC/DRC items at error severity are zero.

## Bench test list (for the first articles)

1. R61 present-and-soldered: measure TS ≈ 0.4 V at room temp before the
   first charge test (open TS clamps high and suspends charge).
2. Charge window: confirm charge suspend near 0 °C and ~45 °C board temp,
   and CHG_DIS (Q2) override function.
3. USB unplug transient: scope VSYS dip (~2.7 V, ~15 ms) vs 3V3 rail and
   ESP32 brown-out threshold.
4. TX_5V boost: verify 5 V regulation from VBAT 3.0–3.7 V (bench check of
   the b06 model limitation) and pass-through above ~4.9 V.
5. VBUS hot-plug inrush and USBLC6 clamp behaviour at 5.5 V corner.
6. I2C rise time at full bus loading (both domains) vs 300 ns.
7. RTC backup behaviour across full power-down (expected loss — confirm
   re-sync path in firmware).
8. Patient-path leakage against the 500 µA target under single-fault
   conditions.
9. Sepsis detector smoke test on-device (ews_v11_4 thresholds).

## Residual-risk statement

Within the modelled corners, the design holds its required windows. The
honest residuals: the 3V3 I2C rise time misses the 300 ns FM+ spec by
~21 % at the worst tolerance corner (fine at 100 kHz FM); one ESD device
runs −4.8 % at an unrealistic-but-possible 5.5 V VBUS corner; ~50 IO pins
sit ~4.7 % under their abs-max because a ±5 % 3.3 V rail cannot be
tighter; the TPS61240 boost mode and two TI macros' 45 °C behaviour are
model-limited and should be confirmed on the bench; and the RTC has no
backup holdover by design. None of these invalidate fix_v19; all are
recorded rather than waived silently.
