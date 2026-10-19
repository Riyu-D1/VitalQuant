# VitalQ v8: finish routing Quilter candidate 1.1 (think first, then route)

Working folder: ~/Downloads/VitalQ_Route_v8 (KiCad 10, kicad-cli at /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli).
Files: vitalq_v2.kicad_pcb (Quilter v7 candidate 1.1, 50x75 mm, 6-layer, 299 parts), vitalq_v2.kicad_pro (from Quilter), vitalq_v2.kicad_sch (v7).
Pristine copy of the Quilter board: quilter_raw/vitalq_v2.kicad_pcb. Never edit it.
Baseline DRC: drc_baseline_quilter_1_1.json. That's 56 unconnected items on 46 nets (GND x14, +3V3 x4, +3V3_ANA x4, SPI_MOSI x4, SPI_SCK x4, the rest 2 each), plus 747 violations: clearance 220, hole_clearance 200, items_not_allowed 199, track_dangling 39 and others. There are also 29 schematic-parity items that are known, accepted BOM-flag items. Leave them alone.
Quilter also flagged 5 outline collisions and 3 dimension violations.

## Hard rules
- No change in function. Remove no parts, add no unnecessary parts, and make no netlist changes. The skin-side sensor cluster, the HV corridor, the electrode pads, the connectors and H1/H2 stay exactly where they are; they are locked. You may nudge other small passives by up to 1 mm if that opens a channel, and you must log every nudge.
- Manufacturing target is JLCPCB 6-layer. Check JLC's current capabilities page for min trace and space, via hole and pad, and hole-to-hole spacing, and write the numbers you use into PLAN.md. Never "fix" violations by loosening rules beyond what JLC can make.
- Work in this folder only. Don't touch ~/VitalQuant-place's working tree except for the final push at the end.

## Phase 1: understand the board BEFORE routing anything (no copper edits in this phase)
Use pcbnew's Python API and DRC to write ANALYSIS.md:
1. Stackup and layer use: which inner layers are GND/power planes or pours, and where they are broken.
2. Triage the 747 violations into (a) real copper problems, (b) rule mismatches between Quilter's .kicad_pro and KiCad (e.g. via-in-pad, keepouts or courtyard rules causing hole_clearance and items_not_allowed), and (c) cosmetic silk/text items. Give counts for each, and say which ones need fixing for JLC.
3. For each of the 46 unrouted nets: both endpoints (ref.pad, xy, layer), the straight-line distance, what blocks the direct path on each layer, and the nearest free via site. Classify each net as PLANE (finished with a via into an existing GND/power plane or pour), EASY, MEDIUM or HARD.
4. A congestion map of the board: render a PNG heatmap or annotate a board render showing where the unrouted nets cross dense areas.
Then write PLAN.md: a predicted route for every net (layer sequence, via positions, which existing tracks need a small local reroute), in this order: PLANE nets first, then HARD, then MEDIUM, then EASY. Also say how each outline collision and dimension violation will be fixed.

## Phase 2: route to the plan, in batches
- Make the routes with scripted pcbnew edits (or interactive-router-equivalent geometry) that follow PLAN.md. Prefer stitching vias into planes for GND and power.
- Work in batches of about 5 nets. After each batch, run DRC and append one line to PROGRESS.md: the batch number, the nets done, the unconnected count, the real-violation count, and the time. Save a dated backup of the .kicad_pcb before each batch.
- Rip-up is allowed only locally: at most about 10 existing segments per net, logged with the reason. Never clear the board or redo Quilter's routing wholesale.

## Anti-loop rules (mandatory)
- At most 3 attempts per net, and each attempt must use a different approach (a different layer, via site or detour side). After 3 failures, mark the net BLOCKED in PROGRESS.md with the reason and move on.
- If two batches in a row fail to lower the unconnected count, stop routing and go to the final report.
- Freerouting is a last resort. Use it only on a named list of BLOCKED nets, with all existing tracks locked or fixed, one run, a 10-minute timeout, and accept the result only if DRC gets better. Never run it on the whole board, and never run it more than once.
- Never repeat a command or approach that has already failed. Check PROGRESS.md before every attempt.

## Phase 3: clean up and deliver
- Fix the outline collisions and dimension violations. Fix every real copper DRC violation and any dangling tracks.
- Goal: 0 unconnected and 0 real violations. If some nets are still BLOCKED, deliver the best board anyway with an exact list.
- Export renders with kicad-cli (top and bottom PNG, plus a 3D render if it works) into renders/, then do a final DRC to drc_final.json.
- Write REPORT.md covering: before and after numbers, every change, every nudge, any BLOCKED nets with the reason, and the JLC rule set used.
- Push this folder (without quilter_raw/) to branch `vitalq-deliverables` of Riyu-D1/VitalQuant under deliverables/VitalQ_Route_v8/. Use a separate git worktree so the main checkout's branch isn't switched. Verify with git ls-remote and git ls-tree. Do NOT delete any local files; that will be done after review.
