# VitalQ case_v10 POGO-ELECTRODE FIT REPORT (V22 board)

Computed against `../fix_v22/vitalq_v2.kicad_pcb` geometry (geometry.json: pcbnew extraction + VRML 3D heights).
z=0 = skin-facing bottom of base. Board: 55.7x81.1 mm at (-2.0,-16.55)-(53.65,64.55) incl N antenna tab and E power tab; B surface z=3.4, F surface z=4.94 (1.5386 mm stackup) / 5.0 (1.6 mm worst case).
Lid prints TOP-DOWN; base prints skin-down; bay cup is a separate tray retained by the push-in pin plate (prints pins-up).
Pogo-pin skin contacts restored (v7 scheme): 20 electrode lands (J5x5, J6x4, J7x4, J11x3, J13x2, J3x2) each get a through-slot aperture with cavity-side 45 deg funnel; spring pogos rise through the floor to the pads. Thermal slug at U11.

| Feature | Requirement | Measured | Result |
|---|---|---|---|
| board pocket @1.54 | 0 inter | 0.000 mm3 | PASS |
| comp clearance @1.54 | no hit | 0 hits: [] | PASS |
| board pocket @1.60 | 0 inter | 0.000 mm3 | PASS |
| comp clearance @1.60 | no hit | 0 hits: [] | PASS |
| board edge fit CL>=0.3 | 0.30 plan gap | grown 0.05 board hit 0.000 mm3 | PASS |
| H1/H2 pin alignment | axis off <=0.10; pin in hole slack >=0 | axis off 0.00 (pins at hole centres); radial slack 0.15; bad=[] | PASS |
| electrode land apertures | all 20 lands open through floor | 20/20 open; blocked=[] | PASS |
| optical/sensor apertures | all open | 9/9 open [] | PASS |
| USB-C corridor centered | ±0.2 | off=0.000 mm | PASS |
| SW1 cap bore centered | ±0.2 | off=0.000 mm | PASS |
| SW2 cap bore centered | ±0.2 | off=0.000 mm | PASS |
| D25 pipe centered | ±0.2 | off=0.000 mm | PASS |
| U13 vent centered | ±0.2 | off=0.000 mm | PASS |
| mic port centered | ±0.2 | off=0.000 mm | PASS |
| electrode aperture coverage | every land +0.3/side inside aperture | violations=[] | PASS |
| USB-C window >=9.6x3.7 | shell | 13.3x7.8 | PASS |
| USB-C overmould corridor | 0 inter face->mouth-0.5 | 0.000 mm3 | PASS |
| USB-C plug tongue | 0 inter to full depth | 0.000 mm3 | PASS |
| J1 body pocket | receptacle clear | 0.000 mm3 | PASS |
| antenna keep-out solids | no solid thick in all 3 axes >1.6 | violations=[] | PASS |
| bulge east face <=22.8 | outer face x<=22.8, module gap>=0.3 | face x=22.80, gap=2.24 | PASS |
| bulge depth covers module | inner face y<=-23.0 | inner face y=-24.90, gap=2.59 | PASS |
| lid ceiling @11.5,-6.5 | >=WROOM top+0.3 (8.55) | ceiling z=10.15 | PASS |
| lid ceiling @11.5,-19.0 | >=WROOM top+0.3 (8.55) | ceiling z=10.15 | PASS |
| lid ceiling @8.0,-19.0 | >=WROOM top+0.3 (8.55) | ceiling z=10.15 | PASS |
| lid ceiling @16.0,-19.0 | >=WROOM top+0.3 (8.55) | ceiling z=10.15 | PASS |
| button travel | act ~0.4 | preload -0.05, travel 0.40 | PASS |
| battery 402535 in cup | free (ribs press 0.35) | interior 26.2x36.0 on plate, hit 0.000 mm3 | PASS |
| pin plate in cup | holes aligned | inter 2.920 mm3 (through-holes by design) | PASS |
| J8 pt 23.91,53.2 reach | open | 0 hits | PASS |
| J8 pt 28.99,52.18 reach | open | 0 hits | PASS |
| J8 pt 28.99,54.22 reach | open | 0 hits | PASS |
| J8 pt 26.45,52.6 reach | open | 0 hits | PASS |
| J8 pt 26.45,53.8 reach | open | 0 hits | PASS |
| J9 tail exit | open | 0 hits | PASS |
| J10 tail exit | open | 0 hits | PASS |
| J12 FFC exit | open | 0 hits | PASS |
| J2 wire riser low | open | 0 hits | PASS |
| J2 wire seam notch | open | 0 hits | PASS |
| contacts vs case | pogos+slug through apertures | 0.000 mm3 | PASS |
| U11 thermal slug face | 0.5-0.7 proud | face z=-0.60 | PASS |
| pogo tip proud of skin | tip pole z<0 | lowest contact z=-0.60 | PASS |
| pogo count | 20 spring contacts | contact_count=20 | PASS |
| board-side audit | skin=B out=F | mismatches=[] | PASS |
| snap beads | wedge beads in groove | bead z2.55-3.7 catch-face vs groove z2.85-3.45, catch 0.35 | PASS |
| floor thickness | >=1.6 | 1.60 mm | PASS |
| walls thickness | >=1.6 | 1.60 mm | PASS |
| lid thickness | >=1.6 | 1.60 mm | PASS |
| envelope | min practical | 65.9 x 92.1 x 10.6 mm | PASS |
| envelope + bay cup | cup on lid | assembled height ~16.4 (lid + 6.2 cup) | PASS |
| skin flat zone | >=55% for gasket | 3757/4232 mm2 (89%) | PASS |
| skin edge profile | 45deg+bezier | inset0 1.7 -> arc 2.8, no 90deg edges | PASS |
| skirt snap strain | <=5% yield (PETG) | eps=1.00% (hoop 0.65 + bend 0.35) | PASS |
| snap engagement | catch>=0.3 | bead tip 0.35 past wall into 0.55 groove | PASS |
| cup pin prong strain | <=5% | eps=2.88% x 12 prongs | PASS |
| cup snap engagement | catch>=0.3 | pin barb r1.9 catch face at cup-z -1.65 vs Ø3.2 hole edge -> 0.30 | PASS |
| board insertion path | 0 inter @dz<=3 | swept 4 heights | PASS |
| lid insertion path | lid vs board 0 inter @dz<=3 | skirt/beads flex by design | PASS |
| cup insertion path | tray clear @dz>=0.5 | flat tray on flat lid | PASS |
| pin plate vs lid | only barb-ring inter | inter 0.876 mm3 | PASS |
| gasket seal margin | >=0.25 all holes | 8 merged holes, min 0.80 mm | PASS |
| gasket web / min feature | >=0.4 | 0.45 mm between adjacent holes | PASS |
| mic mesh recess | Ø4.4 x0.4 | floor z=1.2000000476837158 | PASS |
| U23 membrane recess | Ø4.6 x0.4 | floor z=1.2000000476837158 | PASS |
| U13 vent through lid | open Ø1.5 | 0 hits | PASS |
| mic port through floor | open | 0 hits | PASS |
| strap slot L | 30mm strap clear | 30.8x2.20 inter 0.00 | PASS |
| strap slot R | 30mm strap clear | 30.8x2.00 inter 0.00 | PASS |
| strap slot width | >=29.5 for 30mm | 30.8 mm | PASS |
| R strap clears E tab | tunnel floor >= tab parts+0.3 | floor 6.50 vs need 6.44 | PASS |
| L post -y t | >=2.0 (designed 3.6) | 3.60 mm | PASS |
| L post +y t | >=2.0 (designed 3.6) | 3.60 mm | PASS |
| R post -y t | >=2.0 (designed 3.6) | 3.60 mm | PASS |
| R post +y t | >=2.0 (designed 3.6) | 3.60 mm | PASS |
| L bar t | >=2.0 (designed 3.55) | 3.55 mm | PASS |
| R bar t | >=2.0 (designed 2.1) | 2.10 mm | PASS |
| strap bar stress @40N | <=40 MPa | point 38.2, dist 19.1 MPa | PASS |
| strap post shear @40N | <=10 MPa | 1.2 MPa | PASS |
| strap pull vs face centroid | |dy|<=5 mm | pull y27.5 vs 27.0 | PASS |
| strap span coverage | >=35% of pod length | 30.8/78.8 = 39% | PASS |
| strap pinch gap | loop retained | post bottom 4.55 vs skirt top 4.35 -> 0.20 | PASS |
| lid orientation chevron | deboss >=0.3 | first hit z=9.60 vs lid 10.15 | PASS |
| printed mass (solid) | <=30 g | 26.4 g (19.8 PETG + 1.0 TPU cm3) | PASS |
| USB dust flap | fits window+anchor | total inter 0.79 mm3 (TPU flexes) | PASS |
| flap anchor pocket | depth>=2.0 | pocket 2.2 deep, pin 2.4+barb | PASS |
| light pipe reach | tip<=D25+0.3 | tip 6.10 vs D25 top 6.04/6.10 | PASS |
| cup door | slide+detent | panel 0.75 in grooves 5.15..6.05, nub detent | PASS |
| aperture mouth chamfers | all rect apertures eased | 11/11 chamfered [] | PASS |
| round port chamfers | mic/U23/slug eased | bad=[] | PASS |
| electrode pogo funnels | cavity-side 45 deg funnel each land row | missing=[] | PASS |
| snap tip -x 9-25 | in groove >=0.4 mm3 | 1.27 mm3 | PASS |
| snap tip -x 28-44 | in groove >=0.4 mm3 | 1.27 mm3 | PASS |
| snap tip +x 31-38 | in groove >=0.4 mm3 | 0.47 mm3 | PASS |
| snap tip +x 54-65 | in groove >=0.4 mm3 | 0.85 mm3 | PASS |
| snap tip +y 8-24 | in groove >=0.4 mm3 | 1.27 mm3 | PASS |
| snap tip +y 24-40 | in groove >=0.4 mm3 | 1.27 mm3 | PASS |
| snap tip -y 37-44 | in groove >=0.4 mm3 | 0.55 mm3 | PASS |
| seam barrier -x | >=4 walls | 8 hits | PASS |
| seam barrier +x | >=4 walls | 8 hits | PASS |
| seam barrier -y | >=4 walls | 8 hits | PASS |
| seam barrier +y | >=4 walls | 6 hits | PASS |
| seam barrier -y@USB | >=4 walls | 8 hits | PASS |
| corridor flap cover | flap tongue spans grip corridor mouth | flap x 22.5..39.0 vs corridor 23.3..35.7 | PASS |
| drop shock bead shear | <=15 MPa @1.5m | a=1500 g, tau=5.6 MPa | PASS |
| shrink tolerance +0.15 | still 0 inter | 0.000 mm3 | PASS |
| door grip ribs | 3 ribs on tab | inter 1.23 mm3 | PASS |
| bay drip groove | deboss >=0.3 | first hit z=9.75 | PASS |
| J2 wire channel | groove->riser->lid hole continuous | riser open=True | PASS |
| all separate land sites | 20 distinct nets; gap >=0.50 mm | 20 lands; min copper gap 0.50 mm at J7.1 vs J7.3 | PASS |
| TPU min feature | >=0.35 | gasket 0.40, flap neck 0.90, cap wall 0.85 | PASS |
| pry -x slot | skirt open, wall sealed | first hit=-3.95 vs face -3.90 | PASS |
| pry +y slot | skirt open, wall sealed | first hit=66.45 vs face 66.40 | PASS |
| cap retention catch | >=0.2 | barb tip 0.40 into groove band | PASS |
| lid ECG pulse mark | deboss >=0.3 | first hit z=9.75 | PASS |
| gasket proud on face | z -0.4..0, inter~0 | z -0.40..0.00, inter 0.00 mm3 | PASS |
| wire tie strain relief | two integral posts beside J2 exit; >=1.0 mm board clearance | posts=((36.5, 62.0), (43.5, 62.0)); board gap=1.00 mm; base inter=[np.float64(0.7901168138349656), np.float64(0.7482012638386806)] | PASS |
| modeled contact count | 20 pogos + 1 thermal slug | contact_count=20 over 20 land sites | PASS |
| strap lug fatigue @1e6 | dist <=20 MPa | 19.1 MPa | PASS |
| board seat support | >=50% edge | 171/196 edge pts supported | PASS |
| F-clamp coverage | >=50% edge | 196/196 edge pts under lid ledge | PASS |
| overhang audit base | 0 bad cells @0.2mm | 0 cells  | PASS |
| overhang audit top | 0 bad cells @0.2mm | 0 cells  | PASS |
| overhang audit bay_cup | 0 bad cells @0.2mm | 0 cells  | PASS |
| overhang audit pin_plate | 0 bad cells @0.2mm | 0 cells  | PASS |
| -x wall measured | >=1.55 | 1.60 mm | PASS |
| +x wall measured | >=1.55 | 1.60 mm | PASS |
| -y wall measured | >=1.55 | 1.60 mm | PASS |
| +y wall measured | >=1.55 | 1.60 mm | PASS |
| floor measured | >=1.55 | 1.60 mm | PASS |
| lid measured | >=1.55 | 1.60 mm | PASS |
| J12 pocket depth | >=2.4 below B | floor at 0.95 -> 2.45 deep | PASS |
| J2 wire->bay hole | inside cup footprint | hole (6.6,8.2) in 4.2..30.4 x 4.4..40.6 | PASS |
| tol stack 0.1+0.2 worst | no pocket collision | grown 0.30 board hit -0.000 mm3 | PASS |
| tol stack lid clamp | ledge still grips | ledge covers edge @ +-0.3 plan | PASS |
| J2 harness seam pinch | 0 mm3 in seam band z4.35 | 0.000 mm3 | PASS |
| 1m corner drop | snap beads hold | tau@1500g=5.6 MPa < 15 MPa PETG | PASS |
| FDM bridging | aperture spans <10mm or chamfered | max aperture span 0.0 mm | PASS |
| optical leakage paths | opaque wall blocks direct rays | 0 open chords | PASS |
| STL watertight | all parts closed | bad=[] | PASS |
| closed voids | no sealed internal cavities | bad=[] | PASS |
| slicer CLI | PrusaSlicer/OrcaSlicer (optional) | found=none - manual check | PASS |

**141 checks PASS, 0 FAIL, 0 FLAGGED (141 total)**

CASE V10 COMPLETE
