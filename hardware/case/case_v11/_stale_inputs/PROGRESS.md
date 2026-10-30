# case_v11 PROGRESS — mirror-fix of case_v10

## 2026-10-30 step 0 — intake / evidence review
- Read _inputs_case_v11 (README, scene.py, fit.py, fit_result.json, MIRROR_CHECK.png).
- Read case_v10 pipeline: extract_geometry.py (pcbnew -> geometry.json),
  parse_vrml.py (VRML -> subtree bboxes), merge_heights.py (pcb_y = CY - vrml_y),
  case_lib.py, build_case.py, verify_fit.py, export_step.py.
- Baseline shasums taken of case_v10, fix_v19/22/23/24, _inputs* (in /tmp/baseline_*.sha)
  before any work. JOURNAL.md + LICENSE exist only in the repo (local tree has none).
- Created case_v11 as a copy of case_v10 (minus _stale/, __pycache__, .DS_Store)
  + _inputs/ + _work/ scratch. case_v10 untouched.

## 2026-10-30 step 1 — fix_v24 ground truth
- Copied fix_v24/vitalq_v2.kicad_pcb to _work/v24_board.kicad_pcb (read-only source).
- pcbnew extract (KiCad 10.0.4 bundled python) -> _work/geometry_v24.json:
  bbox (-2.075,-16.575) 55.725x81.125 mm, 302 fps, 12 holes, 62 outline segs —
  outline, holes, pad positions/sides **byte-identical** to the fix_v22-derived
  geometry.json shipped with case_v10 (checked by script diff: 0 diffs).
- Populated ${KIPRJMOD}/lib from the design repo (vitalq.3d + snap.3d) so
  kicad-cli resolves real models: `pcb export glb --subst-models` now fails only
  on U2 (Texas_DSG0008A) and U17 (TDFN-8) — covered by courtyard boxes in verify.
- Exported _work/board_v24.glb (4.7 MB), board_v24_slab.glb (--board-only),
  board_v24.wrl (VRML, units mm).
- parse_vrml + merge_heights re-run on fix_v24 -> installed as case_v11/geometry.json
  (real VRML heights for 37 more parts vs the v22 datasheet fallbacks; J1 max 3.25,
  U1 3.10, J12 B 2.0).
- GLB frame measured: glb (x,y,z) = (kicad_x, height_above_B.Cu, kicad_y) metres.
  Anchors checked: v24_board_PCB slab x -2.05..53.6, z -16.55..64.5;
  USB-C mesh centre z_glb -7.5 = kicad y of J1; Pads_ECG z_glb ~63 = J5 y_k;
  WROOM overhang z_glb -22.25 = antenna overhang -22.31 mm. So glb_y is height,
  glb_z = +kicad_y.  Physical frame is (x_k, -y_k): confirmed by
  right-handedness and by _inputs/fit.py (mirror=True -> 0 deep points on v10).

## 2026-10-30 step 2 — root cause + fix decision
- Root cause: the case is designed in the KiCad *editor* frame (x right, y down,
  viewed from F.Cu). Exported straight to STL with z up, that frame is
  left-handed — i.e. every case_v10 STL is the mirror image of the real board.
- Fix decision: **mirror the case in Y at the export boundary** (y -> -y on every
  case-frame part). "Dropping the flip" in merge_heights was rejected: the
  pcbnew side of the data (outline, holes, pads, courtyards) is KiCad-frame
  regardless, so all literal design coordinates would have to be rewritten in
  the physical frame anyway — same mirror, but unverifiable line-by-line.
  Mirroring the finished solids is one provable transform applied at the exact
  boundary where the wrong handedness entered the artefact.
- contacts_assy slug: make_slug() already builds at (20.2,50.4); the call site
  translated by (+20.2,+50.4,-0.6) again -> single slug at (40.4,100.8),
  completely outside the case (checked by splitting the STL: 20 pogos +
  1 slug at (40.4,100.8), slug volume ~21 mm3). Fix: translate (0,0,-0.6) only.

## Verify rewrite + first v11 pass (checked)
- New `verify_fit.py`: real-geometry test. Board truth = pcbnew extraction of
  `fix_v24/vitalq_v2.kicad_pcb` (`_work/geometry_v24.json`) + kicad-cli GLB
  exports (`_work/board_v24_pads.glb`, `board_v24_slab.glb`). GLB->case frame:
  `(1000*x, -1000*z, 1000*y + ZB)`; board placed B.Cu-down at ZB=3.4, no mirroring.
- 150,000 sampled board/component surface points (+6000 courtyard-box points
  for U2/U17, the two models kicad-cli could not resolve).
- case_v10 FIRST (proof): FAIL — 14 checks fail with mirror signature:
  seat 1.60 (not 3.40), deep points 344+1209, no pins at holes, no USB
  opening, lands outside footprint, pogos 120mm off, slug at (40.4,100.8),
  pocket faces absent, ceiling gap 0.001. Saved `_work/verify_v10_output.txt`.
- Iterations found by the new verifier on v11 (all checked, real geometry):
  1. J7-field model stud dips 0.235 mm below B.Cu; west tip overlapped the
     seat wedge -> added wedge relief `rbox(-1.95,60.5,2.5,-0.6,63.05,3.45)`.
  2. R-lug strap fin end face sat flush (0 gap) with the E-tab edge at
     z>4.55 -> recessed the fin face to y19.7 over the tab band.
  3. Verifier bugs fixed en route: seat probe ring, USB corridor test,
     antenna ray direction, signed-distance sign on the union mesh,
     to_2D recentring in the section sampler.
- Overhang audit now encodes PRINT_NOTES' own rule: 45 deg cone OR
  open-to-plate OR <=2 mm ledge roof = allowed; only deep enclosed canopies
  count as bad (the old run's "0 bad" could not have come from this code on
  this trimesh — mesh_multiplane returns raw segments here, not paths).
- case_v11 RESULT: 24/24 PASS, inside=2 (0.004 mm grazes), deep=0,
  exit 0. case_v10: 14 FAIL, exit 1.
- Next: STEP export (must export phys frame too), renders, README, DRC/ERC.

## 2026-10-30 step 3 — STEP + renders (phys frame)
- `export_step.py` rewritten to the physical frame: real board slab from
  `_work/board_v24_slab.glb` (GLB x->x, -z->y, y->z, m->mm) seated at z=3.4;
  SW caps / light pipe / J8 plug translated at mirrored physical y
  (45.30,-34.61), (2.03,-57.17), (4.0,-42.9), (26.45,-53.4).
- `case_v11.step` written: 13 solids, 246,990 entities, all watertight.
- `render_views.py` rewritten for the phys frame + real GLB board
  (slab + models >0.5 mm3). Wrote renders/hero.png, closed.png,
  skin_side.png, exploded.png, worn.png. Hero/skin views inspected:
  pogo apertures + slug on the skin face, board seated inside.
- Stale v10 artefacts moved to `_work/stale_v10/` (old top-level renders,
  renders/render_*.png, case_v10.step, 18 MB board_vrml.wrl).

## 2026-10-30 step 4 — DRC / ERC (fix_v24, reviewer-equivalent rules)
- `_work/drc/`: fix_v24 pcb+sch + fix_v19 vitalq_v2.kicad_pro rules +
  fix_v23 vitalq_v2.kicad_dru (same recipe as fix_v24/verify).
- `kicad-cli pcb drc --severity-error --schematic-parity --format json`:
  0 violations, 0 unconnected, 0 shorts, 0 parity issues.
- `kicad-cli sch erc --severity-error --format json`: 0 violations.

## 2026-10-30 step 5 — integrity check (final)
- `shasum -a 256 -c` vs baselines for case_v10, fix_v19/22/23/24,
  _inputs_case_v11, _inputs_v24: 1927 files OK, 0 mismatches.
  Baselines + check log copied to `_work/sha_baseline/`.
- JOURNAL.md / LICENSE: no local copies exist (repo-only files); they are
  untouched in the clone — verified via `git status` (clean tree).
- Next: README, REPORT, repo branch `case/v11-mirror-fix`,
  `hardware/case/case_v11/`, push, PR (no merge).
