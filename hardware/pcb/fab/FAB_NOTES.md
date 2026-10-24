# VitalQ v2 — fix_v23 fabrication package

Generated from `fix_v23/vitalq_v2.kicad_pcb` and `fix_v23/vitalq_v2.kicad_sch`
with KiCad **10.0.4** (`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`).

## What changed vs fix_v22

- **DRC errors resolved (10 → 0, measured):**
  - 9 × `clearance` violations (0.100 mm vs 0.1016 mm net-class rule) between pads
    **inside the Q1/Q2/Q4 footprints** — CSD13380F3T (LCSC C2871092) in the standard
    KiCad `Texas_PicoStar_DFN-3_0.69x0.60mm` footprint (TI PicoStar/YJM0003A land
    pattern). These gaps are part of the manufacturer-recommended land pattern and
    cannot be widened without a wrong footprint. Resolved with a **narrow scoped
    rule** in `vitalq_v2.kicad_dru` (0.09 mm minimum, pads of the same parent only,
    Q1/Q2/Q4 only) — matching JLCPCB's published multilayer minimum trace/space of
    0.09 mm (jclpcb.com capabilities page, checked 24 Oct 2026). Not a global
    relaxation.
  - 1 × `npth_inside_courtyard`: J8 (TC2030-IDC-NL) NPTH alignment hole at
    (28.99, 52.184) clipped the U6 (AFE4900YZR) courtyard. Moving J8 was tested and
    rejected (the site has ~0.2 mm margins on several layers; ±0.25 mm shifts
    produced 30–73 new violations including hole clearances and unconnected pads).
    Resolved by **locally narrowing U6's courtyard** on the grazed edge
    (−1.30 → −1.15 mm relative edge): the hole edge remains 0.265 mm clear of the
    actual U6 package body outline; no copper, pads or other footprints changed.
    The embedded footprint and `vitalq.pretty/BGA30N40P5X6_260X210X50.kicad_mod`
    were updated identically.
- **BOM corrected to match repository main** (commits 9082f8b..ce500af, 22 Oct):
  U1 = ESP32-S3-WROOM-1-N16R8 **C2913202** (was C3013941), plus LCSC corrections
  for C1/C47, C7/C45, C9, C21–C70 group, C34, C46, C63/C79, C85, R30/R31,
  R43/R45, R32–R83 group, U18, U23 and D10. J12, U24, U25 are **DNP for this
  assembly run** (removed from BOM_JLC/CPL_JLC; still in BOM.csv/CPL.csv and in
  the design).
- **All fabrication outputs regenerated** from fix_v23 (gerbers X2, drill,
  positions, BOM/CPL, checksums, zip).

## DRC / ERC status (honest statement)

- Command: `kicad-cli pcb drc --severity-error --schematic-parity` on a copy of
  fix_v23 with `fix_v19/vitalq_v2.kicad_pro` rules plus `vitalq_v2.kicad_dru`,
  KiCad 10.0.4.
- Result: **0 violations, 0 unconnected items, 0 shorts, 0 schematic-parity
  issues.** ERC (`kicad-cli sch erc --severity-error`): **0 violations.**
- `--severity-all` reports 154 warning-class findings, all pre-existing classes
  (library footprint issues/mismatches, silkscreen clearance/overlap, text
  size, dangling track/via ends). None are error severity. See
  `../DRC_STATEMENT.md` and `../VERIFY_REPORT.md`.
- The one scoped rule (`vitalq_v2.kicad_dru`) covers only the 0.100 mm internal
  pad gaps of Q1/Q2/Q4; it does not waive any inter-component, hole, copper or
  courtyard check.

## Construction

- **Eight copper layers, FR4, ENIG finish**, nominal board thickness **1.6 mm**.
  The detailed stackup below sums to 1.4776 mm for copper + dielectric
  (1.53856 mm including 0.03048 mm solder mask per face); this nominal/detail
  discrepancy is present in the source design — obtain fabrication-engineering
  confirmation of the final stackup; do not substitute a different stackup.
- Track/gap requirement: at least 0.09 mm. Routed tracks use at least
  0.1016 mm; the Q1/Q2/Q4 footprint-internal pad gaps are 0.100 mm under the
  scoped 0.09 mm rule described above.
- Hole-to-hole and hole-to-copper: at least 0.20 mm.
- All vias are through-vias. Use the separate PTH and NPTH drill files; NPTH
  mounting and Tag-Connect alignment holes remain unplated and open.

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

## Board outline (measured on Edge.Cuts)

- Overall extents: **55.65 × 81.05 mm** (x −2.05…53.6, y −16.55…64.5).
- Main body: 50 × 75 mm (x −2…48, y −10.5…64.5).
- North antenna tab: approximately x −2.05…22.5, y −16.55…−10.5.
- East tab (added in fix_v18): x 48…53.6, y 20…29.9.
- Preserve the outline and every internal creepage slot.

## Antenna note

- The U1 ESP32-S3 module antenna overhangs the north tab edge. The
  `antenna_keepout` zone under/around it must remain **free of copper on all
  layers** — verified empty on the regenerated gerbers (see
  `../VERIFY_REPORT.md`). Do not place components, pours or tracks inside it.

## Required via-in-pad process

- **POFV / filled, copper-capped and planarized via-in-pad is required**,
  including the 0.25 mm land / 0.15 mm nominal-hole vias in solderable or
  electrode lands. Soldermask tenting alone is not an acceptable substitute.
- This specifically includes the U6.E2 via beneath the J8 footprint keepout and
  the J11.3 electrode via at board coordinates x = 46.8 mm, y = 55.3 mm; both
  are 0.25/0.15 mm. Apply the same filled-and-capped process to every via that
  intersects an SMD/BGA land — full list below (114 vias, measured from the
  board file).
- Keep exposed electrode contact surfaces planar and ENIG-finished, without an
  open via hole.
- The J12 `MP` entries are the DNP FFC connector's mechanical locating holes,
  not soldered joints; J12 is not populated in this build.

| Ref | Pad | Via xy (mm) | Via size/drill (mm) | Net | Pad layer |
|---|---|---|---|---|---|
| C13 | 1 | 37.786, 7.781 | 0.400/0.200 | +3V3_ANA | F.Cu |
| C50 | 2 | 16.650, 8.682 | 0.450/0.200 | IN1P_AC | F.Cu |
| C62 | 2 | 12.400, 11.610 | 0.450/0.200 | GND | F.Cu |
| C89 | 2 | 31.400, 54.500 | 0.400/0.200 | GND | F.Cu |
| D10 | 1 | 25.424, 59.061 | 0.400/0.200 | +3V3 | B.Cu |
| D10 | 1 | 24.900, 58.900 | 0.400/0.200 | AFE_N_PAD | B.Cu |
| D10 | 2 | 24.154, 57.372 | 0.400/0.200 | GND | B.Cu |
| J1 | A4 | 31.950, -4.400 | 0.400/0.200 | VBUS | F.Cu |
| J1 | A9 | 27.150, -4.350 | 0.400/0.200 | VBUS | F.Cu |
| J11 | 3 | 46.800, 55.300 | 0.250/0.150 | J11_CE | B.Cu |
| J12 | MP | 0.650, 56.500 | 0.400/0.200 | unconnected-(J12-PadMP) | B.Cu |
| J12 | MP | 0.650, 48.550 | 0.400/0.200 | unconnected-(J12-PadMP) | B.Cu |
| J2 | 2 | 12.400, 4.730 | 0.450/0.200 | GND | B.Cu |
| J3 | 1 | 42.000, 59.850 | 0.250/0.150 | +3V3 | B.Cu |
| J3 | 2 | 44.200, 60.150 | 0.250/0.150 | FSR_ADC | B.Cu |
| J9 | 1 | 46.487, 19.891 | 0.400/0.200 | GND | B.Cu |
| L1 | 1 | 44.650, 45.450 | 0.400/0.200 | PD_A | F.Cu |
| Q3 | 3 | 4.000, 11.310 | 0.450/0.200 | ESP_IO0 | F.Cu |
| R102 | 1 | 8.700, 12.200 | 0.450/0.200 | USB_DP | F.Cu |
| R120 | 2 | 34.429, 49.335 | 0.400/0.200 | GND | B.Cu |
| R120 | 2 | 35.000, 49.850 | 0.400/0.200 | ESP_EN | B.Cu |
| R121 | 1 | 34.275, 46.925 | 0.250/0.150 | SWEAT_WE | B.Cu |
| R123 | 1 | 49.600, 29.000 | 0.400/0.200 | GND | F.Cu |
| R14 | 1 | 31.175, 9.325 | 0.250/0.150 | ADS1292_PWDN | F.Cu |
| R19 | 1 | 10.750, 42.400 | 0.400/0.200 | +3V3 | F.Cu |
| R36 | 2 | 30.639, 34.317 | 0.400/0.200 | +3V3 | B.Cu |
| R36 | 2 | 29.900, 31.300 | 0.400/0.200 | MAX86178_INT | B.Cu |
| R36 | 2 | 29.817, 34.039 | 0.400/0.200 | AFE_N_AC | B.Cu |
| R60 | 2 | 31.150, 29.100 | 0.250/0.150 | CHG_PG | F.Cu |
| R76 | 2 | 3.603, 34.616 | 0.400/0.200 | ESP_IO0 | F.Cu |
| R76 | 2 | 4.482, 34.384 | 0.400/0.200 | ECG2_PAD | F.Cu |
| R76 | 2 | 4.087, 31.662 | 0.400/0.200 | ECG1_PAD | F.Cu |
| R77 | 1 | 10.421, 27.047 | 0.400/0.200 | EDA_SE_PAD | F.Cu |
| R77 | 2 | 10.162, 33.627 | 0.400/0.200 | EXP_INT | F.Cu |
| R78 | 1 | 17.611, 28.256 | 0.400/0.200 | EDA_RE_PAD | F.Cu |
| R78 | 2 | 16.467, 34.797 | 0.400/0.200 | GND | F.Cu |
| R78 | 2 | 16.471, 31.903 | 0.400/0.200 | RE_SURGE | F.Cu |
| R79 | 2 | 23.457, 34.754 | 0.400/0.200 | +3V3 | F.Cu |
| R79 | 2 | 22.716, 34.650 | 0.400/0.200 | SN_SURGE | F.Cu |
| R81 | 2 | 11.070, 35.733 | 0.400/0.200 | +3V3 | B.Cu |
| R81 | 2 | 10.500, 35.800 | 0.400/0.200 | CHG_STAT | B.Cu |
| R82 | 1 | 16.550, 39.975 | 0.250/0.150 | AD5940_RESET | B.Cu |
| R82 | 1 | 17.000, 40.022 | 0.250/0.150 | ADS1292_PWDN | B.Cu |
| R82 | 1 | 16.550, 43.524 | 0.250/0.150 | MX_ECG_INM | B.Cu |
| R82 | 1 | 17.500, 43.600 | 0.400/0.200 | PD_K | B.Cu |
| R83 | 2 | 23.976, 37.432 | 0.400/0.200 | GND | B.Cu |
| R90 | 1 | 44.750, 48.850 | 0.250/0.150 | MISO_AFE | F.Cu |
| TP11 | 1 | 36.350, -8.350 | 0.400/0.200 | ESP_TX | F.Cu |
| TP18 | 1 | 28.402, 1.788 | 0.400/0.200 | SPI_MOSI | F.Cu |
| TP25 | 1 | 29.150, 25.300 | 0.400/0.200 | +3V3 | F.Cu |
| U1 | 35 | 20.600, -8.410 | 0.450/0.200 | SPARE_IN | F.Cu |
| U1 | 41 | 11.400, -5.640 | 0.400/0.200 | GND | F.Cu |
| U1 | 41 | 8.600, -7.040 | 0.400/0.200 | GND | F.Cu |
| U1 | 41 | 10.000, -8.440 | 0.400/0.200 | GND | F.Cu |
| U1 | 41 | 10.000, -7.040 | 0.400/0.200 | GND | F.Cu |
| U1 | 41 | 8.600, -8.440 | 0.400/0.200 | GND | F.Cu |
| U1 | 41 | 11.400, -7.040 | 0.400/0.200 | GND | F.Cu |
| U1 | 41 | 8.600, -5.640 | 0.400/0.200 | GND | F.Cu |
| U1 | 41 | 10.000, -5.640 | 0.400/0.200 | GND | F.Cu |
| U1 | 41 | 11.400, -8.440 | 0.400/0.200 | GND | F.Cu |
| U14 | 4 | 18.989, 35.557 | 0.400/0.200 | +3V3 | F.Cu |
| U14 | 7 | 18.798, 37.723 | 0.400/0.200 | GND | F.Cu |
| U18 | 6 | 27.036, 41.031 | 0.400/0.200 | SWEAT_CE | F.Cu |
| U18 | 6 | 27.986, 40.231 | 0.400/0.200 | SPI_SCK | F.Cu |
| U18 | 9 | 27.400, 42.350 | 0.400/0.200 | CS_FLASH | F.Cu |
| U18 | 9 | 25.000, 46.650 | 0.400/0.200 | MX_VREF | F.Cu |
| U18 | 9 | 26.579, 42.237 | 0.400/0.200 | BIOZ_SN_PAD | F.Cu |
| U19 | 6 | 15.838, 43.635 | 0.250/0.150 | IR_GATE | F.Cu |
| U19 | 8 | 16.013, 44.497 | 0.400/0.200 | CHG_STAT | F.Cu |
| U20 | 5 | 21.250, 50.400 | 0.400/0.200 | +3V3 | F.Cu |
| U21 | 3 | 22.000, 12.850 | 0.450/0.200 | USB_DM | F.Cu |
| U21 | 4 | 23.930, 12.850 | 0.450/0.200 | USB_DM | F.Cu |
| U22 | A2 | 26.500, 46.950 | 0.250/0.150 | SPI_SCK | B.Cu |
| U22 | A4 | 25.700, 46.950 | 0.250/0.150 | SPI_MOSI | B.Cu |
| U22 | B1 | 26.900, 47.350 | 0.250/0.150 | LED3_K | B.Cu |
| U22 | B2 | 26.500, 47.350 | 0.250/0.150 | MAX86178_INT | B.Cu |
| U22 | D1 | 26.900, 48.150 | 0.250/0.150 | LED1_K | B.Cu |
| U3 | 10 | 38.922, 34.660 | 0.400/0.200 | VBAT_SYS | F.Cu |
| U5 | 25 | 12.400, 13.900 | 0.300/0.150 | CP_RX | F.Cu |
| U5 | 26 | 11.900, 13.900 | 0.300/0.150 | CP_TX | F.Cu |
| U6 | A1 | 28.000, 49.800 | 0.250/0.150 | AFE_INM | B.Cu |
| U6 | A3 | 27.200, 49.800 | 0.250/0.150 | PD_INM | B.Cu |
| U6 | B2 | 27.600, 50.200 | 0.250/0.150 | PD2_INP | B.Cu |
| U6 | B3 | 27.200, 50.200 | 0.250/0.150 | PD2_INM | B.Cu |
| U6 | B4 | 26.800, 50.200 | 0.250/0.150 | GND | B.Cu |
| U6 | C4 | 26.800, 50.600 | 0.250/0.150 | +3V3_ANA | B.Cu |
| U6 | D1 | 28.000, 51.000 | 0.250/0.150 | GND | B.Cu |
| U6 | D2 | 27.600, 51.000 | 0.250/0.150 | AFE_BG | B.Cu |
| U6 | D4 | 26.800, 51.000 | 0.250/0.150 | TX1 | B.Cu |
| U6 | D5 | 26.400, 51.000 | 0.250/0.150 | GND | B.Cu |
| U6 | E2 | 27.600, 51.400 | 0.250/0.150 | MISO_AFE | B.Cu |
| U6 | E3 | 27.200, 51.400 | 0.250/0.150 | CS_AFE4900 | B.Cu |
| U6 | E4 | 26.800, 51.400 | 0.250/0.150 | TX3 | B.Cu |
| U6 | E5 | 26.400, 51.400 | 0.250/0.150 | TX2 | B.Cu |
| U6 | F1 | 28.000, 51.800 | 0.250/0.150 | AFE_CLK | B.Cu |
| U6 | F2 | 27.600, 51.800 | 0.250/0.150 | SPI_MOSI | B.Cu |
| U7 | B3 | 38.186, 11.881 | 0.250/0.150 | BIOZ_FP | F.Cu |
| U7 | B4 | 37.786, 11.881 | 0.250/0.150 | AIN4_LPF0 | F.Cu |
| U7 | B5 | 37.386, 11.881 | 0.250/0.150 | BIOZ_SP | F.Cu |
| U7 | B6 | 36.986, 11.881 | 0.250/0.150 | DE0 | F.Cu |
| U7 | B7 | 36.586, 11.881 | 0.250/0.150 | VZERO0 | F.Cu |
| U7 | C4 | 37.786, 11.481 | 0.250/0.150 | GND | F.Cu |
| U7 | C5 | 37.386, 11.481 | 0.250/0.150 | SWEAT_WE | F.Cu |
| U7 | C7 | 36.586, 11.481 | 0.250/0.150 | VBIAS0 | F.Cu |
| U7 | D2 | 38.586, 11.081 | 0.250/0.150 | BIOZ_FN | F.Cu |
| U7 | D7 | 36.586, 11.081 | 0.250/0.150 | VREF_2V5 | F.Cu |
| U7 | E6 | 36.986, 10.681 | 0.250/0.150 | GND | F.Cu |
| U7 | E7 | 36.586, 10.681 | 0.250/0.150 | SPI_MOSI | F.Cu |
| U7 | F3 | 38.186, 10.281 | 0.250/0.150 | +3V3_ANA | F.Cu |
| U7 | F5 | 37.386, 10.281 | 0.250/0.150 | AD5940_GPIO0 | F.Cu |
| U7 | F7 | 36.586, 10.281 | 0.250/0.150 | CS_AD5940 | F.Cu |
| U8 | 1 | 42.516, -4.255 | 0.400/0.200 | PGA1N | F.Cu |
| U8 | 25 | 42.738, -0.288 | 0.400/0.200 | GND | F.Cu |
| U8 | 9 | 38.408, -4.037 | 0.400/0.200 | PGA1N | F.Cu |

## Assembly data

- `assembly/BOM.csv` is the raw `kicad-cli sch export bom` output;
  `assembly/BOM_JLC.csv` is the JLCPCB-grouped ordering BOM and is identical to
  the corrected BOM on repository main. `assembly/CPL.csv` is the full
  placement list (251 parts); `assembly/CPL_JLC.csv` excludes the three
  DNP-for-this-build parts (J12, U24, U25) — 248 rows.
- LCSC identifiers are populated for every orderable row. The only blank LCSC
  cell is the intentional D12 consignment (`XL0603NRC730`, Sealand 0603
  730 nm emitter); U12 (`MLX90632SLD-DCB-100-RE`) maps to LCSC C5349491 with
  consignment/global-source fallback. See `assembly/ASSEMBLY_CHECKS.json`.
- CPL coordinates are in millimetres at the same board origin as the gerbers
  and drills; negative Y values are intentional (KiCad exports Y upward).
  Rotations are native KiCad footprint rotations modulo 360; the assembler
  must confirm package zero-orientation and bottom-side mirroring.
- `positions_kicad.csv` / `vitalq_v2-pos.csv` are the raw position exports.
- This board is a research prototype. Nothing here is a medical, safety or
  regulatory statement.
