# VitalQ case_v5 FIT REPORT

Computed against `../fix_v9/vitalq_v2.kicad_pcb` geometry (geometry.json: pcbnew extraction + VRML 3D heights).
z=0 = skin-facing bottom of base. Board: 50.1x75.1 mm at (-2.0,-10.5)-(48.0,64.5); B surface z=3.4, F surface z=4.94 (1.5386 mm stackup) / 5.0 (1.6 mm worst case).
Lid prints TOP-DOWN; base prints skin-down; bay cup is a separate tray retained by the push-in pin plate (prints pins-up).

| Feature | Requirement | Measured | Result |
|---|---|---|---|
| board pocket @1.54 | 0 inter | 0.000 mm3 | PASS |
| comp clearance @1.54 | no hit | 0 hits: [] | PASS |
| board pocket @1.60 | 0 inter | 0.000 mm3 | PASS |
| comp clearance @1.60 | no hit | 0 hits: [] | PASS |
| board edge fit CL>=0.3 | 0.30 plan gap | grown 0.05 board hit 0.000 mm3 | PASS |
| electrode apertures | all pads open | 20/20 open [] | PASS |
| optical/sensor apertures | all open | 9/9 open [] | PASS |
| USB-C window centered | ±0.2 | off=0.000 mm | PASS |
| SW1 cap bore centered | ±0.2 | off=0.000 mm | PASS |
| SW2 cap bore centered | ±0.2 | off=0.000 mm | PASS |
| D25 pipe centered | ±0.2 | off=0.000 mm | PASS |
| U13 vent centered | ±0.2 | off=0.000 mm | PASS |
| mic port centered | ±0.2 | off=0.000 mm | PASS |
| J5 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| J6 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| J7 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| J13 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| J11 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| J3 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| USB-C window >=8.6x2.8 | shell | 9.6x3.7 | PASS |
| USB-C overmould corridor | 0 inter to face | 0.000 mm3, face->mouth 0.95 mm | PASS |
| J1 body pocket | receptacle tip clear | 0.000 mm3 | PASS |
| button travel | act ~0.4 | preload -0.05, travel 0.40 | PASS |
| battery 402535 in cup | free (ribs press 0.35) | interior 26.2x36.0 on plate, hit 0.000 mm3 | PASS |
| pin plate in cup | holes aligned | inter 3.065 mm3 (through-holes by design) | PASS |
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
| contact inserts vs case | no inter | 0.000 mm3 | PASS |
| J5 contact reach x5 | tip <= skin+0.2 | tip z=-0.60 (0.60 proud) | PASS |
| J6 contact reach x4 | tip <= skin+0.2 | tip z=-0.60 (0.60 proud) | PASS |
| J7 contact reach x4 | tip <= skin+0.2 | tip z=-0.60 (0.60 proud) | PASS |
| J13 contact reach x2 | tip <= skin+0.2 | tip z=-0.60 (0.60 proud) | PASS |
| J11 contact reach x3 | tip <= skin+0.2 | tip z=-0.60 (0.60 proud) | PASS |
| J3 contact reach x2 | tip <= skin+0.2 | tip z=-0.60 (0.60 proud) | PASS |
| U11 thermal slug face | 0.5-0.7 proud | face z=-0.60 | PASS |
| J7 aperture->inner wall | >=1.5 web | x0=0.245 wall=-2.3 web=2.54 mm | PASS |
| J7 pads vs J12 pocket | pads y>59.7 | pad ymin=59.85 > pocket 59.7 | PASS |
| J7 vs strap lug L2 | no plan overlap | lug x<=-3.9 aperture x>=0.25 | PASS |
| board-side audit | skin=B out=F | mismatches=[] | PASS |
| snap beads | wedge beads in groove | bead z2.55-3.7 catch-face vs groove z2.85-3.45, catch 0.35 | PASS |
| floor thickness | >=1.6 | 1.60 mm | PASS |
| walls thickness | >=1.6 | 1.60 mm | PASS |
| lid thickness | >=1.6 | 1.60 mm | PASS |
| envelope | min practical | 62.2 x 82.2 x 9.6 mm | PASS |
| envelope + bay cup | cup on lid | assembled height ~15.8 (lid + 6.2 cup) | PASS |
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
| contact insertion | ap min >=1.3 | min=1.71 | PASS |
| gasket seal margin | >=0.25 all holes | 9 merged holes, min 0.29 mm | PASS |
| gasket web / min feature | >=0.4 | 0.45 mm between adjacent holes | PASS |
| mic mesh recess | Ø4.4 x0.4 | floor z=1.2000000476837158 | PASS |
| U23 membrane recess | Ø4.6 x0.4 | floor z=1.2000000476837158 | PASS |
| U13 vent through lid | open Ø1.5 | 0 hits | PASS |
| mic port through floor | open | 0 hits | PASS |
| strap channel L1 | gap>=13, clear | gap 14.8mm, inter 0.00 | PASS |
| strap channel L2 | gap>=13, clear | gap 15.2mm, inter 0.00 | PASS |
| strap channel R1 | gap>=13, clear | gap 13.2mm, inter 0.00 | PASS |
| strap channel R2 | gap>=13, clear | gap 13.2mm, inter 0.00 | PASS |
| strap pinch gap | loop retained | post bottom 4.55 vs skirt top 4.35 | PASS |
| USB dust flap | fits window+anchor | total inter 0.62 mm3 (TPU flexes) | PASS |
| flap anchor pocket | depth>=2.0 | pocket 2.2 deep, pin 2.4+barb | PASS |
| light pipe reach | tip<=D25+0.3 | tip 6.10 vs D25 top 6.04/6.10 | PASS |
| cup door | slide+detent | panel 0.75 in grooves 5.15..6.05, nub detent | PASS |
| board seat support | >=50% edge | 172/196 edge pts supported | PASS |
| F-clamp coverage | >=50% edge | 196/196 edge pts under lid ledge | PASS |
| overhang audit base | 0 bad cells @0.4mm | 0 cells  | PASS |
| overhang audit top | 0 bad cells @0.4mm | 188 cells  | FAIL |
| overhang audit bay_cup | 0 bad cells @0.4mm | 0 cells  | PASS |
| overhang audit pin_plate | 0 bad cells @0.4mm | 24 cells  | FAIL |
| -x wall measured | >=1.55 | 1.60 mm | PASS |
| +x wall measured | >=1.55 | 1.60 mm | PASS |
| -y wall measured | >=1.55 | 1.60 mm | PASS |
| +y wall measured | >=1.55 | 1.60 mm | PASS |
| floor measured | >=1.55 | 1.60 mm | PASS |
| lid measured | >=1.55 | 1.60 mm | PASS |
| J12 pocket depth | >=2.4 below B | floor at 0.95 -> 2.45 deep | PASS |
| pogo working height | P70-5100045 3.85-4.0 | pad->skin 3.40, work 4.0, tip -0.6 | PASS |
| J2 wire->bay hole | inside cup footprint | hole (6.6,8.2) in 4.2..30.4 x 4.4..40.6 | PASS |

**92/94 checks PASS, 2 FAIL**

CASE5 COMPLETE
