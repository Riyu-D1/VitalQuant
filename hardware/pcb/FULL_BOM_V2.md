# hw_v2 — Full Board BOM (every component)

292 placed parts on `devin/hw-v2-pcb`. Per single board.
"DNP" = designed in but not populated at assembly.

## ICs & modules — 25

| Ref | Part (orderable MPN) | Package | LCSC | Notes |
|---|---|---|---|---|
| U1 | ESP32-S3-MINI-1-N4R2 | Module 15.4×20.5 | C3013941 | MCU, integrated antenna |
| U2 | BQ25170DSGR | WSON-8 | — | LiPo charger |
| U3 | TPS63802DLAR | WSON-10 | — | Buck-boost +3V3 |
| U4 | TPS7A2018PDBVR | SOT-23-5 | — | 1.8 V LDO |
| U5 | CP2102N-A02-GQFN28R | QFN-28 | — | USB-UART (strap option) |
| U6 | AFE4900YZR | BGA-30 | — | PPG/SpO2 AFE |
| U7 | AD5940BCBZ-RL7 | BGA-56 | C650308 | EDA/BioZ potentiostat |
| U8 | ADS1292RIRSMT | QFN-33 | C882777 | ECG AFE |
| U9 | PCA9306DCUR | VSSOP-8 | — | I2C level shifter |
| U10 | AS7341-DLGM | OLGA-8 | — | Spectral sensor |
| U11, U20 | TMP117AIDRVR | DRV-6 | — | Skin + ambient temp ×2 |
| U12 | MLX90632SLD-DCB-100-SP | SFN-5 | — | IR temperature |
| U13 | BME280 | LGA-8 | — | Ambient P/RH/T |
| U14 | LSM6DSV80XTR | LGA-14 | — | IMU |
| U15 | TPS61240YFFR | DSBGA-6 | — | TX_5V boost |
| U16 | SFH 7072 | Module | C2655172 | Optical emitter+PD |
| U17 | MAX17048G+T10 | TDFN-8 | C2682616 | Fuel gauge |
| U18 | W25Q512JVEIQ | WSON-8 | — | 512 Mbit flash |
| U19 | TCA6408ARSVR | UQFN-8 | C2649390 | GPIO expander |
| U21 | USBLC6-2SC6 | SOT-23-6 | C7519 | USB ESD array |
| U22 | MAX86178ENJ+ | WLP-49 | — | ECG/PPG/BioZ AFE (NDA part) |
| U23 | SHT45-AD1B(-R2) | DFN-4 | — | Humidity/temp |
| U24 | RV-3028-C7 | SON-8 | — | RTC |
| U25 | IM69D130V01XTSA1 | LLGA-5 | — | PDM mic (fitted) |

## Optical emitters — 4

| Ref | Part | Pkg | Qty |
|---|---|---|---|
| D10 | NF2W757G-F1 white LED | NF2W757G | 1 |
| D11 | SFH 4053 IR emitter | 0402 | 1 |
| D12 | SFH 4735 730 nm NIR | 0402 | 1 |
| D25 | KT-0603G green status LED | 0603 | 1 (C12624) |

## ESD / protection — 19

| Part | Refs | Qty fitted | DNP |
|---|---|---|---|
| TPD1E10B06DPYR | D1–D9, D21–D30 | 18 | 0 |
| USBLC6-2SC6 | (listed above, U21) | — | — |

## Connectors / switches — 4 orderable

| Ref | Part | Qty | Status |
|---|---|---|---|
| J1 | TYPE-C-31-M-12 USB-C receptacle | 1 | Fitted |
| J12 | FH12-14S-0.5SH(55) FFC-14 | 1 | DNP |
| SW1, SW2 | TS-1088-AR02016 tactile | 2 | Fitted (C720477) |
| J2,J3,J5,J6,J7,J8,J9,J10,J11,J13 | Solder pads / TC2030 footprint | — | Board features, no orderable part |

## Magnetics — 2

| Ref | Part | Value |
|---|---|---|
| L1 | DFE201612E-R47M | 0.47 µH |
| L2 | DFE201612E-1R0M | 1.0 µH |

## Transistors — 4

| Ref | Part | LCSC |
|---|---|---|
| Q1, Q2, Q4 | CSD13380F3T N-FET PicoStar ×3 | C2871092 |
| Q3 | BC847BS,115 dual NPN SOT-363 | — |

## Capacitors — 72

| MPN | Value / Pkg | Qty fit | DNP | Refs |
|---|---|---|---|---|
| GRM155R71C104KA88 | 100 nF / 0402 | 30 | 0 | C8,C13,C15,C18,C24,C25,C27–C29,C36–C41,C50,C51,C56–C63,C76–C79,C81 |
| GRM155R61A105KE15 | 1 µF / 0402 | 18 | 0 | C3,C4,C6,C11,C14,C16,C19,C26,C32,C33,C42,C43,C64,C66,C80,C82,C83,C84 |
| GRM155R61A474KE15 | 470 nF / 0402 | 7 | 0 | C21–C23,C55,C68–C70 |
| GRM1555C1H222JA01 | 2.2 nF / 0402 | 4 | 0 | C48,C49,C52,C53 |
| GRM155R60J475ME87 | 4.7 µF / 0402 | 3 | 0 | C2,C9,C20 |
| GRM155R71H153KA12 | 15 nF / 0402 | 2 | 0 | C54,C67 |
| CL10A476MQ8NRNC | 47 µF / 0603 | 1 | 0 | C85 (C19702) |
| CL10A226MQ8NRNC | 22 µF / 0603 | 1 | 0 | C7 (C59461) |
| GRM188R61A226ME15 | 22 µF / 0603 | 1 | 0 | C45 |
| CL10A475KA8NQNC | 4.7 µF / 0603 | 1 | 0 | C1 (C69335) |
| GRM188R61A475KE15 | 4.7 µF / 0603 | 1 | 0 | C47 |
| GRM188R61A106ME69 | 10 µF / 0603 | 1 | 0 | C44 |
| GRM155R60J106ME05 | 10 µF / 0402 | 1 | 0 | C30 |
| CL10A105KA8NNNC | 1 µF / 0603 | 1 | 0 | C10 |
| GRM155R60J225ME15 | 2.2 µF / 0402 | 1 | 0 | C46 |
| GRM155R71H473KA88 | 47 nF / 0402 | 1 | 0 | C34 |
| GRM1555C1H472JA01 | 4.7 nF / 0402 | 1 | 0 | C35 |
| GRM1555C1H152JA01 | 1.5 nF / 0402 | 1 | 0 | C65 |

## Resistors — 96

| MPN | Value / Pkg | Qty fit | DNP | Refs |
|---|---|---|---|---|
| RC0402FR-0710KL | 10 kΩ / 0402 | 26 | 0 | R5–R9,R11,R14,R49,R52,R53,R60,R65,R67–R71,R74,R98,R100,R101,R112,R113,R115,R116,R117 (C25744) |
| RC0402FR-071KL | 1 kΩ / 0402 | 14 | 0 | R10,R13,R39–R42,R62,R72,R73,R84–R88 (C11702) |
| DPCR2512-51KJT18 | 51 kΩ / 2512 | 13 | 0 | R32–R36, R76–R83 (HV ladder) |
| 0402WGF0000TCE | 0 Ω / 0402 | 11 | 2 | R89–R92,R95–R97,R102,R103,R106,R114 (+R104,R105) C17168 |
| RC0402FR-07100KL | 100 kΩ / 0402 | 9 | 2 | R24,R25,R44,R46,R48,R50,R59,R99,R110 (+R107,R108) C25741 |
| 25121WJ0000T4E | 0 Ω / 2512 | 5 | 0 | R118–R122 (HV cut-points) C2908946 |
| RC0402FR-074K7L | 4.7 kΩ / 0402 | 4 | 0 | R19–R22 |
| 0402WGF1005TCE | 10 MΩ / 0402 | 4 | 0 | R26–R29 (C26082) |
| RC0402FR-071ML | 1 MΩ / 0402 | 3 | 0 | R15–R17 |
| RC0402FR-075K1L | 5.1 kΩ / 0402 | 2 | 0 | R1,R2 |
| RC0402FR-0740K2L | 40.2 kΩ / 0402 | 2 | 0 | R30,R31 |
| RC0402FR-075M11L | 5.11 MΩ / 0402 | 2 | 0 | R43,R45 |
| RC0402FR-07100RL | 100 Ω / 0402 | 2 | 0 | R47,R109 |
| RC0402FR-07200KL | 200 kΩ / 0402 | 2 | 0 | R18,R51 |
| 0603WAF0000T5E | 0 Ω / 0603 | 2 | 0 | R93,R94 (C21189) |
| RC0402FR-0710RL | 10 Ω / 0402 | 1 | 0 | R12 |
| 0402WGF1501TCE | 1.5 kΩ / 0402 | 1 | 0 | R3 (C25867) |
| RC0402FR-07560KL | 560 kΩ / 0402 | 1 | 0 | R23 |
| NCU15XH103F6SRC | 10 kΩ NTC / 0402 | 0 | 1 | R61 (DNP — cell NTC option) |
| RC0402FR-0722K1L | 22.1 kΩ / 0402 | 1 | 0 | R63 |
| RC0402FR-0747K5L | 47.5 kΩ / 0402 | 1 | 0 | R64 |
| RC0402FR-07301KL | 301 kΩ / 0402 | 1 | 0 | R66 |
| RC0402FR-0727KL | 27 kΩ / 0402 | 1 | 0 | R75 |

## Board features (no part to buy)

- TP1–TP28 test-point pads (28)
- FID1–FID6 fiducials (6)
- H1, H2 M2 mounting holes
- J2/J3/J5/J6/J7/J9/J10/J11/J13 solder-pad fields, J8 TC2030 pads

## Distinct orderable line items: 57
## Totals per board: 285 fitted + 7 DNP placements
(Remaining DNP — all intentional options: R61 cell-NTC, R104/R105 CP2102 USB strap
(mutually exclusive with R102/R103), R107/R108 SPARE divider, J12 FFC, J13 pads)
