# Case v7: maximum refinement
Work in ~/Downloads/VitalQ_Hand/case_v7, starting from a copy of case_v6 (106/106 pass, with the strap). Do NOT modify case_v6 or any board file. The SHA-256 of fix_v9/vitalq_v2.kicad_pcb must stay starting 2143d6379537.
Refine the case as far as possible, working autonomously in rounds. Each round: pick the weakest areas, improve them, re-run all checks, and log the round in PROGRESS.md. Areas to cover:
- Comfort on the chest: skin-face contour, edge radii, weight, thickness, and strap pressure distribution.
- Electrode and pogo contact reliability during breathing and motion.
- Durability: drops, the snap fits, lug fatigue, and sweat sealing (gasket, USB-C flap).
- Printability: orientation, supports, tolerances, and the TPU parts.
- Ease of assembly and battery swapping, the button and light-pipe feel, and a clean, premium look.
Add a check for every new feature. Don't over-research; build first and fix whatever the checks flag. Keep it wearable-sized.
Stop when further changes give no meaningful gain, or after about 5 rounds. Then export the STLs and STEP, make all the renders (including the worn view), update the docs, and write CASE7 COMPLETE at the end of case_v7/FIT_REPORT.md only if every check passes.

## Final verification (mandatory, before CASE7 COMPLETE)
After refining, run one exhaustive verification and write the results to case_v7/FINAL_VERIFY.md:
- Case: all fit checks, plus physical simulations: strap-tension stress on the lugs, a 1.5 m drop or impact on the snaps, contact pressure on every skin contact while breathing (about ±5 mm of chest movement), thermal coupling to the TMP117, sweat ingress at the seals, and printability (overhangs, minimum walls, tolerances).
- PCB (fix_v9, read-only; work on copies): re-run every check in verify/final9, including DRC with refilled zones, schematic parity, a side check of every footprint (skin-side parts on B.Cu), power and battery charging over USB-C, signal-integrity notes, the HV gaps, the 238-patient sepsis simulation, and board-to-case fit against the case_v7 STEP.
- Fix any case failure in case_v7. If the PCB fails, do NOT edit fix_v9; report it.
Write CASE7 COMPLETE only when everything passes.
