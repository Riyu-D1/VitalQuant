# BOM cross-check — fix_v23 schematic vs repository main BOM_JLC

Source of truth: `hardware/pcb/fab/assembly/BOM_JLC.csv` on `Riyu-D1/VitalQuant`
main (corrected 22 Oct, commits 9082f8b–ce500af). Schematic LCSC fields updated
to match; fresh `kicad-cli sch export bom` reproduces main's BOM.csv
byte-identical (checked). KiCad 10.0.4.

| Designator(s) | Value | Footprint | LCSC | Result |
|---|---|---|---|---|
| C1, C47 | 4.7u | C_0603_1608Metric | C1705 | AGREE |
| C2, C20 | 4.7u | C_0402_1005Metric | C23733 | AGREE |
| C3, C4, C6, C11, C14, C16, C19, C26, C32, C33, C42, C43, C64, C66, C80, C82, C83, C84 | 1u | C_0402_1005Metric | C52923 | AGREE |
| C7, C45 | 22u | C_0603_1608Metric | C86295 | AGREE |
| C8, C13, C15, C18, C24, C25, C27, C28, C29, C36, C37, C38, C39, C40, C41, C50, C51, C56, C57, C58, C59, C60, C61, C62, C76, C77, C78, C81, C86, C88, C90, C91 | 100n | C_0402_1005Metric | C1525 | AGREE |
| C9 | 10u | C_0402_1005Metric | C15525 | AGREE |
| C10 | 1u | C_0603_1608Metric | C15849 | AGREE |
| C21, C22, C23, C55, C68, C69, C70 | 470n | C_0402_1005Metric | C188347 | AGREE |
| C30, C87 | 10u | C_0402_1005Metric | C15525 | AGREE |
| C34 | 47n | C_0402_1005Metric | C6261448 | AGREE |
| C35 | 4.7n | C_0402_1005Metric | C23691 | AGREE |
| C44, C89 | 10u | C_0603_1608Metric | C19702 | AGREE |
| C46 | 2.2u | C_0402_1005Metric | C444845 | AGREE |
| C48, C49, C52, C53 | 2.2n | C_0402_1005Metric | C22299 | AGREE |
| C54, C67 | 15n | C_0402_1005Metric | C22314 | AGREE |
| C63, C79 | 1u | C_0402_1005Metric | C52923 | AGREE |
| C65 | 1.5n | C_0402_1005Metric | C1528 | AGREE |
| C85 | 47u | C_0603_1608Metric | C730416 | AGREE |
| D1, D2, D3, D4, D5, D6, D7, D8, D9, D21, D22, D23, D24, D26, D27, D28, D29, D30 | TPD1E10B06 | TPD1E10B06_DPY | C48260 | AGREE |
| D10 | JB3030UWT-B-H50EA0000-1Z000001 | NF2W757G | C17610390 | AGREE |
| D11 | SFH4053 | LED_0402_1005Metric | C2900307 | AGREE |
| D12 | XL0603NRC730 | LED_0603_1608Metric | — | AGREE |
| D25 | KT-0603G | LED_0603_1608Metric | C12624 | AGREE |
| D33 | 1N5819WS | D_SOD-323 | C191023 | AGREE |
| J1 | USB-C | USB_C_Receptacle_HRO_TYPE-C-31-M-12 | C165948 | AGREE |
| L1 | 0.47u | L_DFE201612E | C668312 | AGREE |
| L2 | 1.0u | L_DFE201612E | C337892 | AGREE |
| Q1, Q2, Q4 | CSD13380F3T | Texas_PicoStar_DFN-3_0.69x0.60mm | C2871092 | AGREE |
| Q3 | BC847BS,115 | SOT-363_SC-70-6 | C156292 | AGREE |
| Q6 | AO3401A | SOT-23 | C15127 | AGREE |
| R1, R2 | 5.1k | R_0402_1005Metric | C25905 | AGREE |
| R3 | 1.5k | R_0402_1005Metric | C22879 | AGREE |
| R5, R6, R7, R8, R9, R11, R14, R49, R52, R53, R60, R65, R67, R68, R69, R70, R71, R72, R73, R74, R98, R100, R101, R112, R113, R115, R116, R117 | 10k | R_0402_1005Metric | C25744 | AGREE |
| R10, R13, R39, R40, R41, R42, R62, R84, R85, R86, R87 | 1k | R_0402_1005Metric | C11702 | AGREE |
| R12 | 10 | R_0402_1005Metric | C25513 | AGREE |
| R15, R16, R17 | 1M | R_0402_1005Metric | C25797 | AGREE |
| R18 | 200k | R_0402_1005Metric | C26015 | AGREE |
| R19, R20, R21, R22 | 2.2k | R_0402_1005Metric | C25879 | AGREE |
| R23 | 560k | R_0402_1005Metric | C25840 | AGREE |
| R24, R25, R44, R46, R48, R50, R59, R99, R110, R123 | 100k | R_0402_1005Metric | C25741 | AGREE |
| R26, R27, R28, R29 | 10M | R_0402_1005Metric | C17413 | AGREE |
| R30, R31 | 40.2k | R_0402_1005Metric | C25893 | AGREE |
| R32, R33, R34, R35, R36, R76, R77, R78, R79, R80, R81, R82, R83 | 51k | R_2512_6332Metric | C18009 | AGREE |
| R43, R45 | 5.11M | R_0402_1005Metric | C3017614 | AGREE |
| R47, R109 | 100 | R_0402_1005Metric | C25084 | AGREE |
| R51 | 150k | R_0402_1005Metric | C25755 | AGREE |
| R61 | NTC 10k B3380 | R_0402_1005Metric | C77131 | AGREE |
| R63 | 22.1k | R_0402_1005Metric | C853138 | AGREE |
| R64 | 47.5k | R_0402_1005Metric | C25896 | AGREE |
| R66 | 301k | R_0402_1005Metric | C6097400 | AGREE |
| R75 | 27.0k | R_0402_1005Metric | C25779 | AGREE |
| R88 | 150 | R_0402_1005Metric | C25082 | AGREE |
| R89, R90, R91, R92, R95, R96, R97, R102, R103, R106, R114 | 0 | R_0402_1005Metric | C17168 | AGREE |
| R94 | 0 | R_0603_1608Metric | C21189 | AGREE |
| R118, R119, R120, R121, R122 | 51k | R_2512_6332Metric | C18009 | AGREE |
| SW1, SW2 | TS-1088 | SW_SPST_TS-1088-xR020 | C720477 | AGREE |
| U1 | ESP32-S3-WROOM-1-N16R8 | ESP32_S3_WROOM_1 | C2913202 | AGREE |
| U2 | BQ25170DSGR | Texas_DSG0008A_WSON-8-1EP_2x2mm_P0.5mm_EP0.9x1.6mm | C5208201 | AGREE |
| U3 | TPS63802DLAR | WSON-10-1EP_2x3mm_P0.5mm_EP0.84x2.4mm | C2845237 | AGREE |
| U4 | TPS7A2018PDBVR | SOT-23-5 | C963430 | AGREE |
| U5 | CP2102N-A02-GQFN28R | QFN-28-1EP_5x5mm_P0.5mm_EP3.35x3.35mm | C964632 | AGREE |
| U6 | AFE4900YZR | BGA30N40P5X6_260X210X50 | C2651595 | AGREE |
| U7 | AD5940BCBZ-RL7 | BGA56C40P8X7_416X356X55 | C650308 | AGREE |
| U8 | ADS1292RIRSMT | QFN40P400X400X100-33N-D | C882777 | AGREE |
| U9 | PCA9306DCUR | VSSOP-8_2.3x2mm_P0.5mm | C33196 | AGREE |
| U10 | AS7341-DLGM | AS7341DLGT | C2649486 | AGREE |
| U11, U20 | TMP117AIDRVR | TMP117_DRV_NOPASTE | C699536 | AGREE |
| U12 | MLX90632SLD-DCB-100-SP | MLX90632SLDDCB100SP | C5349491 | AGREE |
| U13 | BME280 | Bosch_LGA-8_2.5x2.5mm_P0.65mm_ClockwisePinNumbering | C92489 | AGREE |
| U14 | LSM6DSV80XTR | QFN_LSM6DSV80XTR_STM | C46898862 | AGREE |
| U15 | TPS61240YFFR | TPS61240_YFF | C2070904 | AGREE |
| U16 | SFH 7072 | SFH7072 | C2655172 | AGREE |
| U17 | MAX17048G+T10 | TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm | C2682616 | AGREE |
| U18 | W25Q512JVEIQ | W25Q512_WSON8 | C7389628 | AGREE |
| U19 | TCA6408ARSVR | TCA6408A_RSV | C2649390 | AGREE |
| U21 | USBLC6-2SC6 | SOT-23-6 | C7519 | AGREE |
| U22 | MAX86141ENP+ | MAX86141_WLP20 | C5328762 | AGREE |
| U23 | SHT45-AD1B | SHT45_DFN4 | C5221601 | AGREE |
| U26 | SN74AXC2T245RSWR | Texas_RSW0010A_UQFN-10_1.4x1.8mm_P0.4mm | C1882550 | AGREE |

## Disagreements / notes

- None. Every BOM_JLC row matches the v23 schematic value/footprint/LCSC.

- D12 `XL0603NRC730` keeps an intentionally blank LCSC (consigned part).
- J12 (FH12-14S), U24 (RV-3028-C7), U25 (IM69D130): present in schematic and
  BOM.csv/CPL.csv, excluded from BOM_JLC.csv/CPL_JLC.csv — DNP for this build,
  matching main's 22 Oct edit (commit 5b2b6d3).
- J8 (TC2030-IDC-NL), fiducials, testpoints TP1–TP28, mounting holes H1/H2 and
  the integral `vitalquant:Pads_*` contact pads are board-level features, not
  purchased items — excluded from the JLC assembly CSVs as before.
- BOM_JLC groups by value+footprint+LCSC exactly as on main, including the two
  separate 51 kΩ R_2512 rows (both C18009) carried in main's file.
- LCSC page values not re-verified online for unchanged rows (stock rotates);
  all corrected rows follow the owner's 22 Oct audited values.
