# VitalQ case_v11

Two-part wearable case for the **fix_v24** VitalQ board
(`../fix_v24/vitalq_v2.kicad_pcb`), 20-pogo electrode front end, 402535 LiPo
bay, strap lugs. FDM-printable parts, all watertight.

**v11 is a mirror fix of v10.** case_v10 was built in the KiCad editor
frame (x right, y down, viewed from F.Cu) exported straight to STL — a
left-handed frame, so every v10 solid is the mirror image of the real
board and the physical case could not fit. v11 keeps the design pipeline
and mirrors every case solid in Y at export (`phys()` in `case_lib.py`).
The real fix_v24 board now drops in with 0 collisions >0.1 mm
(`FIT_REPORT.md`, 25/25 PASS). The same verifier fails case_v10 with 14
checks — the mirror signature (`_work/verify_v10_output.txt`). Details:
`REPORT.md`.

## Parts (STLs, mm, watertight)

| File | Part | Print |
|---|---|---|
| `case_base.stl` | skin-side base shell | skin-down |
| `case_top.stl` | top shell + skirt + strap lugs | top-down |
| `bay_cup.stl` | battery tray (snaps onto lid) | bottom-down |
| `pin_plate.stl` | snap-pin retainer for bay cup | pins-up |
| `battery_door.stl` | slide-in door | flat side down |
| `gasket.stl` | skin-side seal sheet | flat |
| `usb_flap.stl` | USB-C dust flap | flat |
| `button_cap.stl` | SW1/SW2 caps (x2) | tip-down |
| `light_pipe.stl` | D25 status light pipe | dome-up |
| `plug.stl` | J8 window plug | flat |
| `contacts_assy.stl` | 20 pogos + 1 thermal slug, assembled | verify/render only |
| `pogo_pin.stl` | single pogo reference | verify/render only |
| `thermal_slug.stl` | U11 thermal insert (copper) | machining model |
| `case_v11.step` | whole assembly (faceted) | CAD exchange |

Print settings (unchanged from v10): 0.2 mm layers, 4 walls, 20–30 %
gyroid (lugs 40 %), PETG / TPU 95A where noted — `PRINT_NOTES.md`.

## Pipeline

```
kicad-cli pcb export glb/step  ->  _work/board_v24*.glb      (ground truth)
extract_geometry.py            ->  pcbnew -> geometry_v24.json
parse_vrml.py + merge_heights  ->  geometry.json (component heights)
build_case.py                  ->  all STLs (Y-mirrored at export)
export_step.py                 ->  case_v11.step (assembled, phys frame)
verify_fit.py                  ->  FIT_REPORT.md (25 checks)
render_views.py                ->  renders/
```

## Frame convention

All v11 STLs/STEP/renders use the **physical frame**: `x = KiCad x`,
`y = -KiCad y`, `z` up from the skin face. Board sits B.Cu-down at seat
z = 3.40. `_inputs/` and `_work/` are local scratch (not for release).

## Verification

`python3 verify_fit.py [--case-dir DIR]` — real-geometry test driven by
the fix_v24 GLB + pcbnew extraction, never by case-generated numbers.
Results: `FIT_REPORT.md`. v10 failure proof: `_work/verify_v10_output.txt`.
