# VitalQ fix_v21: route ALL 41 remaining U1 (ESP32-S3-WROOM-1-N16R8) connections, with NO excuses

You are in a fresh window. Earlier windows (fix_v20, fix_v20_A/B/C) did not finish. **Do not touch those folders.** Work only in
`~/Downloads/VitalQ_Hand/fix_v21/`. Never touch `fix_v19` (the order-ready fallback). **Delete nothing**: move stale files to `fix_v21/_stale/`.
No firmware changes. No ordering, pricing or contacting anyone.

There are **no time caps and no pass caps**. Keep going until the done criteria at the bottom are all true. You are **forbidden** to declare done, write "COMPLETE", or stop with "remaining nets for later" while DRC is not 0/0/0.

## 0. Look at the pictures first (open them with your image viewer/Read tool, really look)
All in `fix_v21/_prompt_pics/` (y grows DOWNWARDS = south, units mm, same coordinates as the board file):
- `PLAN_overview.png`: every suggested lane on every layer, plus the 41 ratsnest lines (lime). Colours: magenta L6 (In5.Cu), orange L4 (In3.Cu), cyan L3 (In2.Cu), blue Bottom (B.Cu).
- `U1zoom_<layer>.png` for F.Cu, In1.Cu…In6.Cu, B.Cu: existing copper on that layer around U1 (tracks orange, GND tracks grey-green, vias grey, pads yellow=U1 / blue=other), the cyan dashed U1 body, and the magenta suggested lanes for that layer. `U1zoom_F.Cu.png` and `U1zoom_ALL_ratsnest_top.png` also show the ratsnest with net labels.
- `render_top.png`, `render_bottom.png`: 3D renders. `layer_<L>.svg.png`: full-board plots per layer.
- Re-render after every batch of ~5 nets so you can compare: `$KP _work/dump_geom.py vitalq_v2.kicad_pcb _work/geom_now.json && python3 _work/draw_pics.py _work/geom_now.json _prompt_pics/now` (mkdir `_prompt_pics/now` first; `$KP` = `/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3`, which has `pcbnew`). Look at the new images and check for crossings before carrying on. If the pictures and this file disagree (e.g. bottom-family lane x values), THIS FILE wins. Edit the `P` dict in `draw_pics.py` if you change a lane, so the picture matches the plan.

## 1. Start state (reviewer-checked 20 Oct 13:20 BST)
- `fix_v21/vitalq_v2.kicad_pcb` = copy of `fix_v20/_work/base_fixed4.kicad_pcb`. Pristine copy: `fix_v21/_work/start_reference.kicad_pcb`.
- DRC (error severity, v19 rules): **1 violation, 41 unconnected, 0 shorts**. The 1 violation: clearance 0.090 mm (needs 0.1016) between **C64 pad 2 (GND) at (1.60, −10.82)** and **U1 pad 4 (LSM6_INT1) at (2.75, −10.95)**. Fix it first: nudge C64 west/north by 0.05–0.2 mm (or rotate it) and re-attach its two tracks. That is allowed.
- The other trial boards were checked too: fix_v20 promoted board 7 viol/40 unconn, fix_v20_A 3/46, B and C unchanged 1/41, B's Freerouted full board 25 viol (4 shorts)/16 unconn. So base_fixed4 is the best start.
- The 41 links are listed in `_work/gb/unconnected41.txt` and in section 4 below.

## 2. How to run DRC (every single time)
```
cd ~/Downloads/VitalQ_Hand/fix_v21
cp ../fix_v19/vitalq_v2.kicad_pro .            # Save() overwrites it, so ALWAYS re-copy
cp ../fix_v19/vitalq_v2.kicad_dru .
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb drc --severity-error --format json -o _work/drc_now.json vitalq_v2.kicad_pcb
```
Count `violations`, `unconnected_items`, and violations of type `shorting_items`. Target: **0 / 0 / 0**.
Rules (v19): min track 0.1016 mm, clearance 0.1016 mm (aim 0.127), min via 0.25/0.15 mm (use 0.45/0.2 where it fits, 0.35/0.2 when tight), hole clearance 0.2 mm, copper-to-edge 0.3 mm. Signals 0.127 mm, +3V3_ESP and GND 0.25 mm.

## 3. What you are ALLOWED to change (the surroundings are NOT locked any more)
You have full authority to make room. Specifically you MAY:
- **Rip up and reroute any non-U1 track or via within about 10 mm of U1** (box x −8 to 31, y −20 to 13), on any layer, including the already-routed USB_DP/DM, IN1P_AC, ECG_P, TS, VBAT, ESP_EN, ESP_TX segments, as long as each ripped net is reconnected and DRC is clean at the end.
- **Move nearby passives** (C64, C8 and any 0402/0603 near U1) by small amounts, and re-attach their tracks.
- Change via sizes, add vias, use **every** layer: Top, L3 (In2.Cu), L4 (In3.Cu), L5 Power (In4.Cu, signals allowed in gaps), L6 (In5.Cu), Bottom. L2 (In1.Cu) and L7 (In6.Cu) are GND planes: you may pass a via through them but do not cut long slots in them.
- **Grow the board outline** (any direction, e.g. widen the west edge by up to 3 mm or the north-east area), and/or **move U1 together with the north tab up to 10 mm north**. The antenna must still sit at/overhang the north board edge and the antenna keep-out must stay empty on all layers. If U1 moves, redo only the stubs/escapes that moved and update coordinates in this plan by the same offset.
- Swap ESP32 GPIOs **only as a last resort**; log every swap (old pin, new pin, net) in `fix_v21/PINSWAP_FOR_FIRMWARE.md`. Firmware is not changed by you.
You must NOT: move the sensor/connector pads that the case_v3 pogo pins land on (J2, test pads, sensor windows) unless it is truly unavoidable; if you do, log the exact move (ref, old xy, new xy) in V21_PROGRESS.md under "CASE IMPACT".

## 4. U1 geometry (WROOM-1 at (11.5, −6.5), rot 0; U1 pads are Top only)
- West column pads 1–14: x = 2.75, y = −14.76 + (n−1)·1.27 (pad 4 −10.95, pad 12 −0.79, pad 14 1.75).
- Bottom row pads 15–26: y = 3.00, x = 4.515 + (n−15)·1.27 (pad 17 7.055, pad 23 14.675).
- East column pads 27–40: x = 20.25, y = 1.75 − (n−27)·1.27 (pad 31 −3.33, pad 39 −13.49).
- Pad 41 = GND exposed pad with a 3×3 via grid at about x 9.6–11.4, y −8.9 to −7.0 (see pictures). Keep new vias ≥0.3 mm from it.
- NC: pads 16, 24, 25, 26, 28, 29, 30 (28–30 are octal PSRAM, must stay unconnected).
- **Escape** for every routed U1 pad: 0.127 mm Top stub from the pad centre straight into the module to an escape via: west (4.25, pad_y), east (18.75, pad_y), bottom (pad_x, 1.55). Column pitch is 1.27 mm so no stagger is needed; if a via clashes, push it 0.6 mm deeper (west 4.85, east 18.15, bottom 0.95).

### The ordering rule (read this, the old prompt got it wrong in places)
For a bundle of parallel L-shaped tracks on one layer: if track A's horizontal is NORTH of track B's horizontal and both then turn SOUTH, A must use the lane on the OUTSIDE of the turn (further along the direction they travel), otherwise they cross. At the far end, if a lane must turn back across its neighbours, **drop to the target's layer with a landing via BEFORE the crossing** instead of crossing. Check every family on the re-rendered picture.

## 5. All 41 connections: exact plan
Coordinates in mm. "→Lx" = landing via then a short stub on layer x to the exact target point. Do the nets in the order listed. Where two lines say "fallback", try the first; if DRC adds anything, revert ONLY that net and try the fallback.

### 5A. Short / non-U1 links (do first)
| # | Net | From | To | Plan |
|---|---|---|---|---|
| 1 | GND | J2 pad 2 (12.40, 5.00) Bottom | stub tip (12.43, 11.61) Top | Do NOT join them. Move the padless dead stub (and only it) to `_stale/` notes / delete it from the board back to its first pad/via, then give J2 pad 2 a GND via within 0.6 mm to the L2/L7 planes. |
| 2 | +3V3_ESP | (0.60, −13.225) | C8 pad 1 (0.60, −10.92) | 0.25 mm Top straight line. |
| 3 | +3V3_ESP | C8 pad 1 (0.60, −10.92) | Top track end (2.761, 4.05) | Via near C8 (0.60, −10.2), 0.25 mm on **L5 Power** south at x 0.6 to y 4.6, via at (2.76, 4.6), Top stub to (2.761, 4.05). Fallback: connect C8 to U1 pad 2 (2.75, −13.49) on Top and use the existing pad-2 supply route. |
| 4 | ESP_EN | C64 track end (1.60, −11.78) | Top track end (6.292, 10.011) | Via (1.6, −11.0) [after nudging C64], **L6** lane x 1.2 south to y 9.6 (≥0.3 mm from H1/H2 holes; move lane east to 1.4 if needed), via, Top to the end. Fallback: L4 lane x 4.6. |
| 5 | USB_DM | (22.917, 13.553) | (21.656, 12.838) | Top straight 0.127 mm (pair continuation, match USB_DP). |
| 6 | USB_DP | (10.552, 13.336) | (10.404, 12.806) | Top straight 0.127 mm. |
| 7 | USB_DM_S3 | Bottom end (4.177, 7.764) | Top end (1.55, 0.70) | Via at the Bottom end, Top at x 1.55 north to y 7.5 then 45° in. Keep 0.15 mm gap to DP_S3. |
| 8 | USB_DP_S3 | Bottom end (4.579, 8.073) | Top end (2.00, 8.60) | Via, short Top link. |
| 9 | ESP_TX | U1 pad 37 (20.25, −10.95) | (21.31, −6.78) Top | Top 45° jog east out of the pad to x 21.31 then south. Check clearance to the pad 36/38 escape stubs. |
| 10 | ESP_TX | (20.804, 6.250) Top | (21.31, −6.78) | Vias at both ends, **L6 lane x 24.0** (moved east of the bottom family; 22.0 collides with the MAX86178_INT landing). |
| 11 | IN1P_AC | (22.494, −0.594) Top | (21.515, 6.696) Top | Vias both ends, **L5 or L6 lane x 24.4** (moved OFF L4, because L4 at x 22.5 blocks AD5940_GPIO0). If L5/L6 fail, L4 x 24.6. |
| 12 | ECG_P | (24.808, −9.959) Top | (15.927, 4.954) Top | Vias both ends, L4: vertical x 25.0 south to y 5.6, west at y 5.6 to x 16.4, via, Top stub. Board is wide here (edge x ≈ 53), so no growth needed. Keep it south of the L4 CHG_PG/GAUGE landing vias (y ≤ 3.8). |
| 13 | I2C_SCL | U1 pad 17 (7.055, 3.0) | (6.779, 4.733) Top | Top, 2 short segments. |
| 14 | AFE4900_ADC_RDY | U1 pad 23 (14.675, 3.0) | (15.077, 5.323) Top | Top short (TS/VBAT are on Bottom). |
| 15 | ADS1292_DRDY | Bottom end (3.435, 4.05) | Bottom end (21.30, −1.0006) | Via near (3.8, 4.05) if needed, **Bottom horizontal y −1.05** from x 3.8 to x 21.3. It is the northernmost Bottom horizontal under the module. Fallback L4 y −1.05. |
| 16 | ESP_IO0 | (3.4108, 4.6646) Top | (0.3447, 3.1209) Top | Top, 2-segment link at y 3.9. |

### 5B. West column → L6 (In5.Cu) family (+ L4 for pad 9)
| # | Pad | Net | Target | Route |
|---|---|---|---|---|
| 17 | 4 | LSM6_INT1 | (3.095, 4.05) Top | Esc via (4.25, −10.95); L6 west to x **3.50**, south at x 3.50 to y 3.5; landing via (3.50, 3.5) [south of pad 14's copper, which ends ≈ y 2.2]; Top stub to target. |
| 18 | 12 | I2C_SDA | (4.531, 4.461) Top | Esc via (4.25, −0.79); L6 west to x **3.80**, south to y 3.9, 45° to (4.3, 4.4); landing via; Top stub. (3.80 track edge to 4.25 via edge = 0.16 mm OK.) |
| 19 | 5 | TMP117_ALERT | (7.832, 4.415) L3 | Esc via (4.25, −9.68); L6 east to x **6.75**, south to y 3.3, east to (7.40, 3.3); landing via → L3 stub to target. |
| 20 | 8 | CS_AD5940 | (9.793, 4.05) L3 | Esc via (4.25, −5.87); L6 east to x **6.40**, south to y 3.9, east to (9.30, 3.9); landing via → L3 stub to target. |
| 21 | 10 | STATUS_LED | (10.70, 6.50) Top | Esc via (4.25, −3.33); L6 east to x **6.05**, south to y 6.2, east to (10.2, 6.2); landing via → Top stub. |
| 22 | 11 | CS_AFE4900 | (10.162, 7.178) Bottom | Esc via (4.25, −2.06); L6 east to x **5.70**, south to y 6.9, east to (9.7, 6.9); landing via → Bottom stub. |
| 23 | 9 | CS_FLASH | (10.05, 8.30) L3 | Esc via (4.25, −4.6); **L4** east at y −4.6 to x 9.5 (stay ≥0.3 mm from the pad-41 via grid; if blocked, use y −5.25), south at x 9.5 to y 7.8; landing via → L3 stub to target. |
Nesting check: north-to-south pads 5, 8, 10, 11 use lanes 6.75, 6.40, 6.05, 5.70 and turn at y 3.3, 3.9, 6.2, 6.9, so nothing crosses. Pads 4 and 12 sit west of the escape vias, so they cross nothing.

### 5C. West → east crossers on L4 (In3.Cu)
| # | Pad | Net | Target | Route |
|---|---|---|---|---|
| 24 | 6 | GAUGE_ALRT | (19.437, 5.085) L3 | Esc via (4.25, −8.41); L4 east at y −8.41 to x 16.6 (passes north of the pad-41 grid; jog to y −9.05 if it clashes), south to y 3.8; via (16.6, 3.8) → **L3** east/south-east to target. |
| 25 | 7 | CHG_PG | (18.864, 4.442) L3 | Esc via (4.25, −7.14); L4 east at y −7.14 to x 16.95, south to y 3.2; via (16.95, 3.2) → L3 to target. If the y −7.14 run hits the pad-41 grid, use y −6.45. |
| 26 | 31 | CS_MAX86178 | (16.586, 5.945) Top | Esc via (18.75, −3.33); **L4** west to x 17.6, south to y 5.5, west to (17.1, 5.5); landing via → Top stub. (Its vertical at 17.6 is east of the pad 6/7 verticals and its west turn at y 5.5 is south of their landing vias, so no crossing.) |

### 5D. East column → L6 (In5.Cu) family
| # | Pad | Net | Target | Route |
|---|---|---|---|---|
| 27 | 36 | ESP_RX | (16.077, 4.632) Bottom | Esc via (18.75, −9.68); L6 west to x **16.60**, south to y 4.2; landing via (16.6, 4.2) → Bottom stub. |
| 28 | 34 | MIC_DAT | (19.964, 4.953) Top | Esc via (18.75, −7.14); L6 west to x **16.95**, south to y 4.8, east to (19.5, 4.8); landing via → Top stub. |
| 29 | 33 | MIC_CLK | (20.449, 5.146) L3 | Esc via (18.75, −5.87); L6 west to x **17.30**, south to y 4.2, east to (20.0, 4.2); landing via → L3 stub. |
| 30 | 32 | MAX86178_INT | (22.393, 4.590) L3 | Esc via (18.75, −4.6); L6 west to x **17.65**, south to y 3.6, east to (21.5, 3.6); landing via → L3 stub (watch the L3 USB pair near x 20.5–21.3; reroute that pair if it's in the way). |
Nesting: lanes west→east 36, 34, 33, 32; east-turners turn at 4.8, 4.2, 3.6 (eastmost lane turns northmost). Landing vias must be ≥0.55 mm apart centre to centre; shift turn y by ±0.3 mm if needed.

### 5E. L3 (In2.Cu) crossers
| # | Pad | Net | Target | Route |
|---|---|---|---|---|
| 31 | 39 | FSR_ADC | (3.492, 4.710) L3 | Esc via (18.75, −13.49); short L3 jog to y −13.2; L3 west at y −13.2 to x 3.75; via (3.75, −13.2) → **L5 Power** south at x 3.75 to y 4.3 (L5 avoids crossing #32/#33, which run west on L3 at y 1.0/1.3); via (3.75, 4.3) → L3 stub to target. Track edge at x 3.81 vs west escape-via edge 4.025: OK. |
| 32 | 27 | ESP_IO0 | (3.411, 4.665) Top | Esc via (18.75, 1.75); L3 north to y 1.0, west at y 1.0 to x 3.4, south to y 4.2; landing via → Top stub. Keep clear of the L3 USB_DP/DM pair (x ≈ 20.5–21.3). |
| 33 | 18 (a) | CS_ADS1292 | (3.679, 4.793) L3 | Esc via (8.325, 1.55); L3 north to y 1.3, west at y 1.3 to x 3.7, south to target. #32 (y 1.0, turns at x 3.4) is north/outside of #33 (y 1.3, turns at x 3.7), so they do not cross. #31 FSR uses L5 for its vertical, so it does not cross them either. |

### 5F. Bottom-row family on Bottom (B.Cu) going east, plus east pads 35/38
Bottom-row horizontals (pad x order means the east pad gets the horizontal nearest the vias):
| # | Pad | Net | Target | Route |
|---|---|---|---|---|
| 34 | 22 | AD5940_GPIO0 | (23.386, 6.431) L4 | Esc via (13.405, 1.55); Bottom north to y 0.95, east to x 21.8; via (21.8, 0.95) → **L4** south-east to target (L4 is free there now that IN1P_AC moved off L4; ECG_P on L4 runs at x 25.0 and y 5.6, so keep north of y 5.3 then land, or reroute ECG_P's west run to y 6.2). |
| 35 | 21 | SPI_MISO | (21.31, 2.177) L3 | Esc via (12.135, 1.55); Bottom to y 0.55, east to x 22.35; south to y 1.25; via (22.35, 1.25) → L3 stub to target. |
| 36 | 20 | SPI_SCK | (21.30, 1.754) Bottom | Esc via (10.865, 1.55); Bottom to y 0.15, east to x 22.8, south to y 1.754, west to target (passes south of the #34/#35 vias at y 0.95/1.25; check ≥0.127 mm). |
| 37 | 19 | SPI_MOSI | (21.31, 3.652) Bottom | Esc via (9.595, 1.55); Bottom to y −0.25, east to x 23.25, south to y 3.652, west to target. |
| 38 | 18 (b) | CS_ADS1292 | (23.171, 2.986) L3 | From pad 18's escape via (8.325, 1.55): Bottom to y −0.65, east to x 23.70, south to y 2.5; via (23.70, 2.5) → L3 stub to target. |
Lane pitch here is 0.45 mm (21.8 / 22.35 / 22.8 / 23.25 / 23.70) so vias fit beside neighbouring tracks. **Frozen-ish:** TS and VBAT on Bottom at x 11.3–16.1, y 2.7–3.6: stay at y < 2.3 there (or reroute TS/VBAT, you are allowed to).
East-column pads to Bottom targets (north of the family, y < −0.65):
| # | Pad | Net | Target | Route |
|---|---|---|---|---|
| 39 | 38 | ADS1292_DRDY | (22.999, −2.376) Bottom | Esc via (18.75, −12.22); Bottom east at y −12.22 to x 23.45, south to y −2.376, west to target. |
| 40 | 35 | SPARE_IN | (23.30, −0.793) Bottom | Esc via (18.75, −8.41); Bottom east at y −8.41 to x 22.65, south to y −0.793, east to target. (#39 vertical at x 23.45 stops at y −2.376, north of #40's final east hop, so no crossing; #15 DRDY at y −1.05 ends at x 21.3, west of x 22.65 — check it.) |
| 41 | 15 | EXP_INT | (−1.568, 9.873) Top | Esc via (4.515, 1.55); **L5 Power** west at y 1.55 to x −1.2 (L5 so it does not cross the L6 lanes at x 3.5/3.8 or the L3 runs), south at x −1.2 to y 9.4; via (−1.2, 9.4) → Top stub to target. Grow the west edge by up to 2 mm if the 0.3 mm edge clearance fails. Watch #3 (+3V3_ESP on L5 at x 0.6): cross it by putting #3 on L4 instead, or jog #41 onto L4 between x 0.2 and 1.0. |
(Numbering 1–41 matches the 41 lines of `unconnected41.txt`, with pad 18 counted twice and ESP_TX twice.)

## 6. Method per net (strict loop)
1. Save a checkpoint before each net (`_work/ckpt/net_NN_pre.kicad_pcb`).
2. Add the tracks/vias with explicit integer-nm coordinates via the pcbnew Python API (`$KP`). Write a small helper `_work/add.py` (add_track(layer, pts, width, net), add_via(x, y, net, size, drill)) — that is fine; what is **not** allowed is an autorouter / A* / rip-up-loop script that decides routes by itself.
3. Run DRC (section 2). If the net added ANY new violation or did not remove its unconnected item: revert only that net, then try in order: (a) shift the lane 0.3 mm, (b) the stated fallback, (c) another free layer (L6 → L4 → L5 → L3 → Bottom), (d) **rip up the neighbouring non-U1 track that blocks it and reroute that neighbour too**, (e) move a nearby passive / grow the board / move U1+tab north. Never give up on a net; if it fails three ways, do (d) and (e).
4. Every 5 good nets: copy to `_work/v21_ckpt_N.kicad_pcb`, re-render pictures (section 0) and look at them.
5. Log each net in `fix_v21/V21_PROGRESS.md`: net, what you did, DRC counts after. A new window must be able to resume from this file alone.
Freerouting is allowed ONLY as an optional helper on a cropped copy (just the U1 area, no keep-outs) for 1–2 stubborn nets, and its result must be hand-checked and pass DRC on the full board. Do not let it run the whole board.

## 7. Done means (all must be true; then and only then print the final line)
- DRC with the v19 `.kicad_pro`/`.kicad_dru`, `--severity-error`, on the saved `fix_v21/vitalq_v2.kicad_pcb`: **0 violations, 0 unconnected, 0 shorts**. Paste the counts.
- Antenna keep-out empty on all layers; antenna at/over the north edge.
- Pin audit: every U1 pad's net matches `pinmap.json` (or `PINSWAP_FOR_FIRMWARE.md` lists the change). Pads 16, 24–26, 28–30 unconnected.
- Zones refilled (`pcbnew.ZONE_FILLER(board).Fill(board.Zones())`) and DRC re-run after the fill.
- Fab outputs regenerated into `fix_v21/fab/` (move any old ones to `_stale/`): Gerbers for all 8 copper layers + masks/silk/paste/Edge.Cuts (`kicad-cli pcb export gerbers`), drill (`kicad-cli pcb export drill`), BOM and CPL (`export_assembly.py` or `kicad-cli pcb export pos` + BOM CSV), one zip `fab/VitalQ_v2_fixv21_fabrication.zip` and its SHA-256.
- New renders `fix_v21/_prompt_pics/final_render_top.png` and `final_render_bottom.png` and re-drawn U1 zoom pictures in `_prompt_pics/final/`.
- `V21_PROGRESS.md` ends with a summary: board size changes, U1 moves, ripped/rerouted neighbour nets, passive moves, CASE IMPACT, pin swaps.
- Then print exactly: `VITALQ V21 COMPLETE  DRC violations=0 unconnected=0 shorts=0`
