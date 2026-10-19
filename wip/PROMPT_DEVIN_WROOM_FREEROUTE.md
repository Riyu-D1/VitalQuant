# VitalQ fix_v20: finish the ESP32-S3-WROOM-1-N16R8 routing with a REAL router (fresh window, full context)

You are starting fresh. Read this whole file, then look at `~/Downloads/VitalQ_Hand/fix_v20/_work/gb/U1_guidance.png`, the reviewer's annotated top-copper view of the U1 area.

## Context
- VitalQ is a chest-worn sepsis monitor: an 8-layer KiCad board in `~/Downloads/VitalQ_Hand/`.
- `fix_v19/` is the verified fallback, with DRC 0/0/0 at error severity. NEVER touch fix_v19.
- `fix_v20/` swaps U1 from the ESP32-S3-MINI-1 to the ESP32-S3-WROOM-1-N16R8 (footprint `ESP32_S3_WROOM_1`, LCSC C2913202). Work only in fix_v20.
- Original copper reference: `fix_v20/_work/pre_promote_backup.kicad_pcb`.
- Design rules: ALWAYS `cp ../fix_v19/vitalq_v2.kicad_pro .` before every DRC, because `board.Save()` overwrites it. Do not edit the rules.
- Run DRC as `kicad-cli pcb drc --severity-error`. Every count below is error severity.
- Delete nothing. Move superseded files to `_stale/`. Touch only files under `~/Downloads/VitalQ_Hand/`.

## Why the previous window failed (reviewer-checked, 19 Oct 22:58)
- It wrote six hand-made A* routers (`fix4_p5.py`, `fix4_p6.py`, `fix4_p7.py`, plus a loop script). The DRC trail went 401/2, 413/0, 1436/63, 1222/82, 539/381, 817/327, and is now 1029/141. Each "fix" pass created new shorts.
- The one "0 unconnected" state (`drc_fix4e.rpt`) was **not** good. It had 105 shorting_items and 35 tracks_crossing. Pads only looked connected because nets were shorted together.
- **Lesson: do not write another router.** Use Freerouting, a proper autorouter, limited to the rework zone, on a clean base.

## Fixed decisions (do not change)
- **Placement:** U1 at (11.5, −6.5) mm, rotation 0, antenna north, Top side. The reviewer checked: 90° or 270° makes the module 25.5 mm wide and hits J1, and 180° points the antenna inwards.
- **Outline:** keep the 6 mm north tab, x −2.05 to 22.5, top edge y −16.55. The board may grow further if truly needed. It only has to stay wearable.
- **Antenna keep-out:** no copper, via or pour on any of the 8 layers.
- **C7/C8** (+3V3_ESP 10 µF / 0.1 µF) go in the tab west of pads 1–2. **C64** (ESP_EN cap) goes beside pad 3. Use fix4.py's positions: C7 at y −14.0, C8 at y −12.1, C64 at (1.6, −11.3), all rotated 90°.
- **Same GPIO numbers** as the MINI; `pinmap.json` stays unchanged. Pads 28–30 (IO35–37, octal PSRAM), pad 16 (IO46) and pads 24–26 stay NC.
- **Non-U1 blockers:** these run under the east pad column and must move to an inner layer **only** inside U1 copper plus 0.5 mm, exactly as fix4.py did. Vias go outside U1 copper plus 0.5 mm.
  - USB_DP/USB_DM: a coupled pair on Layer 3, 90 Ω differential, length-matched.
  - IN1P_AC and ECG_P: on Signal Layer 4.
- Every other net outside the zone must stay byte-identical to `pre_promote_backup`. That includes **TS and VBAT on B.Cu at x 11.3–16.1, y 2.7–3.6**: don't touch them.

## Step 1: build a CLEAN base with U1 left unrouted
1. Take `fix4.py` (rebuild from `pre_promote_backup`) and run only its placement and deletion parts:
   - tab outline, keep-out, U1 placement, cap moves
   - the 2a blocker-net moves
   - whole-segment delete with walk-back to anchors (`anchors.csv`)
   - tab GND pours
   **Skip** its pad-to-anchor routes, bridges and island-merge. Do NOT run p3 to p7.
2. On that base, delete every remaining fragment: zero-length segments, padless dead islands of U1 nets, and any segment of a U1 net inside U1 copper plus 0.3 mm.
3. Add the U1 GND connections by hand:
   - a 3 × 3 via grid in pad 41
   - one via beside each of pads 1 and 40, outside the pad copper plus 0.1 mm
4. Route the USB_DM_S3/USB_DP_S3 pair by hand: pads 13/14 at (2.75, 0.48)/(2.75, 1.75), on Top, down the west corridor x 2.2–3.8, to their anchors near (4.18, 7.76)/(4.58, 8.07), with 90 Ω coupling. Use exact integer-nm endpoints.
5. Route the C7/C8/C64 links by hand with short Top tracks: C7/C8 to pad 2 at 0.3 mm, C7/C8 GND to pad 1, C64 to pad 3.
6. Refill, save as `_work/base_clean.kicad_pcb`, then run DRC. **Gate:** 0 shorting_items, 0 clearance, 0 tracks_crossing, 0 hole_clearance, and 0 items_not_allowed apart from any v19 baseline. The ONLY unconnected items allowed are U1-pad-to-anchor links. If the gate fails, fix the base before going on.

## Step 2: confine Freerouting to the zone
1. Lock every existing track and via (`SetLocked(True)`). KiCad exports locked copper to Specctra as fixed wiring.
2. Rework zone: x −2.05 to 23.5, y −16.55 to 9.0. Add temporary KiCad rule areas (no tracks, no vias, all copper layers) covering the board outside the zone, plus the antenna keep-out.
3. Export Specctra DSN with `pcbnew.ExportSpecctraDSN(board, path)`. If the signature differs on this KiCad version, check `help(pcbnew.ExportSpecctraDSN)`.

## Step 3: run Freerouting (light, local, removable install)
- There is no Java on this Mac. Put everything in `~/Downloads/VitalQ_Hand/tools/`, with no system-wide install and no Homebrew:
  - a portable Eclipse Temurin 21 JRE tarball for macOS (the right arch: check `uname -m`)
  - the latest `freerouting-*-executable.jar` from https://github.com/freerouting/freerouting/releases
- Run: `tools/jre/Contents/Home/bin/java -jar tools/freerouting.jar -de _work/u1.dsn -do _work/u1.ses -mp 100`
  - Freerouting writes the session file when it finishes; give it time. It does many passes by itself.
  - If it can't route a net, it leaves it unrouted. It does not short things, which is why we use it.
- Import the session with `pcbnew.ImportSpecctraSES(board, path)`. If that's not available on this version, parse the `.ses` wires and vias (simple s-expressions, coordinates in the DSN resolution units) and add them as PCB_TRACK/PCB_VIA, matching net names.

## Step 4: finish
1. Remove the temporary rule areas, unlock the tracks and vias, refill, save as `fix_v20/vitalq_v2.kicad_pcb`, re-copy the v19 pro, then run DRC.
2. If anything is left unrouted or in error, fix it: widen the zone by 1 mm, rerun Freerouting, or hand-route those few nets with exact endpoints. Keep going until DRC is **0 errors / 0 unconnected / 0 footprint errors** at error severity.
3. Log progress after every step in `_work/FREEROUTE_PROGRESS.md`, so a new window can resume if this one shuts down.

## Done means (write COMPLETE only when every item is true)
- DRC 0/0/0 at error severity with the v19 pro, on the saved file
- ERC 0
- antenna keep-out empty on all layers
- copper-freeze proof: everything outside the zone is identical to `pre_promote_backup`, except the documented USB_DP/DM, IN1P_AC and ECG_P inner moves
- pin audit against `pinmap.json`
- Gerber re-parse showing the outline
- case changes listed only: tab pocket, module height, antenna pocket
- BOM/CPL with U1 as C2913202
- a new `fab/VitalQ_v2_fixv20_fabrication.zip` with its SHA-256 (move the stale zip to `_stale/`)
- top and bottom renders
- `fix_v20/WROOM_REPORT.md` updated and ending with the line `WROOM FIXV20 COMPLETE`
