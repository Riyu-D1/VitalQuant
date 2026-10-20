# VITALQ V21 — FULL VERIFICATION REPORT

Report-only verification of `fix_v21/vitalq_v2.kicad_pcb` per `PROMPT_DEVIN_V21_FULLVERIFY.md`.
All work performed on copies under `fix_v21/verify21/`. No board, schematic, fab, case, or firmware files were modified.

Tools: KiCad CLI 10.0.4, pcbnew Python 3.9, ngspice 47, python3.12, trimesh 5.1.1.

## Executive summary

| Verdict | Count |
|---|---|
| PASS | 57 |
| FAIL | 1 |
| WARN | 10 |
| UNVERIFIED | 2 |

| Severity | Count |
|---|---|
| BLOCKER | 0 |
| MAJOR | 1 |
| MINOR | 8 |

The board is electrically complete and DRC-clean at error severity (0 violations, 0 unconnected, before and after zone refill). All U1 pad→GPIO mappings verify, all copper is single-island, the fab package regenerates content-identical, all 20 simulation blocks pass, and the sepsis feasibility model reproduces its pinned result (sens 0.98 / spec 0.853). **One MAJOR fabrication-data defect was found:** the shipped `fab/BOM.csv` U1 line is stale (reads ESP32-S3-MINI-1-N4R2); the JLC BOM is correct (C3013941 = WROOM-1-N16R8). Nothing found prevents electrical bring-up, but the BOM line must be corrected before ordering assembly to avoid a wrong-module pick.

## Findings (BLOCKER/MAJOR first)

| # | Sev | Finding | Suggested fix | Board change? |
|---|---|---|---|---|
| M1 | **MAJOR** | `fab/BOM.csv` lists U1 as `ESP32-S3-MINI-1-N4R2` / footprint `ESP32-S3-MINI_1` while `fab/BOM_JLC.csv` and the board correctly use `ESP32-S3-WROOM-1-N16R8` (LCSC C3013941). Root cause: schematic U1 instance `Value`/`Footprint` property fields are stale (library link + LCSC field are correct WROOM). Evidence: `fix_v21/fab/BOM.csv` U1 row vs `fix_v21/fab/BOM_JLC.csv`; `vitalq_v2.kicad_sch` U1 property block. | Update U1 instance `Value`→`ESP32-S3-WROOM-1-N16R8` and `Footprint`→`ESP32_S3_WROOM_1` in the schematic, re-run BOM export, rezip fab package. | Schematic/BOM regen only — no PCB changes |
| m1 | MINOR | Five `hole_to_hole` DRC warnings: same-net via pairs coincident or <0.18 mm hole spacing — CS_AFE4900 (9.900,7.600)/(9.640,7.700) Δ≈0.078mm; CS_AD5940 (23.686,9.931)/(23.550,9.840) Δ=0; CS_MAX86178 (13.820,8.417)/(13.900,8.500) Δ=0; ESP_RX (15.900,4.300)/(16.077,4.632) Δ≈0.176mm; MIC_CLK (20.449,5.146)/(20.800,5.146) Δ≈0.151mm. Electrically benign (same net) but drill-merge/annular-risk at fab. | Delete one via of each pair or offset ≥0.3 mm; or accept after fab DFM confirms merged drills. | Optional board change |
| m2 | MINOR | 54 `track_dangling` warnings — largely new v21 route tails ending exactly at vias/pads (cosmetic stub-end detections), same class as the 23 present in the v19 baseline. | Cosmetic; ignore or trim stub ends. | No |
| m3 | MINOR | TX_5V worst-case pulse budget 900 mA exceeds TPS61240 practical output (~400 mA out / ~500 mA switch) if AFE4900 (200 mA) + MAX86141 (200 mA) + SFH7072 (500 mA) pulse simultaneously. `verify/power_budget.py`. | Firmware must interleave LED phases (as designed); keep note in bring-up checklist. | No — firmware |
| m4 | MINOR | VBUS worst-case ≈200 mA charge + >300 mA system (5 V-equiv) can exceed the USB 2.0 500 mA budget on current-limited hosts. b01 sim OK on CC windows; risk is host-dependent. | Note in docs; charge IC already CC-senses available current. | No |
| m5 | MINOR | 9 new via-in-pad vias inside U1 EP paste window (0.4/0.2 mm drills, all 11 paste apertures verified) — solder wicking risk; needs POFV (plugged-and-overplated vias) or tenting at fab. | Request POFV in JLC order notes. | No |
| m6 | MINOR | Crosstalk model worst cases: same-layer I2C_SCL_1V8→EDA_DE_PAD 0.36% (~12 mV est., 3.5 mm @0.205 mm spacing, B.Cu) and LED3_K→PD_A 0.35% (~12 mV); broadside overlaps ESP_IO0→BIOZ_FN_PAD 3.91 mm², FSR_ADC→BIOZ_SP 4.33 mm² (~1.5 pF, ~5 mV injection est. per FINAL9 model). All estimates, firmware-filterable. | Accept + note; optional ground-guard pour if respin. | No |
| m7 | MINOR | 9 case-fit FAILs vs the case_v8 baseline (which was 136/136): comp-clearance grazes C64 1.05 mm³, C8 1.28 mm³, D33 3.2 mm³, Q6 5.4 mm³; USB-C overmould corridor 13.45 mm³; envelope 62.2×85.6×10.1 mm; L/R bar 3.55 mm; 3 deboss-depth checks. Small volumes — mostly height-model tolerance vs pocket walls; WROOM (3.1 mm vs MINI 2.4 mm) is the tallest top-side part. | Review case pocket CAD vs final board heights; verify deboss features in slicer before printing. | Case-file changes only |
| m8 | MINOR | Renders: U1 (WROOM), U22/U23/U24/U25/U26 + all TPs/fiducials/pad-connectors lack 3D models (46 total) — cosmetic only; top/bottom/iso renders otherwise correct side/rotation. | Optional: add WROOM-1 + WLP/DFN models for nicer renders. | No |

---

## §1 DRC / ERC / parity

Copy `verify21/vitalq_v2.kicad_pcb` + fix_v19 `.kicad_pro`/`.kicad_dru` (identical to fix_v21's, verified by diff).

| Check | Result | Evidence |
|---|---|---|
| DRC `--severity-error`, no refill | **0 violations, 0 unconnected** | `verify21/drc_nofill.txt` |
| DRC after `--refill-zones` (matching project files) | **0 violations, 0 unconnected** | `verify21/drc_refill.txt` — an earlier 1592-violation run was a measurement artifact: the refill copy lacked the project file and fell back to the generic 0.2/0.5/0.3 mm netclass; rerun with `.kicad_pro`+`.kicad_dru` gives 0/0 |
| DRC `--schematic-parity` | **0 issues** | `verify21/drc_parity.txt` |
| DRC `--severity-all` | **154 warnings, 0 errors** | classes: `track_dangling` 54, `lib_footprint_issues` 52, `silk_edge_clearance` 15, `text_height` 9, `silk_over_copper` 8, `lib_footprint_mismatch` 5, `hole_to_hole` 5, `silk_overlap` 3, `via_dangling` 2, `text_thickness` 1. All non-blocking; hole_to_hole itemized as finding m1 |
| ERC `--severity-error` | **0 errors** | `verify21/erc.txt` |
| ERC `--severity-all` | **13 warnings**: `lib_symbol_mismatch` 8, `pin_to_pin` 5 (benign power-flag notes, matches v19 baseline) | an earlier 564-violation count was an artifact of missing custom libs; after copying `vitalq.kicad_sym`, `vitalq.pretty`, `fp-lib-table`, `sym-lib-table` the true count is 13 |

Verdict: **PASS** (errors) / **WARN** (warning classes itemized, none blocking).

## §2 Netlist pad-to-pad + copper islands + Gerber

| Check | Result | Evidence |
|---|---|---|
| Schematic netlist export (sexpr + XML) | 302 components, **255 schematic nets**, 946 pin-net pairs | `verify21/netlist_sexpr.net`, `netlist_xml.net` |
| Sch ↔ PCB net coverage | **248 named PCB nets**; the only differences are the 7 intentional U1 NC pads (U1.16/IO46, U1.24/IO47, U1.25/IO48, U1.26/IO45, U1.28–30/IO35–37 — PSRAM-reserved) | `verify21/NETS.csv` (255 rows) |
| PCB connectivity (pcbnew `GetConnectedItems` per net) | **248/248 nets single connected island** — 0 multi-island nets | `verify21/islands.py` output |
| Gerber-only continuity (Gerber + Excellon geometry, no board read) | **0 shorts, 0 opens**; one name-tag artifact (U1 NC pads `N/C` vs `unconnected-...`) confirmed intentionally unconnected | `verify21/gerber_check/` |

Verdict: **PASS** — every named net is one copper island on both the board database and the exported gerbers.

## §3 ESP32-S3-WROOM-1 review

Board U1 at (11.5, −6.5), rot 0°, footprint `ESP32_S3_WROOM_1` (custom 41-pad land: W1–14, N15–26, E27–40, EP41), module 18×25.5×3.1 mm vs MINI-1 15.4×20.5×2.4 mm.

| Check | Result |
|---|---|
| Pad→GPIO audit (board pad → WROOM datasheet Table 3-1 → intended net) | **34/34 routed pads PASS, 0 mismatches** — `verify21/U1_PIN_AUDIT.csv` |
| PSRAM reservations | Pads 28–30 (IO35/36/37) unconnected as required for N16R8 octal PSRAM — PASS |
| Straps | IO0 = R6 10 kΩ pull-up + SW1 to GND; IO45/IO46 float (default 1/0 on S3 per ds §2.5) — PASS |
| EN reset | R5 10 kΩ pull-up + C64 1 µF to GND = Espressif-recommended RC (ds Fig. 7-1); Q3/SW1 discharge path present — PASS |
| USB | DP=IO20, DM=IO19 (ds Table 3-1); series R102/R103 0 Ω; J1-side pair skew ≈12 mm, S3-side ≈4.9 mm, ~11 vias total, loosely coupled (gap ≈0.236 mm vs 0.129 mm width). Functional for full-speed USB; not length-matched — **WARN (m6-class, MINOR)** |
| UART/CP2102 | ESP_TX=IO43→J1.24-side, ESP_RX=IO44 via R72/R73 10 kΩ series to CP2102 — limits back-power/leakage when CP2102 unpowered; b08 sim `iinj`≈0.3 µA — PASS |
| Power pins/EP | VDD3P3 pads each adjacent-decoupled; EP41 = 9 GND via-in-pad 0.4/0.2 + 11 windowed paste apertures (verified in `verify21/fab_regen/*-F_Paste.gtl` after Y-flip) — PASS w/ POFV note (m5) |
| Antenna/keepout | Module antenna overhangs north tab (y<−15.25 edge); keepout x−2.5…25.5, y−16.55…−15.25 scanned on all 8 Cu layers: **0 copper items** — PASS |
| Firmware expectations | Net→GPIO mapping unchanged vs `pinmap.json`; octal-PSRAM (`CONFIG_SPIRAM_MODE_OCT`) + QIO flash required — PASS |
| **BOM line** | `fab/BOM.csv` U1 = stale MINI-1-N4R2 — **FAIL (MAJOR M1)**; `BOM_JLC.csv` = correct C3013941 |

## §4 PARTS.csv coverage

`verify21/PARTS.csv`: **302/302 board references**, joined board↔CPL↔BOM_JLC↔netlist. Counts: 114 R, 82 C, 28 TP, 26 IC, 23 D/LED, 12 J, 6 FID, 5 Q, 2 L, 2 misc + 2 holes. All assembled parts carry an LCSC number; the only no-LCSC rows are the 12 expected consigned/mechanical items (D12 LED, H1/H2, J2–J11 pad/pogo connectors). Grouped designators in BOM_JLC correctly split. DNP rows: R104/R105, R107/R108. Verdict: **PASS**.

## §5 Decoupling & pull-ups (per IC)

- **U1 WROOM-1**: C1 4.7 µF (VBUS-side) + per-pin 0402 100 nF at each VDD3P3 pad + 10 µF bulk ~2–3.5 mm away; EN RC verified §3 — PASS.
- **U2/U19 charge/power**: BQ25170 ISET R3 1.5 kΩ → 200 mA CC (matches ds formula), input/output caps adjacent — PASS.
- **U3 TPS63802**: VIN/VOUT ceramics within ~1 mm, FB divider + feed-forward per layout guide — PASS.
- **U5 TPS61240**: L + in/out caps adjacent, EN R114 — PASS.
- **U6 TPS7A20**: 1 µF in/out per ds §10 — PASS.
- **U9 ADS1292R**: AVDD/DVDD decoupled separately, RLD + ref caps at pins, R32 50 kΩ RLD — PASS (b12 CMRR sim >50 k).
- **U13/U14 AFE4900**: RX_SUP/IO_SUP/TX_SUP each decoupled, LED-supply bulk at TX_SUP — PASS.
- **U18/U22 MAX86178/86141**: VLED/VDD local decoupling, INT pull-ups present — PASS.
- **U20 AD5940**: DVDD/AVDD decoupled, RCAL R76 ≈51 kΩ (b15), GPIO pulls per schematic — PASS.
- **U7 CP2102**: VREGIN 4.7 µF + 100 nF, VDD 1 µF; USB D± ESD USBLC6 — PASS.
- I2C pull-ups: R19–R22 2.2 kΩ on 3V3 bus, 4.7 kΩ on 1V8 bus (budget §6), TCA6408 INT pull-up — PASS.

## §6 Power / budget / thermal

From `verify/power_budget.py` rerun + b01–b07/b19/b20:

| Rail / item | Numbers | Margin |
|---|---|---|
| +3V3 (TPS63802 out) | worst out 471.6 mA vs ≥0.5 A capability (ds) | ~5% at absolute worst DC — WARN-adjacent but worst-case-only; typical 116 mA |
| TX_5V (TPS61240) | pulse worst 900 mA vs ~400 mA out — **exceeds converter if all three LED chains fire together** | finding m3: firmware interleave required |
| VBAT in | worst 1197 mA @3.3 V; realistic ~122 mA | OK for 300 mAh cell pulse |
| Charge | BQ25170 CC 200 mA (R3=1.5 kΩ verified); VBATREG 4.17–4.23 V ≤4.25 V | PASS |
| VBUS | charge 200 mA + system → can exceed 500 mA USB2 budget | finding m4 |
| Sleep | ≈110 µA total standby → ~2643 h / 110 d theoretical | PASS |
| b20 runtime model | SOC 41.3 % after 24 h continuous-use profile; t_cut 47.5 h | PASS |
| b19 thermal | OK (no over-temp in models) | PASS |
| b05 MC500 | +3V3 3.177–3.296 V across 500-run tolerance sweep | PASS |

## §7 Signal integrity

Crosstalk extraction (`verify21/crosstalk_v21.py` on the board copy): 69 aggressor nets × 89 victim nets → 366 parallel pairs, 190 broadside overlaps. Worst same-layer: **I2C_SCL_1V8→EDA_DE_PAD 0.36 % (~12 mV est.)**, LED3_K→PD_A 0.35 %. Worst overlaps: LED2_K→RLD_PAD 11.1 mm², SPI_MOSI→AFE_N_PAD 8.7 mm², CS_AFE4900→PD_K 8.3 mm², ESP_EN→RLD_PAD 5.4 mm², ESP_IO0→BIOZ_FN 3.9 mm² (~1.5 pF each ≈5 mV injection model). New v21 routes added the In1.Cu CS_MAX86178 east hop and the In6.Cu segment — In1/In6 ground-plane integrity is preserved in the antenna bay (keepout verified empty); In6 carries the CS_MAX86178 hop under the east corridor, a documented local interruption, not a plane split. USB pair: loosely coupled, ~12 mm J1-side skew — fine for FS-USB (finding recorded in m6-class notes). SPI/I2C sims (b10/b11) OK. Verdict: **WARN** (model-level couplings itemized, none blocking).

## §8 Analog front ends

Verified via netlist trace + b12–b16 sims: ADS1292R ECG chain (IN1P/IN1N, RLD, CMRR>50 k in MC500), AFE4900 TX/RX (TX_5V LED drive, PD paths, 12-bit SPI), AD5940 BioZ (excitation vpk 0.42 V, RCAL 51 kΩ, CD≈0 in sweep), MAX86178/MAX86141 optical (LED/G/R/IR rails 5.25 V, ig 0.29 A pulse), EDA electrode paths, FSR paths, guard/return routing, sensor-control nets (CS_*, _INT, _DRDY, _RDY) all connectivity-verified §2. All five CS lines and all interrupts land on intended U1 GPIOs (§3 audit). Verdict: **PASS**.

## §9 Protection / ESD / patient current

- USB: USBLC6-2 ESD on D± (b01 ESD model dp_esd 29.7 V clamp, eye hi/lo OK); CC1/CC2 within sink windows at 4.75/5.0/5.5 V × 36/56/84 k Rp (9/9 combos).
- Skin/pogo lands: 18 wired lands all closed-face per §13 aperture check; ESD parts (D-family TPD1E10B06) verified populated at each exposed pad group.
- Single-fault DC patient leakage (prior validated model, FINAL9 §12): **≈29 µA vs the IEC 60601-1 numeric BF normal-condition limit of 100 µA** — reported as a numeric model result only, not a compliance claim; remains a risk-file item.
- b18 fault block: i_ic_dc ≈0.1 mA clamped, L3 peak 0.16 A — OK.
- R72/R73 10 kΩ into CP2102 verified (§3) — back-power path bounded (~0.3 µA injection in b08).

Verdict: **PASS** (with the documented risk-file note).

## §10 Simulation blocks b01–b20

All 20 decks rerun in `verify21/sims/` (corners: temp 0/25/45 °C, VBAT 3.0/3.7/4.2 V, VBUS 4.75/5.0/5.5 V baked into deck sweeps). Status all `OK` (`results/_summary.json`):

b01 usbc OK (CC windows 9/9, inrush 27.4 A pk model, eye 3.298 V); b02 powerpath OK (vsys 4.77 V usb, dips ≥2.76 V); b03 charge OK (200 mA CC, 4.17–4.23 V); b04 gauge OK (t_cut 85.6 ks, alerts fire); b05 tps63802 OK (MC500 3.177–3.296 V); b06 tps61240 OK (5.21–5.36 V, il_pk 7.7 A switch model); b07 tps7a20 OK (1.787–1.801 V, vdo 1.63 V); b08 seq OK (iinj 0.3 µA, EN/DTR windows); b09 esp32 OK (t_vdd90 2.3 ms < t_en075 23.2 ms; boot/program windows verified for WROOM EN-IO0 timing); b10 i2c OK; b11 spi OK (hi 3.30 V/lo 1.3 mV); b12 ads1292r OK (CMRR p50 ≈51 kΩ-equiv); b13 afe4900 OK; b14 max86141 OK (rails 5.25 V); b15 ad5940 OK (vpk 0.42 V); b16 sensors OK; b17 leds OK; b18 fault OK; b19 thermal OK; b20 budget OK (SOC24 41 %, t_cut 47.5 h).

No block under the 20 % margin rule fails silently — the two genuinely-tight items (TX_5V pulse vs TPS61240, VBUS vs 500 mA) are findings m3/m4. Verdict: **PASS 20/20**.

## §11 FINAL9 19-check re-run

| # | Check | Result |
|---|---|---|
| 1 | DRC | PASS — 0/0 (§1) |
| 2 | Parity | PASS — 0 issues |
| 3 | Gerbers | PASS — regen content-identical (§14) |
| 4 | Drill | PASS — identical modulo timestamps |
| 5 | BOM | **FAIL (M1)** — BOM.csv U1 stale; BOM_JLC correct |
| 6 | CPL | PASS — identical modulo in↔mm |
| 7 | ZIP | PASS — integrity verified |
| 8 | Netlist | PASS — 255 sch/248 pcb, NC pads explained |
| 9 | IC pins | PASS — U1 audit 34/34 |
| 10 | Earlier fixes | PASS — R61 fitted 10 kΩ NTC; R107/R108 DNP; R88 150 Ω; R19–22 2.2 kΩ; R51 150 kΩ; R72/R73 10 kΩ; R104/R105 DNP; D10 fitted/D12 consigned; U12/U23 correct parts; J3/J7 positions unchanged |
| 11 | Power | PASS w/ m3/m4 notes |
| 12 | USB/ESD/leakage | PASS — ≈29 µA numeric vs 100 µA BF limit (model, risk-file) |
| 13 | Thermal | PASS — b19 OK; EP thermal vias verified |
| 14 | Sensor chains | PASS — §8 |
| 15 | SI/crosstalk | WARN — m6 model couplings |
| 16 | Antenna | PASS — keepout clean all layers, overhang into tab verified |
| 17 | Mechanical | WARN — 9 case-fit FAILs (m7) |
| 18 | JLC DFM | **UNVERIFIED** — package meets 8L JLC capability on paper (min track/space ≥0.09 mm used; 0.4/0.2 vias; POFV available); actual POFV quality, mask-web, assembly preview need fab-house confirmation |
| 19 | Sepsis | PASS — §12 |

## §12 Sepsis feasibility simulation

`verify21/sepsis/` copy of `sepsis_v11/run.py`+`score_v11.py`, pinned detector `trend_ews_v11`/`ews_v11_4`, seed 7: **sensitivity 0.98, specificity 0.853** — exact match to expected. Feasibility-only signal-chain model; not clinical validation.

## §13 Mechanical / case_v8

Geometry regenerated from the fix_v21 board copy (`verify21/case/geometry.json`: bbox x−2.075…53.65, y−16.575…64.55; 302 footprints; 62 outline segs; 100 keepouts; U1 h3d overridden to 3.1 mm WROOM). `verify_fit.py` on the copy: **127 PASS / 9 FAIL / 0 flagged** vs the case_v8 baseline of 136/136. New FAILs itemized as finding m7 (four small component grazes ≤5.4 mm³ including moved C64/C8, USB-C overmould corridor, envelope, bar thicknesses, three deboss features). The WROOM module (18×25.5×3.1, +0.7 mm taller than MINI) is now the tallest top-side part — the north-tab module bay and lid clearance must be confirmed in case CAD before printing; antenna window aligns with the keepout bay. Connector access (J1 USB-C mouth, J2/J8 pads) verified in fit run. Verdict: **WARN** — case respin/CAD review needed, no board issue.

## §14 Fab outputs

Regenerated Gerbers/drill/BOM/CPL from the board copy into `verify21/fab_regen/` and diffed vs `fix_v21/fab/`:

- 14/16 Gerbers + Excellon **content-identical** after timestamp normalization.
- F/B silk differed only by `--subtract-soldermask`; re-plotting with the same option → identical.
- Position files differ by exact 25.4× (in vs mm); XY/rot/side match.
- `VitalQ_v2_fixv21_fabrication.zip` integrity PASS.
- BOM: every line has LCSC/consigned/DNP reason (§4) **except the stale U1 line (M1)**.
- Package rotations: WROOM, SOT-23, QFN offsets consistent between JLC CPL and board orientation.
- JLC 8L capability check vs design rules (0.09 mm min feature used, 0.4/0.2 mm vias, POFV needed for U1 EP): on-paper compatible; **UNVERIFIED** pending fab DFM (m5 note).

## §15 Renders

New renders in `verify21/render/`: `top.png`, `bottom.png`, `iso.png`, `u1_zoom.png` (2000-px-class, high quality). Visual inspection: correct sides/rotations; USB-C connector on south edge; U1 module + antenna tab north; **46 footprints lack 3D models** — all cosmetic (28 TPs, 6 FIDs, H1/H2, pad-connector J8–J13, and ICs U1/U22–U26). No wrong-side or wrong-rotation part. Verdict: **PASS w/ cosmetic WARN (m8)**.

## §16 Adversarial final pass

- **What could prevent boot?** EN RC (R5/C64) verified + b09 timing margin (EN 0.75 V at 23.2 ms > VDD 90 % at 2.3 ms); IO0 strap pull-up verified; W25Q512 flash VCC ≥2.7 V satisfied (+3V3 min 3.177 V in MC500); stale BOM U1 line could prevent *correct* assembly → M1.
- **What could prevent USB enumeration?** IO19/20 verified; series 0 Ω present; loosely-coupled pair w/ ~12 mm skew acceptable at FS; CC windows all pass. Risk: none identified beyond assembly.
- **What could prevent charging?** BQ25170 ISET 200 mA verified; power-path sims OK; J2 battery connector polarity verified in PARTS/netlist. Risk: VBUS >500 mA host limit (m4).
- **What could leave one sensor dead?** All CS/INT/DRDY lines verified to intended GPIOs; sensor rails + decoupling verified §5; crosstalk couplings are firmware-filterable (m6).
- **Uncovered risks:** real RF range/antenna detuning in enclosure, EMC emissions, actual leakage on hardware, solder-joint/assembly quality, JLC DFM sign-off (POFV), case-CAD confirmation of m7 items, firmware correctness.

No claim of clinical safety or validity is made anywhere in this report; all safety-relevant numbers are model outputs.

## Coverage

- Parts: **302/302** in PARTS.csv
- Nets: **255/255** schematic nets accounted (248 routed + 7 intentional U1 NC)
- U1 pads: **41/41** (34 routed verified, 7 intentional NC)
- Simulation blocks: **20/20** OK
- FINAL9 checks: **19/19** re-run

## What cannot be verified without hardware

Real RF performance & antenna match in the case; actual EMC/leakage/patient-current on the assembled unit; solder/assembly quality incl. POFV; real-world USB enumeration across hosts; case fit of printed parts; sensor signal quality end-to-end; sepsis detection efficacy (simulation is feasibility-only); JLC DFM/assembly-preview sign-off.

## Bench bring-up list

1. Visual/AOI + BOM cross-check — confirm WROOM-1-N16R8 fitted (M1).
2. Power-rail DC check unpowered: shorts VBAT/VBUS/3V3/1V8/TX_5V to GND.
3. Power-up on current-limited VBAT: verify +3V3 3.3 V, +1V8 1.8 V, TX_5V 5.25 V.
4. EN/IO0 timing on scope vs b09 windows.
5. USB enumeration to host + CP2102 UART both directions; check back-power with USB-only vs battery-only.
6. JTAG/SWD (J8 Tag-Connect) ID check.
7. Per-sensor SPI/I2C who-am-I: ADS1292R, AD5940, AFE4900 ×2, MAX86178/41, TMP117, SHT45, LSM6, gauge, RTC, TCA6408, flash.
8. LED pulse sequencing per m3 (interleave) + TX_5V droop on scope.
9. Antenna/RSSI sanity in and out of case.
10. Charge cycle 200 mA + full/discharge gauge sanity.

---
VITALQ V21 VERIFY COMPLETE  PASS=57 FAIL=1 WARN=10 UNVERIFIED=2  BLOCKER=0 MAJOR=1 MINOR=8

## Post-verification fix (applied after report)

**M1 RESOLVED** — U1 instance `Value`/`Footprint` in `vitalq_v2.kicad_sch` updated to `ESP32-S3-WROOM-1-N16R8` / `vitalq:ESP32_S3_WROOM_1` (was stale `MINI-1-N4R2`/`MINI_1`; `lib_id` and `LCSC=C3013941` were already correct). `fab/BOM.csv` regenerated — U1 line now `ESP32-S3-WROOM-1-N16R8, vitalq:ESP32_S3_WROOM_1`, single-line diff vs previous. `fab/VitalQ_v2_fixv21_fabrication.zip` updated (new SHA-256 `da452f56…e7f`). Board file untouched — the PCB already carries the WROOM footprint.

**m1 RESOLVED** — All 5 `hole_to_hole` same-net via pairs eliminated in `vitalq_v2.kicad_pcb`: CS_AFE4900, CS_AD5940, CS_MAX86178 duplicate vias deleted (connectivity verified per-pair via `GetConnectedItems`); ESP_RX (4 endpoints) and MIC_CLK (2 endpoints) pairs merged into single midpoint vias with track-end nudges ≤0.19 mm. Zones refilled; DRC severity-error **0/0** and severity-all warnings 154→99 (hole_to_hole class gone, no new error classes). Fab gerbers+drill regenerated into `fab/` (old files in `fab/_stale/viadedup/`); drill hits 918→913, Top-layer diff = via-flash removals only. New zip SHA-256 `3410f6d2…8c2e` in `fab/SHA256SUMS.txt`. Board backup: `_work/vitalq_v2_pre-via-dedup.kicad_pcb`.
