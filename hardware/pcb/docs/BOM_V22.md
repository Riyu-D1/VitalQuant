# V22 — Full Board BOM (orderable)

Supersedes `BOM_V2_ADDENDUM.md` (design-intent record) and the archived
`FULL_BOM_V2.md`. The authoritative machine-readable list is
`hardware/pcb/fab/assembly/BOM_JLC.csv` (83 grouped lines, 251 placements
per `CPL_JLC.csv`); this page is the readable summary.

Research prototype — not a medical device; no sourcing/claim warranty.

## ICs & modules — 26 fitted

| Ref | Part | Package | LCSC | Notes |
|---|---|---|---|---|
| U1 | ESP32-S3-WROOM-1-N16R8 | Module | C3013941 | 16 MB flash + 8 MB PSRAM, integrated antenna (north tab) |
| U2 | BQ25170DSGR | WSON-8 | C5208201 | LiPo charger; VSET 27k → 4.2 V, ISET 1.5k → 200 mA |
| U3 | TPS63802DLAR | WSON-10 | C2845237 | Buck-boost +3V3 |
| U4 | TPS7A2018PDBVR | SOT-23-5 | C963430 | 1.8 V LDO |
| U5 | CP2102N-A02-GQFN28R | QFN-28 | C964632 | USB-UART (DNP strap, see below) |
| U6 | AFE4900YZR | BGA-30 | C2651595 | Main PPG/SpO2 AFE |
| U7 | AD5940BCBZ-RL7 | BGA-56 | C650308 | EDA/BioZ potentiostat |
| U8 | ADS1292RIRSMT | QFN-33 | C882777 | ECG AFE |
| U9 | PCA9306DCUR | VSSOP-8 | C33196 | 3V3→1V8 I2C level shifter |
| U10 | AS7341-DLGM | OLGA-8 | C2649486 | 11-ch spectral sensor |
| U11, U20 | TMP117AIDRVR | DRV-6 | C699536 (qty 2) | Skin + dome temperature pair |
| U12 | MLX90632SLD-DCB-100-SP | SFN-5 | C5349491 | **order the 1.8 V I²C variant** — see caveat below |
| U13 | BME280 | LGA-8 | C92489 | Ambient P/RH/T |
| U14 | LSM6DSV80XTR | LGA-14 | C46898862 | IMU, ±80 g accel |
| U15 | TPS61240YFFR | DSBGA-6 | C2070904 | TX_5V boost |
| U16 | SFH 7072 | Module | C2655172 | PPG optical front-end (3 LED + PD) |
| U17 | MAX17048G+T10 | TDFN-8 | C2682616 | Fuel gauge |
| U18 | W25Q512JVEIQ | WSON-8 | C2986165 | 512 Mbit log flash |
| U19 | TCA6408ARSVR | UQFN-8 | C2649390 | GPIO expander (resets/charge ctl) |
| U21 | USBLC6-2SC6 | SOT-23-6 | C7519 | USB ESD array |
| U22 | MAX86141ENP+ | WLP-20 | C5328762 | Satellite PPG AFE (J10 tail); nets keep MAX86178 names |
| U23 | SHT45-AD1B | DFN-4 | C6705435 | Skin-face RH + temp |
| U26 | SN74AXC2T245RSWR | UQFN-10 | C1882550 | MAX86141 SDO 1V8→3V3 level shifter |

## DNP (on schematic, not populated)

| Ref | Part | Why |
|---|---|---|
| U24 | RV-3028-C7 RTC (C2829066) | Absolute wall-clock anchor — populate for multi-day logging |
| U25 | IM69D130 PDM mic (C536262) | Optional chest acoustic channel |
| J12 | FH12-14S-0.5SH FFC (C5378715) | Unified-tail assembly option vs discrete J9/J10/J11 pads |
| R104, R105 | 0 Ω 0402 | Alternate USB strap → CP2102 (R102/R103 fitted instead — never fit both) |
| J13 + D29/D30 + R118/R119 | MX-ECG pad pair + ESD/links | Optional MAX86141 ECG input (pads bare, chain unpopulated) |

## Optical emitters

| Ref | Part | LCSC | Duty |
|---|---|---|---|
| D10 | JB3030UWT-B-H50EA0000-1Z000001 white LED | C17610390 | AS7341 illuminator |
| D11 | SFH 4053 IR emitter | C2900307 | PPG IR (U16) |
| D12 | XL0603NRC730 730 nm NIR | — (generic 0603 NIR) | NIR730_GATE pair w/ AS7341 nir → tissue.sto2 |
| D25 | KT-0603G green status | C12624 | Status LED (GPIO17 via R88) |

## Transistors / discretes

- Q1, Q2, Q4: CSD13380F3T PicoStar (C2871092) — TS short, charge disable, NIR730_GATE
- Q3: BC847BS dual NPN (C156292) — ESP32 auto-program
- Q6: AO3401A P-FET (C15127) — VBUS/load switch on the east power tab
- D1–D30 region: TPD1E10B06-class ESD diodes on every external pad
- R118/R119 etc: 2512 HV-boundary link series on tail chains

## Connectors

| Ref | Type | LCSC | Notes |
|---|---|---|---|
| J1 | USB-C receptacle | C165948 | **at x29.5 rot 180** (V22 move) |
| J5/J6/J7/J11/J13/J3 | SMD land pads | — | skin electrode sites (pogo interface in case_v10) |
| J9/J10 | SMD pad tails | — | distal temp + satellite PPG |
| J2 | LiPo pad pair | — | cell wires → case wire channel |

## Known ordering caveat

**U12 MLX90632**: powered at 3.3 V while sitting on the 1.8 V I²C bus
(via U9 PCA9306 + U26 domain). Melexis ships 1.8 V and 3.3 V
I²C-reference variants — the ordered suffix MUST be the 1.8 V part or
the bus will not reach VIH. See `docs/CONNECTION_AUDIT_V22.md`.
