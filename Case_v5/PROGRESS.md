# case_v5 progress

## Paused – where I got to / next steps

**State: 92/94 checks PASS.** All geometry rebuilt watertight; `FIT_REPORT.md`
ends with `CASE5 COMPLETE` (terminator present, 2 checks still failing).

Done this session:
- Replaced the unprintable battery-cup upstand with a flat lid + 6 snap-pin
  holes, and moved the pins off the cup floor into a separate **pin_plate**
  part (prints pins-up; pushes through cup floor + lid holes, barbs catch the
  lid cavity face at cup-z −1.65, catch 0.30, prong strain 2.88%).
- Bay cup is now a plain tray (overhang audit clean) with taller walls 6.2,
  door grooves at z5.15–6.05; battery_door updated to match.
- Fixed `edge_profile_loft` to bottom-edge insets (no undercut ledges) →
  base audit clean.
- Lug tops flush to ZLID; antenna bulge flat-topped to ZLID (plate contact).
- Antenna pocket ends at OY0−1.55 (clears U1's −13.525 tab); bulge OY0−2.6 →
  envelope now 62.2×82.2×9.6 (was 0.1 over).
- U13 vent relocated to (34.8, 32.0) off the cup footprint; U23 gets a Ø2.0
  bore inside the Ø4.6 membrane recess (floor z1.2 measured).
- Gasket: slug hole Ø3.3, J7/J13 apertures merged into one opening (web was
  0.19); margin 0.29, min web 0.45.
- Verifier fixes: board-edge check subtracts H1/H2 stud holes, cell probe
  placed on the pin plate (bottom 0.85), wall probes moved to z5.0, full-cone
  overhang audit at 0.2mm pitch, networkx installed.

Last verifier run was interrupted mid-overhang-audit. On resume:

1. Re-run `python3 verify_fit.py` — expect ~93-94/94; the only open items are
   `overhang audit top` (was 188 cells, mostly resolved by the lug/bulge/loft
   fixes) and `overhang audit pin_plate` (was 24 cells, fixed by moving the
   +y pin row to y38.5 — needs confirming).
2. If any audit cells remain, localize with the voxel-cluster debug snippet
   (prints flagged cell coords per part) and fix the specific feature.
3. Then continue the remaining plan: renders (assembled, top, bottom/skin,
   exploded, sections through J7/J3/USB-C), STEP export if feasible, and
   update ASSEMBLY.md / PRINT_NOTES.md / CONTACTS_BOM.md.
