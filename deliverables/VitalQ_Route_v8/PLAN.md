# VitalQ Route v8 — Routing Plan

## Final execution disposition

**ROUTING STOPPED after non-improving R8 and R9.** The delivered checkpoint has 43 refilled opens, 212 reported/capped real checks, and 298 total native violations. The uncapped screen has 248 keepout intersections across 142 copper items. Further HV rerouting is not relabeled as cleanup to bypass the mandatory stop.

R4–R7 recovered 13 connections. Non-routing C2/C3 removed 33 unused segments and six unused vias while preserving all existing pad-connected groups. M1 restored J8 except the two authorized U6 POFV sites; M2 relocated all six approved fiducials and specified ENIG. The board remains **NOT FOR FABRICATION**: nine pad-clearance checks, four required dangling checks, the open/keepout inventory, and additional JLC DFM findings remain. REPORT.md and final_result.json contain the final disposition and exact blocked list.

The final JLC screen adds 74 hole-to-copper observations across 20 small vias, one conservative mixed-hole spacing flag, while three initially unspecified via-in-pad process sites were subsequently qualified by metadata-only M3. The inherited project hole clearance is insufficient for a 0.20 mm fabrication requirement. The geometry helper now enforces that stronger constraint; the signal applicator refuses additional via batches under the insufficient rule. No board rules or routes were changed after the stop. A future expressly authorized continuation must establish correct manufacturing/custom rules and refill before routing; old plans must not be replayed. Fifteen geometry/packaging tests pass.

## JLCPCB 6-layer capability numbers used

- Min trace width / space: **0.09 / 0.09 mm** (3.5/3.5 mil)
- Min via (multilayer): **hole 0.15 mm, diameter 0.25 mm**; preferred hole 0.2 mm
- Via-in-pad: **supported on 6-layer** (resin fill + plate over)
- Min BGA pad: **0.20 mm; 0.20–0.25 mm requires ENIG**; drill range 0.15–6.30 mm
- Min via hole-to-hole: **0.20 mm**; component pad-hole spacing: **0.45 mm**; copper-to-edge handled by 0.30 mm rule
- Via hole-to-track / inner foreign copper: **0.20 mm**; generic different-net SMD pad spacing: **0.15 mm**. These are separate from minimum trace/space.
- Board rules in use: clearance 0.1016, track 0.1016, via 0.4/0.2 default —
  **plus a new fanout via size 0.3/0.15 for 0.4 mm BGA via-in-pad**
  (still ≥ JLC minimums; required because no dogbone channel exists at 0.4 mm pitch).

## Refilled execution plan — supersedes the speculative batch schedule below

### Manufacturing recheck (2026-10-08)

Source: https://jlcpcb.com/capabilities/ . Multilayer 1 oz trace/space: 0.09/0.09 mm; 2 oz requires 0.15/0.15 mm. Standard multilayer via hole/pad: 0.15/0.25 mm minimum, 0.2 mm drill preferred. Via hole-edge spacing: **0.20 mm**, component pad-hole spacing: **0.45 mm**. Current general capability lists BGA pads down to 0.20 mm with **ENIG required for 0.20–0.25 mm pads**; the earlier blanket 0.25 mm statement was incomplete. Via-in-pad requires filled/plated-over manufacture, not ordinary tenting. At restart the board said Lead-Free and lacked explicit via-fill flags. M1/M2 qualified the limited J8 sites and set ENIG; metadata-only M3 subsequently qualified the three other detected via-in-pad sites. All detected via-in-pad sites now have explicit filled/capped flags. Manufacturing release requires the correct order specification and resolution of the final DFM audit, not an assumption.

Use the existing 0.1016 mm width/clearance and 0.30 mm edge rule, 0.40/0.20 mm plane stitches, and 0.30/0.15 mm BGA vias only where their full six-layer geometry is legal. The existing project hole-to-hole rule is only 0.1016 mm; enforce the stricter 0.20 mm independently (current audit found zero via-via pairs below 0.20 mm). Do not relax rules or ignore DRC severities. Blind/buried/microvias are not enabled and are not a workaround.

### Explicit user approvals received during the refilled restart

- **J8 limited POFV exception:** restore the original TC2030 exclusion except for filled-and-copper-capped, planar via-in-pad inside U6 ball pads, only where no other U6 escape exists. No open, tented-only, or between-ball vias are permitted in that area. Remove the broad historical notches. Enforce the exception per ball/coordinate/process, and list every permitted via in REPORT.md. This is not approval for a general no-via-area relaxation.
- **FID1–FID6 relocation:** the user authorizes moving only these fiducials onto checked clear board locations, beyond the normal 1 mm passive nudge limit. Log all six before/after positions and preserve their identities/net assignments.

### DRC reporting-cap correction

KiCad's DRC engine has a per-error-code cap (`ERROR_LIMIT=199`; see https://docs.kicad.org/doxygen/drc__engine_8cpp.html). `--all-track-errors` does not lift that cap. The restart's 200 keepout entries are a partial report: an uncapped exact-polygon scan finds **260 item/area intersections across 154 distinct copper items** (`hv_uncapped_restart.json`). After R4/R5 that scan falls to **252 intersections / 146 items**. Use uncapped geometry comparisons alongside every refilled DRC; a near-200 count must never be described as the complete remaining inventory. RLD_PAD has 11 offending original tracks; any full repair must be reviewed against the approximately ten-segment local limit.

### Acceptance and attempt accounting

1. Save a dated PCB/project/schematic checkpoint before each physical batch. Work on a separate candidate; do not overwrite a user edit or the pristine board.
2. Use actual filled polygons and copper connectivity from `geometry_refilled.json`; `analysis_refilled.json` / `.txt` enumerate all 56 restart opens, both endpoints, per-layer blockers, and candidate via coordinates.
3. Plan routes against foreign solid copper, hole spacing, actual slots, board/footprint keepouts, and previous accepted new copper. Pours may recalculate antipads, but every accepted candidate must retain required plane connectivity after refill. Prefer surface/L3 signal routing; do not slit GND planes as a shortcut.
4. Exact route endpoints must contact copper/via annuli; no proximity-radius success. No shared via between different nets. Layer IDs are KiCad IDs, not router array indices.
5. Every candidate and accepted batch uses `kicad-cli pcb drc --refill-zones --save-board --schematic-parity --format json ...` with its matching project/schematic. Compare actual opens and real violations, not total report size. Accept only a connectivity improvement without new substantive violations, or a cleanup that reduces real violations without worsening connectivity. Verify geometry/invariants and hashes before copying an accepted candidate to the working board.
6. Preserve the three-attempt limit and the two consecutive non-improving batch stop. The earlier candidate-only searches are not successful routes. Already exhausted U7 nets and the previously exhausted U11 escape stay BLOCKED; do not restart their old searches under a different script name. Newly revealed plane disconnections are distinct repair defects, not retries of the sensor escape.

### Ordered worklist (IDs refer to `analysis_refilled.json`)

| IDs / nets | Class | Predicted route and limits |
|---|---|---|
| 1–5 GND | PLANE | Relocate the five existing HV-forbidden stitch vias to main GND fill outside keepouts, preserving outer-layer branches. Attempt 1: local F.Cu path to 0.4/0.2 stitch; attempt 2: F.Cu bridge directly to existing grounded copper; attempt 3: separately justified boundary-side detour. No plane/thermal-rule relaxation. |
| 9–12,15–19 +3V3 | PLANE | Same pattern into the surviving In3.Cu +3V3 fill. Plan separately from GND and regenerate obstacles between batches. |
| 13–14 +3V3 sensor | HARD / BLOCKED | U11.5 has no validated escape within existing slots/pads/keepouts; prior multiple approaches exhausted. U20 top-pour-to-power-plane stitch may be repaired separately if its own legal surface access is demonstrated. |
| 6–7 +3V3_ANA | HARD | U6.C4 is under J8's via exclusion; no blind-via workaround. U7 F-row stub is shadowed by bottom MISO_AD. Inspect physical fanout blockers, and permit only a logged local reroute (≤10 existing segments) if a legal site emerges. |
| 24,26,30,31 BIOZ_FP/SP/FN,VREF_2V5 | HARD / BLOCKED | Retained U7 vias are not complete connections. Prior single-layer, via-chain, and coarse-island searches exhausted the limit; reject the unvalidated shared-via/short-endpoint candidates. |
| 22,27,28 VBIAS0,DE0,VZERO0; 33 SPI_MOSI | HARD / prior failures | Preserve prior failed-placement history. Exact-polygon analysis may locate candidate sites but does not reset the attempt budget; no automatic reapplication of removed sites. |
| 20 CS_AD5940; 25 AIN4_LPF0; 29 SWEAT_WE; 35 AD5940_GPIO0 | HARD | Legal F-pad fanout → L3/B route → same-net component. Evaluate local blockers from the exact table before any copper edits; if sealed, use only a distinct bounded local reroute, otherwise BLOCKED. |
| 8 CS_AFE4900;21 MISO_AFE;23 MAX86178_INT;44 AFE_BG | HARD | U6/U22 B-side pad → legal via (outside J8 exclusion where applicable) → L3 → existing destination copper. Locked BGA pads may not move. |
| 32 SPI_MOSI;36–37 SPI_SCK | HARD | Use existing B-side stubs for escape; shortest B/L3 connection with validated end vias. U22 interior escape may require a local foreign-track reroute; never erase pads to open a channel. |
| 47–50 PD2_INP,TX1,TX3,PD2_INM;52–54 LED1/2/3_K | HARD | B-side escape or legal via-in-pad → L3/B route → existing U16/LED copper. Preserve all sensor placement and true slot clearances. |
| 38–39 CP_RX,CP_TX | HARD | Actual U5 pad polygons permit investigating 0.3/0.15 via-in-pad; F→L3→F near R72/R73, then B detour if needed. Do not reuse the old sealed-pad raster result as proof. Respect prior CP_RX search history. |
| 34 AD5940_RESET;40 ADS1292_PWDN;41 IR_GATE | MEDIUM/HARD | U19 F escape along permitted surface, transition outside HV keepout, then L3/B to existing net. No via in the HV area. |
| 42 TMP117_ALERT | HARD | B-side U11 escape and physical FR4 bridge only; true slot polygon and edge clearance are mandatory. |
| 43 AFE_INM;45–46 PD_INP/INM;51 TX2 | MEDIUM | Extend existing B-side stubs, route around actual slots using B/L3 only where allowed, terminate on component copper. |
| 55 AFE_N_PAD | HARD | Long B-side/J5 surface corridor with transitions only outside HV exclusions; keep all electrodes/connectors fixed. |
| 56 J12.MP | MEDIUM | Existing same-net MP pads may be joined on B around connector pads if physically legal; no net reassignment or DRC exclusion. |

First candidate: `plan_R4_GND.json` proposes three validated geometric F-surface stitch paths for the isolated GND branches at (2.749,40.511), (16.299,40.430), and (32.234,54.271). The other two branches failed this first geometric approach and are not silently retried. Proposed +3V3 plans are provisional until the GND edits are included in the obstacle model. Same-net duplicate stitch targets must be deduplicated.

### Real-violation and mechanical cleanup

- The 200 keepout entries are 126 distinct items, including vias. Group by net and contiguous offending span; log each original UUID removed and enforce ≤10 existing segments per net cumulatively. For each eligible span, use boundary vias outside all inner exclusions and a surface route; preserve the existing network at both ends. Nets requiring more than the local limit are BLOCKED, not wholesale rerouted.
- Restore the original J8 no-via polygon before claiming compliance. The two prior ground sites inside it need valid replacement routing; do not retain unexplained notches as a fix.
- Refilled dangling tracks/vias are connected if needed or removed only when proven redundant; no deletion to hide an open.
- Keep the nine Q-footprint clearance checks visible until resolved without unauthorized footprint/netlist modification. A JLC-capable gap is not zero KiCad DRC.
- Use the actual closed outline, not the earlier bounding-box estimates. R122 is not outside the audited outline. FID1–FID6 have copper outside; their panel-fiducial intent and the original five collision/three dimension issue identities are still unverified. Do not move locked parts or invent issue mappings.
- Final DRC, renders, invariant audit, REPORT.md, and separate-worktree delivery are required even if limits leave blocked work.

## Historical Batch 0 — prep (no net connections; enables everything)

1. Fix the 36 `hv_inner` rule areas: restrict to inner layers (G2, L3, P4, G5)
   — matches the human-authored design intent; removes ~199 `items_not_allowed`
   artifacts. Top-layer copper in those areas is by design.
2. Delete/trim the GND stub touching U25's NPTH (hole_clearance, 1 item).
3. Add project/board min-via allowance 0.3 mm dia / 0.15 mm hole for BGA
   via-in-pad (documented; within JLC).
4. DRC + backup. Expected: violations 747 → ~350.

## Batch order: PLANE → HARD → MEDIUM → EASY

### B1 — GND plane stitching (~8 connects)

| net | endpoints | action |
|---|---|---|
| GND | J8.2 (25.18,52.565,F) | F track → via @ (25.70,52.90) → G2/G5 pour (site verified clear) |
| GND | U6 B4,D5,D1 (B, 0.4 BGA) | 3× via-in-pad 0.3/0.15 → pour connects |
| GND | U7 C4,D5,E6 (F, 0.4 BGA) | 3× via-in-pad → pour connects |
| GND | C43.2 (F) | F track → via @ (38.40,15.85) → G2/G5 |
| GND | stub↔stub (0.6 mm gap, F) | bridge/extend stub to via site |

### B2 — power pours (+3V3, +3V3_ANA, VBAT)

| net | endpoints | action |
|---|---|---|
| +3V3 | U11.5 (B) ↔ U20.5 (F), d=2.1 | trace pair across FR4 bridge (X 18.3–20.7, Y 51.7–52.7) exactly like existing I2C/GND routes |
| +3V3 | F islands ↔ P4 pour | stitching vias at island edges (locate: ~2 small F pours) |
| +3V3_ANA | U6.C4 (B) | via-in-pad → L3/P4 run to +3V3_ANA fragment |
| +3V3_ANA | track↔track 2.09 mm | L3/B bridge |

### B3–B6 — BGA via-in-pad fanouts (HARD)

Template per sealed pad: **0.3/0.15 via at pad center** → annular ring on L3
(signal) or B (local) → inner-layer run to net fragment or second via-in-pad
at the far pad. All via sites verified against `viaok` grid + top-side copper.

| net | via-in-pad ends | inner route |
|---|---|---|
| SPI_MOSI | U6.F2 + U22.A4 | L3 run d≈4.9 |
| SPI_SCK | U6.F3 + U22.A2 | L3 run d≈4.9 (+1 stub join) |
| MISO_AFE | U6.E2 | L3 run to R90.1 area → via up |
| CS_AFE4900 | U6.E3 | L3 run to F.Cu stub → via |
| AFE_INM | U6.A1 | L3 run to F stub |
| AFE_BG | U6.D2 | L3 run to C15.1 → via |
| PD_INP/INM | U6.A2/A3 | B-side run to U16.1/2 (U16 free) |
| PD2_INP/INM | U6.B2/B3 | B-side run to U16.3/10 |
| TX1/TX3 | U6.D4/E4 | B-side run to U16.6/8 |
| LED1/2/3_K | U22.D1/C1/B1 | L3 run to LED stubs |
| MAX86178_INT | U22.B2 | L3 run to F stub → via |
| CS_MAX86178 | stub↔stub B | B bridge |
| U7 signals (F BGA): CS_AD5940 F7, SPI_MOSI E7, AD5940_GPIO0 F5, VREF_2V5 D7, AIN4_LPF0 B4, SWEAT_WE C5, VBIAS0 C7, BIOZ_FP B3, BIOZ_SP B5, DE0 B6, VZERO0 B7, BIOZ_FN D2 | ~12 via-in-pad | L3/B runs to respective stubs; several ends (C21,C24,C25,C26,R84,C68,C69,R42,U1) are free on F — short final hop |

### B7 — U5 QFN (HARD)

| net | endpoints | action |
|---|---|---|
| CP_RX | U5.25 (F) → R72.2 (F), d=12 | **Attempt 1:** via-in-pad 0.3/0.15 under pad → L3 run north → via up near R72. **Attempt 2 (if blocked):** rip up ≤4 segs of USB_DM/DP/ESP_EN, escape on F, restore. **Attempt 3:** detour via B.Cu around U5 east edge. |
| CP_TX | U5.26 (F) → R73.1 (F), d=8.8 | same pattern |

### B8 — sensor island + stragglers (MEDIUM/EASY)

| net | action |
|---|---|
| TMP117_ALERT | stub → across FR4 bridge → U11.3 |
| U19 pads (ADS1292_PWDN p3, AD5940_RESET p4, IR_GATE p7) | SOT23-6-like 0.5-pitch pads, sealed ends — inspect, via-dogbone on F or rip-up |
| J12.MP↔MP (d=10.3) | B.Cu trace between shield pads |
| J5.5 AFE_N_PAD↔R36.1 (d=38.7, B) | B.Cu/L3 run along bottom |
| remaining stub↔stub gaps (MISO_FL 1.68, BIOZ_SP_PAD 1.14, EDA_RE_PAD 0.43, SE_SURGE 1.45, J11_RE 1.52, RLD_PAD 1.31, TX2 14.9, SWEAT_WE 29, CS pairs) | same-layer bridges or short via pairs; grid-router assisted |

## Rules during routing

- Batches ≈5 nets; dated backup + DRC + PROGRESS.md line after each.
- ≤10 rip-up segments per net, each logged with reason.
- Max 3 attempts per net, each a different approach; then BLOCKED.
- Stop after 2 consecutive non-improving batches → final report.
- Freerouting: last resort only, named blocked nets, locked tracks, single 10-min run.

## Phase 3 (after routing)

- Zone refill with correct project rules → clears ~410 stale-fill violations.
- Delete dead dangling stubs (keep the ones consumed by routes).
- Verify R122.1 vs real Edge.Cuts; nudge ≤1 mm if outside; log.
- Fix 3 copper slivers.
- Final DRC → `drc_final.json`; renders top/bottom/3D → `renders/`.
- `REPORT.md`; push to `vitalq-deliverables` via worktree (no quilter_raw/).
