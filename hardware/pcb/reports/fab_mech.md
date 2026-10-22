# Fab-gate mechanical/process audit — fix_v19 (`VitalQ_v2_fixv19_fabrication.zip`)

Basis: `hardware/pcb/vitalq_v2.kicad_pcb` @ HEAD (25e12c26, md5-verified identical to
worktree) + `hardware/pcb/fab/VitalQ_v2_fixv19_fabrication.zip` (read-only).
Method: every claim below is computed from the board s-expression, the Excellon
files, or the gerbers inside the zip — nothing eyeballed. Gerbers were used as the
authoritative geometry for via-in-pad because the shipped artwork is what the fab
builds (board-file pad transforms were cross-checked and agree).

Verdicts at a glance:

| # | Check | Result |
|---|-------|--------|
| 1 | POFV enumeration | **List incomplete** — 48 unique vias sit inside SMD lands, not 2. FAB_NOTES names the only 2 it calls out explicitly; 3 more in-pad vias carry no `capping/filling` flag either (R14.1, R60.2, R90.1). All 48 are same-net (0 cross-net shorts). |
| 2 | Drill set | 875 PTH / 8 NPTH confirmed from the .drl files. 92×0.150 mm + 779×0.200 mm via drills + 4×0.600 mm G85 slots. Aspect ratio 10.7:1 on the 0.15 mm holes — above the usual comfort limit; JLCPCB published numbers say manufacturable but at the absolute minimum (surcharge class). |
| 3 | Stackup | 1.4776 mm copper+dielectric / 1.53856 mm with mask — matches FAB_NOTES' own admission. Gap to 1.6 mm nominal: **0.1224 mm (7.65%)**, or 0.06144 mm (3.84%) counting mask. Fab must confirm prepreg ply availability / final pressed thickness. |
| 4 | Cross-doc reconciliation | All parity claims verified except two minor items: **D10 board Value field is stale** (`JB3030UWT-B-H50EA0000-1Z000001` on the board/positions vs `NF2W757G-F1` in BOM/CPL/JLC files) and FAB_VERIFICATION.json still names the **fix_v18** zip. R61 fitted verified; R93 DNP verified; 863+8=871 via arithmetic verified. |
| 5 | Gerber object counts | All 8 copper layers present and populated to the right order of magnitude; no staleness signature. |

---

## 1. POFV / via-in-pad enumeration

### Method
- Parsed all `(via …)` blocks in `vitalq_v2.kicad_pcb`: **871 vias** (matches
  `FAB_VERIFICATION.json` `"board_vias": 871`).
- For each copper-layer gerber in the zip, parsed every flashed component pad
  (objects carrying `%TO.P,…` / `%TO.N,…` attributes) with its real aperture
  geometry (C / R / O / RoundRect / Outline macros, rotation applied), then tested
  every via center for containment.
- Result: **48 unique vias inside SMD pads** (49 pad contacts — one via touches
  both faces of a stacked pad pair). **All 48 are same-net: zero cross-net
  via-in-pad shorts.** This was double-checked at gerber level because the initial
  board-file pass suggested cross-net overlaps (e.g. `IR_GATE` via inside `+3V3`
  pad) — those turned out to be pad-orientation artifacts; the gerbers show each
  such via actually lands on its own net's pad.

### Full coordinate list (board coordinates, KiCad y-down frame — same frame FAB_NOTES uses)

| # | via x,y (mm) | via land/drill (mm) | annular ring | via net | pad hit | board cap/fill flag |
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
| 42 | 27.6000, 51.4000 | 0.25/0.15 | 0.050 | MISO_AFE | B.Cu U6.E2 | **none** ← FAB_NOTES line 49 |
| 43 | 27.6000, 51.8000 | 0.25/0.15 | 0.050 | SPI_MOSI | B.Cu U6.F2 | cap+fill |
| 44 | 28.0000, 51.8000 | 0.25/0.15 | 0.050 | AFE_CLK | B.Cu U6.F1 | cap+fill |
| 45 | 31.4000, 54.5000 | 0.40/0.20 | 0.100 | GND | F.Cu C89.2 | cap+fill |
| 46 | 46.8000, 55.3000 | 0.25/0.15 | 0.050 | J11_CE | B.Cu J11.3 | **none** ← FAB_NOTES line 49 |
| 47 | 42.0000, 59.8500 | 0.25/0.15 | 0.050 | +3V3 | B.Cu J3.1 | cap+fill |
| 48 | 44.2000, 60.1500 | 0.25/0.15 | 0.050 | FSR_ADC | B.Cu J3.2 | cap+fill |

### Findings
- **FAB_NOTES.md line 49 names only 2 of the 48** (rows 42 and 46). Its blanket
  clause (line 50: "Apply the same filled-and-capped process to all other vias
  intersecting SMD/BGA lands") does cover the rest process-wise, but as an
  enumeration the POFV list is **incomplete by a factor of 24**.
- Of the 48 in-pad vias, **5 carry no `capping/filling` flags in the board file**:
  the 2 named (U6.E2, J11.3) **plus R14.1 @ (31.175, 9.325), R60.2 @ (31.150,
  29.100), R90.1 @ (44.750, 48.850)** — all three are 0.25/0.15 vias sitting in the
  end cap of 0402 pads. They are unflagged in the source data and unnamed in the
  doc; only the blanket clause covers them.
- The board file marks **55 vias** (all with both `capping yes` and `filling
  yes`): 43 of them are the flagged in-pad vias above; the other **12** are
  routing vias on CP_RX/CP_TX/SWEAT_WE/FSR_ADC/+3V3 trunks flagged anyway
  ((15.2,39.9)+3V3, (26.3,61.895)FSR_ADC, (35.75,33.35)/(29.65,39.45)/
  (35.75,32.4)/(36.75,29.45) SWEAT_WE, (11.4,12.45)/(22.85,9.2)/(13.45,9.35)
  CP_RX, (18.65,8.8)/(11.7,10.4)/(14.95,8.25) CP_TX) — harmless over-marking.
- Both FAB_NOTES named vias verified at gerber level: `U6,E2` pad net `MISO_AFE` =
  via net (gerber `gerbers/vitalq_v2-Bottom Layer.gbr`), `J11,3` pad net `J11_CE` =
  via net at (46.8, -55.3).
- Via drill distribution inside pads: 45 of 48 are 0.15 mm holes (0.25 or 0.30
  land); 3 are 0.20 mm (R19.1, U20.5/U11.5, C89.2).

## 2. Drill set

Parsed directly from `drill/vitalq_v2-PTH.drl` / `vitalq_v2-NPTH.drl` (hole counts
counted in-file, cross-checked against `DRILL_REPORT.txt`/`vitalq_v2-drill.rpt`):

### PTH — 875 holes total (4 of them G85 slots)
| Tool | drill (mm) | holes | assignment |
|---|---|---:|---|
| T1 | 0.150 | 92 | 81× 0.25/0.15 vias + 11× 0.30/0.15 vias |
| T2 | 0.200 | 165 | 165× 0.40/0.20 vias (drill 0.2000) |
| T3 | 0.200 | 614 | 614× 0.4004/0.2002 vias (drill 0.2002 — separate tool at the 4th decimal) |
| T4 | 0.600 | 4 | 4× G85 slots on `SH` thru_hole pads (GND): 2× 0.6×1.2 mm + 2× 0.6×1.7 mm @ (21.68,4.12–5.22),(21.68,8.55–9.15),(30.32,4.12–5.22),(30.32,8.55–9.15) — connector shield area near J1 |

875 = 871 vias + 4 slotted TH pads — matches `FAB_VERIFICATION.json`
(`"pth_holes": 875`, `"th_pads": 4`).

### NPTH — 8 holes total
| Tool | drill (mm) | holes | notes |
|---|---|---:|---|
| T1 | 0.650 | 2 | (23.11, -5.2) & (28.89, -5.2) — J8 Tag-Connect guide holes |
| T2 | 0.800 | 1 | (28.936, -36.154) |
| T3 | 0.991 | 3 | board pads 0.9906 mm (report rounds to 0.991) |
| T4 | 2.200 | 2 | H1/H2 M2 mounting holes |

Matches `"npth_holes": 8` and FAB_NOTES line 26 (PTH/NPTH split).

### Aspect ratio & annular ring vs 1.6 mm
| hole | ratio (t/hole) | via land | annular ring |
|---|---:|---|---:|
| 0.15 mm via drill | **10.67:1** | 0.25 / 0.30 | 0.050 / 0.075 mm |
| 0.20 mm via drill | 8.0:1 | 0.40 / 0.4004 | 0.100 / 0.1001 mm |
| 0.60 mm TH slot | 2.7:1 | pad 1.0 mm wide | 0.20 mm |
| NPTH 0.65–2.2 | 2.5–0.7:1 | — | — |

JLCPCB published capability boundaries (jlcpcb.com/capabilities, "Drilling" &
"Min. Via hole size/diameter" + jlcpcb.com/news/free-via-in-pad-6-20-layer-pcbs-pofv):
- Multilayer **min drill diameter: 0.15 mm** — the 92 × 0.150 mm holes are **at
  the absolute minimum** (their note ③: "0.1 mm or 0.15 mm hole size with any via
  diameter … will cost more" → surcharge class; preferred min via hole 0.2 mm,
  which the other 779 vias meet).
- **Min via hole size/diameter: 0.15/0.25 mm** — the design's smallest vias are
  exactly 0.15/0.25 = at the capability edge; "via diameter should be 0.1 mm
  (0.15 mm preferred) larger than hole": 0.25/0.15 = +0.10 (at min), 0.30/0.15 =
  +0.15 (at preferred), 0.40/0.20 = +0.20.
- **POFV**: free on 6–20-layer boards (this is 8-layer, so eligible). Their POFV
  spec: annular ring ≥0.05 mm (0.075 preferred) — the 0.25/0.15 in-pad vias are at
  exactly 0.050 mm ring (limit); via hole range published as 0.2–0.5 mm — 45 of
  the 48 in-pad vias are 0.15 mm holes, i.e. below the printed POFV table range
  (the JLC Chinese engineering doc calls 0.15/0.25 the 极限/extreme limit). **Fab
  confirmation required that POFV is run on the 0.15 mm in-pad holes, not just
  the 0.2 mm ones.**
- BGA: pads 0.2–0.25 mm require ENIG — this board carries 0.23/0.24/0.25 mm BGA
  pads (U6/U7/U22) and finish is ENIG ✓.
- PTH component annular (multilayer 1 oz): abs min 0.15, rec 0.20 mm — the 4 SH
  pads have 0.20 mm ring ✓ at recommended.
- Aspect ratio: JLCPCB does not publish a through-via aspect-ratio cap on the
  capabilities table; 10.67:1 at 0.15 mm is at the edge of standard FR4 plating
  practice — flag for fab engineering sign-off together with the POFV question.

## 3. Stackup math (FAB_NOTES.md lines 28–44)

Recomputed from the table:

- Copper: 2×0.0350 + 6×0.0152 = 0.1612 mm
- Dielectric: 0.0994 + 0.3000 + 0.1088 + 0.3000 + 0.1088 + 0.3000 + 0.0994 = 1.3164 mm
- **Total (Cu + dielectric): 1.4776 mm** ✓ matches FAB_NOTES line 22
- With 2×0.03048 mm mask: **1.53856 mm** ✓ matches their 1.5386 figure

Gap vs 1.6 mm nominal: **0.1224 mm = 7.65 %** (copper+dielectric basis), or
**0.06144 mm = 3.84 %** including soldermask. Board is symmetric (prepreg pairs
0.0994/0.0994 and 0.1088/0.1088 — approx 3.9 mil/4.3 mil, i.e. nonstandard ply
combinations like 2116/3313 stacks).

Fab must confirm: (a) the quoted prepreg thicknesses are achievable with their
available ply set after pressing, (b) whether the order will actually deliver
~1.54 mm or a compensated 1.6 mm stack, and (c) that the 0.30 mm cores exist —
per FAB_NOTES' own instruction not to silently substitute a different stackup.

## 4. Cross-document reconciliation (all from the zip)

Counts: BOM.csv 265 · BOM_JLC.csv 251 (82 groups) · CPL.csv 270 · CPL_JLC.csv
251 · positions_kicad.csv 271 · board footprints 302. Full verbatim diff output:
`reports/06_tool_output/parity_report.txt`.

- **Ref-des chain is closed**: 302 footprints − 31 board-level exclusions
  (J8 + TP1–TP28 + H1 + H2, all carrying `exclude_from_pos_files
  exclude_from_bom` attrs) = 271 positions rows; positions = BOM.csv + FID1–6;
  CPL.csv = positions − R93; CPL_JLC = BOM_JLC ref-set (exact match, 251);
  JLC sets = 265 − 14 {9 board-contact connector refs J2,J3,J5,J6,J7,J9,J10,J11,J13
  + 5 DNP resistors R104,R105,R107,R108,R93}.
- **DNP set** (BOM.csv `DNP` column + board `attr dnp`): {J13, R104, R105, R107,
  R108, R93} — six refs. **R61 is FITTED**: board attr `smd` (no dnp), BOM row
  "NTC 10k B3380" no DNP, BOM_JLC LCSC **C77131** Murata NCP15XH103F03RC, placed
  in CPL.csv/CPL_JLC/positions at (31.1547, -7.0914) Top rot90 → fix_v19 claim
  verified; **R93** flagged `DNP` in BOM.csv row 209 and removed from
  CPL.csv/CPL_JLC/BOM_JLC (still present in the raw positions_kicad export —
  consistent with "removed from CPL" in FAB_VERIFICATION).
- **Fiducials**: FID1–6 excluded from BOM (board `exclude_from_bom`) but retained
  in positions + CPL.csv and dropped only from the JLC files — matches
  FAB_VERIFICATION's "6 fiducials (place-only)".
- **J8/TP1–28/H1/H2 legitimacy**: all 31 absent from every CSV via footprint
  attrs; they are cable-press/probe/mechanical features (Tag-Connect TC2030
  spring-pin header, test pads, NPTH mounting holes) — no soldered component
  exists to place, and their copper remains in the gerbers → legitimate.
- **Gerber layer count**: 8 copper gerbers present in zip (Top, Ground L2,
  Layer 3, Signal L4, Power L5, Signal L6, Ground L7, Bottom) = board's 8 copper
  layers ✓; full set is 16 .gbr + 1 .gbrjob (mask ×2, overlay ×2, paste ×2,
  Edge_Cuts, job).
- **Via-delta resolution**: board has **871** vias; ROUND9_REVIEWS.md (written at
  fix_v16) cites "863 via drill sites". FAB_VERIFICATION.json's note — "PTH
  hits=871 (863 vias + 8 new power-path vias); … v16 had 863" — is exactly the
  explanation: **863 + 8 = 871**, and fix_v19 added zero vias (R61 was a
  BOM/property-only change; freeze claims 871 identical). The 8 new vias are
  consistent with the v18 power-path cluster: 4 inside the new east tab —
  (49.6,29.0)GND, (53.0,26.8)VBUS, (52.4,25.4)VBAT, (48.9,26.0)VBAT_SYS — plus 4
  feeder vias at the same round-number grid serving Q6/D33/R123 —
  (45.7,22.53)VBAT, (46.5,26.75)VBAT_SYS, (47.22,24.06)GND, (47.34,21.86)+3V3.
  The VBUS tap via at (27.93,-2.23) is outside the tab (the In3 route it feeds is
  the new part, the via itself is older).
- **Discrepancies found**:
  1. **D10 value-field mismatch**: board `Value` = `JB3030UWT-B-H50EA0000-1Z000001`
     (a different MPN), and `positions_kicad.csv` faithfully carries it, while
     BOM.csv/CPL.csv/BOM_JLC say `NF2W757G-F1` (the intended consigned Nichia
     part; footprint `vitalq:NF2W757G` is correct). Assembly uses the BOM value,
     so this is cosmetic — but it is a board-vs-document inconsistency not
     disclosed in the docs.
  2. **FAB_VERIFICATION.json stale field**: `"zip": "fab/VitalQ_v2_fixv18_fabrication.zip"`
     inside a file named `VitalQ_v2_fixv19_fabrication.zip` — copy-paste remnant;
     the counts inside are nonetheless correct for v19.
  3. Row bookkeeping: FAB_VERIFICATION `bom_rows: 266` counts the header line;
     `cpl_rows: 270` counts data rows — inconsistent but harmless.
  4. CPL.csv keeps the other five DNP refs (R104/105/107/108/J13) while removing
     R93 — inconsistent DNP treatment inside CPL.csv itself (JLC files are
     consistent: all six DNP refs absent).

## 5. Gerber object counts (full table: `reports/06_tool_output/gerber_counts.txt`)

| layer | flashes (D03) | pad-tagged | flashes at via sites | D01 draws | draws in G36 regions | regions |
|---|---:|---:|---:|---:|---:|---:|
| F.Cu (Top) | 2568 | 814 | 1753 | 5080 | 158 | 2 |
| In1.Cu (GND L2) | 1758 | 0 | 1736 | 31419 | 31396 | 1 |
| In2.Cu (L3) | 1758 | 0 | 1736 | 1036 | 0 | 0 |
| In3.Cu (Sig L4) | 1758 | 0 | 1736 | 295 | 0 | 0 |
| In4.Cu (Pwr L5) | 1758 | 0 | 1736 | 50118 | 50099 | 2 |
| In5.Cu (Sig L6) | 1758 | 0 | 1736 | 172 | 0 | 0 |
| In6.Cu (GND L7) | 1758 | 0 | 1736 | 30895 | 30895 | 2 |
| B.Cu (Bottom) | 1926 | 172 | 1760 | 1549 | 0 | 0 |

Read-out: ~2×871 via flashes on every copper layer (annular + thermal/antipad
entries) → no missing via sites anywhere in the export; pad-tagged flashes
F=814/B=172 vs board pads F=841/B=168 (rest are custom/region-drawn pads);
standalone track draws sit on the routed signal layers while the three pour
planes carry their fills as huge region outlines. Order of magnitude matches
board contents (871 vias, 1027 pads, ~8k segments) — **no gross mismatch, no
export-staleness signature.** ROUND9's "867 flashes/copper layer" was the fix_v16
count basis (863 vias + 4 TH) and is not comparable to this ~2×-per-via v19
export.

## Items for fab engineering confirmation before order release
1. POFV list is 48 vias (Section 1) — FAB_NOTES explicitly names 2; confirm the
   blanket clause covers all 48 including the 3 un-flagged 0402-cap sites
   (R14.1/R60.2/R90.1).
2. 0.15 mm via-in-pad holes are below JLCPCB's published 0.2–0.5 mm POFV table —
   confirm filled/capped processing on 0.15 mm holes (45 of the 48) and the 0.15
   mm surcharge option is selected.
3. 10.67:1 aspect ratio on 0.15 mm holes through a ~1.5–1.6 mm stack.
4. Stackup: 1.4776/1.53856 mm detail vs 1.6 mm nominal — prepreg availability and
   delivered thickness must be confirmed, not silently substituted.
5. D10 board `Value` text (JB3030UWT…) vs BOM `NF2W757G-F1` — update the board
   field or confirm the BOM governs assembly.
