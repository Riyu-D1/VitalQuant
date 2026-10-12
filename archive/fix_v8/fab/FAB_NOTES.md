# VitalQ v2 fabrication and assembly notes

## Release basis

- Source board: `fix_v8/vitalq_v2.kicad_pcb`, KiCad 10.0.4 (derived from fix_v7; all earlier revisions untouched).
- Source board SHA-256: `40a0b65d041af08b63c023c5f34672030b241af0f0802d8da3698b02d64ecf6b`.
- fix_v8 deltas vs fix_v7: **J7 chest-BioZ contact pads moved F.Cu → B.Cu** as a staggered 2×2 array at board x≈1.1–3.6 mm, y≈60.7–62.9 mm. Only the four `BIOZ_*_PAD` nets were rerouted (15 segments and 3 through-vias added, 27 segments and 1 via removed); every non-J7 track, via, pad position and net assignment is **identical to fix_v7** (verified object diff: 0 differences). Eight J7 electrode rule areas were added (4 `hv_ownlayer` on B.Cu + 4 `hv_inner` on In5/In6), matching the other electrodes' protection scheme. See `../FIX_REPORT.md`.
- Refilled error-level DRC: **0 errors, 0 unconnected, 0 schematic-parity errors** (29 accepted BOM warnings; 107 warnings total, all pre-existing classes — fewer than fix_v7's 112).
- fix_v6 deltas vs release: same copper as fix_v4/fix_v5 (8,871 tracks/vias byte-identical). BOM/value changes only: **R61, R107, R108 DNP** (join pre-existing J13, R104, R105 — six DNP refs total, footprints retained); R88 -> **150 ohm** (C25082); R51 -> **150k** (C25755); R73 -> **10k** (C25744); R19/R20/R21/R22 -> **2.2k** (C25879). CPL is 248 placements. See `../FIX_REPORT.md`.
- fix_v4 deltas vs release: D10 site-fit pads and D12 at (15.94, 50.6) as in fix_v3; **R104/R105 restored to DNP** so USB-C feeds only the ESP32 native USB port (CP2102 remains fitted but disconnected from the data lines); the pre-existing `antenna_keepout` zone under U1 was verified copper-free on all 8 layers. CPL is now 251 placements. See `../FIX_REPORT.md`.
- The owner accepted the 29 schematic/PCB BOM-exclusion attribute warnings. They concern J8 and TP1–TP28, not electrical netlist differences.
- The unfiltered report retains warning-level PCB findings of the same accepted classes as earlier fixes. There are no unconnected nets.
- Independent keepout audit from earlier fixes stands; fix_v4 adds no new keepouts (the antenna keepout already existed as a board zone).

## Construction

- **Eight copper layers, FR4, ENIG finish.**
- Board-level nominal thickness is **1.6 mm**. The unchanged detailed stackup below sums to **1.4776 mm** for copper plus dielectric, or **1.53856 mm** including the modeled 0.03048 mm soldermask on each face. This nominal/detail discrepancy is present in the source design. Obtain fabrication-engineering confirmation of the final manufacturable stackup; do not silently substitute a different stackup or change the layer order.
- Preserve the supplied outline and every remaining internal creepage slot. The former U-shaped sensor-island slot has intentionally been removed; that region is now solid board.
- Track/gap requirement: at least 0.09 mm. Routed tracks use at least 0.1016 mm; the authorized Q1/Q2/Q4 footprint-internal pad gaps are 0.100 mm with a 0.09 mm scoped rule.
- Hole-to-hole and hole-to-other-net-copper requirements: at least 0.20 mm.
- All vias are through-vias. Use the separate PTH and NPTH drill files. NPTH mounting and Tag-Connect holes remain unplated and open.

| Order | KiCad layer / material | Thickness (mm) |
|---|---|---:|
| 1 | F.Cu — Top Layer | 0.0350 |
| | FR4 prepreg, dielectric 1 | 0.0994 |
| 2 | In1.Cu — Ground Layer 2 | 0.0152 |
| | FR4 core, dielectric 2 | 0.3000 |
| 3 | In2.Cu — Layer 3 | 0.0152 |
| | FR4 prepreg, dielectric 3 | 0.1088 |
| 4 | In3.Cu — Signal Layer 4 | 0.0152 |
| | FR4 core, dielectric 4 | 0.3000 |
| 5 | In4.Cu — Power Layer 5 | 0.0152 |
| | FR4 prepreg, dielectric 5 | 0.1088 |
| 6 | In5.Cu — Signal Layer 6 | 0.0152 |
| | FR4 core, dielectric 6 | 0.3000 |
| 7 | In6.Cu — Ground Layer 7 | 0.0152 |
| | FR4 prepreg, dielectric 7 | 0.0994 |
| 8 | B.Cu — Bottom Layer | 0.0350 |

## Required via-in-pad process

- **POFV / filled, copper-capped and planarized via-in-pad is required**, including the 0.25 mm land / 0.15 mm nominal-hole vias in solderable or electrode lands. Soldermask tenting alone is not an acceptable substitute.
- This specifically includes the authorized U6.E2 via beneath the J8 footprint keepout and the J11.3 electrode via at board coordinates **x=46.8 mm, y=55.3 mm**. The latter is wholly inside its own electrode pad. Both are 0.25/0.15 mm.
- Apply the same filled-and-capped process to all other vias intersecting SMD/BGA lands. Keep exposed electrode contact surfaces planar and ENIG-finished, without an open via hole.
- J8's U6.E2 keepout notch is the exact authorized local exception. The J8/U6 NPTH/courtyard exception was accepted after a conservative minimum NPTH-to-U6-pad clearance of 0.4485 mm was verified.

## Owner-approved partial electrode isolation

- **This board intentionally uses partial, not full-stack, electrode isolation.**
- All 40 `hv_inner` electrode rule areas apply only to **In5.Cu and In6.Cu**, the two inner layers adjacent to the B.Cu skin-electrode side. They no longer apply to In1.Cu through In4.Cu.
- J5/J6/J7/J11/J13 skin-electrode pads are on B.Cu — fix_v8 moved J7's chest-bioimpedance contact pads from F.Cu to B.Cu so all five skin electrodes now face the same side. The owner's global bottom-side partial-isolation decision still applies.
- Outer-layer `hv_ownlayer` protection, all other original keepouts, and all remaining creepage slots are retained. Original keepout-report track segments were not rerouted. Only the later PWDN escape was locally adjusted while relocating offending through-vias.
- Own-electrode apertures are limited to that electrode's own via copper. Separate fill-only guards retain the original copper-pour exclusion coverage on In5.Cu/In6.Cu. No cross-electrode aperture was added. J11_CE's authorized own-net aperture follows its via into J11.3; its old aperture was restored.
- U20 and U11 remain at their original positions on their original faces. The schematic/netlist and layer stackup are unchanged.
- DRC and this fabrication package are not a medical/electrical safety certification.

## Assembly data

- `BOM.csv` and `CPL.csv` use JLC column names. Population follows the PCB's exclusion and DNP flags. Integral `vitalq:Pads_*` copper contacts, testpoints, fiducials, mounting holes and the bare Tag-Connect land pattern are not purchased/placed components and are excluded from the assembly CSVs. Their PCB copper remains in the Gerbers.
- LCSC identifiers are populated for every orderable row. The only blank LCSC cells are the two intentional consignments: D12 (`XL0603NRC730`, Sealand 0603 730 nm — no distributor stocks a 700–770 nm emitter ≤0603; see `../FIX_REPORT.md`) and U12 (`MLX90632SLD-DCB-100-RE`, JLC global-source C17235844 / Mouser). See `ASSEMBLY_CHECKS.json`.
- CPL coordinates are in millimetres at the same absolute board origin as the Gerbers and drills. KiCad exports Y upward; negative coordinates are intentional. Bottom-side X is not separately mirrored. Do not translate only one file set.
- CPL rotations are the native KiCad footprint rotations modulo 360 degrees. The assembler must review package-specific zero orientation, polarity and bottom-side orientation in the assembly preview; no unsupported part-library rotation corrections have been invented.
- `positions_kicad.csv` is the raw position-export audit file. Use the filtered `CPL.csv` for assembly.
