# Supervisor instructions R2 #02 (priorities + expanded authority from the user)

## Expanded authority (user, 17:45) — log every use in change_ledger.json + REPORT.md
- Board size: chest wearable. Stay at 50 x 75 mm if at all possible. Grow ONLY after placement moves and via-in-pad have been tried, and then only a few mm (hard cap 55 x 80 mm); no other shape change without a stated reason.
- You may move/rotate ANY unlocked part, and (within the size cap above) reshape the board outline, and change stackup/via tech (8 layers or HDI only if truly needed; note cost) — netlist/function identical, no parts added/removed.
- Locked parts (skin-side sensor cluster U6/U22/U11/U20/U16, HV corridor parts, connectors J*) MAY now be moved if they are the blocker, but: skin sensors stay on the skin side, stay together as a cluster and still face/contact the skin; HV keeps its isolation clearances (rule areas move WITH their HV parts, same shape and clearance, never shrink); connectors stay reachable at the board edge. Prefer moving unlocked parts first.
- Do not loosen any JLC rule. No commit/push. Delete no files.

## Priority order (do not sink more than one more attempt into U19 now)
1. U19: ONE more attempt with a minimal-disturbance method: "drag with tracks" instead of rip-up. Move U19 (dy +1.00, or rotate 180 deg + dy +1.00 if that puts pins 3/4/7 facing open space south of y 44), move every track endpoint that sits on a moved pad centre by the same transform, keep the rest of each track, then only fix the resulting local clearance hits. Place the escape vias for pins 3 (ADS1292_PWDN), 4 (AD5940_RESET), 7 (IR_GATE) south of the hv_inner band (y > 43.4 + 0.2 + via radius). If it still fails, park U19 (mark DEFERRED, not BLOCKED) and move on.
2. U7 via-in-pad batch (13 opens, biggest win, sites were clear in the survey): 0.25/0.15 POFV at ball centres B3 BIOZ_FP, B4 AIN4_LPF0, B5 BIOZ_SP, B6 DE0, B7 VZERO0, C7 VBIAS0, D2 BIOZ_FN, D7 VREF_2V5, E7 SPI_MOSI, F2/F3 +3V3_ANA, F5 AD5940_GPIO0, F7 CS_AD5940; escape on In3 ("Signal Layer 4", now empty) first, In2 second, B third; reroute the single B.Cu MISO_AD track that blocks the F row. Split into two batches if needed (B/C/D rows, then E/F rows).
3. U22 via-in-pad + U18 nudge (R2-5 from #01) — LED1_K D1, LED2_K C1, LED3_K B1, MAX86178_INT B2 at x 26.9/26.5; escape on In3.
4. U6 via-in-pad + bottom ring rip-up (PROMPT_R2 R2-3/R2-4), escapes on In3/In2.
5. Leftovers (+3V3 stitches, TMP117_ALERT, J12 MP, AFE_N_PAD, U18.7/8, C59.1, U19 deferred) — if a pin is boxed in, move the part (authority above) instead of retrying the same geometry.
6. Cleanup to DRC clean (items_not_allowed, hole_clearance 0.2, dangling, SOT-23 clearances) — 0 real violations except the 29 accepted parity items and library/silk/text presentation warnings.
7. Fab outputs into ./fab/: JLC Gerbers (all 6 copper + masks, paste, silk, Edge.Cuts) + Excellon drill (PTH/NPTH) zipped as fab/vitalq_v2_gerbers.zip; BOM (fab/vitalq_v2_BOM.csv, JLC columns Comment,Designator,Footprint,LCSC) and CPL (fab/vitalq_v2_CPL.csv, Designator,Mid X,Mid Y,Layer,Rotation) via kicad-cli; fab/FAB_NOTES.md: 6-layer S/G/S/S/P/S, ENIG, 1.6 mm, via-in-pad POFV (epoxy filled + capped) for the 0.25/0.15 vias, min via 0.15 hole, track/space 0.09/0.1016. Verify: re-open gerbers with kicad-cli gerber/drill check or a parse, count layers/files, CPL refs == BOM refs == board footprints.

Keep the 3-line summary after every batch. I am watching every 5-10 minutes and will send exact geometry when you report a block.
