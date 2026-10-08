# Routing progress log

## Final closeout

**Routing stopped after R8/R9; zero-open/zero-real goal NOT achieved.** Final refilled result: **43 opens on 36 nets, 212 reported/capped real checks, 298 total native violations, 29 unchanged accepted parity items**. Uncapped inventory: **248 keepout intersections / 142 items**. The final JLC screen additionally exposes 74 small-via hole-to-copper observations across 20 vias and one conservative mixed-hole spacing flag. These remain release blockers, not waivers.

Accepted R4–R7 reduced opens 56 → 43. Accepted non-routing C2/C3 removed 33 unused tracks and six unused vias without pad-connectivity regression. M1 restored J8 with two approved U6 POFV exceptions; M2 placed all six fiducials on-board and specified ENIG; M3 qualified three further existing via-in-pad process sites without changing physical geometry. No other component translation or netlist change occurred. All 359 pad-bearing restart connected groups are preserved; 15 geometry/packaging tests pass.

REPORT.md lists every remaining open, HV inventory, clearance/dangling issue, manufacturing limitation, all six moves, both J8 exceptions, and the original Quilter issue-mapping limitation. Top/bottom/3D renders, final refilled DRC and hash/invariant audits are prepared for separate-worktree delivery. Historical records below are retained; provisional next-action entries do not override the final routing stop. Large generated geometry is losslessly compressed only in the delivery copy, with source hashes retained; no local design file is deleted.

## Acceptance rule

From the refilled restart onward every physical candidate and accepted batch runs KiCad DRC with `--refill-zones --save-board --schematic-parity`. Unconnected counts from stale fills are historical only. Real violations conservatively include clearance, keepouts, dangling copper, hole/edge/short/width/thermal/outline errors; cosmetic/library checks and the 29 accepted parity items are separate. Never claim completion from a generated path alone.

Batch | nets/connections | unconnected | real violations | time | disposition
---|---|---:|---:|---|---
Original | Quilter baseline | 56 stale-fill | not comparable | baseline file | 747 total; 29 accepted parity items
0 | HV layer intent / NPTH stub / via sizes | 56 stale-fill | not established | 2026-10-08 18:15 | 749 total; restored inner keepouts exposed real inner-copper conflicts
1 | GND U6/U7/J8/C43; local AFE_INP/ESP_TX/RESETZ jogs | 49 stale-fill | not established | 2026-10-08 18:47 | 753 total; J8 notch was not a validated mechanical fix
2 | SE_SURGE, CS_MAX86178, MISO_FL, EDA_RE_PAD, J11_RE, RLD_PAD, BIOZ_SP_PAD gap bridges | 42 stale-fill | not established | 2026-10-08 18:53 | 741 total; wrong-layer insertion corrected before this result
3 | +3V3_ANA bridge and four retained U7 signal vias | 42 stale-fill | not established | 2026-10-08 19:08 | 753 total; no open-count reduction, vias did not finish nets
Audit | All nets, zones refilled | 56 | 254 | 2026-10-08 19:56 | 340 total; 29 accepted parity items; 5 GND + 9 extra +3V3 branch opens exposed
Restart | Current board re-read, immutable and refilled checkpoints | 56 | 254 | 2026-10-08 19:59 | `drc_resume_refilled.json`; no placement/netlist changes; resume after user's renewed instruction
R4 planning | GND isolated HV branches: five first-approach cases | 56 unchanged | 254 unchanged | 2026-10-08 20:10 | Three local F-to-legal-stitch candidates; two geometrically failed; no copper applied yet

## Attempt ledger

Counts are not reset by changing scripts, grid resolution, or refilling zones. The previous thread did not maintain a reliable per-command counter, so the following conservatively preserves known exhausted searches. New refilled defects are explicitly identified separately from previously attempted escapes.

| Net / affected connection | Approaches already attempted | Status / next allowed action |
|---|---|---|
| BIOZ_FP, BIOZ_SP, BIOZ_FN, VREF_2V5 at U7 | Single-layer searches; destination via/three-via intersections; coarse island-chain and full-resolution leg searches (more than three searches occurred before this audit) | **BLOCKED: attempt limit reached.** No further normal-router retry. Candidate paths shared via coordinates and stopped short; none were applied. |
| +3V3 U11.5 escape | Direct B/F-zone access; north/south/west detours; via-in-pad/two-via L3 bridge | **BLOCKED: attempt limit reached.** Pads/slots/opposite-side copper prevent validated escape; keep sensor placement fixed. |
| DE0, VZERO0, VBIAS0 U7 via sites | Initial placement failed DRC; shifted/alternate sites investigated; unsafe vias removed | **BLOCKED pending review of exhausted prior placement history.** Exact candidate geometry alone is not permission to reset attempts. |
| SPI_MOSI U7.E7 | Initial and shifted via sites failed; VREF/GND/adjacent pad conflicts; via removed | **BLOCKED: no further retry of prior sites.** U6/U22 branch is a distinct open but same net's failed-history is retained. |
| CP_RX U5.25 | Prior F-only escape search failed using old raster | One failed approach known; legal via-in-pad/L3 is a distinct approach if validated. |
| GND refill-isolated via at (6.829248,62.871193) | R4 approach 1: local F route to new legal 0.4/0.2 plane stitch, no route within 4 mm window | 1 failed approach; next: direct F connection to existing grounded copper, not the same stitch scan. |
| GND refill-isolated via at (19.451354,40.422007) | R4 approach 1: same local F-to-new-stitch strategy failed | 1 failed approach; next: direct F connection to existing grounded copper. |
| GND refill-isolated branches at (2.748924,40.511387), (16.298583,40.430233), (32.233742,54.270815) | R4 approach 1: three geometric candidates found | Pending refilled DRC; not yet complete. |
| +3V3 refill-isolated branch at (24.520481,40.624680) | Initial local F-to-new-stitch plan failed | 1 failed approach; next: connect F copper to a neighboring same-net branch or existing legal stitch. |
| Other +3V3 refill-isolated branches | Eight provisional geometric plans, not applied | Revalidate against accepted GND copper; deduplicate same-net stitch sites. |

Two consecutive routing batches without an accepted reduction require stopping routing and producing the best-effort final report. This includes a named routing batch in which every search fails and no physical candidate can be produced. Diagnostics are not successful batches, and unrelated cleanup cannot reset the counter. R4–R7 improved connectivity. **R8 and R9 each produced no candidates on their five named nets: the non-improvement streak is 2, and ROUTING IS STOPPED.** The accepted M1 board has 43 refilled opens and 243 reported/capped real violations. R9's approved U6 POFV sites still did not yield a complete F/B/L3 path; no additional vias or partial paths were applied. Further HV rerouting is also blocked by this stop, not relabeled as cleanup. Only non-routing removal of demonstrably unused copper, the approved fiducial relocation, final verification/artifacts, and delivery proceed. Freerouting has not been used; if used at all it is limited to one named-blocked-net run with all existing routing fixed and a ten-minute timeout.

## Rip-up / restoration log

- Historical raw-to-restart comparison: six original segments replaced/trimmed; no parts removed, no placement changes, no pad/net changes. Exact before/after copper is recorded in `audit_resume.json`.
- GND: trimmed the U25 NPTH-contacting stub; AFE_INP and ESP_TX: local L3 jogs; AFE4900_RESETZ: local B dogleg for the U6 ground via. Historical abandoned stitch vias were removed and existing ground vias reused.
- J8: the historical footprint keepout was notched without mechanical validation. This remains an unresolved change, not a legitimate DRC waiver; restore the original polygon and account for its real via conflicts before claiming compliance.
- All later removals must list original UUIDs and reasons. Cumulative local rip-up limit: about ten existing segments per net. No board-wide rip-up.

## Updated approvals and report interpretation

- User approved ONLY filled, copper-capped, planar JLC POFV via-in-pad at U6 balls inside J8's area, where no other escape exists. Restore all other original J8 exclusion geometry and remove the broad old notches. No open/tented-only/between-ball exceptions. Log every allowed ball/coordinate/process in REPORT.md. This supersedes the earlier absence of mechanical authorization, not the general keepout rule.
- User approved relocating FID1–FID6 onto verified on-board sites, with all six moves logged. No other placement restriction changed.
- KiCad has a roughly 199-entry per-error-code reporting cap. `--all-track-errors` does not remove it. Restart uncapped scan: 260 item/area pairs, 154 unique items; after R4/R5: 252 pairs, 146 items. Historical totals of reported real violations are lower bounds while this cap is reached. Newly exposed old keepout reports remain counted, never waived.
- R7 planned signal group: CP_RX (distinct via-in-pad approach), CP_TX, CS_AD5940, AIN4_LPF0, SWEAT_WE. Use F/B/L3, exact through-via legality, no rip-up, and a five-millimetre endpoint corridor; record every attempt before search. Previously exhausted U7 nets remain blocked.

## Outstanding hard tasks

All remaining nets, the 126 distinct HV-keepout items, 27 dangling-track checks, 18 dangling-via checks, nine footprint clearance checks, the original Quilter outline/dimension identities, six out-of-outline FID pads, final renders/report/DRC, and remote delivery remain open. See the refilled worklist in PLAN.md and every endpoint/blocker in analysis_refilled.json. No fabrication-ready claim is justified.

- 2026-10-08T20:16:16.897011+01:00 **R4** GND: 56 → 53 refilled opens; 254 → 250 real violations; 336 total. NOT ACCEPTED. 3 original vias relocated/deduplicated, no original track segments removed, no nudges. Exact changes and DRC comparison: `batch_R4.json`; backup `backups/20261008_201553_pre_R4`.

- 2026-10-08T20:18:00.734116+01:00 **R4 ACCEPTED after delta audit**: 56 → 53 refilled opens; 254 → 250 real violations. 2 newly reported keepout entries concern unchanged old copper with unchanged keepout geometry; these remain counted, not waived. Exact UUID/geometry comparison in `batch_R4.json`. Both sides verified with `--all-track-errors --refill-zones`.

- 2026-10-08T20:21:18.652774+01:00 **R5** +3V3: 53 → 48 refilled opens; 250 → 247 real violations; 333 total. NOT ACCEPTED. 5 original vias relocated/deduplicated, no original track segments removed, no nudges. Exact changes and DRC comparison: `batch_R5.json`; backup `backups/20261008_202103_pre_R5`.

- 2026-10-08T20:22:04.964475+01:00 **R5 ACCEPTED after delta audit**: 53 → 48 refilled opens; 250 → 247 real violations. 7 newly reported keepout entries concern unchanged old copper with unchanged keepout geometry; these remain counted, not waived. Exact UUID/geometry comparison in `batch_R5.json`. Both sides verified with `--all-track-errors --refill-zones`.

- 2026-10-08T20:24:49.571627+01:00 **R6** +3V3: 48 → 46 refilled opens; 247 → 243 real violations; 329 total. ACCEPTED. 2 original vias relocated/deduplicated, no original track segments removed, no nudges. Exact changes and DRC comparison: `batch_R6.json`; backup `backups/20261008_202430_pre_R6`.

- 2026-10-08T20:28:36.261224+01:00 R7 planning: **CP_RX**, attempt 2/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R7 CP_RX geometric result: {"date": "2026-10-08T20:28:36.261224+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "candidate", "batch": "R7", "track_count": 26, "via_count": 4}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:28:59.145893+01:00 R7 planning: **CP_TX**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R7 CP_TX geometric result: {"date": "2026-10-08T20:28:59.145893+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "candidate", "batch": "R7", "track_count": 48, "via_count": 4}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:29:53.279843+01:00 R7 planning: **CS_AD5940**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R7 CS_AD5940 geometric result: {"date": "2026-10-08T20:29:53.279843+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R7", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 39 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:30:30.440109+01:00 R7 planning: **AIN4_LPF0**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R7 AIN4_LPF0 geometric result: {"date": "2026-10-08T20:30:30.440109+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R7", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 21 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:30:51.820794+01:00 R7 planning: **SWEAT_WE**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R7 SWEAT_WE geometric result: {"date": "2026-10-08T20:30:51.820794+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "candidate", "batch": "R7", "track_count": 140, "via_count": 6}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:32:01.422221+01:00 **R7 ACCEPTED**, CP_RX, CP_TX, SWEAT_WE: 46 → 43 refilled opens; 243 → 243 reported real violations (keepout count capped); 329 total; new real faults 0; open regressions {}. No original copper removed, no nudges. Detailed paths/vias and checkpoint: `batch_R7.json`.

- 2026-10-08T20:34:38.656690+01:00 R8 planning: **AD5940_GPIO0**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R8 AD5940_GPIO0 geometric result: {"date": "2026-10-08T20:34:38.656690+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R8", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 38 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:34:55.829074+01:00 R8 planning: **ADS1292_PWDN**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R8 ADS1292_PWDN geometric result: {"date": "2026-10-08T20:34:55.829074+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R8", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 411 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:35:14.864876+01:00 R8 planning: **AD5940_RESET**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R8 AD5940_RESET geometric result: {"date": "2026-10-08T20:35:14.864876+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R8", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 414 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:35:34.163392+01:00 R8 planning: **IR_GATE**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R8 IR_GATE geometric result: {"date": "2026-10-08T20:35:34.163392+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R8", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 205 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:35:50.505471+01:00 R8 planning: **TMP117_ALERT**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R8 TMP117_ALERT geometric result: {"date": "2026-10-08T20:35:50.505471+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R8", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 144 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:51:10.305250+01:00 **M1 ACCEPTED — J8 restoration / POFV metadata**: 43 refilled opens; 243 reported real violations. Original J8 area rebuilt with only 2 precisely bounded authorized U6-ball exceptions; all broad old notches removed. 11 existing BGA via-in-pad sites explicitly marked filled and copper-capped. No copper/placement/net changes. `batch_M1.json`, `j8_permitted_vias.json`. This cleanup does not reset the routing non-improvement counter.

- 2026-10-08T20:56:04.181957+01:00 R9 planning: **+3V3_ANA**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R9 +3V3_ANA geometric result: {"date": "2026-10-08T20:56:04.181957+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R9", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 25 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:56:26.729625+01:00 R9 planning: **AFE_BG**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R9 AFE_BG geometric result: {"date": "2026-10-08T20:56:26.729625+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R9", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 554 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:56:46.500988+01:00 R9 planning: **PD2_INP**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R9 PD2_INP geometric result: {"date": "2026-10-08T20:56:46.500988+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R9", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 56988 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:57:05.304445+01:00 R9 planning: **PD2_INM**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R9 PD2_INM geometric result: {"date": "2026-10-08T20:57:05.304445+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R9", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 49584 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T20:57:23.902261+01:00 R9 planning: **TX1**, attempt 1/3: Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor.
- R9 TX1 geometric result: {"date": "2026-10-08T20:57:23.902261+01:00", "approach": "Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor", "status": "failed", "batch": "R9", "reason": "No F/B/L3 path within 5mm endpoint corridor; expanded 38866 cells"}. Candidate is not an accepted connection until refilled DRC passes.

- 2026-10-08T21:08:04.171617+01:00 **C1 non-routing cleanup REJECTED**: 43 → 45 refilled opens; 243 → 215 reported real violations; dangling checks 35 → 7. Removed 35 DRC-identified unused copper items; no copper added. Full pad-component connectivity preservation: False. Remaining/newly exposed leaves stay logged; local per-net rip-up limits enforced. `batch_C1.json`. Routing remains stopped.

- 2026-10-08T21:13:50.042026+01:00 **C2 non-routing cleanup ACCEPTED**: 43 → 43 refilled opens; 243 → 214 reported real violations; dangling checks 35 → 6. Removed 31 DRC-identified unused copper items; no copper added. Full pad-component connectivity preservation: True. Remaining/newly exposed leaves stay logged; local per-net rip-up limits enforced. `batch_C2.json`. Routing remains stopped.

- 2026-10-08T21:20:42.131008+01:00 **C3 non-routing cleanup ACCEPTED**: 43 → 43 refilled opens; 214 → 212 reported real violations; dangling checks 6 → 4. Removed 8 DRC-identified unused copper items; no copper added. Full pad-component connectivity preservation: True. Remaining/newly exposed leaves stay logged; local per-net rip-up limits enforced. `batch_C3.json`. Routing remains stopped.

- 2026-10-08T21:25:41.920585+01:00 **M2 ACCEPTED — authorized FID1–FID6 relocation**: 43 refilled opens, 212 reported real violations. Six 1mm fiducials / 2mm mask apertures moved to optically and mechanically clear on-board sites; reference fields hidden, identifiers/net assignments retained. All other footprints, routing, outline, zones and parity unchanged. Exact six translations in `batch_M2.json`. Surface-finish metadata corrected from Lead-Free to ENIG for JLC's 0.20–0.25mm BGA requirement; stackup dimensions unchanged. Routing remains stopped.

- 2026-10-08T21:45:33.580698+01:00 **M3 ACCEPTED — process metadata only**: existing via-in-pad at R19.1, C59.2 and C89.2 marked filled/copper-capped POFV. No geometry, placement, net, rule or connection changed. Refilled result remains 43 opens / 212 reported real / 298 total / 29 parity. These are outside J8; its two exceptions are unchanged. `batch_M3.json`. Routing stays stopped.
