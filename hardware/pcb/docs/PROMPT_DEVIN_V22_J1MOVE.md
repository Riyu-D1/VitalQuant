# VitalQ fix_v22: move J1 (USB-C) east so a real USB-C cable can plug in

Work ONLY in `~/Downloads/VitalQ_Hand/fix_v22/`. It is a copy of fix_v21: the old `fab/` is in `_stale/fab_v21`, and the old prompts are in `_stale/`.
- Never touch `fix_v21`, `fix_v19` or `fix_v20*`, and never touch any `case_*` folder. **Delete nothing**: stale files go to `fix_v22/_stale/`.
- No firmware edits. No ordering, pricing or contacting anyone. No safety or compliance claims.
- **No time caps.** Do not stop or print the completion line until every item in section 7 is true.

## 1. The problem (reviewer-checked 21 Oct, geometry from the fix_v21 board)
- J1 = `Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12`, Top side, at **(26.0, −7.8), rotation 180**. Its mouth faces −y.
  - Fab outline: x 21.48–30.52, y −11.5 to −4.1. The front is at y −11.5, i.e. 1.0 mm past the board edge at y −10.5.
  - Courtyard: x 20.655–31.345, y −11.975 to −2.485.
  - Pads: the signal row is at y −3.755. The shell THT tabs are at x ±4.32 from centre, at y −4.67 and −8.85.
- fix_v20 added the WROOM board tab. The board edge now steps north at **x 22.5** and runs from y −10.5 to −16.55 (the tab covers x −1.05…22.5). The WROOM body runs to x 20.56 and pokes out to y −22.31. fix_v19's edge was straight at y −10.5, so this is new.
- J1 axis to the tab edge is only **3.50 mm**, and to the WROOM body **5.44 mm**.
  - A USB-C plug grip (overmould) is 8.3–12.4 mm wide and about 6.5 mm thick, centred on the receptacle axis about 1.6 mm above the board top. It therefore reaches down level with the board itself.
  - Any grip wider than 7.0 mm hits the PCB tab, and wider than 10.9 mm it also hits the WROOM.
  - **Result: real cables cannot plug in fully.**
- **Correction:** `fix_v21/VERIFY_REPORT.md` filed this as m7 "USB-C overmould corridor 13.45 mm³ … case-file changes only / MINOR". **That was wrong.** The blocker is the board tab itself, which no case change can fix. Treat it as a board-level MAJOR, now being fixed.
- See `_prompt_pics/usbc_clearance_v21.png` (v19 vs v21 side by side).

## 2. Pictures: open and look at them first
`_prompt_pics/`:
- `usbc_clearance_v21.png`.
- `J1area_<layer>.png` for F.Cu, In1–In6.Cu and B.Cu. Each shows that layer's copper around J1:
  - cyan = J1 nets (VBUS, USB_DP/DM, USB_CC1/CC2);
  - red box = J1 now; lime dashed = the J1 target courtyard at x 29.5;
  - magenta = the 12.4 mm plug-grip corridor that must stay clear;
  - orange = R2/TP11/TP20.
- `render_top_v21.png`.

Re-draw after each step, then look at the pictures again:
```
$KP _work/j1pics.py vitalq_v2.kicad_pcb _work/j1area.json && python3 _work/draw_j1.py
```
`$KP` = `/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3`. Once J1 has moved, edit `dx` in `draw_j1.py` to 0 so the lime box matches.

## 3. The move (exact)
- Move J1 to **(29.5, −7.8)**, rot 180, Top. The minimum is x 29.2; 29.5 is preferred. y and rotation stay the same, so the front stays 1.0 mm past the y −10.5 edge (front at y −11.5).
- New courtyard: x 24.155–34.845, y −11.975 to −2.485.
- Board edge in front of J1: it must be straight at y −10.5 from at least **x 22.8 to x 36.2** (12.4 mm grip + 0.5 mm margin on each side). The current edge runs straight from x 22.5 to 48.0 at y −10.5, so it qualifies. Confirm it by script.
- **Parts that clash with the new courtyard (reviewer-computed). Move each minimally**, using a free spot close to its current function, and log old → new xy:
  - **R2** (USB_CC2 5.1k pull-down to GND), x 31.83–32.82, y −6.17 to −4.26. Keep it within ~3 mm of J1's CC2 pad. Its sibling CC1 resistor (find it via the USB_CC1 net) should also sit close to its pad.
  - **TP11** (ESP_TX test point), x 32.98–35.03, y −9.50 to −7.45. Move it east (e.g. ≈ x 36.8, same y) or anywhere probe-reachable. Its net is ESP_TX.
  - **TP20** (CS_ADS1292 test point), x 32.69–34.74, y −2.76 to −0.71. It grazes the new courtyard bottom by ~0.25 mm; shift it ≥0.3 mm south or east.
  - **R26** (+3V3_ANA / IN1P, ADS1292 front-end), x 33.29–35.20, y −4.66 to −3.67. This is analogue: move it the shortest distance (east or south) and keep its tracks short. Do not route digital or USB nets next to IN1P.
- **Vias inside the new courtyard (9 total, nets ESP_TX, GND, IN1P, USB_CC2, VBUS).** Any via under J1's pads, under the shell THT tabs (now at x 25.18 / 33.82, y −4.67 / −8.85) or within 0.3 mm of them must move. Vias under the plastic body away from pads are OK only if DRC is clean and they are tented.
- **Top-layer tracks under the new J1 pads/tabs must be re-routed.** The J1 area contains ADS1292/front-end nets (IN1P, IN1P_AC, PGA*, ECG_*); see `J1area_F.Cu.png`. Re-route them minimally around J1, or on an inner layer, keeping each analogue net's length about the same, and log every one.

## 4. Re-routing J1's nets (only these, plus whatever J1/R2/TP11/TP20/R26 displaces)
- **USB_DP / USB_DM:** route as a pair from J1 (A6/B6 = DP, A7/B7 = DM; join A↔B at the connector as before) to the existing pair path (R102/R103 → U1 IO20/IO19).
  - Target 90 Ω differential. Compute the width and gap from the stack-up; the current pair is 0.129 mm wide with a loose 0.236 mm gap.
  - Keep the pair coupled. Match the length within 0.15 mm if practical (V21 found ~12 mm skew J1-side: improve it if you can without touching the WROOM routes, and report before/after).
  - Minimise vias, and keep vias symmetric.
- **USB_CC1 / USB_CC2:** short, to the 5.1k pull-downs.
- **VBUS:** both VBUS pad pairs (A4/B9, A9/B4) to the existing VBUS path (ESD/TVS → U2 BQ25170 pin 1, C63).
  - Width ≥0.4 mm on outer layers (≥0.6 mm on inner) for ≥500 mA plus margin. Use at least 2 vias (0.45/0.2) per layer change.
  - Keep the ESD/TVS device and C63 within ~5 mm of J1.
- **GND / shield:** the shell tabs (SH) and A1/A12/B1/B12 go to GND. Add ≥4 GND stitching vias around the new J1 footprint (outside the pads/tabs, inside the board, not in the corridor), plus solid GND pour under J1 on In1/In6.
- **Do not touch the 41 WROOM routes** made in fix_v21. If a J1 net must cross one, do a minimal local rip-up of that one segment only, re-route it, and log it.
- Keep the antenna keep-out (x −2.5…25.5, y −16.55 to −15.25, all layers) empty. Refill all zones (`pcbnew.ZONE_FILLER(board).Fill(board.Zones())`) after the changes and save.
- If x 29.5 truly cannot work, x 29.2 is the absolute minimum. **Never go below it.** If more room is needed, you may move other nearby small passives or test points, logged.

## 5. Checks (all must be run; collect everything, do not stop at the first failure)
1. DRC on the saved board, after copying `../fix_v19/vitalq_v2.kicad_pro` and `.kicad_dru` into `fix_v22/`:
   `kicad-cli pcb drc --severity-error --schematic-parity`
   **0 errors / 0 unconnected / 0 shorts / 0 parity.** Also list the `--severity-all` warning counts compared with v21.
2. ERC: `kicad-cli sch erc --severity-error` = 0.
3. **Corridor script** `_work/check_corridor.py` (pcbnew API).
   - Define the receptacle face y_f = J1 front (y −11.5) and axis x_a = J1 x.
   - The grip corridor is the box x_a ± 6.2, y from y_f − 0.0 out to y_f − 15.0, z from −1.65 to +4.85 relative to the board top.
   - Assert **no board outline (Edge.Cuts polygon), no footprint courtyard/body (including U1 as 2.44–20.56 × −22.31…3.31, h 3.1), no copper** intersects it. Repeat for an 8.3 mm grip.
   - Print the clearances: axis → tab edge, axis → U1 body, axis → nearest other part, margin each side for 12.4 mm and 8.3 mm, and the edge straight span in front.
   - **The 12.4 mm grip must pass with ≥0.5 mm per side.**
4. Connectivity: every J1 pad is on the correct net, and every net that touched a moved part is still one copper island. Run a Gerber-only continuity check for the J1 nets (reuse `../sims/fullsim_v20/gerber_check`).
5. USB: the pair's impedance estimate, length and skew, and via count (before/after). CC resistor values and placement. ESD part distance to J1.
6. Power: VBUS track width vs current (IPC-2152), via count, and the ESD/C63/U2 distances.
7. Analogue: for R26/IN1P and any re-routed front-end net, give the length before/after, the closest digital/USB neighbour and its spacing, and re-run `../verify/crosstalk.py` (or `fix_v21/verify21/crosstalk_v21.py`, adapted) for the changed nets.
8. Re-run sections **1, 2, 3 (USB / antenna parts), 6, 14 and 15** of `fix_v22/_stale/PROMPT_DEVIN_V21_FULLVERIFY.md` (a copy of the fix_v21 spec) on fix_v22: DRC/ERC, connectivity, USB/antenna, power, fab outputs and renders.

## 6. Fab outputs and docs
- Regenerate into `fix_v22/fab/`:
  - Gerbers (8 copper + masks, silk, paste, Edge.Cuts) and drill;
  - **`BOM.csv` with the U1 fix from V21 finding M1.** Do this only if it is a schematic property-field fix of U1 Value/Footprint and nothing else: set Value `ESP32-S3-WROOM-1-N16R8`, Footprint `ESP32_S3_WROOM_1`, then log it and re-run ERC/parity;
  - `BOM_JLC.csv`, `CPL.csv`, `CPL_JLC.csv`;
  - `VitalQ_v2_fixv22_fabrication.zip`, plus its SHA-256.
- **Check J1's CPL rotation**: it must match the JLC convention for HRO TYPE-C-31-M-12 (C165948), with the same orientation as v21 and only XY changed. Also confirm the CPL XY of R2, TP11, TP20 and R26 match the board.
- Renders: `_prompt_pics/final_top.png`, `final_bottom.png`, an isometric view, and a close-up of J1 with the tab and WROOM (`kicad-cli pcb render`, with zoom/rotate). Re-draw the `J1area_*` pictures into `_prompt_pics/final/`.
- `fix_v22/V22_PROGRESS.md`: every move (ref, old xy → new xy), every re-routed net (layer, length before/after), the USB pair numbers, the DRC counts, the corridor clearances, and any WROOM-route rip-ups (ideally none).
- `fix_v22/CASE_CHANGES_FOR_V22.md`: list only, **no case edits**. Include:
  - the new J1 window centre x = J1 x (29.5) and the window size (≥9.6 × 3.7 mm);
  - a plug-grip recess of about **12.5 × 7 mm**, centred on the receptacle axis, open from the case outer face to within ≤0.5 mm of the receptacle face;
  - the dust-flap anchor relocation (it was at x 34.5, y −11.3);
  - the **antenna bulge must reach y ≤ −23** to enclose the WROOM (which ends at −22.31, and case_v8 only reaches −18.4);
  - the bulge's east wall must stay west of the grip recess (bulge east face ≤ x 22.8);
  - the lid clearance for WROOM height 3.1 mm;
  - the C64/C8/D33/Q6 grazes from V21 m7.

## 7. Done means
- DRC 0/0/0 (+0 parity) and ERC 0 on the saved `fix_v22/vitalq_v2.kicad_pcb`.
- The corridor script PASSes for the 12.4 mm grip with ≥0.5 mm margin per side, and for the 8.3 mm grip.
- The antenna keep-out is empty, and the WROOM routes are unchanged (or the logged minimal rip-ups re-routed).
- Fab zip and SHA-256 done, the CPL J1 rotation is checked, renders done, and both MD files are written.
- Then print exactly:
  `VITALQ V22 COMPLETE  DRC errors=<n> unconnected=<n> shorts=<n>  J1_x=<x>  grip12.4_margin_west=<mm> east=<mm>  axis_to_tab=<mm>  axis_to_U1=<mm>`
