# PRIORITY task: enlarge the VitalQ board outline for Quilter (v7)

This interrupts the case research. Resume that afterwards.
Commit locally only. Do NOT push.

## Background
Quilter's first job on ~/Downloads/VitalQ_Quilter_v6 (46 x 70 mm, 299 parts) only reached 88–91% routed because the board is too dense. Riyansh chose a bigger board.

## Do this
1. Make a NEW folder, ~/Downloads/VitalQ_Quilter_v7, copied from v6 (project name stays vitalq_v2: vitalq_v2.kicad_pro, .kicad_sch, .kicad_pcb).
2. Enlarge the Edge.Cuts outline to about 50 x 75 mm.
   - Keep the same shape style and corner radii.
   - Keep the mounting holes H1/H2 at a sensible inset from the new edges.
3. Keep the locked parts sensible:
   - Keep the skin-side sensor cluster and the HV/electrode-protection corridor locked and TOGETHER. Shift the whole group as one if needed so it stays centred. NEVER break up the skin-facing cluster.
   - Move edge connectors (J1 USB-C and any others that must sit on an edge) so they stay flush with the new edge, on the same edge they were on originally.
   - If applicable, keep the ESP32 antenna and its keep-out at the board edge.
   - Keep ALL 59 previously locked parts locked. Leave everything else unlocked and parked off-board exactly as in v6, so Quilter places it.
4. Do NOT change any connectivity, part, footprint, symbol or net.
5. Resave the files with KiCad itself, so Quilter accepts them:
   `/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli sch upgrade --force` and `pcb upgrade --force`.
   Keep the schematic-to-board links (path, sheetname and sheetfile) and the project name vitalq_v2 intact.
6. Copy bypass_capacitors_fixed.csv (85 rows) into v7.

## Verify (all must pass)
- 299 footprints on the board.
- The schematic-vs-board parity check shows only the known 29 BOM-flag items.
- A netlist diff against v6 shows NO connectivity change.
- Report the outline size you measured.
- No locked part lies outside the board edge or overlaps it.
- Delete ~/Downloads/VitalQ_Quilter_v6 ONLY after v7 passes every check. Then apply the same change in ~/VitalQuant-place and commit it locally (no push).

## Report
- The new outline dimensions.
- Which locked parts moved, and by how much (dx, dy in mm).
- The results of every verification check, and the commit hash.
Then resume the queued case research task.
