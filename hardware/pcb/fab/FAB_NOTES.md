# VitalQ v2 fabrication and assembly notes

**ORDER `VitalQ_v2_fixv19d_fabrication.zip` — it supersedes `fixv19c`, `fixv19b`, and `fixv19a`.**

## fixv19d (2025-07-03) — last BOM revision: four resistor substitutions; copper untouched

All wmsc-API stock-verified; same value/package, same-or-better tolerance. Sourcing only — no design change.

| Refs | Old (stock 0) | New | Stock | Note |
|---|---|---|---|---|
| R43, R45 | C283954 CRCW04025M11FKED | **C166448** RALEC RTT025114FTH, 5.11M ±1% 62.5mW 0402 | 7,400 | ADS1292R input-bias pair (RLDOUT→AFE_INP/INM) — kept on one code so both legs match. Backup: C368375 (4,600) |
| R63 | C853138 RT0402BRE0722K1L | **C163478** YAGEO RC0402FR-0722K1L, 22.1k ±1% 62.5mW 0402 | 1,724,900 | **Deliberate ±0.1%→±1% tolerance downgrade — accepted, not oversight**: R63+R64 form the CP2102N VBUS divider; ratio is already bounded by R64's ±1%, so 0.1% on R63 buys nothing. At 5 V VBUS divider = 3.41 V; ±1% on R63 moves it ~34 mV — inside the CP2102N VBUS-sense tolerance. Backup: C43473 (17,100) |
| R64 | C25896 0402WGF4752TCE | **C100449** LIZ CR0402FF4752G, 47.5k ±1% 62.5mW 0402 | 17,700 | Other divider leg; tolerance unchanged. Backup: C137974 (6,800) |
| R66 | C6097400 MR04X3013FTL | **C102962** RALEC RTT023013FTH, 301k ±1% 62.5mW 0402 | 10,000 | ~6 µA bleeder +1V8→GND; tolerance not load-bearing, kept ±1% anyway. Backup: C172047 (9,800) |

- **Assembly note — order bring-up spares**: the thin-stock AFE parts are the ones that die during bring-up. Source **two extra of each** of U7 AD5940 (15 in stock), U8 ADS1292R (16), U22 MAX86141 (84), U16 SFH7072 (39) and U12 MLX90632 (3) when placing the LCSC order — one dead AFE should not cost three weeks waiting for a reel.
- Extended-code wmsc-0 parts (ESP32-S3, LSM6DSV80X, W25Q512, RV-3028, SHT45, IM69D130) intentionally untouched — JLC-sourced, confirmed at BOM upload.
- Gerber/drill members byte-identical to fixv19a (sha256 member-by-member); connectivity stands — copper unchanged.

## fixv19c (2025-07-03) — final BOM pass, stock-verified via wmsc API; copper untouched

Stock re-verified against the **wmsc detail API** (`wmsc.lcsc.com/ftps/wm/product/detail`, field `stockNumber`) — the authoritative live figure; the HTML product page "Out of Stock / Notify Me" block is **not** live state and had falsely flagged C1525 in fixv19b.

- **REVERTED: 100 nF line back to C1525** (Samsung CL05B104KO5NNNC). wmsc API: **7,144,700 pcs** — it was never out of stock; fixv19b's C71629 swap fixed nothing. muRata C71629 remains a valid in-spec alternative but is no longer used.
- **10 kΩ 0402 ×28 (R5..R117): C25744 (UNI-ROYAL 0402WGF1002TCE) stock 0 → substituted C407753 = UNI-ROYAL 0402WGD1002TCE** — 10 kΩ **±0.5 %** (better than required ±1 %), 62.5 mW, 0402, wmsc stock **1,382,700**.
- **22 µF 0603 ×2 (C7 ESP32 VDD33 bulk, C45 U3 VOUT bulk): C86295 (Samsung CL10A226MP8NUNE 10 V) stock 0 → substituted C2762594 = Samsung CL10A226MO7JZNC, 22 µF, 16 V, X5R, 0603**, wmsc stock **86,665**. Same Samsung CL10 platform as the original; the 16 V rating means 3.3 V sits at 21 % of rated vs 33 %. Per Samsung's published DC-bias characteristic for CL10A226 (0603/22 µF/16 V), typical retention at 3.3 VDC is ≈65–70 % → **effective ≈14–15 µF** (vs ~12–14 µF for the original 10 V part) — a marginal *improvement* on the rail that was already the board's weakest decoupling. Curve figure is manufacturer-typical, not a JLC-measured value. Runners-up rejected: YAGEO C109447 (10 V, weaker derating), muRata C126630/C162690 (10 V, 1.3 k/13.6 k stock), Chinocera C22367828 (no published curves).
- **J12 FH12-14S-0.5SH(55): C5378715 stock 0 → consigned** (deliberately NOT substituted — it is a mating connector; footprint and mate are the product). Blank LCSC cell + ` (consigned)` MPN suffix per D10/D12 pattern; added to ASSEMBLY_CHECKS consigned list. **Source Mouser or DigiKey (Hirose FH12-14S-0.5SH(55)); J12 will not be fitted by the assembler** — hand-fit at bench.
- Confirmed adequate, left unchanged: C5349491 MLX90632 = 3 pcs (need 1), C544431 TPS61241YFFR = 67 pcs (need 1).
- Gerber and drill members **byte-identical to fixv19a** (sha256 member-by-member); connectivity sweep (970 pads, OPEN=0, SHORT=0) stands — copper is unchanged.
- Remaining items needing a human at order time: thin-stock qty-1 ICs (MLX90632 3, TCA6408 13, AD5940 15, ADS1292R 16, SFH7072 39, MAX86141 84); extended-code lcsc.com zeros to verify inside the JLC BOM tool (ESP32-S3 module, LSM6DSV80X, W25Q512, RV-3028, SHT45, IM69D130); J12 hand-fit; still-unresolved OOS jellybeans C283954 5.11 M / C853138 22.1 k / C25896 47.5 k / C6097400 301 k (CP2102N divider + presale row — flagged, not substituted).

## fixv19b (2025-07-03) — BOM-only stock fix; copper untouched

- **C1525 (Samsung CL05B104KO5NNNC, 100 nF 0402 X7R 16 V) reported Out of Stock — LATER DISPROVEN, reverted in fixv19c** (the HTML page "Notify Me" block was not live state; wmsc API shows 7.1 M in stock) → substituted **C71629 = muRata GRM155R71C104KA88D** (100 nF, 16 V, X7R, 0402, identical electrical/footprint spec, ~954k in stock at check time). One line changed in `BOM_JLC.csv`; all 32 designators unchanged.
- **Fallback if C71629 is out at order time: C105883 = YAGEO CC0402KRX7R8BB104** (100 nF, 25 V, X7R, 0402 — higher V rating, harmless).
- Gerber and drill members are **byte-identical to fixv19a** (sha256-verified member-by-member); the earlier connectivity sweep (970 pads, OPEN=0, SHORT=0) therefore still stands — no re-run needed.
- Remaining stock caveat carried from fixv19a: **TPS61241YFFR (C544431) stock was ~67 pcs** — confirm at order time; full-board stock sweep in `../reports/11_stock_sweep.*`.

## Release basis

- **fix_v19 deltas vs fix_v18: R61 fitted — no copper, placement or property changes beyond R61.** The BQ25170 `TS` pin had a DNP resistor site even though the planned LiPo has no pack NTC; R61 is now Murata **NCP15XH103F03RC** (LCSC **C77131**), a 10 kΩ ±1 % B25/50 = 3380 K NTC in the existing 0402 footprint. With no pack NTC on J2, R61 senses board temperature near the cell and gives the charger its required cold/hot inhibit window (~0 °C / ~45 °C, I_TS ≈ 38 µA). Copper freeze vs fix_v18 proven by `sims/fullsim_v19/copper_freeze/freeze_check.py`: 8016 segments, 871 vias, 107 zones, 1027 pads, 302 placements — all identical; the only file delta is the R61 value/LCSC property text. Fab zip: `VitalQ_v2_fixv19_fabrication.zip`. **Note: if a 3-wire pack with its own NTC is ever fitted, R61 must revert to DNP** (parallel NTCs force a permanent HOT fault).
- Source board: `hardware/pcb/vitalq_v2.kicad_pcb` (repo-root board file at **fix_v19** state — the cumulative result of the fix_v4→fix_v19 delta chain documented in this section; the stale `fix_v9/...` working-copy path previously quoted here was wrong), KiCad 10.0.4. Audited board SHA-256: **`7844150655ab983eb00a81855474900df8f12ee16e7af0bdd834f6fcc11a69d0`** (see `../reports/`). **Provenance caveat from the audit:** U14's source footprint (`vitalq:QFN_LSM6DSV80XTR_STM` — all 14 pads transposed to 0.254×0.7874 in the 0.5 mm pitch columns, producing −0.287 mm pad overlaps incl. +3V3×GND / SCL×SDA) was found **divergent from the shipped gerbers** — gerber-level aperture decode shows the zip's U14 pads render correctly, i.e. this zip predates the broken U14 footprint edit in the source board. The source defect has since been fixed; the corrected footprint must be carried into the board before any gerber re-export so the divergence can never ship.
- fix_v18 deltas vs fix_v16: **charger power path added (load-sharing)** — new Q6 (AO3401A P-ch MOSFET, SOT-23, LCSC C15127) replaces the R93 0-ohm link between VBAT and VBAT_SYS: drain(pin3)=VBAT, source(pin2)=VBAT_SYS, gate(pin1)=VBUS; D33 (1N5819WS, SOD-323, LCSC C191023) anode=VBUS cathode=VBAT_SYS feeds the system rail from USB when present; R123 (100k 0402, LCSC C25741) pulls the gate net to GND when VBUS is absent. **R93 is now DNP** — VBAT and VBAT_SYS meet only through Q6. Rationale: an ~80 mA system load on the charger OUT node prevented charge termination/timeout behaviour in fix_v16 (see COMPSIM_REPORT.md). **Board outline enlarged: east edge extended +5.6 mm** — new tab x 48->53.6 mm over y 20->29.9 mm only (overall board now 55.6 x 75.0 mm; all mounting holes, connectors and every pre-existing track/via/zone/footprint are unchanged — verified object-level diff: only R93 (dnp flag) and the east-edge segment differ, everything else is purely additive). New copper: 8 vias, 26 segments on F.Cu/In3/In5. VBUS reaches the tab via a new In3.Cu route tapped at via (27.93,-2.23). Fab zip: `VitalQ_v2_fixv18_fabrication.zip`.
- fix_v16 deltas vs fix_v15: **PCB value fields synced** — C9 4.7u->10u, C63/C79 100n->1u footprint value text now matches schematic (R7 had updated sch+BOM/CPL only; F.Fab text layer only, zero copper/mask/paste/silk change — regenerated Gerbers verified content-identical). Fab zip: `VitalQ_v2_fixv16_fabrication.zip`.
- fix_v15 deltas vs fix_v14: C79 (+3V3_ANA local decap at U6 RX_SUP, C1) 100n -> **1u** (LCSC C14445, Samsung CL05A105KP5NNNC 0402 1uF 10V X5R, in stock) per AFE4900 SBAS857 pin-table requirement of 1uF local decap. C9 (VDD_CP2102 reg output, U5.6) 4.7u -> **10u** (LCSC C307415, Samsung CL05A106MP8NUB8 0402 10uF 10V X5R, in stock) so post-DC-bias effective capacitance meets the CP2102N 4.7uF spec. **C63 (VBUS decap) relocated (26.33,11.96) -> (28.60,3.90) rotated -90deg and revalued 100n -> 1u (same LCSC C14445)** so U2 BQ25170 IN has its required >=1uF bypass within ~1.2 mm of the pin (was 8.6 mm); the freed VBUS/GND stubs at the old site were rejoined/removed (0 unconnected, 0 DRC errors). Gerbers regenerated: only the Top Layer content differs vs fix_v14; inner layers, mask, paste, Edge_Cuts and drill are content-identical (header timestamps only).
- fix_v9 deltas vs fix_v8: **J3 force-sensor contact pads (Pads_FSR: +3V3 / FSR_ADC) moved F.Cu → B.Cu** at board (43.1, 59.85) — pad 1 `+3V3` at (42.0, 59.85), pad 2 `FSR_ADC` at (44.2, 59.85). `+3V3` escapes through a 0.25/0.15 POFV via-in-pad to the In4 +3V3 plane; `FSR_ADC` escapes through a second POFV via-in-pad to a new In1 run landing on via (26.3, 61.895), which joins the existing F.Cu FSR trunk (stub diagonal trimmed, 8 dead J3-feed segments removed). Every non-J3 track, via, pad position and net assignment is **identical to fix_v8** (verified object diff: 0 differences). J3 is a force-sensor pair, not a patient-contact electrode — **no hv rule areas were added**. See `../FIX_REPORT.md`.
- fix_v8 deltas vs fix_v7 (carried forward): **J7 chest-BioZ contact pads moved F.Cu → B.Cu** as a staggered 2×2 array at board x≈1.1–3.6 mm, y≈60.7–62.9 mm. Only the four `BIOZ_*_PAD` nets were rerouted; every non-J7 object is identical to fix_v7. Eight J7 electrode rule areas added, matching the other electrodes' protection scheme.
- Refilled error-level DRC: **0 errors, 0 unconnected, 0 schematic-parity errors** (29 accepted BOM warnings; 105 warnings total, all pre-existing classes — fewer than fix_v8's 107 and fix_v7's 112).
- fix_v6 deltas vs release: same copper as fix_v4/fix_v5 (8,871 tracks/vias byte-identical). BOM/value changes only: **R61, R107, R108 DNP** (join pre-existing J13, R104, R105 — six DNP refs total, footprints retained); R88 -> **150 ohm** (C25082); R51 -> **150k** (C25755); R73 -> **10k** (C25744); R19/R20/R21/R22 -> **2.2k** (C25879). CPL is 248 placements. See `../FIX_REPORT.md`.
- fix_v4 deltas vs release: D10 site-fit pads and D12 at (15.94, 50.6) as in fix_v3; **R104/R105 restored to DNP** so USB-C feeds only the ESP32 native USB port (CP2102 remains fitted but disconnected from the data lines); the pre-existing `antenna_keepout` zone under U1 was verified copper-free on all 8 layers. CPL is now 251 placements. See `../FIX_REPORT.md`.
- The owner accepted the 29 schematic/PCB BOM-exclusion attribute warnings. They concern J8 and TP1–TP28, not electrical netlist differences.
- The unfiltered report retains warning-level PCB findings of the same accepted classes as earlier fixes. There are no unconnected nets.
- Independent keepout audit from earlier fixes stands; fix_v4 adds no new keepouts (the antenna keepout already existed as a board zone).

## Construction

- **Eight copper layers, FR4, ENIG finish.**
- Board-level nominal thickness is **1.6 mm**. The unchanged detailed stackup below sums to **1.4776 mm** for copper plus dielectric, or **1.53856 mm** including the modeled 0.03048 mm soldermask on each face. This nominal/detail discrepancy is present in the source design. Obtain fabrication-engineering confirmation of the final manufacturable stackup; do not silently substitute a different stackup or change the layer order.
- The v18 outline is 55.6 x 75.0 mm (east tab added, see above). Preserve the supplied outline and every remaining internal creepage slot. The former U-shaped sensor-island slot has intentionally been removed; that region is now solid board.
- Track/gap requirement: at least 0.09 mm. Routed tracks use at least 0.1016 mm; the authorized Q1/Q2/Q4 footprint-internal pad gaps are 0.100 mm with a 0.09 mm scoped rule.
- Hole-to-hole and hole-to-other-net-copper requirements: at least 0.20 mm.
- All vias are through-vias. Use the separate PTH and NPTH drill files. NPTH mounting and Tag-Connect holes remain unplated and open.

| Order | KiCad layer / material | Thickness (mm) |
|---|---|---:|
| 1 | F.Cu — Top Layer | 0.0350 |
| | FR4 prepreg, dielectric 1 | 0.0994 |
| 2 | In1.Cu — Ground Layer 2 | 0.0152 |
| | FR4 core, dielectric 2 | 0.3000 |
| 3 | In2.Cu — Layer 3 | 0.0152 |
| | FR4 prepreg, dielectric 3 | 0.1088 |
| 4 | In3.Cu — Signal Layer 4 | 0.0152 |
| | FR4 core, dielectric 4 | 0.3000 |
| 5 | In4.Cu — Power Layer 5 | 0.0152 |
| | FR4 prepreg, dielectric 5 | 0.1088 |
| 6 | In5.Cu — Signal Layer 6 | 0.0152 |
| | FR4 core, dielectric 6 | 0.3000 |
| 7 | In6.Cu — Ground Layer 7 | 0.0152 |
| | FR4 prepreg, dielectric 7 | 0.0994 |
| 8 | B.Cu — Bottom Layer | 0.0350 |

## Required via-in-pad process

- **POFV / filled, copper-capped and planarized via-in-pad is required**, including the 0.25 mm land / 0.15 mm nominal-hole vias in solderable or electrode lands. Soldermask tenting alone is not an acceptable substitute.
- This specifically includes the authorized U6.E2 via beneath the J8 footprint keepout and the J11.3 electrode via at board coordinates **x=46.8 mm, y=55.3 mm**. The latter is wholly inside its own electrode pad. Both are 0.25/0.15 mm.
- Apply the same filled-and-capped process to all other vias intersecting SMD/BGA lands. Keep exposed electrode contact surfaces planar and ENIG-finished, without an open via hole.
- J8's U6.E2 keepout notch is the exact authorized local exception. The J8/U6 NPTH/courtyard exception was accepted after a conservative minimum NPTH-to-U6-pad clearance of 0.4485 mm was verified.

## POFV via-in-pad — full 48-via enumeration

- Gerber-level audit (`../reports/fab_mech.md` §1) enumerated **48 unique vias inside SMD lands** (49 pad contacts — one via touches both faces of the stacked U20.5/U11.5 pads). The previous section names only 2 of them explicitly; the authoritative list is below. **All 48 are same-net — zero cross-net via-in-pad shorts** (verified by aperture decode of the shipped gerbers, not just the board file).
- **⚠ 45 of the 48 in-pad vias are 0.150 mm drills — BELOW JLCPCB's published 0.2 mm POFV minimum via hole.** The only 0.200 mm in-pad drills are R19.1, U20.5/U11.5 and C89.2. Aspect ratio at 0.15 mm through the ~1.5 mm stack is 10.67:1 and the 0.25/0.15 annular ring is exactly at the 0.050 mm POFV minimum. **Written fab confirmation is required before order release** that POFV fill+cap+planarize will be run on the 0.150 mm in-pad holes, not just the 0.2 mm ones, and that the 0.15 mm drill/surcharge class is accepted.
- **5 of the 48 vias carry no explicit cap/fill flag in the board file** — the 2 named above plus three 0402 end-cap sites: **U6.E2 @ (27.600, 51.400); J11.3 @ (46.800, 55.300); R14.1 @ (31.175, 9.325); R60.2 @ (31.150, 29.100); R90.1 @ (44.750, 48.850)**. The missing flags are an authoring artifact, not an intent to skip processing. **Instruction: fill + cap + planarize ALL 48 vias in this table** regardless of the board-file flag column.
- Full list (board coordinates, KiCad y-down frame — the same frame used elsewhere in these notes; signs of Y in the CPL/gerbers follow the same convention):

| # | via x,y (mm) | via land/drill (mm) | annular ring (mm) | via net | pad hit | board cap/fill flag |
|--:|---|---|---|---|---|---|
| 1 | 31.1750, 9.3250 | 0.25/0.15 | 0.050 | ADS1292_PWDN | F.Cu R14.1 | **none** |
| 2 | 36.5859, 10.2808 | 0.25/0.15 | 0.050 | CS_AD5940 | F.Cu U7.F7 | cap+fill |
| 3 | 37.3859, 10.2808 | 0.25/0.15 | 0.050 | AD5940_GPIO0 | F.Cu U7.F5 | cap+fill |
| 4 | 38.1859, 10.2808 | 0.25/0.15 | 0.050 | +3V3_ANA | F.Cu U7.F3 | cap+fill |
| 5 | 36.5859, 10.6808 | 0.25/0.15 | 0.050 | SPI_MOSI | F.Cu U7.E7 | cap+fill |
| 6 | 36.9859, 10.6808 | 0.25/0.15 | 0.050 | GND | F.Cu U7.E6 | cap+fill |
| 7 | 36.5859, 11.0808 | 0.25/0.15 | 0.050 | VREF_2V5 | F.Cu U7.D7 | cap+fill |
| 8 | 38.5859, 11.0808 | 0.25/0.15 | 0.050 | BIOZ_FN | F.Cu U7.D2 | cap+fill |
| 9 | 36.5859, 11.4808 | 0.25/0.15 | 0.050 | VBIAS0 | F.Cu U7.C7 | cap+fill |
| 10 | 37.3859, 11.4808 | 0.25/0.15 | 0.050 | SWEAT_WE | F.Cu U7.C5 | cap+fill |
| 11 | 37.7859, 11.4808 | 0.25/0.15 | 0.050 | GND | F.Cu U7.C4 | cap+fill |
| 12 | 36.5859, 11.8808 | 0.25/0.15 | 0.050 | VZERO0 | F.Cu U7.B7 | cap+fill |
| 13 | 36.9859, 11.8808 | 0.25/0.15 | 0.050 | DE0 | F.Cu U7.B6 | cap+fill |
| 14 | 37.3859, 11.8808 | 0.25/0.15 | 0.050 | BIOZ_SP | F.Cu U7.B5 | cap+fill |
| 15 | 37.7859, 11.8808 | 0.25/0.15 | 0.050 | AIN4_LPF0 | F.Cu U7.B4 | cap+fill |
| 16 | 38.1859, 11.8808 | 0.25/0.15 | 0.050 | BIOZ_FP | F.Cu U7.B3 | cap+fill |
| 17 | 11.9000, 13.9000 | 0.30/0.15 | 0.075 | CP_TX | F.Cu U5.26 | cap+fill |
| 18 | 12.4000, 13.9000 | 0.30/0.15 | 0.075 | CP_RX | F.Cu U5.25 | cap+fill |
| 19 | 31.1500, 29.1000 | 0.25/0.15 | 0.050 | CHG_PG | F.Cu R60.2 | **none** |
| 20 | 10.7500, 42.4000 | 0.40/0.20 | 0.100 | +3V3 | F.Cu R19.1 | cap+fill |
| 21 | 15.8377, 43.6355 | 0.25/0.15 | 0.050 | IR_GATE | F.Cu U19.7 | cap+fill |
| 22 | 25.7000, 46.9500 | 0.25/0.15 | 0.050 | SPI_MOSI | B.Cu U22.A4 | cap+fill |
| 23 | 26.5000, 46.9500 | 0.25/0.15 | 0.050 | SPI_SCK | B.Cu U22.A2 | cap+fill |
| 24 | 26.5000, 47.3500 | 0.25/0.15 | 0.050 | MAX86178_INT | B.Cu U22.B2 | cap+fill |
| 25 | 26.9000, 47.3500 | 0.25/0.15 | 0.050 | LED3_K | B.Cu U22.B1 | cap+fill |
| 26 | 26.9000, 48.1500 | 0.25/0.15 | 0.050 | LED1_K | B.Cu U22.D1 | cap+fill |
| 27 | 44.7500, 48.8500 | 0.25/0.15 | 0.050 | MISO_AFE | F.Cu R90.1 | **none** |
| 28 | 27.2000, 49.8000 | 0.25/0.15 | 0.050 | PD_INM | B.Cu U6.A3 | cap+fill |
| 29 | 28.0000, 49.8000 | 0.25/0.15 | 0.050 | AFE_INM | B.Cu U6.A1 | cap+fill |
| 30 | 26.8000, 50.2000 | 0.25/0.15 | 0.050 | GND | B.Cu U6.B4 | cap+fill |
| 31 | 27.2000, 50.2000 | 0.25/0.15 | 0.050 | PD2_INM | B.Cu U6.B3 | cap+fill |
| 32 | 27.6000, 50.2000 | 0.25/0.15 | 0.050 | PD2_INP | B.Cu U6.B2 | cap+fill |
| 33 | 21.2500, 50.4000 | 0.40/0.20 | 0.100 | +3V3 | F.Cu U20.5 **and** B.Cu U11.5 (stacked pads, same via) | cap+fill |
| 34 | 26.8000, 50.6000 | 0.25/0.15 | 0.050 | +3V3_ANA | B.Cu U6.C4 | cap+fill |
| 35 | 26.4000, 51.0000 | 0.25/0.15 | 0.050 | GND | B.Cu U6.D5 | cap+fill |
| 36 | 26.8000, 51.0000 | 0.25/0.15 | 0.050 | TX1 | B.Cu U6.D4 | cap+fill |
| 37 | 27.6000, 51.0000 | 0.25/0.15 | 0.050 | AFE_BG | B.Cu U6.D2 | cap+fill |
| 38 | 28.0000, 51.0000 | 0.25/0.15 | 0.050 | GND | B.Cu U6.D1 | cap+fill |
| 39 | 26.4000, 51.4000 | 0.25/0.15 | 0.050 | TX2 | B.Cu U6.E5 | cap+fill |
| 40 | 26.8000, 51.4000 | 0.25/0.15 | 0.050 | TX3 | B.Cu U6.E4 | cap+fill |
| 41 | 27.2000, 51.4000 | 0.25/0.15 | 0.050 | CS_AFE4900 | B.Cu U6.E3 | cap+fill |
| 42 | 27.6000, 51.4000 | 0.25/0.15 | 0.050 | MISO_AFE | B.Cu U6.E2 | **none** ← named above |
| 43 | 27.6000, 51.8000 | 0.25/0.15 | 0.050 | SPI_MOSI | B.Cu U6.F2 | cap+fill |
| 44 | 28.0000, 51.8000 | 0.25/0.15 | 0.050 | AFE_CLK | B.Cu U6.F1 | cap+fill |
| 45 | 31.4000, 54.5000 | 0.40/0.20 | 0.100 | GND | F.Cu C89.2 | cap+fill |
| 46 | 46.8000, 55.3000 | 0.25/0.15 | 0.050 | J11_CE | B.Cu J11.3 | **none** ← named above |
| 47 | 42.0000, 59.8500 | 0.25/0.15 | 0.050 | +3V3 | B.Cu J3.1 | cap+fill |
| 48 | 44.2000, 60.1500 | 0.25/0.15 | 0.050 | FSR_ADC | B.Cu J3.2 | cap+fill |

## Owner-approved partial electrode isolation

- **This board intentionally uses partial, not full-stack, electrode isolation.**
- All 40 `hv_inner` electrode rule areas apply only to **In5.Cu and In6.Cu**, the two inner layers adjacent to the B.Cu skin-electrode side. They no longer apply to In1.Cu through In4.Cu.
- J5/J6/J7/J11/J13 skin-electrode pads are on B.Cu — fix_v8 moved J7's chest-bioimpedance contact pads from F.Cu to B.Cu so all five skin electrodes now face the same side. fix_v9 additionally moved the J3 FSR contact pads to B.Cu; J3 is a force-sensor pad pair (not a patient-contact electrode), so no hv rule areas were added for it. The owner's global bottom-side partial-isolation decision still applies.
- Outer-layer `hv_ownlayer` protection, all other original keepouts, and all remaining creepage slots are retained. Original keepout-report track segments were not rerouted. Only the later PWDN escape was locally adjusted while relocating offending through-vias.
- Own-electrode apertures are limited to that electrode's own via copper. Separate fill-only guards retain the original copper-pour exclusion coverage on In5.Cu/In6.Cu. No cross-electrode aperture was added. J11_CE's authorized own-net aperture follows its via into J11.3; its old aperture was restored.
- U20 and U11 remain at their original positions on their original faces. The schematic/netlist and layer stackup are unchanged.
- DRC and this fabrication package are not a medical/electrical safety certification.

## Assembly data

- `BOM.csv` and `CPL.csv` use JLC column names. Population follows the PCB's exclusion and DNP flags. Integral `vitalq:Pads_*` copper contacts, testpoints, fiducials, mounting holes and the bare Tag-Connect land pattern are not purchased/placed components and are excluded from the assembly CSVs. Their PCB copper remains in the Gerbers.
- LCSC identifiers are populated for every orderable row. The only blank LCSC cells are the two intentional consignments: D12 (`XL0603NRC730`, Sealand 0603 730 nm — no distributor stocks a 700–770 nm emitter ≤0603; see `../FIX_REPORT.md`) U12 (`MLX90632SLD-DCB-100-RE`, JLC global-source C17235844 / Mouser). **R10 update:** schematic now also carries LCSC C5349491 (exact -SP catalog entry); keep consignment/global-source as fallback if C5349491 is unstocked. See `ASSEMBLY_CHECKS.json`.
- CPL coordinates are in millimetres at the same absolute board origin as the Gerbers and drills. KiCad exports Y upward; negative coordinates are intentional. Bottom-side X is not separately mirrored. Do not translate only one file set.
- CPL rotations are the native KiCad footprint rotations modulo 360 degrees. The assembler must review package-specific zero orientation, polarity and bottom-side orientation in the assembly preview; no unsupported part-library rotation corrections have been invented.
- `positions_kicad.csv` is the raw position-export audit file. Use the filtered `CPL.csv` for assembly.

## Assembly notes

- **R61 — DNP at assembly (charger TS parallel-NTC fix).** R61 (NTC 10k B3380, LCSC C77131) is listed as fitted in the BOM/CPL of this package because fix_v19 populated it for board-side TS sensing with 2-wire packs. The audit (`../reports/07_findings.md` H1) showed that with any pack that carries its own 10k NTC on J2.3, R61 parallels it → R_TS ≈ 5 kΩ → V_TS ≈ 190 mV vs the BQ25170 V_HOT threshold of 188 mV typ → the charger can sit in hot-suspend at 25 °C. **Do not place R61.** (This supersedes the "R61 fitted" intent in the fix_v19 release basis above; the CSVs will be re-flagged at repack.)
- **U15 — substitute TPS61241YFFR for TPS61240YFFR.** The audit (H2) found the TPS61240 guaranteed output (200 mA EC spec; "up to 250 mA @3.2 V in") undersized for the ~300 mA worst-case optical-LED load on TX_5V → droop/shutdown risk. **TPS61241YFFR is the drop-in replacement** (same DSBGA-6 footprint, 300 mA spec). Order/kit U15 as TPS61241YFFR; BOM/CPL update follows at repack.
- **LED orientation caution — `LED_AK` footprints mark pin 1 = ANODE**, the reverse of the conventional pin-1/dot = cathode convention many assembly houses assume. **D12 and D25: verify orientation against the part datasheet, not the pin-1 mark** (audit-flagged assembly-rotation risk). **D10 is safe**: its site carries an explicit `K` cathode silk.
- **Consigned parts D10 / D12.** D10 = Nichia **NF2W757G-F1** white LED (consigned; note the board's D10 `Value` field is stale — `JB3030UWT-B-H50EA0000-1Z000001` — but the BOM/CPL/JLC value NF2W757G-F1 governs assembly, audit-verified). D12 = **XL0603NRC730** (Sealand 0603 730 nm) per the consignment note in Assembly data above. U12 (MLX90632SLD-DCB-100-RE) remains global-source/consigned as documented there.

## Known residual risks (accepted)

Documented audit findings (`../reports/07_findings.md`, `../reports/fab_mech.md`) accepted for this build — none blocks shipment of the fix_v19 package; all are next-revision candidates.

- **U3 (TPS63802) VOUT bulk capacitance is 15.95 mm from the pin** — only 100 nF local; the datasheet requires C_O,eff ≥ 7 µF "as close as possible" (C45 22 µF sits at 15.95 mm). Documented instability-margin risk; a layout fix is deferred to the next rev.
- **U4 (TPS7A2018) IN has no ≥ 1 µF capacitor within 9.42 mm** — only 100 nF at 5.28 mm local. Same margin-risk class as U3; accepted, next-rev.
- **SHT45 keepout still violated (not fixed in the board)** — a GND via (0.4/0.2) plus a GND trace run under the U23 package at ~(7.715, 46.461), contrary to Sensirion SHT4x DS §5.3/Fig.13 "no copper under the sensor" (heater-degradation risk). Present in the fix_v19 board file and shipped gerbers; accepted, next-rev.
- **J13 "MX ECG" connector is a dead stub** — its pads route through D29/D30 TVS and R118/R119 51 kΩ to no IC pin; J13 itself is already DNP (MAX86178-swap leftover) and the MX_ECG_INP/INM nets terminate as stubs. A fitted ECG-labelled connector would mislead integrators; accepted — remove or rewire to an AFE channel next rev.
- **Stackup 1.4776 mm detail vs 1.6 mm nominal** — see Construction: the specified copper+dielectric stack sums to 1.4776 mm (1.53856 mm with soldermask), a 0.1224 mm / 7.65 % shortfall vs the 1.6 mm nominal with nonstandard ~0.0994/0.1088 mm prepreg plies. **Fab to confirm prepreg ply availability and the actual delivered pressed thickness** — do not silently substitute a different stackup.
