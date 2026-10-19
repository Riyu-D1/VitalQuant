# VitalQ v9 — Phase R2: structural fix + route to 0 unconnected

This is a FRESH PHASE (R2). The two-non-improving-batch streak and the per-net attempt counters from B0–B2/F0 are RESET to zero. Batch ids in this phase: R2-1, R2-2, ... One PROGRESS.md line per batch, as before.
Working board: ./vitalq_v2.kicad_pcb (with ./vitalq_v2.kicad_pro and ./vitalq_v2.kicad_dru). Your supervisor (Grok Bot) renders your board after each batch and may type exact next instructions into this window; follow them, they are within these rules.

## Goal
0 refilled unconnected items, JLC-legal, refilled DRC clean except the 29 accepted schematic-parity items (and library/silk/text presentation warnings, reported separately as before).

## Hard rules (unchanged from PROMPT.md / AGENTS.md)
- No netlist or function change; add/remove no parts. Locked parts (U6, U22, U16, U11, U20, J12, connectors, electrodes, H1/H2) stay exactly where they are. Skin-side sensor cluster stays clustered. HV isolation: never put vias or inner-layer copper inside hv_inner rule areas, never edit/shrink/move ANY rule area.
- JLC 6-layer limits, never loosened: track/gap >= 0.09 mm (prefer 0.1016), via hole-to-hole >= 0.2, via hole to any other copper >= 0.2, min via 0.25 dia / 0.15 hole, through vias only. Via-in-pad only as POFV (epoxy-filled + capped), flagged in REPORT.md.
- Truth = `kicad-cli pcb drc --refill-zones --save-board --all-track-errors --schematic-parity --format json` on the saved board with .kicad_pro/.kicad_dru present.
- Dated backup before every batch (backups/YYYYMMDD_HHMMSS_<batch>/), exactly one PROGRESS.md line after every batch. NO git commit / push / worktree. Delete no files (archive/ subfolder for superseded files).
- Anti-loop: max 3 genuinely different attempts per net; after 3, mark BLOCKED with reason. The two-non-improving-batch stop still applies within R2, BUT a stop means: write the PROGRESS line, print "R2 STOPPED: <reason>" and WAIT for supervisor instructions in this window (do not end the session).
- Allowed moves (log every one: ref, from, to, rotation, reason): unlocked passives <= 1 mm; unlocked ICs U18 and U19 as specified below; anything bigger only if the supervisor instructs it.

## Steps

### R2-1 Restack to S/G/S/S/P/S
- Top=signal, In1 "Ground Layer 2"=GND plane (keep GND.LAYER_1 unchanged), In2 "Layer 3"=signal, In3=signal (was power), In4=power, Bottom=signal.
- Move the zones +3V3.LAYER_3 and VBAT_SYS.LAYER_3 from In3.Cu to In4.Cu (same outline, priority, net, clearance, thermal settings). Remove zone GND.LAYER_4 (In4 GND pour); move it to archive as a note in REPORT (the zone, not a part; GND stays fully connected through In1 — verified on a copy: refilled opens stay 43, GND not worsened).
- Rename layers for clarity: In3.Cu user name "Signal Layer 4", In4.Cu user name "Power Layer 5" (cosmetic; update REPORT fab notes: stack S/G/S/S/P/S).
- Do NOT touch rule areas (they already cover all inner layers).
- Refilled DRC: unconnected must stay 43 (or lower), no new real violations. Log R2-1.

### R2-2 U18 nudge + HV-zone parts
- U18 (W25Q512 WSON-8, F side, unlocked) sits over U22 (B side) balls. Its pins 2 (MISO_FL) and 3 (+3V3) straddle U22 column x=26.9 (C1 LED2_K @26.9,47.75; D1 LED1_K @26.9,48.15), so no via-in-pad is possible there today.
- Move U18 by dx = -0.31 mm (27.2096,44.3670 -> 26.8996,44.3670), rotation unchanged (90). This centres the pin2/pin3 gap on x=26.9: pin2 edge 26.515, pin3 edge 27.285, so a 0.25/0.15 via at x=26.9 has 0.31 mm hole-to-pad and 0.26 mm copper-to-pad. Free columns for U22 C/D-row via-in-pad after the nudge: x=25.7 and x=26.9 only. Re-attach every U18 pin track (CS_FLASH, MISO_FL, +3V3, GND, SPI_MOSI, SPI_SCK, +3V3) with short jogs; check the +3V3 via @(27.56,48.92) still clears pin3.
- C59.1 (+3V3): via 0.4/0.2 @(14.90,39.95), F.Cu track C59.1 -> (14.90, C59.1.y) -> via (proven 43->42 on a copy).
- U18 pin7 (+3V3): via @(26.65,40.15) to the +3V3 power plane (now In4). Pin8 (+3V3, 25.305-0.31=24.995, 40.617) is inside hv_inner: connect pin8 to pin7 on F.Cu along the pin row ONLY if it clears +3V3_ANA; otherwise route pin8 on F.Cu out of the hv_inner footprint to its own legal via.
- U19: do NOT relocate (no free courtyard spot within 9 mm). Instead, for every U19 open (ADS1292_PWDN, IR_GATE, AD5940_RESET, GND island U19.16/D30.2), route on F.Cu/B.Cu out of the hv_inner area (x14.3-19.7 / 20.8-26.1, y40.1-43.4) and drop the via outside it. Outer layers are allowed inside hv_inner (hv_ownlayer only forbids pours).
- Refilled DRC, log R2-2.

### R2-3 Via-in-pad fan-out under U6 / U22 / U7
- Via size 0.25/0.15 POFV, placed exactly at the ball centre. Precedent: U6 already has 0.3/0.15 GND vias in B4, D1, D5.
- U22 (B side): B1 LED3_K (26.9,47.35), B2 MAX86178_INT (26.5,47.35), C1 LED2_K (26.9,47.75), D1 LED1_K (26.9,48.15) — plus any other U22 ball still in the unconnected list whose column is free on F (rows C/D: only x=25.7 or 26.9 after the U18 nudge; rows A/B: all columns).
- U6 (B side): every ball still unconnected: A1 AFE_INM, A2 PD_INP, A3 PD_INM, B2 PD2_INP, B3 PD2_INM, D2 AFE_BG, D4 TX1, E2 MISO_AFE, E3 CS_AFE4900, E4 TX3, E5 TX2, F2 SPI_MOSI, F3 SPI_SCK, A4/C1/C4 +3V3_ANA as needed. Above U6 on F only J8 pads (25.18/26.45, 52.56) and the ESP_RX/+3V3_ANA F tracks are near; reroute those F tracks if a via needs the spot.
- U7 (F side): B3 BIOZ_FP, B4 AIN4_LPF0, B5 BIOZ_SP, B6 DE0, B7 VZERO0, C7 VBIAS0, D2 BIOZ_FN, D7 VREF_2V5, E7 SPI_MOSI, F2/F3 +3V3_ANA, F5 AD5940_GPIO0, F7 CS_AD5940 (F row blocked on B by one MISO_AD track: reroute it locally).
- Each new via must clear 0.2 mm hole-to-copper to every foreign track/via/pad on ALL six layers; rip blocking foreign segments in the window only (logged) — that is step R2-4.

### R2-4 Rip up the bottom ring around U6/U22
- In the window x 23.5–28.5, y 45.5–53.0, B.Cu: rip up local segments of TX_5V (24 seg), TX4 (11), +1V8 (13), PD_K (10) that block ball escapes; also local In2 segments of ESP_TX/ESP_RX/AFE_INP and F segments of ESP_RX/MISO_FL/CS_FLASH/+3V3_ANA if they block via sites. Log every removed segment uuid.
- Re-route the ripped nets FIRST on In3 (now free signal layer under the chips) and In2, then the outer layers.

### R2-5 Route the remaining opens
- Order: U22 nets, U6 nets, U7 nets, +3V3/GND island stitches, TMP117_ALERT, J12 MP, AFE_N_PAD, then any leftovers. Use In3 + In2 under the chips (they are now the two inner signal layers). Keep 0.1016 tracks; 0.09 only inside BGA fan-out windows.
- After each batch: refilled DRC; reject the batch if unconnected rises or real violations rise.

### R2-6 Cleanup + final
- Clear copper inside keepout rule areas (items_not_allowed) by local reroute, fix hole_clearance / hole_to_hole to 0.2, fix dangling tracks/vias and SOT-23 pad clearances.
- Final refilled DRC with --schematic-parity -> drc_final_R2.json; renders per copper layer into renders/R2/; REPORT.md R2 section: before/after numbers, every move/rip-up/via-in-pad, stackup change, BLOCKED list.

Start with R2-1 now. After each batch print a 3-line summary (batch, unconnected, real violations, what is next) so the supervisor can read it in this window.
