# POWER group — fab-gate findings
Board: `hardware/pcb/vitalq_v2.kicad_pcb`, fab bundle `VitalQ_v2_fixv19_fabrication.zip`, rev **fix_v19 @ b9002188**.
Truth sources: pad nets re-extracted from the `.kicad_pcb` (see note at bottom — `01_netlist_pinmap.csv` net column is empty for all 1,027 pads, extractor artifact), `BOM_JLC.csv` / `CPL.csv` from the zip, datasheet texts in `reports/datasheets/`.

Package check first: **BQ25170DSGR is WSON-8 (DSG0008A, 2×2 mm)** — the audit brief's "WSON-10" is wrong; the board's `Texas_DSG0008A_WSON-8-1EP` footprint and the datasheet orderable addendum (WSON DSG | 8) agree. U3 is the WSON-10 (DLA0010A, VSON-HR 3×2 mm). All four pin↔name maps verified against the exact ordered package's pin table.

---

## BLOCKER
*(none)*

## HIGH

### F-PWR-01 — TS pin sees two parallel NTC paths; charger can sit at the hot-suspend threshold at 25 °C
- **Where:** U2 (BQ25170DSGR) pin 3 → net `TS` → R61 (NCP15XH103F03RC, 10 kΩ β=3380, →GND) **and** J2-3 (battery-pack NTC terminal) **and** Q2 drain (CHG_DIS switch).
- **Datasheet:** ITS_BIAS = 38 µA nom (36.5–39.5, EC p.7); VHOT = 188 mV typ (176–200) ≈ 45 °C; VCOLD = 1.04 V ≈ 0 °C; "Use a 10-kΩ NTC thermistor in the battery pack (recommend: 103AT-2)" (§8.2.1.2.1 p.21). Temperature window can be modified "by adding resistors in parallel and in series" — i.e., the IC tolerates/expects a deliberate network, but here the parallel is *incidental*.
- **Evidence:** V_TS = ITS_BIAS × R_TS. With only R61: 38 µA × 10 kΩ = 380 mV — mid-window, correct. With a standard 10 kΩ pack NTC on J2-3: R_TS = 5 kΩ → **V_TS ≈ 190 mV ≈ VHOT (188 mV typ)** at room temperature → charge suspends/intermittently faults (STAT 1 Hz blink, Table 7-3 p.17) as if the battery were hot. Whether a given unit charges at all depends on ITS_BIAS/NTC spread.
- **Consequence:** with a 3-wire NTC pack the product may never finish a charge cycle; with a 2-wire pack the pack temperature is unmonitored (safety trade).
- **Smallest fix:** BOM — mark R61 DNP when building with NTC-equipped packs (or specify 2-wire packs and accept losing pack temp sense). Re-spin: route TS exclusively to J2-3 and keep R61 on a stuffing option keyed to pack choice. β mismatch of R61 (3380 vs recommended 3435) is a secondary ~1–2 °C window shift — acceptable once the parallel issue is resolved.

### F-PWR-02 — TPS61240 guaranteed output current (200 mA) is below the LED load (~300 mA)
- **Where:** U15 (TPS61240YFFR) B2 VOUT → `TX_5V_RAW` → R96 (0 Ω) → `TX_5V` → U16 SFH 7072 LED chain.
- **Datasheet:** EC table §7.5 p.5 — "Output current, VIN 2.3 V to 5.5 V: **200 mA**"; device-comparison table p.3 — TPS61240 "Supports 5 V, up to **250 mA** loading down to 3.2 V input"; TPS61241 "= up to **300 mA**" (valley current limit 600 mA vs 500 mA). Marketing line "up to 450 mA" (§1 features) is not a guaranteed operating point.
- **Evidence:** brief states the SFH 7072 chain draws up to ~300 mA on TX_5V. That exceeds the guaranteed spec; at VBAT_SYS < 3.2 V (depleting cell) capability degrades further → TX_5V sag / limit cycling during LED bursts.
- **Smallest fix:** BOM — swap to **TPS61241YFFR** (same DSBGA-6 YFF pinout, fixed 5 V, 600 mA limit, 300 mA loading spec) — drop-in, no re-spin. No firmware change.

## MEDIUM

### F-PWR-03 — U3 VOUT has only 0.1 µF within 5 mm; the recommended 22 µF bulk is 15.9 mm away
- **Where:** U3 (TPS63802DLAR) pin 6 `+3V3` → nearest same-net cap C38 (100 nF) @ 4.96 mm; C45 (22 µF 0603) @ 15.9 mm.
- **Datasheet:** §10.2.2.3 p.19 — "recommended nominal output capacitor value is a single 22 µF … placed as close as possible to the VOUT and PGND pins"; §8.3 p.5 — effective CO ≥ 7 µF (VO>2.3 V).
- **Assessment:** rail-level effective capacitance is probably still ≥7 µF (C45 ~22 µF derated + sprinkles), so the converter will likely *function*; the violation is placement — loop inductance between VOUT and its bulk degrades load-transient response and EMI on a 1.4–2 MHz converter.
- **Smallest fix:** rework/bottom-side 22 µF (or ≥10 µF) 0603 near pin 6–8; better: re-spin placement.

### F-PWR-04 — U4 IN lacks a local ≥1 µF capacitor
- **Where:** U4 (TPS7A2018PDBVR) pin 1 IN on `+3V3` → nearest same-net cap C60 (100 nF) @ 5.28 mm; C91 @ 5.65 mm.
- **Datasheet:** pin table p.4 — "use the nominal value or larger capacitor from IN to ground… place the input capacitor as close to the IN and GND pins as possible"; §5.3 p.5 — CIN nominal 1 µF.
- **Assessment:** input is an already-regulated 3.3 V rail with bulk elsewhere → low functional risk, but the datasheet's value *and* placement guidance are both unmet locally.
- **Smallest fix:** rework — add 1 µF 0402 adjacent to pins 1/2; fold into next BOM/re-spin.

### F-PWR-05 — no thermal vias under U2 (DSG0008A) or U3 (DLA0010A) exposed pads
- **Where:** U2 pad 9 EP (0.9×1.6 mm, F.Cu only) — zero vias inside pad area; nearest GND via (29.16, 4.49) is 2.0 mm outside pad edge. U3 pad 11 EP (0.84×2.4 mm) — nearest GND via (38.95, 33.42) 1.1 mm outside pad edge.
- **Datasheet:** BQ25170 Table 5-1 p.3 — "Solder thermal pad to the board **with vias connecting to solid GND plane**"; DSG0008A land-pattern example (pkg dwg 4218900/E) shows (0.2) TYP via. TPS63802 DLA0010A land pattern (pkg dwg 4223750/D) — EP segments to ground plane.
- **Evidence:** BQ25170 worst-case dissipation ≈ (5 V − 2.8 V) × 0.2 A ≈ 0.44 W in a 2×2 mm WSON; without vias, heat exits only through the F.Cu pour → elevated junction temperature, earlier TREG (125 °C current foldback, p.7) during low-battery charging.
- **Smallest fix:** re-spin — add the datasheet-recommended ~0.2 mm thermal vias under both EPs (filled/tented). Rework cannot add vias; accept TREG derating as interim.

### F-PWR-06 — U3↔L1 switch-node loop ~14–16 mm
- **Where:** U3 pin 9 `SW_L1` → L1 pad 1 (44.956, 46.168): 16.0 mm; pin 7 `SW_L2` → L1 pad 2: 14.2 mm. (U15↔L2 ~8 mm — noted, less severe.)
- **Datasheet:** §12.1 p.28 — "the inductor should be placed as close as possible to the IC"; Table 10-2 p.19 lists the DFE201612E series itself (0.47 µH, Isat 5.5 A) — part choice correct, placement is not.
- **Consequence:** long hot loop at ~2 MHz switching → radiated EMI and ground bounce on a sensor-laden board.
- **Smallest fix:** re-spin placement; no electrical defect.

## LOW / COSMETIC

- **C-PWR-07 — U2 OUT bypass at 4.59 mm.** C82 (1 µF) satisfies "at least 1-µF…placed close" (Table 5-1 p.3) loosely; plus C85 47 µF bulk at 24 mm. Cosmetic.
- **C-PWR-08 — R24 = 100 kΩ sits exactly at the TPS63802 R2 limit.** §10.2.2.5 p.19: "the low-side resistor R2…must not exceed 100 kΩ". Nominal 100 kΩ complies; the +1 % tail (101 kΩ) technically exceeds — the limit exists vs IFB (5–100 nA) error; impact <0.1 % VOUT. Note only; consider 97.6 kΩ on re-spin.
- **C-PWR-09 — land-pattern deltas (all benign).** U2 signal pads 0.60×0.25 mm vs TI example 0.50×0.25 (+0.1 mm toe); U3 pads 0.75×0.25 vs example 0.6×0.25; U4 pads 1.325×0.6 vs DBV example 1.1×0.6 (KiCad/IPC-7351 style, aids fillet). U15 lands Ø0.23 mm @ 0.4 mm pitch = exact YFF0006 match (pkg dwg 4223785). U2 EP 0.9×1.6 = package EP exactly; split F.Paste apertures mirror the "87 % coverage" stencil example (pkg dwg 4218900/E). No action.
- **C-PWR-10 — R61 β=3380 vs recommended 103AT-2 β=3435** (§8.2.1.2.1 p.21): ~1–2 °C window shift. Superseded by F-PWR-01.

## Verified good (explicitly checked, no defect)

- **Pin maps:** U2 1=IN,2=ISET,3=TS,4=GND,5=STAT,6=PG,7=VSET,8=OUT,9=EP (Table 5-1 p.3) — all correct. U3 1=EN,2=MODE,3=AGND,4=FB,5=PG,6=VOUT,7=L2,8=GND,9=L1,10=VIN,11=EP (Table 7-1 p.4) — all correct. U4 1=IN,2=GND,3=EN,4=N/C,5=OUT (pin table p.4) — all correct. U15 A1=VIN,A2=GND,B1=L,B2=VOUT,C1=EN,C2=FB (pin table p.3) — all correct.
- **VSET:** R75 = 27.0 kΩ → 4.20 V Li-Ion (Table 7-1 p.14, exact entry). **ISET:** R3 = 1.5 kΩ → ICHG = KISET/RISET = 300/1500 = **200 mA** (180–220 mA), inside 375 Ω–30 kΩ legal range; >50 mA so the low-current RC addition isn't required (§7.3.1.1 p.14).
- **Load sharing Q6/D33/R123:** Q6 AO3401A G=VBUS, S=VBAT_SYS, D=VBAT; D33 anode=VBUS, cathode=VBAT_SYS; R123 100 kΩ VBUS→GND. USB present → Q6 off (VGS≈+0.35 V), D33 supplies VBAT_SYS ≈ 4.6–4.7 V (< 5.5 V U15 limit, < 6 V U3 limit). USB absent → R123 pulls gate low → Q6 on → VBAT_SYS=VBAT; D33 blocks battery→VBUS backfeed. **R93 (direct VBAT↔VBAT_SYS 0 Ω) is DNP** per BOM.csv/BOM_JLC.csv — power-path is genuinely active. R107 (VBAT→SPARE_IN) also DNP. Q2 (CSD13380F3T, G=CHG_DIS,S=GND,D=TS) implements the datasheet-sanctioned "TS pin can be pulled down to disable charging" (§8.2.1.2.1 p.21).
- **STAT/PG pull-ups:** R49/R60 = 10 kΩ to +3V3 — exact datasheet values.
- **Inductors:** L1 DFE201612E-R47M (0.47 µH) — the exact series in TPS63802 Table 10-2 (Isat 5.5 A); L2 DFE201612E-1R0M (1.0 µH) inside TPS61240's 0.4–1.5 µH window (§7.3 p.4). Saturation margins large vs 0.5–0.6 A TPS61240 valley limit.
- **No "must not float" violations:** U3 EN/MODE terminated, PG legally open; U4 EN tied, N/C floated correctly; U15 EN driven + 100 kΩ pulldown; all U2 pins terminated.
- **Voltage domains:** CHG_STAT/CHG_PG pulled to +3V3 (matches TCA6408A/ESP32 3.3 V domain); EN pins see ≤3.3 V (≤6 V rated); VBAT_SYS ≤ ~4.7 V within U3/U15 VIN ratings.
- **Decoupling distances (measured pad-to-pad):** U2.IN→C63 1 µF @ **2.87 mm**; U2.OUT→C82 1 µF @ 4.59 mm; U3.VIN→C46 2.2 µF @ 3.29 mm / C44 10 µF @ 3.99 mm; U3.VOUT→C38 0.1 µF @ 4.96 mm (22 µF @ 15.9 mm); U4.IN→C60 0.1 µF @ 5.28 mm; U4.OUT→C6 1 µF @ **2.84 mm**; U15.VIN→C44 10 µF @ **2.26 mm**; U15.VOUT→C47 4.7 µF @ **2.09 mm**.

## Audit-trail note

`01_netlist_pinmap.csv` has an **empty net column on all 1,027 pads** — `extract_pads.py` reads `net[2]` (numeric net-id format) but this KiCad file stores `(net "NAME")`. All net assignments here were re-extracted from the `.kicad_pcb` s-expressions directly (positions unchanged — coordinates match the CSV). Flag for whoever owns `extract_pads.py`; a fixed CSV was generated to `/tmp/pinmap_full.csv` for this audit.

Datasheet-corpus integrity: `reports/datasheets/1N5819WS.txt` actually contains a **Diodes Inc. DDTC-series NPN pre-biased-transistor datasheet (SOT-323)**, not the Hottech 1N5819WS Schottky — the PDF/txt pair is mislabeled. D33/Q6 pin polarities were therefore verified from the project symbol descriptions in `vitalq.kicad_sym` ("Schottky 40V 1A SOD-323. Pin1=K pin2=A"; "P-ch MOSFET … Pins: 1=G 2=S 3=D (rev-polarity/load-share)") instead of the corpus text.
