# Case v6: chest strap integration + refinement
Work in ~/Downloads/VitalQ_Hand/case_v6 (copy case_v5 as the start; do NOT modify case_v5 or any board file; fix_v9/vitalq_v2.kicad_pcb SHA-256 must stay 2143d6379537...).
Goal: the device must be worn on the chest with an elastic strap, like a Polar H10 / Garmin HRM chest strap.
1. Quick research (max ~20 min, then build): how commercial chest-strap sensors attach (snap studs, slot/loop lugs, clip-in pod), standard elastic strap widths (20-30 mm), and what keeps the pogo pins/electrodes pressed evenly on skin while breathing and moving. Pick the best fit for a 62x82 mm pod and write the decision in STRAP_DESIGN.md.
2. Implement: integrated strap slots/lugs (or clip-in cradle) sized for a common elastic chest strap width you choose, with strong roots (no thin weak bridges, PETG), smooth radiused edges so the strap does not fray or dig into skin, slight curvature/contour of the skin face if it improves contact without moving any contact out of reach, and strap pull loading the skin contacts evenly. Keep it wearable-sized; don't enlarge drastically.
3. Keep refining: comfort (edge radii, weight), printability (no new overhang failures), and anything you spot from case_v5.
4. Add checks to verify_fit.py: strap slot width/clearance, lug wall thickness >=2 mm, skin contacts still proud of skin face, and a simple strap-tension load check on the lugs. Re-run ALL checks.
5. Export STL + STEP, renders (assembled with strap shown, skin face, exploded, section through lug, J7, J3, USB-C), update docs.
Don't over-research; start building and fix whatever checks flag. Write CASE6 COMPLETE at the end of case_v6/FIT_REPORT.md only when every check passes and renders exist.
