# VitalQ case_v3 FIT REPORT

Computed against `../fix_v7/vitalq_v2.kicad_pcb` geometry (geometry.json: pcbnew extraction + VRML 3D heights).
z=0 = skin-facing bottom of base. Board: 50.0x75.0x1.6 mm at (-2.0,-10.5)-(48.0,64.5), B surface z=3.4, F surface z=5.0.

| Feature | Requirement | Measured | Result |
|---|---|---|---|
| board pocket | 0 inter | 0.000 mm3 | PASS |
| comp clearance (all 299) | no hit | 0 hits: [] | PASS |
| electrode apertures | all pads open | 14/14 open [] | PASS |
| optical/sensor apertures | all open | 9/9 open [] | PASS |
| USB-C window centered | ±0.2 | off=0.000 mm | PASS |
| SW1 plunger centered | ±0.2 | off=0.000 mm | PASS |
| SW2 plunger centered | ±0.2 | off=0.000 mm | PASS |
| D25 pipe centered | ±0.2 | off=0.000 mm | PASS |
| U13 vent centered | ±0.2 | off=0.000 mm | PASS |
| mic port centered | ±0.2 | off=0.000 mm | PASS |
| J5 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| J6 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| J13 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| J11 aperture ctr | ±0.2 | off=0.000 mm | PASS |
| USB-C window >=8.6x2.8 | shell | 9.6x3.8 | PASS |
| USB-C overmould corridor | 0 inter to face | 0.000 mm3, face->mouth 1.15 mm | PASS |
| button travel | act 0.25-0.4 | preload 0.05, travel 0.40 | PASS |
| battery bay 25.8x35.8 | fit 402535 | bay 25.8x35.8 free, hit 0.00 mm3 | PASS |
| J7 pad 11.2,62.85 reach | open | 0 surf hits | PASS |
| J7 pad 13.4,62.85 reach | open | 0 surf hits | PASS |
| J7 pad 15.6,62.85 reach | open | 0 surf hits | PASS |
| J7 pad 17.8,62.85 reach | open | 0 surf hits | PASS |
| J3 pad 25.4,62.85 reach | open | 0 surf hits | PASS |
| J3 pad 27.6,62.85 reach | open | 0 surf hits | PASS |
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
| J13 contact reach x2 | tip <= skin+0.2 | tip z=-0.60 (0.60 proud) | PASS |
| J11 contact reach x3 | tip <= skin+0.2 | tip z=-0.60 (0.60 proud) | PASS |
| U11 thermal slug face | skin ±0.2 | face z=0.00 flush | PASS |
| J7 BioZ pads | flag | F.Cu — wired remote electrodes via top windows; no direct-skin path | FLAG |
| snap beads | 6 in groove | bead z2.62-3.12 vs groove z1.9-3.0, catch 0.12mm | PASS |
| floor thickness | >=1.6 | 1.60 mm | PASS |
| walls thickness | >=1.6 | 1.80 mm | PASS |
| lid thickness | >=1.6 | 1.80 mm | PASS |

**44/44 checks PASS, 1 flagged**
