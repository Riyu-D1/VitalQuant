# hw_v2 — Bottom (skin-facing) side

148 parts. All skin-contact sensing inside ~x7–25, y44–56 (one ~18×12 mm site).
Electrode pads on south edge (y≈60), tails on east edge (x≈38.8).

## Sensing cluster

| Ref | Part | Pos (x,y) | Function |
|---|---|---|---|
| U16 | SFH 7072 | 13.4, 53.5 | PPG emitters + photodiode |
| U12 | MLX90632 | 10.3, 48.3 | IR temperature |
| U11 | TMP117 | 20.2, 47.9 | Contact skin temp |
| U10 | AS7341 | 20.0, 53.0 | 11-ch spectral reflectance |
| U23 | SHT45 | 7.6, 44.5 | Microclimate humidity/temp |
| D10 | NF2W757G | 24.8, 55.0 | White LED (AS7341 illuminator) |
| D11 | SFH 4053 | 14.0, 46.3 | IR emitter |
| D12 | SFH 4735 | 15.9, 48.3 | 730 nm NIR emitter |

## Analog front-ends

| Ref | Part | Function |
|---|---|---|
| U6 | AFE4900 | PPG/SpO2 AFE (drives U16) |
| U22 | MAX86178 | ECG/PPG/BioZ AFE |
| U7 | AD5940 | EDA / BioZ / sweat potentiostat |
| U8 | ADS1292R | ECG AFE |
| U9 | PCA9306 | 1.8V I2C shifter (AS7341) |

## Connectors / pads

| Ref | Function |
|---|---|
| J5 | ECG pads: ECG1/ECG2/AFE_P/AFE_N/RLD |
| J6 | EDA pads: CE/RE/SE/DE |
| J13 | MAX86178 ECG pair *(DNP)* |
| J10 | Optical tail (LED/PD) |
| J11 | Sweat site (WE/RE/CE) |
| J9 | I2C tail (+3V3/SDA/SCL/GND) |
| J12 | FFC-14 tail aggregator *(DNP)* |
| J2 | LiPo pads (BAT+/BAT−/NTC) |

## HV boundary

| Ref | Function |
|---|---|
| R32–R36 | 51k creepage ladder (ECG/AFE), y≈28 |
| R80–R83 | 51k creepage ladder (BioZ), y≈36 |
| R120–R122 | 0Ω 2512 cut-points (J11/J13) |
| D4, D5, U21 | ESD clamps (USB lines) |

## Power

| Ref | Function |
|---|---|
| U2 | BQ25170 LiPo charger |
| C85 | 47 µF VBAT bulk (new) |
| C2/C7/C20/C30/C44/C45 | Rail bulk |
| Q1, Q2, Q4 | Load/gate FETs |

## Other

~55 resistors (pull-ups/links/dividers), ~25 decoupling caps (per-IC),
H1 mounting hole, FID4/FID6 fiducials.
