# VitalQ v9: finish routing to 0 unconnected (6-layer JLC), from the user's hand-edited board

Working folder: ~/Downloads/VitalQ_Route_v9 (KiCad 10.0.4, kicad-cli at /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli; pcbnew Python at /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3).

Files here:
- vitalq_v2.kicad_pcb: the user's hand-edited board (copied from ~/Downloads/vitalq_v2.kicad_pcb, saved 2026-10-09 10:19; a newer KiCad autosave from 10:36 exists in ~/Downloads/.history but was NOT used).
- vitalq_v2.kicad_pro: from VitalQ_Route_v8 (correct rules: 0.1016 mm track/clearance; ENIG). The user's own project file had KiCad default rules (0.2 mm / 0.6 mm vias), which is why their edits used 0.6 mm vias.
- vitalq_v2.kicad_sch, vitalq_v2.kicad_prl: from VitalQ_Route_v8.
- Read-only references: ~/Downloads/VitalQ_Route_v8 (REPORT.md, ANALYSIS.md, PROGRESS.md, blocked_connections_final.json, hv_uncapped_final.json, geometry_audit_final.json, scripts/). Never write there.

Starting point (refilled DRC with these rules): 43 unconnected items, ~299 violations.
Root causes of the 43: 19 at U6/U22 (skin-side 0.4 mm BGA/WLP, locked), 13 at U7 (0.4 mm BGA), 4 from parts inside HV no-via zones (U19, C59.1, U18.7/8), 2 GND islands stuck in HV zones, 3 +3V3 sensor-island stitches (U20/U11, F.Cu +3V3 island, TMP117_ALERT), 1 AFE_N_PAD long HV run, 1 J12 MP pair.

## Hard rules
- No change in function. No netlist change. Remove no parts and add no parts.
- Locked parts stay exactly where they are (U6, U22, U16, U11, U20, J12, connectors, electrodes, H1/H2, HV corridor). The skin-side sensor cluster stays clustered on the skin side. The HV corridor stays isolated: never add vias or inner-layer copper inside hv_inner rule areas, never edit or shrink rule areas.
- You may nudge small unlocked passives by at most 1 mm, and log every nudge (ref, from, to, reason). Unlocked ICs explicitly allowed to move in this plan: U19 (and U18 only if needed), logged the same way.
- JLC 6-layer limits, never loosened: track/gap >= 0.09 mm (prefer 0.1016), via hole-to-hole >= 0.2 mm, via hole to track/copper >= 0.2 mm, min via 0.15 hole / 0.25 dia. Via-in-pad only as POFV (epoxy filled + capped, free default on JLC 6-layer); flag every via-in-pad. Through vias only: no blind, buried or micro vias. Source: https://jlcpcb.com/capabilities/pcb-capabilities
- Work only in this folder. NO git commit, NO git push, NO worktrees. Do not delete any files anywhere (move superseded files into an archive/ subfolder if needed).
- Truth = `kicad-cli pcb drc --refill-zones --format json` on the saved board with this .kicad_pro present. Never accept a stale-fill result.
- Before every batch save a dated backup into backups/YYYYMMDD_HHMMSS_<batch>/ and after every batch append exactly one line to PROGRESS.md: batch id, nets done, unconnected count, real-violation count, time.

## Anti-loop rules (mandatory)
- At most 3 attempts per net, each a genuinely different approach (different layer, via site or detour side). After 3 failures mark the net BLOCKED in PROGRESS.md with the reason and move on.
- If two batches in a row fail to lower the unconnected count, stop routing and go to the final report.
- Never repeat a command or approach that already failed; check PROGRESS.md (and v8's PROGRESS.md/REPORT.md) before every attempt.
- Freerouting only as a last resort, only on a named list of BLOCKED nets, all existing copper fixed, one run, 10-minute timeout, accept only if refilled DRC improves. Never on the whole board, never twice.

## Ordered plan

### Step 1: rules and via sizes
- Confirm the .kicad_pro rules (0.1016 track/clearance). Define two via sizes: 0.25/0.15 mm (BGA via-in-pad, POFV) and 0.4/0.2 mm (everything else). Add a custom rule (.kicad_dru) enforcing via hole-to-copper >= 0.2 mm and hole-to-hole >= 0.2 mm, as JLC requires. Record the rule set in PLAN.md. Run refilled DRC and log the baseline line in PROGRESS.md.

### Step 2: undo the user's default-rule artefacts
- Replace the user's five 0.6/0.3 mm vias (IOVDD @38.456,8.484 and @44.001,-9.210; LED2_K @1.729,51.051 and @36.708,58.413; +3V3 @31.251,59.69) with 0.4/0.2 vias at a legal nearby spot, keeping the same connectivity.
- Move the user's ~10 mm LED2_K track off Ground Layer 2 (In1.Cu) onto Layer 3 / F / B so the GND plane is not cut. Keep LED2_K's connectivity no worse.

### Step 3: local fixes (expected about 9 opens)
- GND island 1: R9.2 + via @(6.83,62.87) sits in an HV zone and never reaches a plane. Route a top-layer GND track from R9.2 to R51.2 (3.67,63.27), which is on the main GND. Avoid I2C_SCL_1V8 (a straight line crosses it).
- GND island 2: U19.16 + D30.2 + via @(19.45,40.42) inside the HV zone. Route a short top-layer track to the existing GND via @(18.40,39.86), avoiding TX5_EN (a straight line crosses it). Moving U19 (below) may make this simpler.
- C59.1 (+3V3): new 0.4/0.2 via @(14.90,39.95), F.Cu track C59.1 -> (14.90, C59.1 y) -> via. Already proven on a copy: 43 -> 42 with no new violations.
- U18 pins 7/8 (+3V3): via @(26.65,40.15) for pin 7 (proven to reach the In3 +3V3 plane); give pin 8 its own legal via or path (the straight path along y=40.15 shorts +3V3_ANA).
- U20.5 / U11.5 / F.Cu +3V3 island: via on the FR4 bridge @ about (20.05,52.2) (clear, reaches the In3 +3V3 pour), routed from U20.5 (F) and U11.5 (B) around the U20.7/U11.4 GND pads and inside the edge clearance. Then TMP117_ALERT across the same bridge.
- J12 MP pair: join with two vias + a short inner-layer (Layer 3) link or a top-layer route. A straight B.Cu track at x=-0.1 shorts IR_GATE/ADS1292_DRDY vias.
- Move U19 (unlocked) fully out of the hv_inner zone x14.3-19.7, y40.1-43.4 to the nearest legal spot, re-connecting its existing nets. Log the move. This also helps ADS1292_PWDN, IR_GATE, AD5940_RESET.

### Step 4: U7 via-in-pad escapes (expected about 13 opens)
- Put 0.25/0.15 POFV vias in the U7 signal pads that need escape: B3 BIOZ_FP, B4 AIN4_LPF0, B5 BIOZ_SP, B6 DE0, B7 VZERO0, C7 VBIAS0, D2 BIOZ_FN, D7 VREF_2V5, E7 SPI_MOSI, F2/F3 +3V3_ANA, F5 AD5940_GPIO0, F7 CS_AD5940 (plus E3/E4/E5 GND to plane if useful). A survey showed these pad sites are clear of foreign tracks on every layer, except the F row, which is blocked on B.Cu by one MISO_AD track that may be locally rerouted. Escape on Layer 3 (In2) or B.Cu straight outward, then route to the targets (C21, C24, C25, C26, C68, C69, R84, R42, R14, U1.18, U19, CS_AD5940 stub).

### Step 5: U6/U22 rip-up and via-in-pad re-route (expected about 19 opens)
- Via-in-pad sites under U6/U22 are blocked by existing tracks: on U6 by ESP_TX / ESP_RX / AFE_INP on Layer 3 and ESP_RX on F.Cu; on U22 by MISO_FL / CS_FLASH / +3V3_ANA on F.Cu (and MX_SDO_1V8 / GND on B).
- In a window of roughly 5 x 8 mm around U6/U22 (about x 23-29, y 45-53), rip up those nets' local segments (ESP_TX, ESP_RX, AFE_INP, MISO_FL, CS_FLASH, local +3V3_ANA). This is an authorised exception to the 10-segment limit for these nets in this window only; log every removed segment.
- Place 0.25/0.15 POFV vias in the U6/U22 signal pads that need escape, fan out, then re-route the U6/U22 opens (PD_INP/INM, PD2_INP/INM, TX1/2/3, AFE_INM, AFE_BG, MISO_AFE, CS_AFE4900, SPI_MOSI, SPI_SCK, +3V3_ANA, MAX86178_INT, LED1/2/3_K) and then the ripped-up nets. Count each re-route attempt against the anti-loop rules.
- If the window cannot close on Layer 3 + F + B, use Power Layer 4 (In3.Cu) under the chips: shrink the +3V3/VBAT_SYS pours locally and route signals there, keeping the pours connected.
- If 6 layers still proves impossible for U6/U22, STOP and report exactly what is blocked. Do NOT switch to 8 layers.

### Step 6: remaining long nets
- AFE_N_PAD (J5.5 -> R36.1): outer layers only through the HV area; use the inner-layer gaps between hv_inner areas only where no hv_inner area is touched.
- DE0 / AD5940_GPIO0 long runs if still open.

### Step 7: cleanup
- Clear the ~248 copper items inside keepout rule areas (v8 hv_uncapped_final.json) by local reroute, not by editing rule areas.
- Fix the 74 small-via hole-to-copper / hole-to-hole spacing issues (v8 geometry_audit_final.json) to JLC's 0.2 mm.
- Fix dangling tracks/vias and the 9 SOT-23 pad clearance items where possible without loosening rules.

### Step 8: report
- Final `kicad-cli pcb drc --refill-zones --format json -o drc_final.json`; renders into renders/.
- REPORT.md: before/after numbers, every change, every nudge/move, every rip-up, BLOCKED nets with reasons, JLC rule set, and fab notes (6-layer, ENIG, via-in-pad POFV epoxy filled & capped, 0.15 mm holes).
- NO commit, NO push, delete nothing.
