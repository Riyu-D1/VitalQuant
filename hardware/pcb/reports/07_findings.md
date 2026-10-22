# 07 — Consolidated findings, fix_v19 @ b9002188 (zip-authoritative)

Sources: findings_power/mcu/afe/sensa/sensb/disc/elec/footprints.md, fab_mech.md, matrix FAIL rows.
Severity: BLOCKER = dead/damaged board at power-up; HIGH = function/spec fails in plausible use;
MEDIUM = degraded/marginal; COSMETIC = docs/silk.

## BLOCKER

**B1 — U15 TPS61240YFFR has zero stencil apertures (no solder paste).**
All 6 pads `(layers "F.Cu" "F.Mask")` — no F.Paste (board file, U15 footprint pads A1..C2).
F_Paste gerber shows 0 flashes at (40.80,-38.82) ±2mm; B_Paste also 0 (independently decoded).
Consequence: DSBGA-6 part cannot be soldered by the assembler → TX_5V never rises →
entire optical chain (SFH7072, AFE4900 TX, MAX86141 VLED) dead; board otherwise boots.
Smallest fix: add F.Paste to the footprint pads and regenerate the fab zip. Classification:
**fab-file regeneration** (no copper re-spin). The current zip must NOT be ordered as-is.

## HIGH

**H1 — F-PWR-01: charger TS pin sees two parallel NTC paths.**
R61 "NTC 10k B3380" is POPULATED (BOM_JLC, no DNP; pinmap R61.1=TS, R61.2=GND) and J2.3
pack-NTC terminal shares TS. With a standard 10k pack NTC: R_TS=5k → 38µA×5k=190mV ≈
VHOT 188mV typ (BQ25170 EC p.7, §8.2.1.2.1). Charger can suspend at 25°C with a protected pack.
Smallest fix: **BOM/CPL note — do not place R61** when pack NTC is used (no copper change).

**H2 — F-PWR-02: TPS61240 underspec'd for the LED load.**
Guaranteed IOUT=200mA (EC p.5; "up to 250mA @3.2Vin" p.3) vs ~300mA optical LED worst case.
Smallest fix: **BOM swap to TPS61241YFFR** (same DSBGA-6, 300mA spec — drop-in, verify pinout).

**H3 — U3 (TPS63802) VOUT bulk cap 15.95mm away; only 100nF local.**
Datasheet requires CO,eff ≥7µF "as close as possible"; C45 22µF at 15.95mm (decap_distances.csv).
Smallest fix: layout change → **re-spin scope**, or accept documented instability margin risk.

**H4 — U4 (TPS7A2018) IN has no ≥1µF within 9.42mm** (100nF at 5.28mm only). Same class as H3.

**H5 — J13 "MX ECG" connector is dead hardware** (found independently by two audit groups).
Pads → D29/D30 TVS + R118/R119 51k → no IC pin; J13 itself DNP (MAX86178-swap leftover).
A fitted ECG-labelled connector misleads integrators; nets MX_ECG_INP/INM are stubs.
Smallest fix: **re-spin** to wire to an AFE channel, or remove/relabel next rev.

**H6 — U14 source footprint has all 14 pads transposed (0.254×0.7874 in 0.5mm pitch columns)
→ −0.287mm overlaps: +3V3×GND, SCL×SDA shorts IF gerbers are regenerated.** Shipped zip
gerbers are CORRECT (aperture decode: pads render radially). The ordered board is safe; the
source is broken → must fix `vitalq:QFN_LSM6DSV80XTR_STM` (and lib copies) before ANY
re-export — which B1 already requires. Classification: **source defect** (no impact on this order).

**H7 — POFV: 48 vias in SMD lands (49 pad contacts), 45 of them 0.150mm drill — below
JLCPCB's published 0.2mm POFV minimum; 5 in-pad vias carry no explicit cap/fill flag**
(R14.1, R60.2, R90.1 beyond the 2 named in FAB_NOTES). Aspect ratio 10.67:1 is at the fab's
surcharge boundary; annular ring 0.050mm exactly at minimum. Not a design defect — an
order-execution risk requiring **written fab confirmation before payment**.

## MEDIUM (grouped)

- **Decoupling/margins** (elec+power): PCA9306 VREF1→cap 14.61mm & no 100pF on VREF2; U22 VDD/VLED 8.4–9.9mm; W25Q512 VCC 9.47mm; +3V3_ANA is a plain 0Ω link (unfiltered AFE feed); TX_5V→AFE4900 TX_SUP margin 0.15V; +1V8 margin 0.173V; U3↔L1 switch loop 14–16mm; no thermal vias under U2/U3 EPs.
- **Datasheet deviations**: U22.D4 PD2_IN floating (DS: "tie to PD_GND"); U8 ADS1292R pads +36%/EP −26%; U19 TCA6408 pads +33–50% + centre pads omitted; U26/U9/U11/U20/U4/U21/Q6 pads +20–50% over recommended land; WLP mask margins: U6/U7 mixed Ø0.23/0.25 pads, mask webs at the 0.1mm limit, U22 none (NSMD expected at 0.4mm); MLX90632 EP full-area paste vs Fig26 windowpane+vias.
- **Sensor placement**: SHT45 — GND via (0.4/0.2) + trace under package violates Sensirion §5.3 keepout (heater degradation).
- **Optical/LED chain**: D10 NF2W757G Vf up to 3.3V through AS7341 LDR sink → ~0.3V headroom → white-LED self-test margin (sensa M2/disc B5); D25 KT-0603G Vf-to-3.4V on +3V3 via 150Ω → dim/dark risk; LED_AK footprints put pin1=anode vs conventional pin1=cathode — assembly-rotation risk on D12/D25 (D10 safe: explicit K silk).
- **AFE domain**: AFE4900 RX_SUP≤TX_SUP ordering vs gated TX_5V — firmware must sequence via RESETZ (FW constraint); U8 CLK tied GND — legal only while CONFIG2.CLK_EN=0 (FW constraint); J12 taps IC-side of J11's 51k protection; RTC_INT reaches only TP28 (no MCU wake — by design, firmware aware).
- **USB/ESD**: USBLC6 15mm from J1 (topology right, placement mid-route); ESD gaps — PD lines to U22, FSR_ADC (10k only), J9/J12 I2C, CC1/CC2 unclamped.
- **Firmware/hardware straps**: IM69D130 SELECT=GND=RIGHT channel but firmware decodes I2S_PDM_SLOT_LEFT (fw fix or re-strap); IM69D130 PDM clock 2.048MHz in datasheet band-gap (documented).
- **Fab process**: stackup 1.4776/1.5386mm vs 1.6mm nominal (0.1224mm=7.65% gap; nonstandard prepregs — fab must confirm ply availability); ERC/DRC could not be re-run (no kicad-cli — see 09).
- **Traceability**: D10 placed footprint Value/LCSC still the superseded Cree part (BOM/CPL correct; assembly-safe, consigned); FAB_VERIFICATION.json names fix_v18 zip (counts correct for v19).

## COSMETIC

- CP2102N fitted-but-isolated draws ~9.5mA on VBUS while never enumerating (consider DNP if spare-only); ESP_RX floats when bridge unpowered (noise-only).
- Mixed 0.23/0.25mm pads inside U6/U7 WLP footprints (in-spec, inconsistent).
- Tented vias under U14 LGA body; stale audit CSVs (audit_v14.net etc.) predate v18.
- ROUND9 "no fiducial within 5mm of edge" — wrong: 5/6 fiducials inside 5mm (FID4/6 at 1.31mm).

## Verified clean (high-value confirmations)

- All 3 WLP ball maps (U6 5×6, U7 8×7 all-56-mnemonics, U22 5×4) orientation-correct.
- ESP32: all 8 reserved in-package pins unconnected; straps correct; 8-layer antenna keepout.
- I2C address map collision-free on both domains (matches firmware board_pins.h); PCA9306 topology canonical; every 3.3V↔1.8V crossing goes through U9/U26 (or 6V-tolerant MAX86141 pins).
- SFH7072 pinout exact; all MOSFET gates driven w/ pull-downs; Q3 auto-reset cross-wiring right; load-share topology right; CP2102N VBUS divider exactly DS 22.1k/47.5k; USB-C CC=5.1k Rd each; 18×TPD all wired signal→GND (bidirectional, orientation moot).
- Zero over-abs-max rail violations (80 load-pin rows); zero cross-net via-in-pad shorts (gerber-verified); all 33 zip SHA256s verify; BOM/CPL/positions parity clean; DNP set = {J13,R93,R104,R105,R107,R108}.
- Via arithmetic: 871 = 863(v16) + 8(v18 power-path, east-tab cluster) — exact.
