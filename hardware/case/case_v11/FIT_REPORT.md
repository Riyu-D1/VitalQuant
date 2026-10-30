# VitalQ case real-geometry FIT REPORT

Case under test: `./` (STLs)  |  board: fix_v24 GLB (_work/board_v24_pads.glb) + pcbnew extraction (_work/geometry_v24.json).
Points sampled: 150000 real board/component surface points (+6000 supplemental).
Frame: STL frame is the physical frame (x=KiCad x, y=-KiCad y, z up from skin); board placed B.Cu-down at the measured seat z=3.40 with NO mirroring allowed.
kicad-cli 10.0.4 exports. tolerance: deep >0.1 mm counts a point.

| Feature | Requirement | Measured | Result |
|---|---|---|---|
| case parts present | all STLs load | missing=[] | PASS |
| board seat plane | 3.40 +/-0.05 (design ZB) | measured ledge top z=3.400 from 295 probes | PASS |
| real board pts inside case_base | 0 deeper than 0.1 mm | inside=2 deep=0 max_depth=0.017 | PASS |
| real board pts inside case_top | 0 deeper than 0.1 mm | inside=0 deep=0 max_depth=0.000 | PASS |
| mounting-hole pin alignment | each real board hole vs case pin: axis off <=0.10 | bad=[] | PASS |
| USB-C window vs J1 | centre off <=0.2; opening >=9.6 wide | open span x 22.8..35.9 w=13.2 centre off=0.15 wall probes=41 | PASS |
| USB-C overmould corridor | 0 inter face->mouth-0.5 | 0.000 mm3 | PASS |
| electrode lands open through floor | all 20 lands over open through-apertures | 20/20 open bad=[] | PASS |
| electrode land vs pogo pin XY | each land: pogo within 0.20 | pogos=20 worst=[] | PASS |
| thermal slug | exactly one slug at U11, face 0.45-0.75 proud | bad=[] | PASS |
| antenna tab in pocket | 0 inter at board z-band | 0.000 mm3 | PASS |
| east tab in pocket | 0 inter at board z-band | 0.000 mm3 | PASS |
| antenna pocket depth | inner face >= module end +22.31 +0.3 | face y=23.30 gap=0.99 | PASS |
| east tab pocket depth | pocket face >= tab edge 53.6 +0.3 | face x=53.90 gap=0.30 | PASS |
| board edge <-> wall clearance | min gap >=0.20 (pocket CL 0.3, probed); 0 inside | inside=0 min gap 0.300 at [ 51.65 -20.     3.98] over 3093 perimeter pts | PASS |
| component tops <-> lid ceiling | min gap >=0.30 | min gap 0.501 at ('U1', np.float64(2.15), np.float64(9.25)) | PASS |
| components <-> lid (any direction) | min gap >=0.20 (strap-fin ledge relieved); 0 inside | inside=0 min gap 0.369 at [ -0.97 -56.66   5.12] over 35426 F-side pts | PASS |
| components <-> floor clearance | min gap >=0.15; 0 inside | inside=0 min gap 0.365 at [ -1.6  -56.93   1.31] over 12963 B-side pts | PASS |
| battery 402535 in cup | cell fits (press ribs 0.35) | inter 0.000 mm3 | PASS |
| TP29/TP36 <-> lid ledge | min gap >=0.03 (ledge relieved) | min gap 0.283 at TP36 | PASS |
| overhang audit base | 0 bad deep canopies @0.2mm | 0 cells  | PASS |
| overhang audit top | 0 bad deep canopies @0.2mm | 0 cells  | PASS |
| overhang audit bay_cup | 0 bad deep canopies @0.2mm | 0 cells  | PASS |
| overhang audit pin_plate | 0 bad deep canopies @0.2mm | 0 cells  | PASS |
| STL watertight | all parts closed | bad=[] | PASS |

**25 checks PASS, 0 FAIL, 0 FLAGGED (25 total)**

VERDICT: PASS
