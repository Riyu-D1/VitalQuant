# VitalQ case_v11 — REPORT

Date: 2026-10-30. KiCad CLI **10.0.4**
(`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`).
Python: system `python3` + trimesh/shapely/manifold3d; pcbnew extraction via
KiCad's bundled python 3.9.

Scope: fix the mirror fault in `case_v10` — **copies only**, all work in
`case_v11/` + `_work/` scratch. Board of truth:
`../fix_v24/vitalq_v2.kicad_pcb`.

## 1. Root cause (checked)

case_v10 builds geometry in the KiCad *editor* frame: `merge_heights.py`
converts VRML y with `pcb_y = CY - vrml_y`, i.e. all design coordinates are
KiCad coordinates (x right, y down, viewed from F.Cu). `build_case.py` then
exported those solids straight to STL with z up. A KiCad-frame drawing with
z-up is **left-handed** — every v10 STL is the mirror image of the real
board in Y.

Evidence (checked, from `_inputs_case_v11/fit.py` + `fit_result.json`,
reproduced here by the new verifier): with the board Y-mirrored
("designframe") v10 reports 0 deep points; placed as-is (physical frame)
it reports 344 deep points in `case_base` and 1209 in `case_top`
(max depth ~0.97 mm). J1 (USB-C, physical (29.5, +7.8)) sits 7.45 mm from
the v10 window; the old FIT_REPORT passed only because it measured the
board in the same flipped frame it had just generated — self-consistent,
not real geometry.

## 2. The fix (checked)

**Mirror the case solids in Y at the export boundary** (`phys()` in
`case_lib.py`, applied to every part in `build_case.py`'s export loop and
to assembled offsets in `export_step.py`/`render_views.py`).

Alternative considered and rejected (inferred): "dropping the flip" in
`merge_heights.py`. The pcbnew side of the data (outline, holes, pads,
courtyards) is KiCad-frame regardless of what the VRML half does, so
every literal coordinate in `build_case.py`/`case_lib.py` would need
rewriting in the physical frame — the same mirror, but smeared across
hundreds of unverifiable literals. Mirroring finished solids is one
provable transform at the exact boundary where the wrong handedness
entered the artefact.

Second fix (checked): `contacts_assy.stl` applied the slug translation
twice — single slug ended at (40.4, 100.8), outside the case. Call site
now applies only (0,0,-0.6); exactly one slug at physical (20.2, -50.4).

Reliefs added after real-geometry verification (checked):
- `rbox(-1.95, 60.5, 2.5, -0.6, 63.05, 3.45)` — a J7-field footprint model
  stud dips 0.235 mm below B.Cu; its west tip clipped the seat wedge.
- `rbox(47.5, 19.65, 4.4, 55.6, 20.01, ZT_MAX+0.1)` — the R-lug strap fin
  ended flush with the east-tab edge (0 mm contact).

## 3. New verifier (checked)

`verify_fit.py` is standalone and real-geometry-driven:
- Board truth: `pcbnew` extraction of `fix_v24/vitalq_v2.kicad_pcb`
  (`_work/geometry_v24.json`) + `kicad-cli pcb export glb`
  (`_work/board_v24_pads.glb`, `board_v24_slab.glb`). U2/U17 models are
  unresolved by kicad-cli — covered by 6000 courtyard-box points.
- GLB→physical frame: `(1000*x, -1000*z, 1000*y + 3.4)` mm — mapping
  verified against J1, J5 pads, WROOM overhang anchors. Board placed
  B.Cu-down at the *measured* seat, **no mirroring allowed**.
- 150,000 sampled board/component surface points; `contains()` +
  unsigned `closest_point` distances (union signed distances are
  unreliable on this trimesh — checked).
- 24 checks: parts present, seat plane, base/top deep-point counts,
  mounting holes, USB-C window + overmould corridor, 20 electrode lands
  over apertures, land↔pogo XY, thermal slug, antenna/east tab pockets,
  pocket depths, perimeter wall clearance, lid ceiling, component pinch,
  floor clearance, 402535 bay, 4 overhang audits, watertight.
- Run: `python3 verify_fit.py [--case-dir DIR]` → `FIT_REPORT.md` +
  exit 0/1.
- `_work/` assets (GLBs + `geometry_v24.json`, ~12.3 MB total) are
  **committed** — the verifier runs from a fresh clone with no build
  step. Regeneration commands in `_work/README.md`.

### case_v11 result — PASS (checked)

`FIT_REPORT.md`: **25/25 PASS**, 0 board points deeper than 0.1 mm
(2 grazes at 0.017 mm), seat 3.400 mm, hole axes aligned, USB window
13.2 mm wide centred 0.15 mm off J1, 20/20 lands over apertures with a
pogo within 0.20 mm, one slug at U11, pocket gaps 0.99/0.30 mm, min wall
gap 0.300 mm, min ceiling 0.501 mm, min lid pinch 0.369 mm, floor
0.365 mm, TP29/TP36 lid ledge 0.283 mm, battery fits, 0 bad overhang
cells, all STLs watertight. Exit 0.

### case_v10 result — FAIL (checked, mirror signature)

`_work/verify_v10_output.txt`: **11 PASS / 14 FAIL** (of 25), verdict
"mirror-frame fault signature present":
- seat 1.600 mm (design 3.40); deep pts base 344 / top 1209 (max 0.967)
- mounting holes: no pins found at H1/H2
- USB-C: no through-opening at J1 mouth; corridor intersection 150.9 mm³
- electrode lands: 0/20 over apertures ("land outside case footprint"),
  nearest pogos ~121 mm away
- slug at (40.4, 100.8) not (20.2, -50.4)
- pocket faces absent (antenna gap 42.5, east −152.6), edge points inside
  walls, ceiling gap 0.001

The verifier passes v11 and fails v10 — it measures real geometry, not
the case's own design frame.

## 3b. Follow-up pass (checked, 2026-10-30)

Review-driven fixes applied on top of the mirror fix:

- **Fresh-clone verification**: `_work/` assets committed (~12.3 MB);
  `verify_fit.py` runs as cloned, no board-file build step.
- **Thin parts thickened**: `pin_plate` 0.35 → 0.60 mm, `bay_cup` floor
  0.5 → 0.8 mm — both now above PRINT_NOTES' own 0.4 mm minimum. The
  extra 0.3 mm of cup floor grew *into* the lid's battery-pocket depth
  (which had margin), so the cup/door stack moved down 0.3 mm and the
  plate up 0.25 mm to keep absolute part planes; assembled overall
  height is now **16.75 mm** (was 17.05). No stack growth needed.
- **Clearances widened without moving the board**: board-edge↔wall
  0.100 → **0.300 mm** (≥0.20); component↔lid pinch 0.086 →
  **0.369 mm** (≥0.20) via a strap-fin/R-lug relief covering
  y 28.8–30.6, x 48.5–54.6 (KiCad frame). The wall check now skips the
  intentional F-clamp ledge overhang band (the clamp interface, not a
  wall face).
- **TP29/TP36** (fix_v25, PR #21, lid side — RLDREF (46.88, 7.60),
  IOVDD (45.04, −9.44) KiCad): pad edges touched the clamp-wedge inner
  face (~0.4 mm lateral). Added lid reliefs; verifier row now measures
  **0.283 mm** min gap ≥ 0.03.
- **Watertight fix (checked)**: pin-hole chamfer steps straddle the
  0.30 mm-deep plate-pocket floor so the pocket boolean no longer lands
  a coplanar annulus; `case_top.stl` round-trips watertight.
- **Docs**: ASSEMBLY/PRINT_NOTES direction statements corrected to the
  physical frame (USB-C + antenna + chevron on +y); unreferenced v8/v10
  inputs moved to `_stale_inputs/`.
- **Renders**: `render_blender.py` (Blender 4.5 EEVEE) replaces the
  matplotlib plot renders — real GLB board, 1920×1200, hero /
  exploded / skin-side / closed / worn (torso block to scale).

## 4. Other checks (checked)

- **Overhang audit**: encodes PRINT_NOTES' own allowances (≥45° cone OR
  open-to-plate OR ≤2 mm ledge roof); only deep enclosed canopies count.
  0 bad cells on base/top/bay_cup/pin_plate at 0.2 mm pitch.
- **DRC** (`_work/drc/`): fix_v24 pcb + fix_v19 `vitalq_v2.kicad_pro`
  rules + fix_v23 `vitalq_v2.kicad_dru`.
  `kicad-cli pcb drc --severity-error --schematic-parity --format json`:
  **0 errors, 0 unconnected, 0 shorts, 0 parity mismatches**.
- **ERC**: `kicad-cli sch erc --severity-error --format json`:
  **0 errors**.
- **STEP**: `case_v11.step`, 13 solids, 246,990 entities, all watertight.
- **Integrity**: `shasum -a 256 -c` against baselines for case_v10,
  fix_v19/22/23/24, _inputs_case_v11, _inputs_v24 → **1927 files OK, 0
  mismatches** (`_work/sha_baseline/`). JOURNAL.md/LICENSE: repo-only
  files; untouched in the clone (git status clean).
- **Renders**: `renders/{hero,closed,skin_side,exploded,worn}.png` —
  Blender 4.5 (EEVEE), inspected; real fix_v24 GLB inside the case,
  pogo apertures + slug + gasket on the skin face, board seated.

## 5. Open items

- U2/U17 ship no 3D models — courtyard boxes used in verification
  (inferred: worst-case volumes, conservative).
- The former 0.086 mm component↔lid pinch is relieved (now 0.369 mm,
  checked).
- v10 docs (CHECKS/PRINT_NOTES/ASSEMBLY etc.) are carried over with
  headers updated; they describe the shared lineage and mark stale
  direction statements where they remain. v11's authoritative check is
  `FIT_REPORT.md`.
