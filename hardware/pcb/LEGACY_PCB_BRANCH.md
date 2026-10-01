# `origin/pcb` branch — legacy hw_v1 board contents

From `vitalq_hw_v1_bom.csv` on that branch. ~210 populated parts.

## ICs

| Ref | Part | Function |
|---|---|---|
| U1 | ESP32-WROOM-32E-N8R2 | MCU — **old WROOM, no S3, no native USB** |
| U2 | BQ25170DSGR | LiPo charger |
| U3 | TPS63802DLAR | Buck-boost → +3V3 |
| U4 | TPS7A2018 | 1.8 V LDO (AS7341) |
| U5 | CP2102N | USB-UART bridge (only USB path) |
| U6 | AFE4900YZR | PPG/SpO2 AFE |
| U7 | AD5940BCBZ | EDA / BioZ potentiostat |
| U8 | ADS1292RIRSMT | ECG AFE |
| U9 | PCA9306 | I2C level shifter |
| U10 | AS7341-DLGM | 11-ch spectral |
| U11, U20 | TMP117 ×2 | Skin + ambient temp |
| U12 | MLX90632 | IR temperature |
| U13 | BME280 | Ambient P/RH/T |
| U14 | LSM6DSV80XTR | IMU |
| U15 | TPS61240 | TX_5V boost |
| U17 | MAX17048 | Fuel gauge |
| U18 | W25Q512 | 512 Mbit flash |
| U19 | TCA6408A | GPIO expander |

## Optical

| Ref | Part |
|---|---|
| U16 | SFH 7072 emitter+PD |
| D10 | NF2W757G white LED |
| D11 | SFH 4053 IR emitter |
| D25 | KT-0603G status LED |

## Connectors

| Ref | Function |
|---|---|
| J1 | USB-C |
| J2 | LiPo pads |
| J3 | FSR pads |
| J5 | ECG pads |
| J6 | EDA pads |
| J7 | Chest bioZ pads |
| J8 | TC2030 debug |

## Protection / misc

- D1–D9, D21–D24: 13× TPD1E10B06 ESD clamps
- U21 USBLC6-2SC6 (USB ESD)
- Q1, Q2 CSD13380F3 FETs; Q3 BC847BS auto-program; SW1/SW2 buttons
- L1 0.47 µH, L2 1.0 µH
- ~140 passives, fiducials, testpoints

## vs hw_v2 (`devin/hw-v2-pcb`)

**Missing vs hw_v2:** MAX86178, SHT45, RV-3028 RTC, IM69D130 mic,
D12 730 nm NIR + Q4 gate, tails J9–J13, HV cut-points R118–R122,
DNP clamps D26–D30, strap links R102–R106, C85 bulk cap (~45 parts).

**Different:** WROOM-32E (hw_v2 → ESP32-S3-MINI-1-N4R2).
