# Task: deboss the VitalQ emblem on the case cover

Work in ~/VitalQuant-place (branch devin/hw-v2-pcb). Commit locally only. Do NOT push.

## The emblem
Riyansh chose the round-1 "QRST monogram" mark (~/Downloads/VitalQ_Logo_v1/c1_qrst_mark_*.svg). He likes the REVERSED version: the Q ring with the key-like ECG tail ("key to your vitals"). Use that mark's geometry: the Q ring plus the PQRST tail. No wordmark text.

## What to do in ~/VitalQuant-place/case/case.py
- DEBOSS the emblem (cut INTO the surface, not raised) into the outer top face of the case COVER.
- Import or convert the SVG paths into build123d (e.g. `import_svg`). Scale and centre them, then project or wrap them onto the domed cover surface so the cut depth is uniform across the curve.
- Add these parametric constants at the top of case.py:
  - EMBLEM_SIZE: about 18–22 mm.
  - EMBLEM_X / EMBLEM_Y: centred by default, clear of the screw ears and of the battery pocket below.
  - EMBLEM_DEPTH: default 0.6 mm.
- The wall left under the engraving must be AT LEAST 1.0 mm. Check this explicitly against the battery pocket underneath, and reduce the depth or move the emblem if needed.
- Minimum feature width about 0.5 mm for FDM. Thicken the tail strokes if needed, but keep the PQRST shape recognisable.
- It must print with no supports (the cover prints rim-down, so the top-face deboss needs none, but check).
- Do NOT change anything else about the case.

## Outputs
- Updated cover STEP and STL, plus the assembly STEP.
- Renders: iso, top, and a close-up of the emblem lit at an angle so the deboss is visible.
- Put ALL case files (the base and cover STEP/STL, the assembly, the renders and the README updated with an emblem section) in a NEW folder: ~/Downloads/VitalQ_Case_v2/.
- After v2 is verified, delete ~/Downloads/VitalQ_Case_v1/. Verified means the files open, there are no overlaps, and the wall under the emblem is at least 1.0 mm.
- Commit locally (no push).

## Report
The emblem size, the depth, the minimum wall under the emblem (and where it is), the minimum feature width, and the commit hash.
