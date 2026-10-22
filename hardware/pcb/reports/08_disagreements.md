# 08 — Disagreement analysis vs prior audit rounds

Target: `hardware/pcb/vitalq_v2.kicad_pcb` (fix_v19) @ `b9002188`,
sha256 `7844150655ab983eb00a81855474900df8f12ee16e7af0bdd834f6fcc11a69d0`.
Method: independent s-expression re-parse of the board file; prior claims
read from `pinmap.json`, `pin_audit_r9/ROUND9_REVIEWS.md`,
`pin_audit_r7/DFM_FAULT_R7.md`, `FIX_V19.md`, `FIX_REPORT.md`,
`FULLSIM_V19_REPORT.md`, `fab/FAB_VERIFICATION.json`, `fab/FAB_NOTES.md`,
`audit_components.csv`, `audit_nets.csv`, `audit_v14.net`,
`audit_netlist.xml`, `verify/drc_v19.rpt`, and the fix_v19 fab zip.

---

## A. pinmap.json vs board — verdict: FULLY CONSISTENT

Raw diff: `reports/06_tool_output/prior_audit_diff.txt`.
Every one of the **401 per-pin claims** in `pinmap.json` was checked against
the board's pad→net assignments **and** against the schematic-symbol pin
names (the claimed `NAME_PAD` field):

- **0 net mismatches, 0 missing pads** across U1–U26.
- **0 pin-name mismatches** vs `vitalq.kicad_sym` pin names for all 26 ICs.
- All 26 `comp` MPN claims match the board `Value` properties.
- The only U-device pads pinmap.json does not list are the 6 unnumbered
  mechanical/thermal pads (`U2,U3,U5,U17,U18,U25` pad `""`), all net-less —
  correct to omit.

### A.1 — Found defect in the *prior round's own truth file* (systematic)

`reports/01_netlist_pinmap.csv` was billed as the extracted pad→net truth
(1,027 rows). **Its `net` column is empty on all 1,027 rows.**
`reports/tools/extract_pads.py:91` reads `net[2]`, i.e. it expects KiCad ≤8
pad syntax `(net <id> "<name>")`. This board is **KiCad 10.0**
(`(version 20260206)`), where pads carry `(net "<name>")` with no numeric
id — so `net[2]` never exists and every row silently wrote `''`.
Any prior-round conclusion that leaned on this CSV for *net* verification
verified nothing; pad positions/footprint/ref columns are still valid.
Re-extraction (corrected parser) is in `06_tool_output/pinmap_truth.csv`.

---

## B. Disagreements and stale assertions

| # | Prior claim (source) | This audit's finding | Verdict |
|---|---|---|---|
| B1 | `"zip": "fab/VitalQ_v2_fixv18_fabrication.zip"` — `fab/FAB_VERIFICATION.json:2` (inside the fix_v19 zip) | The enclosing zip is `VitalQ_v2_fixv19_fabrication.zip`; its 33-entry SHA256SUMS manifest verifies 100%, and the repo copy of the JSON is byte-identical to the zipped copy. The JSON is the **fix_v18-era verification doc carried into the v19 archive**. Its numbers still apply because v19 changed no copper (see C-table) | **Stale field, not wrong zip.** Cosmetic but confusing; the filename field was never regenerated for v19 |
| B2 | "DRC … violations: 61" — `FAB_VERIFICATION.json:44`; vs "0 errors / 48 warnings / 0 unconnected" — `ROUND9_REVIEWS.md:29` | `verify/drc_v19.rpt` says "**Found 61 DRC violations**" with class breakdown 20 track_dangling / 15 silk_edge_clearance / 8 silk_over_copper / 3 silk_overlap / 9 text_height / 1 text_thickness / 5 lib_footprint_mismatch = 61 — identical to FULLSIM_V19 §C. ROUND9's 48 was measured on **fix_v16** (it says so: "(fix_v16)"). Same checker (`kicad-cli pcb drc`), different board revisions — not a disagreement between checkers | **Reconciled.** 48 = v16 baseline, 61 = v18/v19. +13 delta explained by FAB_VERIFICATION: "+1 lib_footprint_mismatch (Q6), +8 silk_over_copper, +1 silk_edge_clearance, +3 silk_overlap — cosmetic on added footprints" |
| B3 | "Source board: `fix_v9/vitalq_v2.kicad_pcb`" — `fab/FAB_NOTES.md:6` vs "`pcb/` contains the order-ready … **fix_v19**" — `hardware/README.md:3` | No `fix_v*/` directory exists anywhere in this repo (only `archive/case_v7`); the source board is `hardware/pcb/vitalq_v2.kicad_pcb` at fix_v19. The `fix_v9/` string is a carried lineage header from the author's local workspace (`/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_vN/`, per `audit_v14.net:4`). README is correct | **Stale provenance path in FAB_NOTES** (the file itself is the v19 notes doc, updated 2026-10-19 per zip timestamps) |
| B4 | "Panel: single board **50x75**" — `ROUND9_REVIEWS.md:12` | Edge.Cuts bounds = **55.60 × 75.00 mm** (x −2→53.6, y −10.5→64.5), matching FAB_VERIFICATION `outline_mm` and the "+5.6 mm east tab" note | **Stale** — measured pre-v18. Current outline verified correct |
| B5 | "No fiducial within 5 mm of board edge (JLC rule); 6 fids OK" — `ROUND9_REVIEWS.md:11` | 6 fiducials exist but **5 of 6 sit <5 mm from an edge**: FID1 (−0.41,1.88)→1.59 mm, FID4 (3.06,−9.19)→1.31 mm, FID6 (46.69,63.19)→1.31 mm, FID3 (43.66,61.70)→2.80 mm, FID5 (5.11,59.94)→4.56 mm; only FID2 (27.73,14.82) is >5 mm clear | **REFUTED** — the measured claim is wrong on the current outline (edge distances unchanged post-v18 except the east tab; this is a genuine surviving error, not a revision artifact) |
| B6 | `pinmap.json` — 401 pin claims | See §A: 401/401 net-correct, pin-name-correct, MPN-correct | **Confirmed — no disagreement rows** |
| B7 | `audit_components.csv` | **299 data rows vs 302 board footprints.** Missing exactly `Q6`, `D33`, `R123` — the three fix_v18 power-path parts → file structure predates v18, though its R61 row was hand-patched to the v19 value (`NTC 10k B3380`). Additionally the `dnp` column is **empty on all 299 rows** — none of the 6 board-DNP parts (R93, R104, R105, R107, R108, J13) is flagged. D10 row carries the stale board Value (see B9) | **Stale + silent DNP blindness.** Not regenerated after v18; R61 patched by hand |
| B8 | `audit_nets.csv` | No Q6/D33/R123 anywhere; `VBAT_SYS` row lists 7 pads `C44 C46 L2 R93 U15 U3` — board truth is 9 (adds Q6.2, D33.1) | **Stale** — predates fix_v18 |
| B9 | "Value/LCSC updated in sch + BOM + CPL + `vitalq.pretty/NF2W757G.kicad_mod`" — `FIX_V19.md:12-13` (D10 = Nichia NF2W757G-F1) | Sch D10 `Value="NF2W757G-F1"` `LCSC=""` ✓; `vitalq.pretty/NF2W757G.kicad_mod` Value=NF2W757G-F1 ✓; BOM.csv/BOM_JLC.csv = NF2W757G-F1 (consigned) ✓ — **but the placed PCB footprint still reads `Value="JB3030UWT-B-H50EA0000-1Z000001"`, `LCSC="C17610390"`** (the superseded Cree part). `audit_components.csv` faithfully propagates the stale board field | **NEW DEFECT — stale placed-footprint properties.** Never caught because `verify/drc_v19.rpt` ran *without* `--schematic-parity`, and the "parity: 4" list in FAB_VERIFICATION (C9,C63,C79,U12) is v18-era. Fabrication impact: nil (D10 consigned; BOM correct). Traceability impact: board file contradicts sch/BOM on the exact part FIX_V19 was written to fix |
| B10 | "29 schematic/PCB BOM-exclusion attribute warnings … J8 and TP1–TP28" — `FAB_NOTES.md:15`, echoed `FIX_REPORT.md:44,50` | Enumeration checks: **J8 (1) + TP1–TP28 (28) = 29 exactly.** All 29 are `exclude_from_bom` on the PCB *and* `in_bom no` in the schematic; all are no-place items (28 testpoints + J8 Tag-Connect land pattern). J8's pads do carry nets (+3V3/GND/ESP_TX/ESP_RX/ESP_EN/ESP_IO0) but nothing is purchased — **no electrical/BOM requirement is waived**. Note: since sch+pcb flags now agree, a fresh parity run would not obviously reproduce 29 warnings; the count is likely carried from earlier revisions | **Enumeration confirmed; possibly stale count** (unverifiable without kicad-cli parity run; H1/H2/FID1–6 are also BOM-excluded but never claimed — consistent, since they're mechanical items with in_bom=no on both sides) |
| B11 | `audit_v14.net` / `audit_netlist.xml` | Sources: `fix_v14/vitalq_v2.kicad_sch` (2026-10-18T17:53) and `fix_v12/…` (17:23), exported from the author's local tree. **Both predate v15–v19**: no Q6/D33/R123 components; R61 = plain `10k`/`C25744` (the R73 code lineage), not the v19 `NTC 10k B3380`/`C77131` | **Stale reference lists** — safe as historical artifacts, unusable as current-truth netlists |
| B12 | "board_vias: 871 … PTH hits=871 (863 vias + 8 new power-path vias); v16 had 863" — `FAB_VERIFICATION.json:5-7` vs "863 via drill sites covered" — `ROUND9_REVIEWS.md:4,30` | Board file: **871 `(via` objects exactly**. PTH drill: 92+165+614 = 871 via tools + 4×0.6 mm G85 slot sites (TH pads) = **875 PTH**, 8 NPTH — matching `DRILL_REPORT.txt` and ROUND9's "4 G85 slot sites = through pads". The v18-added 8 are consistent with: tab vias GND(49.6,29), VBUS(53,26.8), VBAT(52.4,25.4), VBAT_SYS(48.9,26); approach vias VBAT(45.7,22.53), VBAT_SYS(46.5,26.75), GND(46.25,28.75); VBUS tap at (27.93,−2.23) — the exact coordinate quoted by FAB_NOTES/FAB_VERIFICATION (gerber Y-sign flipped) | **Reconciled** — 863 is the v16 baseline, 871 current. +8 power-path vias confirmed present (exact membership of the 8 is inferential — no v16 board in repo to diff) |

---

## C. Prior findings confirmed on the current board

| Claim | Source | Verified |
|---|---|---|
| Object counts: 8,016 segments / 871 vias / 107 zones / 1,027 pads / 302 placements | `FULLSIM_V19_REPORT.md:80-81`, `FAB_NOTES.md:5` | Exact: 8,016 `(segment`, 871 `(via`, 107 `(zone` objects (106 board + 1 footprint-embedded = antenna keepout), 1,027 pads, 302 footprints |
| R61 fitted: Murata NCP15XH103F03RC, 10 kΩ B3380, C77131, TS→GND | `FIX_V19.md:15-46` | Board: Value `NTC 10k B3380`, LCSC `C77131`, no dnp flag, pads TS/GND; in BOM_JLC (C77131) and CPL_JLC |
| R93 now DNP (Q6 replaces 0-Ω link) | `FAB_VERIFICATION.json:21` | `(attr … dnp)` on R93; absent from CPL_JLC |
| Q6 pinout: pin1=VBUS(gate), pin2=VBAT_SYS(source), pin3=VBAT(drain); D33 anode=VBUS→cathode=VBAT_SYS; R123 gate→GND | `FAB_NOTES.md:7` | Exact on-board pad nets; positions (50.5,22.9)/(50.3,26.7)/(50.5,28.9) match the "new_parts" block |
| 6 DNP refs total | `FAB_NOTES.md:13` + v18 reconcile | R93, R104, R105, R107, R108, J13 = 6 |
| J2 pin 3 open for 2-wire pack; R61 provides TS | `FIX_V19.md:18-20` | J2 has 3 pads only: 1=VBAT, 2=GND, 3=TS (the open pin is at the *pack* side; board net confirmed) |
| U6 AFE4900 OSC: `AFE_CLK` = U6.CLK + R10 1 k→GND | `DFM_FAULT_R7.md:5-7` | R10=1k, pads AFE_CLK/GND; U6.F1 on AFE_CLK; no other pads on net — correctly strapped |
| U24 CLKOUT pin 1 NC | `DFM_FAULT_R7.md:9-11` | Pad 1 net = `unconnected-(U24-CLKOUT-Pad1)` (proper KiCad no-connect) |
| U16 SFH7072: 4 anodes TX_5V, cathodes TX1–TX4 | `DFM_FAULT_R7.md:14` | Pads 4/5/7/11=TX_5V; pads 6/8/9/12=TX1/TX3/TX4/TX2 |
| TP27=STAT, TP28=RTC_INT; PG on R60 test pad | `ROUND9_REVIEWS.md:16` | TP27=CHG_STAT; TP28=RTC_INT; R60.2=CHG_PG |
| RTC_INT reaches only TP28 (carried-open item) | `ROUND9_REVIEWS.md:18` | RTC_INT members: U24.2, TP28, R112.2 (pull-up), R115.1 — **no MCU pin**; consistent |
| R115 pulls RTC backup to GND (b16 finding: RTC loses time at full power-down) | `FULLSIM_V19_REPORT.md:68,184` | R115 pads = RTC_VBK/GND — hardware defect confirmed real |
| C63 relocated to (28.60,3.90) rot −90°, 1 µF VBUS decap | `FAB_NOTES.md:9`, `ROUND9_REVIEWS.md:4` | C63 at (28.6,3.9,−90), value `1u`, pads VBUS/GND, ~2 mm from U2 (26.67,4.47) |
| U21 USBLC6 clamp on VBUS/D+/D−; R104/R105 DNP disconnect | `ROUND9_REVIEWS.md:21` | U21 pads on USB_DP/USB_DM/VBUS; R104/R105 dnp |
| J8 = Tag-Connect TC2030, SWD-less USB/UART programming | `ROUND9_REVIEWS.md:16` | `Connector:Tag-Connect_TC2030-IDC-NL…`, nets +3V3/GND/ESP_TX/ESP_RX/ESP_EN/ESP_IO0 |
| U6.E2 POFV via beneath J8 keepout; J11.3 electrode via (46.8,55.3), 0.25/0.15 | `FAB_NOTES.md:49` | MISO_AFE via-in-pad at U6.E2 = (27.6,51.4) under J8 site; via at exactly (46.8,55.3), size 0.25 / drill 0.15, net J11_CE |
| Skin-side audit: 14 parts on B.Cu (J3,J5,J6,J7,J11,J13,D10–D12,U10–U12,U16,U23); outward parts on F.Cu | `FIX_REPORT.md:52` | All 14 = B.Cu; J1,J8,SW1,SW2,D25,U13,U25 = F.Cu. J3 at (43.1,59.85) B.Cu exactly |
| 61 warnings class breakdown | `FULLSIM_V19_REPORT.md:84-88` | `verify/drc_v19.rpt` class counts identical (20/15/8/3/9/1/5 = 61) |
| Outline 55.6 × 75.0, east tab x 48→53.6 only | `FAB_VERIFICATION.json:8-12` | Edge.Cuts bounds verified; max x = 53.6 |
| Fab zip: 37 files, gerbers+drills+BOM/CPL+SHA256SUMS | `FULLSIM_V19_REPORT.md:105-109` | 37 files; all 33 manifest hashes verify against zip members; no missing/extra files |
| CPL 251 placements / BOM_JLC 82 groups / D10,D12 consigned / U12 = JLC C5349491 | `FIX_V19.md:58-60`, `ASSEMBLY_CHECKS.json` | CPL_JLC = 251 data rows; BOM_JLC = 82 data rows; D10/D12 rows `(consigned)` w/ blank LCSC; sch U12 LCSC=C5349491 (board LCSC blank = the documented U12 parity diff) |
| "867 flashes/copper layer … 863 via drill sites" | `ROUND9_REVIEWS.md:4` | Consistent for v16 (863 vias + 4 TH pads = 867). Current set: 871+4 = 875, matching PTH count |

## D. Unverifiable in this environment (no board diff basis / no kicad-cli)

- **Copper freeze vs fix_v18** (`FIX_V19.md:3-5`, FULLSIM "whole-file diff = 3 lines"): no fix_v18 board exists in the repo to diff against; claim rests on `sims/fullsim_v19/` artifacts not present here.
- **ERC = 0 errors / 13 warnings** and the **29 schematic-parity warnings**: `kicad-cli` not installed; parity rpt absent (v19 DRC ran without `--schematic-parity` — which is exactly why B9 survived).
- **22 corrected LCSC codes** (`FIX_V19.md:62-84`): board `Value` fields match the *intended* values (spot-checked R3-region/R12=10, R18=200k, R123=100k, C63/C79=1u, C9=10u, R88=150, R51=150k, R73=10k); stock/validity of the JLC part numbers can't be re-queried.
- **Sepsis cohort** (ews_v11_4 0.98/0.853): model artifact, not board state.
- ROUND9's gerber-level assertions on *fix_v16 gerbers* (old C63 site clean etc.): current gerbers are the v18/v19 set; the claims are consistent with current drill/gerber data where re-checkable.

## E. Summary

- **pinmap.json: clean** — 401/401 claims verified at net, pin-name, and MPN level. The prior pin audits' per-pin work stands.
- **The largest systematic error was in the prior round's own tooling**: `01_netlist_pinmap.csv` shipped 1,027 empty net cells (KiCad-10 format incompatibility) — a silent failure.
- **One new real defect**: placed footprint D10 still carries the pre-fix Cree MPN/LCSC (B9). Docs/BOM/sch correct; board instance stale. Cosmetic for fabrication (consigned part) but a genuine sch↔pcb divergence.
- **Stale fields confirmed**: `FAB_VERIFICATION.json.zip` (fix_v18 name), `FAB_NOTES.md` source-board path (fix_v9), `audit_components.csv`/`audit_nets.csv` (pre-v18, missing Q6/D33/R123 + no DNP flags), `audit_v14.net`/`audit_netlist.xml` (fix_v14/fix_v12).
- **Refuted measurement**: ROUND9's "no fiducial within 5 mm of edge" — 5 of 6 fids are inside 5 mm.
- **Number reconciliations**: 61 (v18/v19 DRC) vs 48 (v16 baseline) — same checker, different revisions; 871 = 863+8 power-path vias; 875 PTH = 871+4 TH slots; 29 BOM-exclusion refs = J8+TP1–28 exactly.
