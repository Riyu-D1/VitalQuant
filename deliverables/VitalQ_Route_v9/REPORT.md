# VitalQ v9 — stopped routing checkpoint

## UNFINISHED — NOT FOR FABRICATION

**Zero unconnected was not achieved.** The final board has **43 unconnected items**. B1 and B2 each failed to reduce that count, triggering the mandatory stop. No routing, U19 relocation, BGA rip-up, HV reroute, or cleanup-by-rerouting was performed after the stop. This is a bounded-attempt outcome, not proof that six-layer routing is impossible.

The only accepted physical changes are the five requested oversized-via reductions, at unchanged coordinates. The B2 U18.7 track/via candidate passed no-regression checks but closed no reported open; it was rejected as unnecessary added copper. It remains in candidates/B2 for inspection, not as a delivery alternative.

Final PCB SHA-256: `0e39582da92d1a0d7a8469529c6715c0fc2fe7262560839b913961585bfc0e93`. KiCad 10.0.4; 299 footprints; 1020 pads; 7813 track segments; 682 through vias; six copper layers; ENIG.

## Before and after

| Measurement | Original under inherited rules | JLC-rule baseline B0 | Final |
|---|---:|---:|---:|
| Unconnected items | 43 | 43 | 43 |
| Reported real violations (capped) | 213 | 253 | 253 |
| All native violations | 299 | 339 | 339 |
| Schematic-parity findings, separate | 29 | 29 | 29 |

The 40-count increase versus inherited rules exposes existing hole-clearance problems under the requested 0.20 mm rules. It is not newly routed damage. The comparable B0-to-final real count is unchanged. Library/silk/text findings total 86 and are separated from the real count; the unchanged 29 parity findings are not waived or edited.

Final real breakdown:

| Native type | Count |
|---|---:|
| items_not_allowed | 200 |
| hole_clearance | 40 |
| clearance | 9 |
| track_dangling | 2 |
| via_dangling | 2 |

Uncapped scan: **249 item/keepout pairs across 143 distinct items**, or 345 item/area/layer intersections. Native keepout reporting is capped near 200; it is not a complete inventory. Independent all-via/all-layer hole-to-foreign-copper screening finds **36 observations across 10 vias**; the 0.20 mm drilled-hole-pair screen finds **0**. These screens overlap native findings and must not be added together as unique defects. v8's 74 observations used older fills/rules and a different screen; do not claim 74 repairs from this comparison.

## Accepted changes

### JLC rules and via presets

- Track width and copper clearance remain 0.1016 mm globally and in every netclass; never reduced to waive the nine 0.1000 mm SOT-23 pad gaps.
- Project hole-clearance and hole-to-hole minima increased from 0.1016 to 0.20 mm.
- Added vitalq_v2.kicad_dru: via hole-to-foreign-copper >=0.20 mm; mechanical hole edge separation >=0.20 mm; blind/buried/micro vias prohibited. The hole-to-hole rule is net-independent. Same-net attached annuli/tracks necessarily connect to holes and are not foreign-copper clearance cases.
- Added exactly two presets: 0.25/0.15 mm BGA POFV and 0.40/0.20 mm ordinary vias.
- Minimum via diameter 0.30 -> 0.25 mm and annular ring 0.075 -> 0.05 mm, solely to implement the explicitly requested 0.25/0.15 geometry. Drill minimum remains 0.15 mm. No requested JLC limit was relaxed.
- Copper-edge/slot clearance remains 0.30 mm; six-layer stackup and ENIG finish remain unchanged; no rule areas were edited, shrunk, or newly excepted.

### Five physical via edits

| Net | Position (mm), unchanged | Before diameter/hole | After diameter/hole | UUID |
|---|---|---|---|---|
| +3V3 | (31.250851, 59.690000) | 0.60/0.30 mm | 0.40/0.20 mm | d622aca2-8d9b-4898-bed1-51d5d39bd211 |
| IOVDD | (44.000900, -9.210447) | 0.60/0.30 mm | 0.40/0.20 mm | 064d0850-e21a-41bb-957d-2808efe641f0 |
| IOVDD | (38.455600, 8.483600) | 0.60/0.30 mm | 0.40/0.20 mm | 1730f0aa-b758-44ee-a5e8-3eda4926ae9f |
| LED2_K | (1.729158, 51.050952) | 0.60/0.30 mm | 0.40/0.20 mm | 05c621f5-5c36-44c0-8538-0ec5c3e6ebb5 |
| LED2_K | (36.708030, 58.412753) | 0.60/0.30 mm | 0.40/0.20 mm | c4597e39-a925-49d1-8bb9-5e789cae1cff |

All five sites passed full-layer foreign-copper, drill, keepout, and edge screening after resizing. No relocation was needed. Complete connected-pad-group preservation was checked after refill.

### Moves, nudges and rip-up

- Component moves: **none**, including U19 and U18.
- Passive nudges: **none**.
- Accepted removed segments or vias: **none**; accepted added segments or vias: **none**.
- The authorized U6/U22 rip-up exception was never used because the global stop occurred first.
- No part, footprint/pad identity, net assignment, schematic, connector, electrode, mounting hole, rule area, outline, or power-pour definition changed.
- Full before/after UUID geometry is in design_diff_final.json; change_ledger.json includes all accepted edits and the rejected B2 addition.

## Batch and attempt outcomes

| Batch | Disposition | Opens | Real | Evidence |
|---|---|---:|---:|---|
| B0 | Rules established; saved/refilled baseline | 43 | 253 | drc_B0.json |
| B1 | Five via reductions accepted; LED detour failed | 43 | 253 | candidates/B1/{applied,decision,audit,drc}.json |
| B2 | Local route candidate rejected; global stop reached | 43 | 253 | candidates/B2/{applied,decision,audit,drc}.json |

Every batch has its own dated backup and exactly one PROGRESS.md result line. v8 history was read before planning and before each B2 net strategy; no exhausted v8 search was rerun merely on a finer grid. The v9 attempt labels are session labels, not erasure of the v8 failure history. No net reached three new v9 attempts because the global stop occurred first. Freerouting was not used.

**LED2_K:** the saved board has 16 In1.Cu segments totaling 48.154235 mm, including the cited 10.130743 mm segment (UUID 94820769-c7cc-43a1-a955-c79294772607). B1 considered an In2 replacement of that segment with nearest legal stitches within +/-0.6 mm of both ends. Neither endpoint yielded a legal site with an In1 joining stub. No route was applied and all 16 original segments remain on the ground layer. The wider chain also exceeds the ordinary ten-segment rip-up budget; no broader exception was assumed.

**B2 local outcomes:**

| Connection | Outcome |
|---|---|
| R9.2 -> R51.2 GND | No F-only detour in x2.5..9, y60..64.15 without violating foreign-copper/edge clearance. Original copper retained. |
| U19/D30 GND -> (18.398446,39.860258) | No F-only detour in x17.5..20.7, y39.1..44. Original copper retained. |
| C59.1 | Suggested (14.90,39.95) annulus overlaps hv_inner. The distinct B2 site (14.85,39.85) conflicts with I2C_SCL via 7958787c-9ee9-4aea-94c5-94b0b03579c3: hole-to-foreign-copper about 0.101366 mm, below 0.20 mm. No via added. |
| U18.7 | Candidate at (26.65,40.15), with one F track, passed DRC/no-regression audit but did not reduce 43 opens. Rejected, not in final board. |
| U18.8 | No legal independent north-side via in the tested x24.55..25.55, y39.25..40.05 site window. No path placed. |
| U20.5 / U11.5 | Bridge site (20.05,52.2) is clear, but neither the F U20 path nor B U11 path completed inside the tested slot/copper-clearance window. No partial feed was retained. |
| TMP117_ALERT | Distinct bridge site (19.5,52.2) conflicts with I2C_SDA via and In2 I2C_SCL tracks; no bridge route placed. |
| J12 MP pair | Lower MP center (-0.1,57.65) has an F.Cu FSR_ADC track conflict. Two exact-pad-center vias / In2 strategy not applied. |

The geometry planner uses a conservative 0.002 mm construction margin and exact continuous checks after 0.04 mm surface-grid searches. A failed bounded search is not an exhaustive proof of no legal manual route. Candidate and diagnostic site scans within one strategy were not used to restart net attempt counters. All reported remaining opens stay real.

## Unfinished ordered work

- LED2_K ground-plane removal remains blocked as above.
- Local GND, C59, U18.8, sensor bridge, TMP117_ALERT, J12 MP and U19 relocation/reconnection remain unfinished.
- U19 movement was **not attempted** before the global stop; no legal fully reconnected new location is claimed.
- U7 exact POFV routing was **not attempted**. Read-only current-board site checks found more blockers than the initial survey described; table below records them.
- U6/U22 window rip-up/fanout/restoration and In3 fallback were **not attempted**. No claim that six layers are impossible and no 8-layer conversion.
- AFE_N_PAD and the remaining long DE0/AD5940_GPIO0 runs were **not attempted**.
- HV keepout rerouting, small-via-spacing repair, dangling copper repair and SOT-23 pad-gap repair remain unfinished. Cleanup was not used to evade the stop. Moving whole SOT-23 footprints cannot change their internal pad-to-pad spacing; no footprint geometry or rule relaxation was applied.

### U7 read-only site findings (not routing attempts)

| Pad | Net | Exact-center 0.25/0.15 site finding |
|---|---|---|
| B3 | BIOZ_FP | No fixed-copper/keepout/edge/drill blocker in this screen; connectivity route not attempted |
| B4 | AIN4_LPF0 | vias GND 768c7b5d-5bc9-44f9-b413-cd420896d223 |
| B5 | BIOZ_SP | vias SWEAT_WE 45c2c533-ae59-4204-90b9-9b0bf394ff14 |
| B6 | DE0 | tracks SWEAT_WE a81f634e-c62b-46f9-9999-f1598e83b47e; tracks SWEAT_WE 537b036c-795a-4a36-8ad9-39af5b540ba5 |
| B7 | VZERO0 | tracks RE0 1addcfb2-a074-438b-8a95-5ae442b91257 |
| C7 | VBIAS0 | No fixed-copper/keepout/edge/drill blocker in this screen; connectivity route not attempted |
| D2 | BIOZ_FN | No fixed-copper/keepout/edge/drill blocker in this screen; connectivity route not attempted |
| D7 | VREF_2V5 | No fixed-copper/keepout/edge/drill blocker in this screen; connectivity route not attempted |
| E7 | SPI_MOSI | vias GND 14e9177f-095f-4f9f-a518-ab46ec4b1629 |
| F2 | +3V3_ANA | tracks MISO_AD 81f3e2ff-d789-4fbf-9fcd-f88375baf23b |
| F3 | +3V3_ANA | tracks MISO_AD 81f3e2ff-d789-4fbf-9fcd-f88375baf23b |
| F5 | AD5940_GPIO0 | tracks MISO_AD 81f3e2ff-d789-4fbf-9fcd-f88375baf23b |
| F7 | CS_AD5940 | tracks MISO_AD 81f3e2ff-d789-4fbf-9fcd-f88375baf23b |

### Exact final opens

Every row is BLOCKED by the global stop. Local failures and inherited v8 reasons are retained in blocked_connections_final.json; no net is reclassified NC.

| ID | Net | Endpoint A | Endpoint B |
|---:|---|---|---|
| 1 | GND | Track [GND] on Top Layer, length 0.5400 mm @ (6.829248,62.871193) | Pad 2 [GND] of R51 on Top Layer @ (3.669360,63.265463) |
| 2 | GND | Via [GND] on Top Layer - Bottom Layer @ (18.398446,39.860258) | Track [GND] on Top Layer, length 0.0017 mm @ (19.451354,40.423699) |
| 3 | +3V3_ANA | Pad C4 [+3V3_ANA] of U6 on Bottom Layer @ (25.300000,50.600000) | Track [+3V3_ANA] on Bottom Layer, length 0.2000 mm @ (25.300000,49.800000) |
| 4 | +3V3_ANA | Track [+3V3_ANA] on Top Layer, length 0.2000 mm @ (37.785910,12.280775) | Track [+3V3_ANA] on Top Layer, length 0.2000 mm @ (38.385910,10.280775) |
| 5 | CS_AFE4900 | Track [CS_AFE4900] on Top Layer, length 0.4623 mm @ (20.949590,23.731030) | Pad E3 [CS_AFE4900] of U6 on Bottom Layer @ (25.700000,51.400000) |
| 6 | +3V3 | Pad 1 [+3V3] of C59 on Top Layer @ (15.647664,40.989838) | Track [+3V3] on Top Layer, length 0.2848 mm @ (18.330105,38.650623) |
| 7 | +3V3 | Pad 5 [+3V3] of U20 on Top Layer @ (21.250000,50.400000) | Pad 5 [+3V3] of U11 on Bottom Layer @ (19.150000,50.400000) |
| 8 | +3V3 | Zone '+3V3.F.Cu.0' [+3V3] on Top Layer, priority 0 @ (21.559698,50.033449) | Zone '+3V3.LAYER_3' [+3V3] on Power Layer 4, priority 0 @ (47.071951,-10.494920) |
| 9 | +3V3 | Pad 7 [+3V3] of U18 on Top Layer @ (26.574594,40.617033) | Pad 8 [+3V3] of U18 on Top Layer @ (25.304594,40.617033) |
| 10 | CS_AD5940 | Track [CS_AD5940] on Layer 3, length 4.5116 mm @ (25.604204,9.692224) | Pad F7 [CS_AD5940] of U7 on Top Layer @ (36.585910,10.280775) |
| 11 | MISO_AFE | Pad E2 [MISO_AFE] of U6 on Bottom Layer @ (26.100000,51.400000) | Pad 1 [MISO_AFE] of R90 on Top Layer @ (44.897430,49.061869) |
| 12 | VBIAS0 | Pad 1 [VBIAS0] of C25 on Top Layer @ (33.536729,9.612181) | Pad C7 [VBIAS0] of U7 on Top Layer @ (36.585910,11.480775) |
| 13 | MAX86178_INT | Pad 2 [MAX86178_INT] of R113 on Top Layer @ (29.692752,30.745931) | Pad B2 [MAX86178_INT] of U22 on Bottom Layer @ (26.500000,47.350000) |
| 14 | BIOZ_FP | Pad 2 [BIOZ_FP] of R84 on Top Layer @ (24.814799,25.267751) | Pad B3 [BIOZ_FP] of U7 on Top Layer @ (38.185910,11.880775) |
| 15 | AIN4_LPF0 | Pad B4 [AIN4_LPF0] of U7 on Top Layer @ (37.785910,11.880775) | Pad 1 [AIN4_LPF0] of C26 on Top Layer @ (39.331875,14.834525) |
| 16 | BIOZ_SP | Pad 2 [BIOZ_SP] of C69 on Top Layer @ (36.085501,18.694213) | Pad B5 [BIOZ_SP] of U7 on Top Layer @ (37.385910,11.880775) |
| 17 | DE0 | Pad 2 [DE0] of R42 on Top Layer @ (-0.474207,13.996713) | Pad B6 [DE0] of U7 on Top Layer @ (36.985910,11.880775) |
| 18 | VZERO0 | Pad 1 [VZERO0] of C24 on Top Layer @ (34.945910,14.834525) | Pad B7 [VZERO0] of U7 on Top Layer @ (36.585910,11.880775) |
| 19 | BIOZ_FN | Pad 2 [BIOZ_FN] of C68 on Top Layer @ (31.699536,19.866088) | Pad D2 [BIOZ_FN] of U7 on Top Layer @ (38.585910,11.080775) |
| 20 | VREF_2V5 | Pad 1 [VREF_2V5] of C21 on Top Layer @ (33.536729,12.541869) | Pad D7 [VREF_2V5] of U7 on Top Layer @ (36.585910,11.080775) |
| 21 | SPI_MOSI | Pad F2 [SPI_MOSI] of U6 on Bottom Layer @ (26.100000,51.800000) | Pad A4 [SPI_MOSI] of U22 on Bottom Layer @ (25.700000,46.950000) |
| 22 | SPI_MOSI | Pad E7 [SPI_MOSI] of U7 on Top Layer @ (36.585910,10.680775) | Track [SPI_MOSI] on Bottom Layer, length 1.9818 mm @ (33.077758,8.064430) |
| 23 | AD5940_RESET | Pad F1 [AD5940_RESET] of U7 on Top Layer @ (38.985910,10.280775) | Pad 4 [AD5940_RESET] of U19 on Top Layer @ (16.487664,42.585463) |
| 24 | AD5940_GPIO0 | Pad 18 [AD5940_GPIO0] of U1 on Top Layer @ (8.150000,6.500000) | Pad F5 [AD5940_GPIO0] of U7 on Top Layer @ (37.385910,10.280775) |
| 25 | SPI_SCK | Pad F3 [SPI_SCK] of U6 on Bottom Layer @ (25.700000,51.800000) | Pad A2 [SPI_SCK] of U22 on Bottom Layer @ (26.500000,46.950000) |
| 26 | SPI_SCK | Pad 6 [SPI_SCK] of U18 on Top Layer @ (27.844594,40.617033) | Pad A2 [SPI_SCK] of U22 on Bottom Layer @ (26.500000,46.950000) |
| 27 | ADS1292_PWDN | Track [ADS1292_PWDN] on Top Layer, length 0.3209 mm @ (31.247547,8.695386) | Pad 3 [ADS1292_PWDN] of U19 on Top Layer @ (16.887664,42.585463) |
| 28 | IR_GATE | Track [IR_GATE] on Top Layer, length 0.1847 mm @ (3.005476,49.618643) | Pad 7 [IR_GATE] of U19 on Top Layer @ (15.837664,43.635463) |
| 29 | TMP117_ALERT | Track [TMP117_ALERT] on Top Layer, length 0.2709 mm @ (13.610881,32.948588) | Pad 3 [TMP117_ALERT] of U11 on Bottom Layer @ (21.250000,51.050000) |
| 30 | AFE_INM | Pad A1 [AFE_INM] of U6 on Bottom Layer @ (26.500000,49.800000) | Track [AFE_INM] on Top Layer, length 8.3911 mm @ (40.811200,57.034301) |
| 31 | AFE_BG | Pad D2 [AFE_BG] of U6 on Bottom Layer @ (26.100000,51.000000) | Pad 1 [AFE_BG] of C15 on Top Layer @ (28.775559,50.041869) |
| 32 | PD_INP | Pad 1 [PD_INP] of U16 on Bottom Layer @ (10.400000,57.250000) | Pad A2 [PD_INP] of U6 on Bottom Layer @ (26.100000,49.800000) |
| 33 | PD_INM | Pad 2 [PD_INM] of U16 on Bottom Layer @ (11.600000,57.250000) | Pad A3 [PD_INM] of U6 on Bottom Layer @ (25.700000,49.800000) |
| 34 | PD2_INP | Pad 3 [PD2_INP] of U16 on Bottom Layer @ (12.800000,57.250000) | Pad B2 [PD2_INP] of U6 on Bottom Layer @ (26.100000,50.200000) |
| 35 | TX1 | Pad 6 [TX1] of U16 on Bottom Layer @ (16.400000,57.250000) | Pad D4 [TX1] of U6 on Bottom Layer @ (25.300000,51.000000) |
| 36 | TX3 | Pad 8 [TX3] of U16 on Bottom Layer @ (15.200000,54.750000) | Pad E4 [TX3] of U6 on Bottom Layer @ (25.300000,51.400000) |
| 37 | PD2_INM | Pad 10 [PD2_INM] of U16 on Bottom Layer @ (12.800000,54.750000) | Pad B3 [PD2_INM] of U6 on Bottom Layer @ (25.700000,50.200000) |
| 38 | TX2 | Pad 12 [TX2] of U16 on Bottom Layer @ (10.400000,54.750000) | Pad E5 [TX2] of U6 on Bottom Layer @ (24.900000,51.400000) |
| 39 | LED1_K | Pad D1 [LED1_K] of U22 on Bottom Layer @ (26.900000,48.150000) | Track [LED1_K] on Layer 3, length 10.6096 mm @ (35.990099,60.557234) |
| 40 | LED2_K | Pad C1 [LED2_K] of U22 on Bottom Layer @ (26.900000,47.750000) | Track [LED2_K] on Ground Layer 2, length 10.1307 mm @ (19.272879,45.639149) |
| 41 | LED3_K | Track [LED3_K] on Layer 3, length 1.2800 mm @ (29.954604,57.941372) | Pad B1 [LED3_K] of U22 on Bottom Layer @ (26.900000,47.350000) |
| 42 | AFE_N_PAD | Pad 5 [AFE_N_PAD] of J5 on Bottom Layer @ (15.200000,62.900000) | Pad 1 [AFE_N_PAD] of R36 on Bottom Layer @ (29.900000,27.150000) |
| 43 | unconnected-(J12-PadMP) | Pad MP [unconnected-(J12-PadMP)] of J12 on Bottom Layer @ (-0.100000,47.350000) | Pad MP [unconnected-(J12-PadMP)] of J12 on Bottom Layer @ (-0.100000,57.650000) |

## Keepout inventory by net

| Net | Distinct tracks | Distinct vias |
|---|---:|---:|
| AFE4900_RESETZ | 0 | 2 |
| AFE_P_PAD | 4 | 1 |
| BIOZ_FP_PAD | 0 | 1 |
| CHG_DIS | 4 | 0 |
| CHG_PG | 0 | 1 |
| CHG_STAT | 4 | 0 |
| CS_AD5940 | 1 | 0 |
| CS_ADS1292 | 1 | 0 |
| CS_FLASH | 7 | 0 |
| ECG1_PAD | 2 | 0 |
| ECG2_PAD | 2 | 0 |
| EDA_CE_PAD | 4 | 1 |
| EDA_RE_PAD | 3 | 1 |
| EDA_SE_PAD | 1 | 1 |
| ESP_EN | 5 | 2 |
| ESP_IO0 | 5 | 0 |
| ESP_RX | 3 | 0 |
| ESP_TX | 2 | 0 |
| EXP_INT | 6 | 0 |
| GND | 0 | 2 |
| I2C_SCL_1V8 | 0 | 1 |
| J11_CE | 1 | 1 |
| J11_RE | 2 | 1 |
| J13_INM | 0 | 1 |
| J13_INP | 0 | 1 |
| LED1_K | 7 | 0 |
| LED2_K | 6 | 1 |
| LED3_K | 10 | 0 |
| LED_AN | 4 | 0 |
| MX_ECG_INM | 0 | 1 |
| PD_A | 10 | 0 |
| RE_SURGE | 1 | 0 |
| RLD_PAD | 11 | 1 |
| RTC_INT | 5 | 0 |
| RTC_VBK | 2 | 0 |
| SN_SURGE | 1 | 0 |
| SPI_MOSI | 1 | 0 |
| SWEAT_CE | 2 | 0 |
| SWEAT_RE | 1 | 1 |
| SWEAT_WE | 2 | 1 |
| TMP117_ALERT | 1 | 0 |

## Via-in-pad and fabrication notes

Source rechecked: https://jlcpcb.com/capabilities/pcb-capabilities (2026-10-09). Use the 6-layer / multilayer 1 oz capability basis, not the looser BGA fanout limits mentioned on the capability page.

- Six layers, ENIG; minimum track/gap requested >=0.09 mm, actual project minimum 0.1016 mm.
- Through vias only; minimum 0.15 mm holes / 0.25 mm diameter. Ordinary new-via preset 0.40/0.20 mm.
- Via hole-to-foreign-copper and hole-to-hole >=0.20 mm. All remaining findings must be resolved before fabrication.
- Order **epoxy-filled, copper-capped, planar POFV** for every via-in-pad. JLC states this is the default for six layers and above; confirm the actual order process and any small-hole surcharge. CAD flags alone are not an order instruction.
- Existing legitimate larger vias are retained; the presets do not certify or automatically convert every existing via.
- No fabrication/Gerber release, assembly sign-off, HV safety, medical-device, sterility, or regulatory compliance is claimed.

All 12 detected surface pad/via overlaps are listed below. All have existing filled/capped metadata; no new via-in-pad was accepted in v9. Full UUID/process inventory: geometry_audit_final.json.

| Pad | Net | Via center (mm) | Diameter/hole (mm) | Process |
|---|---|---|---|---|
| U7.D5 | GND | (37.385900,11.080800) | 0.300/0.150 | Existing filled + capped; order epoxy POFV |
| U7.E6 | GND | (36.985900,10.680800) | 0.300/0.150 | Existing filled + capped; order epoxy POFV |
| U6.D5 | GND | (24.900000,51.000000) | 0.300/0.150 | Existing filled + capped; order epoxy POFV |
| C59.2 | GND | (15.400000,39.900000) | 0.400/0.200 | Existing filled + capped; order epoxy POFV |
| U6.B4 | GND | (25.300000,50.200000) | 0.300/0.150 | Existing filled + capped; order epoxy POFV |
| U6.D1 | GND | (26.500000,51.000000) | 0.300/0.150 | Existing filled + capped; order epoxy POFV |
| U7.C4 | GND | (37.785900,11.480800) | 0.300/0.150 | Existing filled + capped; order epoxy POFV |
| C89.2 | GND | (31.400000,54.500000) | 0.400/0.200 | Existing filled + capped; order epoxy POFV |
| R19.1 | +3V3 | (10.750000,42.400000) | 0.400/0.200 | Existing filled + capped; order epoxy POFV |
| U7.C5 | SWEAT_WE | (37.376454,11.490246) | 0.300/0.150 | Existing filled + capped; order epoxy POFV |
| U5.25 | CP_RX | (12.400000,13.900000) | 0.300/0.150 | Existing filled + capped; order epoxy POFV |
| U5.26 | CP_TX | (11.900000,13.900000) | 0.300/0.150 | Existing filled + capped; order epoxy POFV |

## Verification and deliverables

- Final command: `kicad-cli pcb drc --refill-zones --save-board --all-track-errors --schematic-parity --format json -o drc_final.json vitalq_v2.kicad_pcb`, with matching project/custom rules present. Saved fills, not stale fills, are used.
- audit_final.json: 175 original multi-pad connected groups checked; **zero regressions**, zero new keepout intersections, zero new independent hole-clearance/pair findings. All footprint positions, sides, locks, pad geometry/net assignments, rule areas, outline and zone definitions are identical to B0.
- design_diff_final.json directly compares the final board to the unmodified 10:19 source backup: all parts/pads/tracks/rule areas unchanged, exactly five via size changes, schematic byte-identical, six layers. Hashes bind this report to the saved final board.
- Five geometry regression tests pass (`.venv` reference interpreter with `-B scripts/test_geometry.py`): all-layer via-hole clearance, 0.4 mm BGA pitch, same-net hole spacing, complete-via keepout intersection, exact route endpoints. These tests validate helpers, not fabrication readiness.
- renders/: final_top.png, final_bottom.png, final_3d.png, final_top_copper.svg, final_bottom_copper.svg. PNGs visually reviewed. Renders are aids only; some footprint models are absent and they do not prove assembly fit or electrical completion.
- The initial isometric CLI argument was rejected; corrected to separate positive-angle arguments. Both failure and successful retry logs remain in renders/, along with the manifest. No board change was needed.
- Dated source/rules/pre-batch/final backups remain in backups/. Rejected candidates and diagnostics remain in this folder. No files deleted, no git commit/push, no worktrees, no modifications to v8.
- PLAN.md records the rules and ordered plan; PROGRESS.md records one result line per batch; state.json records the enforced stop. Batch planners/applicator refuse replay or post-stop routing.

**To resume requires an explicit new user decision about the stop/attempt policy and the remaining blocked work. Do not rerun the failed strategies or treat the candidate files as approved boards.**


# R2 phase (structural fix + 8-layer re-route) — status final

## NOT FABRICATION-READY
Final refilled DRC (`drc_final_R2.json`, saved board, --refill-zones --schematic-parity): **8 unconnected**,
**212 real violations** {'items_not_allowed': 201, 'hole_clearance': 4, 'npth_inside_courtyard': 1, 'track_dangling': 6}, 29 accepted schematic-parity items, presentation warnings separate.
Start of R2: 43 unconnected / 253 real (6-layer). The board is an engineering checkpoint; fab outputs in `fab/` are for review/quote only.

## Structural changes (all logged in PROGRESS.md, state_R2.json, candidates/*/ledger.json)
- R2-1 restack S/G/S/S/P/S (6L); R2-28/29/30 (supervisor order, user-authorised) **8 copper layers** F / In1 GND / In2 / In3 / In4 +3V3+VBAT_SYS /
  In5 / In6 GND / B; In6 GND plane = copy of the In1 outline; all rule areas that covered In1-In4 extended to In5/In6 (shapes unchanged).
- Moves (supervisor-authorised): **U6 (AFE4900, locked) +1.5 mm east**, rot 180 unchanged (the +2.5 mm order put the F-row balls on the J8
  Tag-Connect NPTH); **U18 dx -0.31 mm** (frees U22 C1/D1 POFV); **U11 (locked TMP117) rotated 180° in place** so U11.5 sits under same-net U20.5
  (POFV +3V3 stitch) and U11.3 gets a 0.5 mm dog-bone via. No parts added/removed; netlist identical; skin cluster kept; HV rule areas untouched.
- J8 Tag-Connect no-via area: per-via notches (annulus + 0.002 mm) only for U6 ball POFVs (SUPERVISOR_R2_05); everything else in J8 restored.
- Rules: board minimum clearance 0.09 mm (= JLC minimum) used only by a custom intra-footprint pad-gap rule (SOT-23 pads 0.100 mm apart);
  netclass 0.1016 mm unchanged; via hole-to-copper / hole-to-hole 0.2 mm; through vias only.
- Via-in-pad: 49 filled+capped vias (U7, U6, U22 ball vias at 0.25/0.15, U11/U20 stitch) — **POFV mandatory**, list in `pofv_vias_R2.json`.

## Batches accepted/adopted in R2
R2-1, R2-3, R2-10, R2-12, R2-15, R2-16, R2-17c, R2-17d, R2-18, R2-30, R2-31, R2-32, R2-34, R2-35, R2-37, R2-39, R2-40, R2-42, R2-43, R2-46, R2-48

## Remaining opens
- Track [+3V3] on Top Layer, length 0.2848 mm | Pad 1 [+3V3] of C59 on Top Layer
- Pad 6 [I2C_SDA] of U11 on Bottom Layer | Pad 6 [I2C_SDA] of U20 on Top Layer
- Pad E2 [MISO_AFE] of U6 on Bottom Layer | Pad 1 [MISO_AFE] of R90 on Top Layer
- Track [AD5940_RESET] on Top Layer, length 0.2000 mm | Pad 4 [AD5940_RESET] of U19 on Top Layer
- Pad 1 [ADS1292_PWDN] of R14 on Top Layer | Pad 3 [ADS1292_PWDN] of U19 on Top Layer
- Track [IR_GATE] on Top Layer, length 0.1847 mm | Pad 7 [IR_GATE] of U19 on Top Layer
- Pad 10 [PD2_INM] of U16 on Bottom Layer | Pad B3 [PD2_INM] of U6 on Bottom Layer
- Pad C5 [PD_A] of U22 on Bottom Layer | Track [PD_A] on Layer 3, length 1.5709 mm

## Remaining real violations
- items_not_allowed: copper inside keepout rule areas (mostly inherited HV / hv_ownlayer / hole_keepout hits; rule areas are never edited).
- hole_clearance / dangling / npth_inside_courtyard: listed in drc_final_R2.json.

## Process notes
- The 3-attempt rule was overridden by the supervisor for the U6 wall and reset for the 8-layer board; voided/aborted runs are archived in
  archive/aborted_tool_errors with reasons in state_R2.json.
- Tool fixes made during R2: rule-file guard for every KiCad job, exact JLC via check (pad holes to via copper), per-route 60 s budget,
  dangling-repair cascade cap, corridor/lane reservations for BGA escapes.
