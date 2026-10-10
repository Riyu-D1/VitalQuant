# Supervisor instructions R2 #01 (answers to your 5 questions + next batches)

Streak/acceptance policy for R2 (replaces the generic rule):
- R2-1 (restack) is structural and does NOT count toward the stop streak. Streak = 0 now.
- A batch is ACCEPTED if, after refilled DRC, unconnected does not rise AND real violations do not rise vs the batch start; it counts as "improving" if unconnected falls OR real violations fall. Stop only after two consecutive non-improving batches, then print "R2 STOPPED" and wait (as before).
- Per-net attempt counters for C59.1, U18.8, ADS1292_PWDN, IR_GATE, AD5940_RESET are reset, because the geometry changes below (part moves) make them new problems.

Answers:
1. Streak: reset (see above).
2. YES: local rip-up and re-route of +3V3_ANA, TX_5V and MISO_FL (and any other foreign F.Cu/B.Cu segment hit by the moved U18 pads/EP) inside x 23.5–29.5, y 39.5–49.0 is authorised. Log every removed segment uuid. Keep the U18 dx = -0.31 mm nudge (needed for U22 C1/D1 via-in-pad).
3. YES: local rip-up of the blocking F.Cu/B.Cu segments around U19, C59 and U18.8 is authorised, including inside the HV area ON OUTER LAYERS ONLY (hv_ownlayer only forbids pours). Re-routes inside any hv_inner area must stay on F.Cu/B.Cu; In2/In3 re-routes only outside hv_inner. Log every removed segment.
4. NO: do not accept a via touching the hv_inner edge. Use the part moves below instead.
5. YES: run the U19.16/D30.2 GND island fix by itself as batch R2-3 right now (43 -> 42). Accept it.

Placement moves (supervisor-instructed; verified on a copy: courtyard-legal, inside board outline, and they move ALL signal pads out of every hv_inner area):
- U19: move dy = +1.00 mm, dx = 0 (17.0877,43.4355 -> 17.0877,44.4355), rotation unchanged. With this move 0 U19 net pads remain inside hv_inner. Re-attach all U19 pin tracks (shift/re-route stubs), then drop the vias for ADS1292_PWDN, IR_GATE, AD5940_RESET (and any other U19 inner-layer escapes) OUTSIDE hv_inner, i.e. below y 43.4 band edge where the pin now lies. Alternative legal positions if needed: (dx,dy) = (+0.25,+1.0), (+0.5,+1.0), (+0.75,+1.0), (+1.0,+1.0).
- C59: move (dx,dy) = (+0.25, -1.00) (15.6477,40.5098 -> 15.8977,39.5098), rotation unchanged. This puts C59.1 out of hv_inner and is courtyard-legal (1.03 mm; authorised exception to the 1 mm passive limit). Re-attach C59.2 and give C59.1 a 0.4/0.2 via to the +3V3 plane (now In4) outside hv_inner. Alternatives: (+0.5,-1.0), (+0.75,-1.0).
- Log both moves in change_ledger.json and REPORT.md.

Batch order now:
- R2-3: GND island U19.16/D30.2 alone (accept at 42).
- R2-4: U19 move + C59 move + re-attachment + ADS1292_PWDN / IR_GATE / AD5940_RESET / C59.1 escapes (treat as one batch; acceptance at batch end).
- R2-5: U18 nudge (-0.31) + authorised rip-up/re-route around U18 + U18.7/U18.8 +3V3 vias (pin8 now outside hv? check; if still inside, F.Cu stub out of hv_inner to its own via).
- Then continue with PROMPT_R2 R2-3.. steps (via-in-pad fan-out U22/U6/U7, U6/U22 bottom-ring rip-up, remaining routing), renumbered sequentially.
After every batch print the 3-line summary. I render each batch and will send more exact instructions.
