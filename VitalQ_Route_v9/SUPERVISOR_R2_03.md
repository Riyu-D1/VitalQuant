# Supervisor instructions R2 #03 — KEEP the R2-6 candidate and repair it (do not discard)

R2-6 candidate (candidates/R2-6/vitalq_v2.kicad_pcb) is a real improvement: unconnected 42 -> 32, real 252 -> 245, hole_clearance 40 -> 26. Its 7 "new faults" are 6 track_dangling + 1 via_dangling WARNINGS plus 2 nets it broke (SWEAT_WE, MISO_AD). Make it the base of batch R2-7 (copy it over the working board after a dated backup) and repair on top of it. Acceptance for R2-7: unconnected < 32 + 0 and real <= 245 and no new dangling items.

I rendered the candidate (F/In2/In3/B around U7) and queried its copper. Exact fixes:

## 1. Dangling In3 stubs (delete the dangling segment only; the nets are connected elsewhere)
- CS_AD5940, Signal Layer 4: stub ending near (36.536,10.181) (vertical (36.586,9.55)->(36.586,10.28) leftover).
- VBIAS0, Signal Layer 4: (36.486,11.431), 0.65 mm stub west of C7 via (36.586,11.481).
- BIOZ_FP, Signal Layer 4: (38.136,11.981), 0.7 mm stub south of B3 via (38.186,11.881).
- BIOZ_SP, Signal Layer 4: (37.436,11.981), 0.7 mm stub south of B5 via (37.386,11.881).
- BIOZ_FN, Signal Layer 4: (38.686,11.031), the dangling part of the T east of D2 via (38.586,11.081) (keep the part that leads to the long vertical at x~38.99 if that is the real route).
- uuids from your decision.json: 35a19dc6, 3854008f, 47792255, adf22370, eec6d8c7 (check each is one of the above before deleting). Pre-existing GND dangling (28.936,37.201) / via (6.829,62.871) / AFE4900_RESETZ zero-length (10.75,61.05) are old — leave them for cleanup.

## 2. SWEAT_WE (U7 C5 @ 37.386,11.481) — currently boxed in by via-in-pads
Neighbouring via-in-pads: D5 GND (37.386,11.081), C4 GND (37.786,11.481), B5 BIOZ_SP (37.386,11.881); the only free neighbour pocket C6/D6 (36.986,11.481 / 11.081) is closed by C7 VBIAS0, D7 VREF_2V5, E6 GND, B6 DE0 vias. At 0.4 pitch no track can pass between two via-in-pads (JLC 0.2 hole-to-track), so a via must go.
- Delete the GND via-in-pad at D5 (37.386,11.081) (GND is redundant here: E6 and C4 GND vias stay). Tie D5 pad to E5 pad (37.386,10.681, GND) with a straight 0.1016 F.Cu track; make sure E5 reaches E6 (36.986,10.681) or E4/E3 GND on F (add the 0.4 mm pad-to-pad F track E5->E6 if not already there).
- Inner-layer channel now open (all of these balls have NO via): D5 (37.386,11.081) -> D4 (37.786,11.081) -> D3 (38.186,11.081) -> C3 (38.186,11.481) -> C2 (38.586,11.481) -> B2 (38.586,11.881) -> A2 (38.586,12.281) -> south out of the array; and the north branch D4 -> E4 (37.786,10.681) -> F4 (37.786,10.281) -> G4 (37.786,9.881) -> north out. Track centres on ball centres are 0.40 from every neighbouring via (legal: needs >= 0.32 for a 0.09-0.1016 track).
- Route SWEAT_WE from the C5 via on In2 (or In3) through one of those channels to the rest of the net. The net continues on B.Cu from via (36.95,14.55) down to (36.95,16.0) -> (37.15,16.35) -> (38.3,17.5) -> (38.65,17.75..18.9) ...; you may join that B track anywhere (e.g. drop a new 0.4/0.2 via on the In2/In3 path and land on the B track near (37.15,16.35)-(38.3,17.5)), and then delete the orphaned old In2 piece (36.85,12.70)->(36.8,14.4)->via(36.95,14.55) and that via if they become dangling.

## 3. MISO_AD (U7 E8 @ 36.186,10.681 -> R91.1 @ 44.313,11.855)
- The old B.Cu run along y~10.24 was ripped because it sat on the F-row via-in-pads (y 10.281). Left: F stub E8 -> via (34.465,10.447) with B stub to (34.607,10.305); and R91 side F track -> via (43.224,11.147) with B stub to (43.082,11.006).
- Via-free inner channels through U7: row G (y = 9.881, all G balls have no via) and column 8 (x = 36.186, no vias). On B there are now only via barrels under U7 plus the CS_AD5940 / SPI_MOSI escape diagonals west of U7 (to (36.586,10.281) and (36.586,10.681)).
- Preferred: B.Cu from via (34.465,10.447) north/around the CS_AD5940 diagonal to y ~9.3, east along y ~9.3 (north of row G, clear of the G-row pads which are F-only), then down the east side to via (43.224,11.147). If B is blocked west of U7, use In2 along row G (y 9.881) between the F-row vias (0.40 clearance) — but first check In2's diagonal near (35.0,10.95)->(37.64,8.5); use In3 if that one is in the way. Reuse the two existing MISO_AD vias; delete the two 0.2 mm B stubs if not used.

## After R2-7
Continue SUPERVISOR_R2_02 priorities: U22 via-in-pad + U18 nudge next, then U6. Print the 3-line summary after each batch.
