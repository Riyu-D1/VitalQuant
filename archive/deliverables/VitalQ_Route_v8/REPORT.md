# VitalQ Route v8 — Best-effort delivery report

## Status: UNFINISHED — NOT FOR FABRICATION

This is a research-prototype routing checkpoint, not a manufacturing release. The requested zero-open/zero-real-violation goal was **not achieved**. Routing stopped when R8 and R9 both failed to improve connectivity, as PROMPT.md requires. Cleanup did not reset that stop, and no hidden rerouting followed it.

Final PCB SHA-256: `2e72cea440d3ceca4b4397c833fa101f08e9898f24b6b74c597eff5b1def5eb5`. KiCad 10.0.4; six copper layers; 299 footprints; 1020 pads; unchanged 50 × 75 mm outline.

## Before and after

| Measurement | Refilled restart | Final |
|---|---:|---:|
| Unconnected items | 56 | 43 on 36 nets |
| Reported real copper/routing checks | 254 | 212 |
| Total native DRC violations | 340 | 298 |
| Accepted schematic-parity/BOM checks | 29 | 29, unchanged |
| Uncapped keepout item/area intersections | 260 | 248 |
| Distinct copper items intersecting keepouts | 154 | 142 |
| Dangling track/via checks | 27 / 18 | 2 / 2 |
| Pad copper outside the real outline | 6 fiducials | 0 |

The original Quilter report had 56 opens and 747 violations, but its stale fills and altered keepout-layer interpretation make that total unsuitable as the like-for-like restart comparison. Batches 1–3's 42-open stale-fill result was not accepted as truth. All later board checks refill zones.

KiCad caps keepout reporting near 199 entries. The final 212 reported real checks comprise 199 keepout, nine clearance, two dangling-track and two dangling-via checks. **212 is not the complete inventory.** The uncapped geometry screen finds 248 keepout intersections, and independent manufacturing checks below add further release blockers. These different screens overlap and must not be summed as distinct physical defects.

## Accepted changes and rejected candidates

| Batch | Accepted? | Opens before → after | Reported real before → after | Time / backup |
|---|---|---:|---:|---|
| R4 | yes | 56 → 53 | 254 → 250 | 2026-10-08T20:16:16.897011+01:00; 24s from checkpoint; `backups/20261008_201553_pre_R4` |
| R5 | yes | 53 → 48 | 250 → 247 | 2026-10-08T20:21:18.652774+01:00; 16s from checkpoint; `backups/20261008_202103_pre_R5` |
| R6 | yes | 48 → 46 | 247 → 243 | 2026-10-08T20:24:49.571627+01:00; 20s from checkpoint; `backups/20261008_202430_pre_R6` |
| R7 | yes | 46 → 43 | 243 → 243 | 2026-10-08T20:32:01.422221+01:00; 25s from checkpoint; `backups/20261008_203136_pre_R7` |
| M1 | yes | 43 → 43 | 243 → 243 | 2026-10-08T20:51:10.305250+01:00; 27s from checkpoint; `backups/20261008_205043_pre_M1_J8` |
| C1 | NO | 43 → 45 | 243 → 215 | 2026-10-08T21:08:04.171617+01:00; 167s from checkpoint; `backups/20261008_210517_pre_C1` |
| C2 | yes | 43 → 43 | 243 → 214 | 2026-10-08T21:13:50.042026+01:00; 223s from checkpoint; `backups/20261008_211007_pre_C2` |
| C3 | yes | 43 → 43 | 214 → 212 | 2026-10-08T21:20:42.131008+01:00; 187s from checkpoint; `backups/20261008_211735_pre_C3` |
| M2 | yes | 43 → 43 | 212 → 212 | 2026-10-08T21:25:41.920585+01:00; 19s from checkpoint; `backups/20261008_212523_pre_M2_FIDs` |
| M3 | yes | 43 → 43 | 212 → 212 | 2026-10-08T21:45:33.580698+01:00; 30s from checkpoint; `backups/20261008_214504_pre_M3_POFV` |

R8: AD5940_GPIO0, ADS1292_PWDN, AD5940_RESET, IR_GATE, TMP117_ALERT — all five searches failed; no copper applied. R9: +3V3_ANA, AFE_BG, PD2_INP, PD2_INM, TX1 — all five failed, including the narrowly authorized U6 POFV approach. The non-improvement streak reached two. Failure means no complete path found by that bounded approach, not a proof that every conceivable engineering redesign is impossible. Freerouting was not used.

- R4 relocated/reconnected three GND stitches; R5 recovered five +3V3 branches; R6 recovered two more. No original track rip-up or component motion in these batches.
- R7 completed CP_RX, CP_TX and SWEAT_WE electrically under the current project rules. CS_AD5940 and AIN4_LPF0 failed. Final manufacturing screening means these electrical gains are **not** a JLC manufacturing sign-off.
- M1 removed the old broad J8 notches and retained only the two authorized U6-ball exceptions below. Existing BGA via-in-pad process flags were made explicit. A small disconnected original keepout remainder is stored as a separate footprint rule-area component because KiCad otherwise omitted it on save. Saved/refilled geometry equality is checked.
- C1 was rejected: bulk dangling removal increased GND and AFE4900_RESETZ opens and broke one existing pad-connected group. The working board was never replaced by this candidate.
- C2 removed 25 demonstrated-unused tracks and six vias, preserving the GND and AFE4900_RESETZ candidates. C3 removed eight dead +3V3 tail segments at U18.8 and C59.1; their isolated pads remain present and explicitly open. Total non-routing cleanup: **33 segments and six vias**; zero copper additions. Both accepted cleanup candidates passed complete pad-connectivity preservation checks.
- M2 moved only FID1–FID6, hid their silk reference fields without changing identities, and changed the legacy surface-finish metadata from Lead-Free to ENIG. All other placements, board outline/slots, schematic, net assignments, board-zone geometry and project rules remained unchanged.

Historical pre-restart edits are preserved in PROGRESS.md and audit_resume.json: restoration of 36 hv_inner layer sets, the NPTH-contacting GND stub trim, six original-segment replacements/trims on GND/AFE_INP/ESP_TX/AFE4900_RESETZ, seven small gap bridges, and the earlier BGA/fanout trials. Unused U7 signal vias from unsuccessful trials were removed by C2. Candidate artifacts and rejected backups are retained and are not delivery-board alternatives.

Exact added paths/vias, removed UUIDs/geometries, and reasons are in `batch_*.json`. `cleanup_removal_ledger.json` records cumulative segment removals, all at or below ten per net. USB_DM_S3 is at nine; +3V3 at eight. No board-wide rip-up occurred.

## Authorized fiducial relocations

All measurements are millimetres in board coordinates; F = F.Cu, B = B.Cu. Each remains a 1 mm copper dot with a 2 mm mask opening. Entire copper/mask apertures are inside the board. The minimum measured center-to-other-copper distance is about 1.1097 mm. Component bodies and rule areas were excluded conservatively. These are the only component translations.

| Ref | Side | Before (x,y) | After (x,y) | Move distance |
|---|---|---|---|---:|
| FID1 | F | [148.595, -2.265] | [-0.407599, 1.884231] | 149.060359 |
| FID2 | F | [152.645, -2.265] | [27.734599, 14.815758] | 126.072838 |
| FID3 | F | [156.695, -2.265] | [43.659421, 61.703419] | 129.880717 |
| FID4 | B | [160.745, -2.265] | [3.06, -9.19] | 157.836988 |
| FID5 | F | [164.795, -2.265] | [5.110384, 59.94] | 171.372806 |
| FID6 | B | [168.845, -2.265] | [46.69, 63.19] | 138.586439 |

The user explicitly approved the >1 mm exception for these six fiducials. No passive nudge or locked-part movement occurred. R122 remains in its original on-board position. The outer contour is valid and unchanged, including all slots. Original Quilter identifiers for the five outline collisions and three dimension violations were not available in the audited artifacts; correcting the six outside fiducials is **not** asserted to prove all eight original findings resolved. Their precise mapping and final assembly/mechanical review remain blocked work.

## J8 / U6 permitted POFV exceptions

| U6 ball | Net | Via center (mm) | Diameter / drill | Required process |
|---|---|---|---|---|
| B4 | GND | [25.3, 50.2] | 0.30 / 0.15 mm | resin-filled, copper-capped, planar POFV |
| D1 | GND | [26.5, 51.0] | 0.30 / 0.15 mm | resin-filled, copper-capped, planar POFV |

The original no-via area is preserved everywhere except each actual approved annulus plus 0.002 mm geometric tolerance. No open/tented-only or between-ball via is permitted there. The ball-specific policy records a sealed surface component and no ordinary legal/reachable same-net via escape. See j8_pofv_policy.json and j8_permitted_vias.json for UUIDs, polygon evidence, ball identity and process flags. Both exceptions survive reload/refill and are validated by scripts/j8_pofv.py. CAD metadata does not order the fabrication process; POFV must be explicit in a future fabrication order.

## JLC capability set and additional manufacturing blockers

Source rechecked 2026-10-08: https://jlcpcb.com/capabilities/ .

| Capability / chosen constraint | Value |
|---|---|
| Multilayer 1 oz trace / space capability | 0.09 / 0.09 mm |
| Width / copper clearance used in routing | 0.1016 / 0.1016 mm |
| 2 oz multilayer trace / space | 0.15 / 0.15 mm; do not order this layout as 2 oz |
| Standard minimum via hole / diameter | 0.15 / 0.25 mm; 0.20 mm drill preferred |
| Plane-stitch / small-via dimensions used | 0.40/0.20 mm and 0.30/0.15 mm |
| Via hole-edge separation | 0.20 mm minimum, independently checked |
| Component pad-hole separation | 0.45 mm; also used conservatively for mixed hole pairs |
| Via hole-to-track / inner foreign copper | 0.20 mm; final screen below exposes deficiencies |
| General different-net SMD pad spacing | 0.15 mm; fine-pitch package approval cannot be inferred from trace/space capability |
| Small BGA pads | 0.20–0.25 mm requires ENIG; via-in-pad requires filled/plated-over processing |
| Copper-to-edge rule retained | 0.30 mm |
| NPTH / non-plated slot capability | 0.50 mm drill / 1.0 mm slot; local complex-slot machining still requires review |

**Additional DFM screen: 74 hole-to-copper observations across 20 small vias.** The 0.30/0.15 mm via annulus plus the inherited 0.1016 mm copper/antipad clearance does not guarantee 0.20 mm hole-to-foreign-copper clearance. 6 affected vias existed at restart and 14 were added in R7. Observations include planes and fixed copper and overlap across layers. They remain release blockers; native DRC's existing min_hole_clearance of 0.1016 mm does not certify JLC compliance.

The planning model now enforces the additional 0.20 mm via-hole constraint, with two new clearance regressions and a compressed-geometry test (15 total pass). The signal-batch applicator refuses further via batches under the insufficient inherited project hole rule. **No route was retried and no board rule was silently changed after the stop.** A future authorized routing session must first establish correct fabrication/custom rules, refill, and repair the resulting geometry; replaying old candidate plans is unsafe.

The conservative hole-pair screen also flags 1 mixed/component pair(s); all via-via pairs satisfy 0.20 mm. Detail/UUIDs and exact gaps are in geometry_audit_final.json. The nine native Q-footprint clearance checks remain real: a 0.1000 mm pad gap fails the 0.1016 mm project rule and must not be waved through merely because 0.09 mm etching is possible. JLC's general SMD-pad rule is a separate requirement.

M3 subsequently qualified the following three existing via-in-pad sites with filled/capped process metadata, with unchanged physical geometry and refilled DRC. The final audit has no detected via-in-pad instance lacking those flags. The process must still be ordered explicitly; the separate clearance/keepout/open blockers remain:

| Ref.pad | Net | Via center | Via UUID |
|---|---|---|---|
| R19.1 | +3V3 | [10.75, 42.4] | `3ca1cb3e-c285-48b0-955d-e1ba546680d5` |
| C59.2 | GND | [15.4, 39.9] | `4beaf43d-d10c-4e12-8c8f-5a21f2bb7b1b` |
| C89.2 | GND | [31.4, 54.5] | `9f2ec7eb-a007-4389-b724-daaf375a1dc0` |

The final files are intentionally not described as manufacturable. No Gerber/fabrication order was released, and no safety, sterility, medical-device or regulatory claim is made.

## Remaining real copper and cleanup work

The uncapped keepout inventory has 120 distinct tracks and 22 distinct vias across 41 nets. All entries, geometry and keepout UUIDs are in hv_uncapped_final.json. RLD_PAD alone has eleven offending original tracks, beyond the normal local-removal budget. HV rerouting was not attempted after the mandatory routing stop.

| Net | Offending tracks | Offending vias |
|---|---:|---:|
| `AFE4900_RESETZ` | 0 | 2 |
| `AFE_P_PAD` | 4 | 1 |
| `BIOZ_FP_PAD` | 0 | 1 |
| `CHG_DIS` | 4 | 0 |
| `CHG_PG` | 0 | 1 |
| `CHG_STAT` | 4 | 0 |
| `CS_AD5940` | 1 | 0 |
| `CS_ADS1292` | 1 | 0 |
| `CS_FLASH` | 7 | 0 |
| `ECG1_PAD` | 2 | 0 |
| `ECG2_PAD` | 2 | 0 |
| `EDA_CE_PAD` | 4 | 1 |
| `EDA_RE_PAD` | 3 | 1 |
| `EDA_SE_PAD` | 1 | 1 |
| `ESP_EN` | 5 | 2 |
| `ESP_IO0` | 5 | 0 |
| `ESP_RX` | 3 | 0 |
| `ESP_TX` | 2 | 0 |
| `EXP_INT` | 6 | 0 |
| `GND` | 0 | 2 |
| `I2C_SCL_1V8` | 0 | 1 |
| `J11_CE` | 1 | 1 |
| `J11_RE` | 2 | 1 |
| `J13_INM` | 0 | 1 |
| `J13_INP` | 0 | 1 |
| `LED1_K` | 7 | 0 |
| `LED2_K` | 5 | 1 |
| `LED3_K` | 10 | 0 |
| `LED_AN` | 4 | 0 |
| `MX_ECG_INM` | 0 | 1 |
| `PD_A` | 10 | 0 |
| `RE_SURGE` | 1 | 0 |
| `RLD_PAD` | 11 | 1 |
| `RTC_INT` | 5 | 0 |
| `RTC_VBK` | 2 | 0 |
| `SN_SURGE` | 1 | 0 |
| `SPI_MOSI` | 1 | 0 |
| `SWEAT_CE` | 2 | 0 |
| `SWEAT_RE` | 1 | 1 |
| `SWEAT_WE` | 2 | 1 |
| `TMP117_ALERT` | 1 | 0 |

The two required dangling-track and two dangling-via checks remain on GND/AFE4900_RESETZ. C1 demonstrated that indiscriminate removal worsens connectivity. They need actual route/junction repair, not deletion or suppression. The nine pad-clearance checks remain unchanged. Cosmetic/library findings (86 total) and all 29 accepted schematic-parity items remain documented separately in drc_final.json; no BOM flag or net assignment was changed.

## Exact blocked/uncompleted connections

All 43 final opens are listed below. 'Blocked' includes the global stop, not only exhausted individual nets. Full DRC endpoints, UUIDs, coordinates and attempt histories are in blocked_connections_final.json. Detailed original per-layer physical blockers for all 56 restart opens remain in analysis_refilled.json/.txt.

| ID | Net | Endpoint A | Endpoint B | Disposition |
|---:|---|---|---|---|
| 1 | `GND` | Via [GND] on Top Layer - Bottom Layer @ (6.829248, 62.871193) | R51.2 @ (3.669360, 63.265463) | Remaining HV-stranded branches: prior legal surface-stitch approach failed; global stop. |
| 2 | `GND` | Track [GND] on Top Layer, length 0.0017 mm @ (19.451354, 40.423699) | Via [GND] on Top Layer - Bottom Layer @ (18.398446, 39.860258) | Remaining HV-stranded branches: prior legal surface-stitch approach failed; global stop. |
| 3 | `+3V3_ANA` | U6.C4 @ (25.300000, 50.600000) | Track [+3V3_ANA] on Bottom Layer, length 0.2000 mm @ (25.300000, 49.800000) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 4 | `+3V3_ANA` | Track [+3V3_ANA] on Top Layer, length 0.2000 mm @ (37.785910, 12.280775) | Track [+3V3_ANA] on Top Layer, length 0.2000 mm @ (38.385910, 10.280775) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 5 | `CS_AFE4900` | Track [CS_AFE4900] on Top Layer, length 2.2346 mm @ (21.411852, 23.731030) | U6.E3 @ (25.700000, 51.400000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 6 | `+3V3` | C59.1 @ (15.647664, 40.989838) | Track [+3V3] on Top Layer, length 0.2848 mm @ (18.330105, 38.650623) | Remaining plane/sensor branches unresolved; U11 escape exhausted. Global stop prohibits further routing. |
| 7 | `+3V3` | U11.5 @ (19.150000, 50.400000) | U20.5 @ (21.250000, 50.400000) | Remaining plane/sensor branches unresolved; U11 escape exhausted. Global stop prohibits further routing. |
| 8 | `+3V3` | Zone '+3V3.F.Cu.0' [+3V3] on Top Layer, priority 0 @ (21.559698, 50.033449) | Zone '+3V3.LAYER_3' [+3V3] on Power Layer 4, priority 0 @ (47.071951, -10.494920) | Remaining plane/sensor branches unresolved; U11 escape exhausted. Global stop prohibits further routing. |
| 9 | `+3V3` | U18.8 @ (25.304594, 40.617033) | U18.7 @ (26.574594, 40.617033) | Remaining plane/sensor branches unresolved; U11 escape exhausted. Global stop prohibits further routing. |
| 10 | `CS_AD5940` | Track [CS_AD5940] on Layer 3, length 0.3481 mm @ (25.484709, 9.365275) | U7.F7 @ (36.585910, 10.280775) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 11 | `MISO_AFE` | U6.E2 @ (26.100000, 51.400000) | R90.1 @ (44.897430, 49.061869) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 12 | `VBIAS0` | C25.1 @ (33.536729, 9.612181) | U7.C7 @ (36.585910, 11.480775) | Historical U7 escape attempts exhausted; no further retry permitted. |
| 13 | `MAX86178_INT` | R113.2 @ (29.692752, 30.745931) | U22.B2 @ (26.500000, 47.350000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 14 | `BIOZ_FP` | R84.2 @ (24.814799, 25.267751) | U7.B3 @ (38.185910, 11.880775) | Historical U7 escape attempts exhausted; no further retry permitted. |
| 15 | `AIN4_LPF0` | U7.B4 @ (37.785910, 11.880775) | C26.1 @ (39.331875, 14.834525) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 16 | `BIOZ_SP` | C69.2 @ (36.085501, 18.694213) | U7.B5 @ (37.385910, 11.880775) | Historical U7 escape attempts exhausted; no further retry permitted. |
| 17 | `DE0` | R42.2 @ (-0.474207, 13.996713) | U7.B6 @ (36.985910, 11.880775) | Historical U7 escape attempts exhausted; no further retry permitted. |
| 18 | `VZERO0` | C24.1 @ (34.945910, 14.834525) | U7.B7 @ (36.585910, 11.880775) | Historical U7 escape attempts exhausted; no further retry permitted. |
| 19 | `BIOZ_FN` | C68.2 @ (31.699536, 19.866088) | U7.D2 @ (38.585910, 11.080775) | Historical U7 escape attempts exhausted; no further retry permitted. |
| 20 | `VREF_2V5` | C21.1 @ (33.536729, 12.541869) | U7.D7 @ (36.585910, 11.080775) | Historical U7 escape attempts exhausted; no further retry permitted. |
| 21 | `SPI_MOSI` | U6.F2 @ (26.100000, 51.800000) | U22.A4 @ (25.700000, 46.950000) | Prior U7 escape attempts exhausted; other branches halted by the global R8/R9 stop. |
| 22 | `SPI_MOSI` | U7.E7 @ (36.585910, 10.680775) | Track [SPI_MOSI] on Bottom Layer, length 1.9818 mm @ (33.077758, 8.064430) | Prior U7 escape attempts exhausted; other branches halted by the global R8/R9 stop. |
| 23 | `AD5940_RESET` | Track [AD5940_RESET] on Top Layer, length 0.2000 mm @ (38.985910, 10.280775) | U19.4 @ (16.487664, 42.585463) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 24 | `AD5940_GPIO0` | U1.18 @ (8.150000, 6.500000) | U7.F5 @ (37.385910, 10.280775) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 25 | `SPI_SCK` | U6.F3 @ (25.700000, 51.800000) | U22.A2 @ (26.500000, 46.950000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 26 | `SPI_SCK` | U18.6 @ (27.844594, 40.617033) | U22.A2 @ (26.500000, 46.950000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 27 | `ADS1292_PWDN` | R14.1 @ (31.247547, 9.016244) | U19.3 @ (16.887664, 42.585463) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 28 | `IR_GATE` | Track [IR_GATE] on Top Layer, length 0.2673 mm @ (3.005476, 49.885940) | U19.7 @ (15.837664, 43.635463) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 29 | `TMP117_ALERT` | R65.2 @ (13.610881, 32.948588) | U11.3 @ (21.250000, 51.050000) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 30 | `AFE_INM` | U6.A1 @ (26.500000, 49.800000) | Track [AFE_INM] on Top Layer, length 8.3911 mm @ (40.811200, 57.034301) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 31 | `AFE_BG` | U6.D2 @ (26.100000, 51.000000) | C15.1 @ (28.775559, 50.041869) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 32 | `PD_INP` | U16.1 @ (10.400000, 57.250000) | U6.A2 @ (26.100000, 49.800000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 33 | `PD_INM` | U16.2 @ (11.600000, 57.250000) | U6.A3 @ (25.700000, 49.800000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 34 | `PD2_INP` | U16.3 @ (12.800000, 57.250000) | U6.B2 @ (26.100000, 50.200000) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 35 | `TX1` | U16.6 @ (16.400000, 57.250000) | U6.D4 @ (25.300000, 51.000000) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 36 | `TX3` | U16.8 @ (15.200000, 54.750000) | U6.E4 @ (25.300000, 51.400000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 37 | `PD2_INM` | U16.10 @ (12.800000, 54.750000) | U6.B3 @ (25.700000, 50.200000) | No accepted complete path in the logged bounded approach; global stop after R8/R9. |
| 38 | `TX2` | U16.12 @ (10.400000, 54.750000) | U6.E5 @ (24.900000, 51.400000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 39 | `LED1_K` | U22.D1 @ (26.900000, 48.150000) | Track [LED1_K] on Layer 3, length 10.6096 mm @ (35.990099, 60.557234) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 40 | `LED2_K` | U22.C1 @ (26.900000, 47.750000) | Track [LED2_K] on Bottom Layer, length 5.0208 mm @ (43.244097, 49.200000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 41 | `LED3_K` | Track [LED3_K] on Layer 3, length 6.4684 mm @ (36.423009, 57.941372) | U22.B1 @ (26.900000, 47.350000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 42 | `AFE_N_PAD` | J5.5 @ (15.200000, 62.900000) | R36.1 @ (29.900000, 27.150000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |
| 43 | `unconnected-(J12-PadMP)` | J12.MP @ (-0.100000, 47.350000) | J12.MP @ (-0.100000, 57.650000) | Unfinished connection; no further attempt permitted after the global R8/R9 stop. |

J12 MP pads remain a real same-net open; they were not reclassified NC. Previously exhausted BIOZ/VREF/U7 and U11 escapes were not retried under renamed scripts or finer grids.

## Verification and artifacts

- Final native check: `kicad-cli pcb drc --refill-zones --save-board --all-track-errors --schematic-parity --format json -o drc_final.json vitalq_v2.kicad_pcb`.
- `audit_final.json`: protected invariants pass; pristine Quilter hash unchanged; project and schematic byte-identical to restart; only approved fiducials moved; through vias only; board outline/zone definitions unchanged from restart.
- `pad_connectivity_final.json`: all 359 pad-bearing restart connected groups preserved; zero regressions. The open count is independently confirmed by saved-board KiCad DRC.
- `.venv/bin/python scripts/test_route_geometry.py`: 15 tests pass, including filled-plane holes, keepout/via body checks, layer connectivity, exact route endpoints, narrow diagonal corridors, J8 ball/net policy, and JLC hole clearance.
- Exact geometry requires the existing Shapely/NumPy environment; pcbnew scripts use KiCad 10's bundled Python. Historical scripts and stale candidate plans are retained for audit, not a safe driver to replay against the final board.
- CLI renders exported successfully: renders/final_top.png, final_bottom.png, final_3d.png; plus final_top_copper.svg and final_bottom_copper.svg. Renders are visual review aids, not proof of complete component models or assembly fit.
- Dated board/project/schematic backups and per-batch DRC reports remain under backups/. No local project file was deleted. quilter_raw/ is retained locally but excluded from remote delivery.

## Delivery

Target: Riyu-D1/VitalQuant, branch vitalq-deliverables, directory deliverables/VitalQ_Route_v8/. Publication uses a separate worktree without switching the main checkout. Local virtualenvs, caches, editor locks and the delivery worktree itself are not deliverables; all source design, requested documentation, audit data, batch evidence, scripts and renders are retained. Generated geometry JSON files above 50 MiB are stored losslessly as `.json.gz` to avoid huge Git blobs; original local files are retained. Geometry readers support the compressed fallback, and each source/stored path and uncompressed SHA-256 is in delivery_manifest.json. `requirements-routing.txt` pins the tested analysis dependencies. The delivery workflow records a byte-for-byte (or decompressed-byte-equivalent) staging manifest and verifies the pushed head with git ls-remote and the delivered tree with git ls-tree. See the final session result for the authoritative pushed commit; this report does not invent a commit hash before commit creation.
