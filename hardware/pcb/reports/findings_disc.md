# Findings — Discretes + Optical LED group (fab-gate audit)

**Target:** fix_v19 @ `b9002188` (`vitalq_v2.kicad_pcb` sha `78441506…`, fab zip `VitalQ_v2_fixv19_fabrication.zip` sha `5f976845…`)
**Scope:** U16 (SFH 7072), D10/D11/D12/D25 LEDs, D1–D9/D21–D24/D26–D30 ESD (18× TPD1E10B06DPYR), D33 1N5819WS, Q1/Q2/Q4 CSD13380F3T, Q3 BC847BS-7-F, Q6 AO3401A, L1/L2 DFE201612E.
**Method:** pure s-expression re-extraction of `vitalq_v2.kicad_pcb` (pad→net map; the shipped `01_netlist_pinmap.csv` has an empty `net` column — its parser read `net[2]` but pads store `(net "NAME")` single-arg; corrected parse used throughout), cross-checked vs `vitalq_v2.kicad_sch` embedded symbol pin-maps, `fab/BOM_JLC.csv`, `fab/BOM.csv` (DNP column), `fab/CPL_JLC.csv`, and `reports/datasheets/*.txt`.

---

## A. Corrected premise

**TPD1E10B06DPYR is BIDIRECTIONAL, not unidirectional.** The brief said "unidirectional part!"; the datasheet in corpus says the opposite: *"TPD1E10B06 is a bidirectional TVS"*, symmetric breakdown `VBR1,2 = VBR2,1 = 6 V`, recommended range "Pin 1 to 2 or Pin 2 to 1: ±5.5 V" (`TPD1E10B06DPYR.txt` lines ~310, ~229, ~182). Consequence: mounting orientation is electrically irrelevant, though the design still follows the datasheet convention pin1=IO / pin2=GND on all 18 placements. **No polarity defect here.** The `vitalq:D_TVS_2` schematic symbol is even documented "Bidirectional TVS" — consistent.

## B. Real findings (ordered by severity)

### B1 — WARN: `MX_ECG_INP` / `MX_ECG_INM` are dead-end nets; D29/D30 clamp stubs
`MX_ECG_INP` contains exactly two pads: `J13.1 → R118 → D29` and nothing else; `MX_ECG_INM`: `J13.2 → R119 → D30`. **No IC pad exists on either net.** J13 itself is **DNP** (`fab/BOM.csv` line 117, "MX ECG pads"). So the two fitted TVS diodes protect stubs that reach no silicon — wasted placements, and if the mux-ECG feature is intended for a future rev the downstream net is missing entirely, not just the connector. Severity: low (J13 is DNP); recommend deleting or completing the chain before relying on it.

### B2 — WARN: ESD coverage gaps on exposed pads
| Exposed pad/connector | Nets | Protection |
|---|---|---|
| J10 Pads_1x06 (optical sensor) | LED1_K, LED2_K, LED3_K, LED_AN, PD_K, PD_A | **none** — PD_K/PD_A go straight to U22 (MAX86141) pins; LED_AN = TX_5V via 0Ω R114 |
| J12 FH12-14S FPC pins 5–10 | same LED/PD nets | **none** (only SWEAT_* pins 11–13 are clamped by D26–D28) |
| J12 pins 2–3, J9 Pads_1x04 | I2C_SDA, I2C_SCL, +3V3 | **none** (2.2k pull-ups only) |
| J3 Pads_FSR pin 2 | FSR_ADC | **none** — J3.2 → R8(10k) → ESP32 GPIO5 direct |
| J1 USB-C CC1/CC2 | USB_CC1/CC2 | **none** — U21 USBLC6-2SC6 covers only DP/DM/VBUS (pins 1,3,4,6,5); CCs have 5.1k R1/R2 only |
| J2 Pads_LiPo | VBAT, TS | none (normal for battery pads) |
| J8 TC2030 debug | ESP_TX/RX/EN/IO0 | none (conventional for debug headers) |

J10/J12 are the material gap if the optical flex or sensor pads are user/skin-accessible — a strike on PD_K/PD_A lands directly on MAX86141 analog pins with no series element. Recommend TVS or at minimum series-R on those lines at the flex interface, and a note on CC/FSR.

### B3 — WARN: LED pin-1 = ANODE convention on generic cathode-marked footprints (assembly risk)
All four discrete LEDs use the custom `vitalq:LED_AK` symbol, documented "pin 1 anode, pin 2 cathode", wired pad1=anode:
- D11 pad1=IR_AN, D12 pad1=NIR_AN, D25 pad1=LED_A, D10 pad1=+3V3 (anode side).
The generic KiCad `LED_0402/0603` footprints place the pin-1/cathode indicator on pad 1 — i.e., the footprint mark sits on the **anode** pad in this design (verified: silk dot at (−1.09,0) adjacent to pad1 for D11/D12; chamfered fab corner + extended silk on pad1 side for D25). For D11 this is actually *correct* by its own datasheet ("Package marking: Anode", SFH4053 pin 1 = anode). For D12 (XL0603NRC730, consigned — cathode marking unknown) and D25 (KT-0603G), a vendor/JLC technician following the common "mark = cathode" convention would mount them backwards; a backwards LED simply doesn't light, but it's a real fab-gate risk. **Action:** add explicit A/K orientation callouts to the fab notes/assembly drawing and confirm rotation in the JLC preview; verify the consigned XL0603NRC730's actual marking before build. CPL rotations themselves are consistent (all CPL entries equal the board footprint rotations — see D).

### B4 — WARN: D25 status LED marginal drive on +3V3
KT-0603G Vf = 2.7–3.2 V @5 mA (test box says "Normal VF = 2.9–3.4 V"). With R88 = 150 Ω on 3.3 V: I ≈ (3.3−2.9)/150 ≈ 2–4 mA typical, and a high-bin unit (Vf≈3.3–3.4 V) may emit essentially nothing. Recommend dropping R88 to ≤68–100 Ω or accepting dim indication. Severity: cosmetic/functional.

### B5 — WARN: D10 NF2W757G-F1 headroom on +3V3 via AS7341 LDR sink
Nichia Vf bin range 2.4–3.3 V @65 mA (typ 2.9 V). AS7341 LDR pin is a 4–258 mA programmable current sink (reg 0x74 LED_DRIVE[6:0], abs-max 3.6 V). At low/medium drive (≤~20–40 mA, Vf~2.6–2.8 V) there is ~0.5–0.7 V compliance — works. At high bins/high programmed current the rail starves the sink (Vf max 3.3 V = rail). No series R is needed given the current sink. Severity: functional corner — cap LED_DRIVE in firmware or accept reduced brightness on high-Vf units.

### B6 — WARN: TS node has two parallel NTC paths — borderline hot-fault at room temp
BQ25170 TS spec (corpus): `ITS_BIAS ≈ 38 µA`, charge window ≈ 188 mV (hot) … 1.04 V (cold); "Pulling TS < VTS_ENZ (40–60 mV) will disable the charger" (Q2's CHG_DIS function — correct and intended). But `TS` = {U2.3, R61 (NTC 10k→GND), J2.3 (pack NTC→GND), Q2.3}. If **both** NTCs are present: R_eq ≈ 5 kΩ → V_TS ≈ 190 mV at 25 °C — sitting on the hot threshold. Verify whether R61 is meant as a fallback for packs without thermistor (then it's fine when J2.3 is NC), or whether thresholds were computed for the parallel combination. Severity: charger reliability corner.

### B7 — INFO: electrode TPDs sit on the IC side of the 51 kΩ series chain
Every electrode-facing TPD is on the IC-side node, not the connector pad node, e.g. J5.1 ECG1_PAD →R32(51k)→ `ECG_P` {D1, C50, C52, R30, **U8.6 direct**} — and the BIOZ/EDA chains additionally route through second resistors/coupling caps (e.g. J7.1→R80→FP_SURGE→C67→FP_ISO→R84→U7.B3 with D21 on the mid node). This is consistent with a defibrillation-protection convention (the 51k 2512s take the energy, TVS clamps residual at the IC pin) — but then the 51k resistors see near-full strike voltage; **verify the 2512s are pulse/defib rated** (they are generic YAGEO RV2512 in the BOM, not documented pulse-rated). For IEC 61000-4-2 (30 kV-rated TVS), the clamp still limits the IC node correctly. D26–D28 are the exception — they sit directly on the J12-exposed net (pad-side placement, the stronger arrangement).

### B8 — PASS: SFH 7072 (U16) internal wiring verified end-to-end
Datasheet pinout (corpus txt p.9): 1=PD1_C, 2=PD1_A, 3=PD2_C, 4=IR_A, 5=G1_A, 6=G1_C, 7=R_A, 8=R_C, 9=IR_C, 10=PD2_A, 11=G2_A, 12=G2_C — matches the `vitalq:SFH7072` symbol pin-for-pin. Board: anodes (4,5,7,11)→TX_5V; cathodes G1_C→TX1(U6.D4), G2_C→TX2(U6.E5), R_C→TX3(U6.E4), IR_C→TX4(U6.C5); PD cathodes→AFE4900 INP/INP2, anodes→INM/INM2 — AFE4900 datasheet explicitly wants "INP: PD cathode / INM: PD anode" ✓. Vf vs TX_5V=5.0 V: worst-case green 2.8 V → ≥2.2 V headroom for the AFE's current sinks; TX_SUP(5 V) ≥ RX_SUP(3.3 V) satisfies the AFE4900 supply ordering. **Caveat (firmware, not board):** SFH7072 pulse ratings (300 mA green/red, 1 A IR) exceed the TPS61240's ~450 mA output ceiling feeding all four emitters — TX currents must be kept within rail capability.

### B9 — PASS: MOSFET gates all driven, none floating
- Q1 gate `IR_GATE` ← TCA6408A P-port (push-pull per datasheet) + R48 100k→GND. Drives IR LED low-side; VDS on drain ≤3.3 V ≪ 12 V; ~17 mA ≪ 2.1 A.
- Q2 gate `CHG_DIS` ← TCA6408A + R59 pull-down (charge enabled by default at boot — fail-safe).
- Q4 gate `NIR730_GATE` ← AD5940 **GPIO2** (ball E1 per AD5940 pin table; push-pull, IOVDD≈3.3 V via R12) + R110 pull-down.
- Q6 (AO3401A, 1=G/2=S/3=D verified): G=VBUS, S=VBAT_SYS, D=VBAT — load-share. Gate pulled down when VBUS absent via **R123 100k→GND** plus the R50+R51 and R63+R64 divider paths — not floating. With VBUS present: VGS ≈ +0.4 V (off, body diode blocks SYS→BAT since 4.6 V_SYS > 4.35 V_BAT max); absent: VGS ≈ −VBAT (on). Ratings: |VDS|≤5.5 V vs −30 V; |VGS|≤5.5 V vs ±12 V; I_D −4 A vs ~≤1 A system draw. Topology is the source-on-load-side variant — verified functional both directions.
- Q3 (BC847BS-7-F, SOT-363 pinout 1=E1,2=B1,3=C2,4=E2,5=B2,6=C1 — same for Nexperia and Diodes): classic cross-wired ESP32 auto-reset — E1=CP_RTS, B1←CP_DTR via R70(10k), C1→ESP_EN; E2=CP_DTR, B2←CP_RTS via R71, C2→ESP_IO0. EN/IO0 pulled up by R5/R6; C64 on EN. Safe at CP2102 idle and unpowered states. **Verified correct.**

### B10 — Corpus defect: `reports/datasheets/1N5819WS.pdf/.txt` is the WRONG document
The file is a **Diodes DDTC-series NPN pre-biased-transistor** datasheet (SOT323, "50V NPN PRE-BIASED TRANSISTORS"), not a 1N5819WS Schottky. Specs verified instead against generic 1N5819WS datasheets on the web (Slkor/Pingjing/Goodark, all consistent: VRRM 40 V, IF(AV) 1.0 A, Vf ≤0.6 V @1 A, SOD-323, cathode marked). See manifest — flagged `WRONG-DOCUMENT`. D33 itself is wired correctly (K=VBAT_SYS pad1, A=VBUS pad2; conducts VBUS→system, blocks backfeed; ~0.45–0.6 V drop → VBAT_SYS≈4.4–4.65 V on USB; 1 A rating adequate for ~0.1–0.8 A system draw, marginal only at stacked bursts).

### B11 — PASS: Inductors
- **L1** DFE201612E-R47M 0.47 µH on TPS63802 (SW_L1↔SW_L2, U3.9/7): required effective L 0.37–0.57 µH → 0.47±20 % = 0.376–0.564 ✓; **TI datasheet Table 10-2 explicitly recommends DFE201612E 0.47 µH** (Isat 5.5 A, DCR 26 mΩ — identical numbers to the Murata spec). Murata spec: Isat 5.5 A / Itemp 4.5 A vs switch peak limit 3.8–5.75 A — margin thin only at the absolute worst-case limit corner (the TI-recommended part itself). DCR 26 mΩ → <10 mV IR drop at 0.35 A.
- **L2** DFE201612E-1R0M 1.0 µH on TPS61240 (VBAT_SYS→TX_SW, U15.B1): required 0.4–1.5 µH ✓; Isat 4.0 A / Itemp 2.9 A / DCR 48 mΩ vs 500–600 mA valley limit — oversized, zero saturation concern.

### B12 — PASS/INFO: land patterns and phantom pads
- TPD1E10B06_DPY: pads 0.30×0.50 @ ±0.35 = **0.7 mm pitch — exactly the TI DPY0002A (X1SON-2) recommended land** (datasheet tail shows 2×(0.3)/2×(0.5)/(0.7)).
- PicoStar Q1/Q2/Q4: pads 1,2 = 0.35×0.25 @ ∓0.175; pad3 = 0.25×0.60 @ +0.225; the two **unnumbered pads are F.Paste-only stencil apertures (0.25×0.2)** matching TI's recommended split stencil — not copper, not a defect. CPL rot = fp rot for all discretes (no orientation offsets).
- D_SOD-323 (D33): 0.6×0.45 @ ±1.05 — KiCad standard, fab glyph's cathode bar lands on pad1 = VBAT_SYS ✓.
- SOT-23 (Q6), SOT-363 (Q3): standard KiCad lands (0.95 / 0.65 mm pitch).
- LED_0603: D25 uses 0.875×0.95 @ ±0.7875; D12 uses a modified 0.8/0.67×0.6 @ ∓0.56 (asymmetric, machine-solder style) — both serviceable for 1608.
- SFH7072: 12× 0.9×1.0 pads @1.2 mm pitch, rows at ±1.25 mm — consistent with the 7.5×3.9 mm package, but the datasheet "Recommended Solder Pad" figure's numeric dims were not text-extractable → **exact pad-pattern mm: UNVERIFIABLE** (recommend a one-time overlay check in the PCB tool).
- NF2W757G: asymmetric pads (anode 0.89×1.95, cathode 1.28×2.4, ~0.66 mm gap) — matches Nichia's asymmetric-electrode convention in spirit; exact recommended-pattern numbers were ASCII-ambiguous in the extracted text → **UNVERIFIABLE detail, flagging for overlay check.**

### B13 — INFO: CPL/rotation and netlist consistency
All audited parts: `fab/CPL_JLC.csv` rotation == board footprint rotation (D4/D5/D11/D12/U16 180°, D10 90°, Q1/Q6/L1 270°(=−90°), L2 90°, others 0°). No rotation corrections recorded — meaning part orientation delegates entirely to footprint pin-1 geometry; combined with B3 this is the only LED-orientation control, so it must be right.

## C. Verification limits (documented effort)
- `01_netlist_pinmap.csv` net column is empty for **all** 1027 pads (parser bug — pads store `(net "NAME")`, script read index 2). Audit used a corrected re-extraction; recommend regenerating `01` for the other groups.
- AFE4900 and XL0603NRC730 have no local datasheet — verified via TI datasheet text (Scribd mirror of SBAS revision) and the Sealand product page respectively; both marked `unfetched` in the manifest.
- SFH7072 / NF2W757G land-pattern figures are image-only in the PDFs; pad geometry checked plausibility-only.
- Q3 pin equivalence Nexperia↔Diodes SOT-363 assumed standard; both vendors' SOT-363 BC847BS share the industry E1,B1,C2/E2,B2,C1 pinning (Diodes pinout figure present but not text-extractable in corpus pdf).

## D. Bottom line for fab gate
No blockers found in the discrete group. Two items should gate **assembly confidence** rather than fab: (1) LED anode-on-pin-1 convention needs an explicit fab note or the consigned 0603s may mount backwards (B3); (2) resolve the D29/D30 dead stubs and decide whether J10/J12 sensor lines want ESD protection (B1/B2). Everything else is margin/robustness commentary.
