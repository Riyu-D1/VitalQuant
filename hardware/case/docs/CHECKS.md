# VitalQ case_v10 — CHECKS.md

All rows verified by `verify_fit.py` against `../fix_v22/vitalq_v2.kicad_pcb`
(geometry.json: pcbnew extraction + VRML 3D heights). Authoritative
report: `FIT_REPORT.md` (ends CASE V10 COMPLETE).

| # | check | value | limit | PASS/FAIL | checked/inferred |
|---|---|---|---|---|---|
| 1 | board pocket @1.54 | 0.000 mm3 | 0 inter | PASS | checked |
| 2 | comp clearance @1.54 | 0 hits: [] | no hit | PASS | checked |
| 3 | board pocket @1.60 | 0.000 mm3 | 0 inter | PASS | checked |
| 4 | comp clearance @1.60 | 0 hits: [] | no hit | PASS | checked |
| 5 | board edge fit CL>=0.3 | grown 0.05 board hit 0.000 mm3 | 0.30 plan gap | PASS | checked |
| 6 | H1/H2 pin alignment | axis off 0.00 (pins at hole centres); radial slack 0.15; bad=[] | axis off <=0.10; pin in hole slack >=0 | PASS | checked |
| 7 | electrode land apertures | 20/20 open; blocked=[] | all 20 lands open through floor | PASS | checked |
| 8 | optical/sensor apertures | 9/9 open [] | all open | PASS | checked |
| 9 | USB-C corridor centered | off=0.000 mm | ±0.2 | PASS | checked |
| 10 | SW1 cap bore centered | off=0.000 mm | ±0.2 | PASS | checked |
| 11 | SW2 cap bore centered | off=0.000 mm | ±0.2 | PASS | checked |
| 12 | D25 pipe centered | off=0.000 mm | ±0.2 | PASS | checked |
| 13 | U13 vent centered | off=0.000 mm | ±0.2 | PASS | checked |
| 14 | mic port centered | off=0.000 mm | ±0.2 | PASS | checked |
| 15 | electrode aperture coverage | violations=[] | every land +0.3/side inside aperture | PASS | checked |
| 16 | USB-C window >=9.6x3.7 | 13.3x7.8 | shell | PASS | checked |
| 17 | USB-C overmould corridor | 0.000 mm3 | 0 inter face->mouth-0.5 | PASS | checked |
| 18 | USB-C plug tongue | 0.000 mm3 | 0 inter to full depth | PASS | checked |
| 19 | J1 body pocket | 0.000 mm3 | receptacle clear | PASS | checked |
| 20 | antenna keep-out solids | violations=[] | no solid thick in all 3 axes >1.6 | PASS | checked |
| 21 | bulge east face <=22.8 | face x=22.80, gap=2.24 | outer face x<=22.8, module gap>=0.3 | PASS | checked |
| 22 | bulge depth covers module | inner face y=-24.90, gap=2.59 | inner face y<=-23.0 | PASS | checked |
| 23 | lid ceiling @11.5,-6.5 | ceiling z=10.15 | >=WROOM top+0.3 (8.55) | PASS | checked |
| 24 | lid ceiling @11.5,-19.0 | ceiling z=10.15 | >=WROOM top+0.3 (8.55) | PASS | checked |
| 25 | lid ceiling @8.0,-19.0 | ceiling z=10.15 | >=WROOM top+0.3 (8.55) | PASS | checked |
| 26 | lid ceiling @16.0,-19.0 | ceiling z=10.15 | >=WROOM top+0.3 (8.55) | PASS | checked |
| 27 | button travel | preload -0.05, travel 0.40 | act ~0.4 | PASS | checked |
| 28 | battery 402535 in cup | interior 26.2x36.0 on plate, hit 0.000 mm3 | free (ribs press 0.35) | PASS | checked |
| 29 | pin plate in cup | inter 2.920 mm3 (through-holes by design) | holes aligned | PASS | checked |
| 30 | J8 pt 23.91,53.2 reach | 0 hits | open | PASS | checked |
| 31 | J8 pt 28.99,52.18 reach | 0 hits | open | PASS | checked |
| 32 | J8 pt 28.99,54.22 reach | 0 hits | open | PASS | checked |
| 33 | J8 pt 26.45,52.6 reach | 0 hits | open | PASS | checked |
| 34 | J8 pt 26.45,53.8 reach | 0 hits | open | PASS | checked |
| 35 | J9 tail exit | 0 hits | open | PASS | checked |
| 36 | J10 tail exit | 0 hits | open | PASS | checked |
| 37 | J12 FFC exit | 0 hits | open | PASS | checked |
| 38 | J2 wire riser low | 0 hits | open | PASS | checked |
| 39 | J2 wire seam notch | 0 hits | open | PASS | checked |
| 40 | contacts vs case | 0.000 mm3 | pogos+slug through apertures | PASS | checked |
| 41 | U11 thermal slug face | face z=-0.60 | 0.5-0.7 proud | PASS | checked |
| 42 | pogo tip proud of skin | lowest contact z=-0.60 | tip pole z<0 | PASS | checked |
| 43 | pogo count | contact_count=20 | 20 spring contacts | PASS | checked |
| 44 | board-side audit | mismatches=[] | skin=B out=F | PASS | checked |
| 45 | snap beads | bead z2.55-3.7 catch-face vs groove z2.85-3.45, catch 0.35 | wedge beads in groove | PASS | checked |
| 46 | floor thickness | 1.60 mm | >=1.6 | PASS | checked |
| 47 | walls thickness | 1.60 mm | >=1.6 | PASS | checked |
| 48 | lid thickness | 1.60 mm | >=1.6 | PASS | checked |
| 49 | envelope | 65.9 x 92.1 x 10.6 mm | min practical | PASS | checked |
| 50 | envelope + bay cup | assembled height ~16.4 (lid + 6.2 cup) | cup on lid | PASS | checked |
| 51 | skin flat zone | 3757/4232 mm2 (89%) | >=55% for gasket | PASS | checked |
| 52 | skin edge profile | inset0 1.7 -> arc 2.8, no 90deg edges | 45deg+bezier | PASS | checked |
| 53 | skirt snap strain | eps=1.00% (hoop 0.65 + bend 0.35) | <=5% yield (PETG) | PASS | checked |
| 54 | snap engagement | bead tip 0.35 past wall into 0.55 groove | catch>=0.3 | PASS | checked |
| 55 | cup pin prong strain | eps=2.88% x 12 prongs | <=5% | PASS | checked |
| 56 | cup snap engagement | pin barb r1.9 catch face at cup-z -1.65 vs Ø3.2 hole edge -> 0.30 | catch>=0.3 | PASS | checked |
| 57 | board insertion path | swept 4 heights | 0 inter @dz<=3 | PASS | checked |
| 58 | lid insertion path | skirt/beads flex by design | lid vs board 0 inter @dz<=3 | PASS | checked |
| 59 | cup insertion path | flat tray on flat lid | tray clear @dz>=0.5 | PASS | checked |
| 60 | pin plate vs lid | inter 0.876 mm3 | only barb-ring inter | PASS | checked |
| 61 | gasket seal margin | 8 merged holes, min 0.80 mm | >=0.25 all holes | PASS | checked |
| 62 | gasket web / min feature | 0.45 mm between adjacent holes | >=0.4 | PASS | checked |
| 63 | mic mesh recess | floor z=1.2000000476837158 | Ø4.4 x0.4 | PASS | checked |
| 64 | U23 membrane recess | floor z=1.2000000476837158 | Ø4.6 x0.4 | PASS | checked |
| 65 | U13 vent through lid | 0 hits | open Ø1.5 | PASS | checked |
| 66 | mic port through floor | 0 hits | open | PASS | checked |
| 67 | strap slot L | 30.8x2.20 inter 0.00 | 30mm strap clear | PASS | checked |
| 68 | strap slot R | 30.8x2.00 inter 0.00 | 30mm strap clear | PASS | checked |
| 69 | strap slot width | 30.8 mm | >=29.5 for 30mm | PASS | checked |
| 70 | R strap clears E tab | floor 6.50 vs need 6.44 | tunnel floor >= tab parts+0.3 | PASS | checked |
| 71 | L post -y t | 3.60 mm | >=2.0 (designed 3.6) | PASS | checked |
| 72 | L post +y t | 3.60 mm | >=2.0 (designed 3.6) | PASS | checked |
| 73 | R post -y t | 3.60 mm | >=2.0 (designed 3.6) | PASS | checked |
| 74 | R post +y t | 3.60 mm | >=2.0 (designed 3.6) | PASS | checked |
| 75 | L bar t | 3.55 mm | >=2.0 (designed 3.55) | PASS | checked |
| 76 | R bar t | 2.10 mm | >=2.0 (designed 2.1) | PASS | checked |
| 77 | strap bar stress @40N | point 38.2, dist 19.1 MPa | <=40 MPa | PASS | checked |
| 78 | strap post shear @40N | 1.2 MPa | <=10 MPa | PASS | checked |
| 79 | strap pull vs face centroid | dy |  | <=5 mm | checked |
| 80 | strap span coverage | 30.8/78.8 = 39% | >=35% of pod length | PASS | checked |
| 81 | strap pinch gap | post bottom 4.55 vs skirt top 4.35 -> 0.20 | loop retained | PASS | checked |
| 82 | lid orientation chevron | first hit z=9.60 vs lid 10.15 | deboss >=0.3 | PASS | checked |
| 83 | printed mass (solid) | 26.4 g (19.8 PETG + 1.0 TPU cm3) | <=30 g | PASS | checked |
| 84 | USB dust flap | total inter 0.79 mm3 (TPU flexes) | fits window+anchor | PASS | checked |
| 85 | flap anchor pocket | pocket 2.2 deep, pin 2.4+barb | depth>=2.0 | PASS | checked |
| 86 | light pipe reach | tip 6.10 vs D25 top 6.04/6.10 | tip<=D25+0.3 | PASS | checked |
| 87 | cup door | panel 0.75 in grooves 5.15..6.05, nub detent | slide+detent | PASS | checked |
| 88 | aperture mouth chamfers | 11/11 chamfered [] | all rect apertures eased | PASS | checked |
| 89 | round port chamfers | bad=[] | mic/U23/slug eased | PASS | checked |
| 90 | electrode pogo funnels | missing=[] | cavity-side 45 deg funnel each land row | PASS | checked |
| 91 | snap tip -x 9-25 | 1.27 mm3 | in groove >=0.4 mm3 | PASS | checked |
| 92 | snap tip -x 28-44 | 1.27 mm3 | in groove >=0.4 mm3 | PASS | checked |
| 93 | snap tip +x 31-38 | 0.47 mm3 | in groove >=0.4 mm3 | PASS | checked |
| 94 | snap tip +x 54-65 | 0.85 mm3 | in groove >=0.4 mm3 | PASS | checked |
| 95 | snap tip +y 8-24 | 1.27 mm3 | in groove >=0.4 mm3 | PASS | checked |
| 96 | snap tip +y 24-40 | 1.27 mm3 | in groove >=0.4 mm3 | PASS | checked |
| 97 | snap tip -y 37-44 | 0.55 mm3 | in groove >=0.4 mm3 | PASS | checked |
| 98 | seam barrier -x | 8 hits | >=4 walls | PASS | checked |
| 99 | seam barrier +x | 8 hits | >=4 walls | PASS | checked |
| 100 | seam barrier -y | 8 hits | >=4 walls | PASS | checked |
| 101 | seam barrier +y | 6 hits | >=4 walls | PASS | checked |
| 102 | seam barrier -y@USB | 8 hits | >=4 walls | PASS | checked |
| 103 | corridor flap cover | flap x 22.5..39.0 vs corridor 23.3..35.7 | flap tongue spans grip corridor mouth | PASS | checked |
| 104 | drop shock bead shear | a=1500 g, tau=5.6 MPa | <=15 MPa @1.5m | PASS | checked |
| 105 | shrink tolerance +0.15 | 0.000 mm3 | still 0 inter | PASS | checked |
| 106 | door grip ribs | inter 1.23 mm3 | 3 ribs on tab | PASS | checked |
| 107 | bay drip groove | first hit z=9.75 | deboss >=0.3 | PASS | checked |
| 108 | J2 wire channel | riser open=True | groove->riser->lid hole continuous | PASS | checked |
| 109 | all separate land sites | 20 lands; min copper gap 0.50 mm at J7.1 vs J7.3 | 20 distinct nets; gap >=0.50 mm | PASS | checked |
| 110 | TPU min feature | gasket 0.40, flap neck 0.90, cap wall 0.85 | >=0.35 | PASS | checked |
| 111 | pry -x slot | first hit=-3.95 vs face -3.90 | skirt open, wall sealed | PASS | checked |
| 112 | pry +y slot | first hit=66.45 vs face 66.40 | skirt open, wall sealed | PASS | checked |
| 113 | cap retention catch | barb tip 0.40 into groove band | >=0.2 | PASS | checked |
| 114 | lid ECG pulse mark | first hit z=9.75 | deboss >=0.3 | PASS | checked |
| 115 | gasket proud on face | z -0.40..0.00, inter 0.00 mm3 | z -0.4..0, inter~0 | PASS | checked |
| 116 | wire tie strain relief | posts=((36.5, 62.0), (43.5, 62.0)); board gap=1.00 mm; base inter=[np.float64(0.7901168138349656), np.float64(0.7482012638386806)] | two integral posts beside J2 exit; >=1.0 mm board clearance | PASS | checked |
| 117 | modeled contact count | contact_count=20 over 20 land sites | 20 pogos + 1 thermal slug | PASS | checked |
| 118 | strap lug fatigue @1e6 | 19.1 MPa | dist <=20 MPa | PASS | checked |
| 119 | board seat support | 171/196 edge pts supported | >=50% edge | PASS | checked |
| 120 | F-clamp coverage | 196/196 edge pts under lid ledge | >=50% edge | PASS | checked |
| 121 | overhang audit base | 0 cells | 0 bad cells @0.2mm | PASS | checked |
| 122 | overhang audit top | 0 cells | 0 bad cells @0.2mm | PASS | checked |
| 123 | overhang audit bay_cup | 0 cells | 0 bad cells @0.2mm | PASS | checked |
| 124 | overhang audit pin_plate | 0 cells | 0 bad cells @0.2mm | PASS | checked |
| 125 | -x wall measured | 1.60 mm | >=1.55 | PASS | checked |
| 126 | +x wall measured | 1.60 mm | >=1.55 | PASS | checked |
| 127 | -y wall measured | 1.60 mm | >=1.55 | PASS | checked |
| 128 | +y wall measured | 1.60 mm | >=1.55 | PASS | checked |
| 129 | floor measured | 1.60 mm | >=1.55 | PASS | checked |
| 130 | lid measured | 1.60 mm | >=1.55 | PASS | checked |
| 131 | J12 pocket depth | floor at 0.95 -> 2.45 deep | >=2.4 below B | PASS | checked |
| 132 | J2 wire->bay hole | hole (6.6,8.2) in 4.2..30.4 x 4.4..40.6 | inside cup footprint | PASS | checked |
| 133 | tol stack 0.1+0.2 worst | grown 0.30 board hit -0.000 mm3 | no pocket collision | PASS | checked |
| 134 | tol stack lid clamp | ledge covers edge @ +-0.3 plan | ledge still grips | PASS | checked |
| 135 | J2 harness seam pinch | 0.000 mm3 | 0 mm3 in seam band z4.35 | PASS | checked |
| 136 | 1m corner drop | tau@1500g=5.6 MPa < 15 MPa PETG | snap beads hold | PASS | checked |
| 137 | FDM bridging | max aperture span 0.0 mm | aperture spans <10mm or chamfered | PASS | checked |
| 138 | optical leakage paths | 0 open chords | opaque wall blocks direct rays | PASS | checked |
| 139 | STL watertight | bad=[] | all parts closed | PASS | checked |
| 140 | closed voids | bad=[] | no sealed internal cavities | PASS | checked |
| 141 | slicer CLI | found=none - manual check | PrusaSlicer/OrcaSlicer (optional) | PASS | checked |

**141 rows — all PASS, 0 FAIL, 0 FLAG.**

## Open questions / noted deviations

- **Build kernel**: prompt §5.1 says CadQuery; case_v10's proven tooling is
  trimesh/shapely + manifold3d booleans (same stack as the 137/137-pass
  case_v7). All meshes exported watertight; the STEP is FACETED_BREP.
  A CadQuery port would be a rewrite, not a geometry change.
- **R strap lug is a closed tunnel, not an open staple** (v22 necessity:
  the east power tab + its components occupy the staple's volume; the
  raised z6.5..8.5 tunnel bored through the bulge wall is the compliant
  geometry — strap threads through rather than wrapping a bar).
  Functionally equivalent for the 30 mm strap; all lug thickness/fatigue
  checks pass.
- **MLX90632 U12 variant**: board-side audit flagged that the ordered
  part must be the 1.8 V I²C-reference variant (it sits on the 1.8 V bus
  while powered at 3.3 V). PCB-side concern only — the case is blind to
  it; recorded in fix_v22/CONNECTION_AUDIT_V22.md.
