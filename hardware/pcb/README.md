# VitalQ hw_v1

Research prototype. This is not a medical device. Nothing here is a claim
about safety, sterility, biocompatibility, or regulatory clearance. The
The defibrillator parts are not an IEC 60601-2-27 type-test claim.
The 51 kΩ pulse resistors and the gas-discharge tubes are a research-prototype
attempt to follow the TI and TDK pulse guidance. They do not make this a
defibrillator-proof medical input.

The schematic is drawn explicitly. The PCB is an unrouted placement:
**36.5 × 52.2 mm**, rectangular, **4 layers**, parts on both sides.
Six layers were not required. The analog nets are ordinary and the
stackup is the JLCPCB 4-layer default.

Regenerate and check with `hardware/pcb/check.sh` (KiCad 9).

The firmware pin map is `PINMAP.md`. Every resistor and capacitor is
in `PASSIVES.md`. Orderable part numbers are in `vitalq_hw_v1_bom.csv`.
Footprint sources are in `lib/SOURCES.md`.

## What changed from the previous board

One part was replaced. The XC6206P332MR (U3) is now a TI TPS63802
buck-boost, 3.3 V, with the datasheet 0.47 µH inductor and the 10 µF /
22 µF 0603 capacitors. EN is tied to VBAT so the rail exists before the
I/O expander runs. MODE is grounded (power save). The exposed pad is GND.

J4, the off-board PPG pads, is gone. The ams OSRAM SFH 7072 is on the
board, on the skin side. Anodes go to the TPS61240 5 V rail (TX_5V).
Cathodes go to AFE4900 LED1–LED4. The two photodiodes go to two AFE4900
PD inputs. The footprint has an 0.8 mm edge-cut slot between the emitters
and the detectors, and a window outline on User.1.

Everything else that was on the previous board is still here, including
R8 and R10. The list is at the bottom of this file.

## Board

The module antenna keep-out is x = 0 to 6.5 mm, every copper layer.
Pads start at x ≥ 6.55 mm. USB-C is rotated so the opening is the right
edge. The buck-boost, the 5 V boost, the charger, and the CP2102 sit on
the top, on the right, next to USB. The flash, the IMU, the BME280, and
the expander are also on the top, below the module. Analog parts are on
the bottom, away from that switcher column.

Skin side, one cluster: SFH 7072 (its own emitter/detector slot), the
AS7341 with the white LED and the 860 nm LED about 5 mm from the
aperture, and the MLX90632. An 0.8 mm slot separates those LEDs from the
AS7341. J5 and J6 sit on the bottom and top edges of that cluster.
U11 is on a slotted island beside the LEDs. U20 is on the top at the
same XY, outside the module body. The island neck faces the cluster.

Above that cluster, still on the bottom, is the protection strip. Five
2512 pulse resistors, then four EDA gas-discharge tubes, then five ECG
gas-discharge tubes. The LiPo pads, the NTC divider, and the charge-enable
FET sit on the top, over the right-hand end of that strip, clear of the
module courtyard.

Electrodes are flat solder pads. No headers. J5 is five pads: ADS1292R
positive, ADS1292R negative, RLD, AFE4900 positive, AFE4900 negative.
J6 is the four EDA pads. The EDA cable is a flex tail to the shoulder.
The electrodes on that tail must land at least 5 cm from the ECG
electrodes. That distance is on the harness, not on this 36.5 mm board.

The board is not routed. JLCPCB 4-layer numbers are in
`vitalq_hw_v1.kicad_pro`: 0.09 mm track and clearance, 0.2 / 0.45 mm
vias, 0.3 mm copper-to-edge, 0.1 mm solder-mask web, mask expansion 0.
Reference designators are hidden. Fab reference text is 0.35 mm.

## Part count

**159 parts.** The previous 146 are still here. Thirteen were added:
D12–D20, Q2, R59, R60, and R61.

| Group | Qty | Parts |
| --- | --- | --- |
| ICs | 20 | U1–U20, listed below |
| MOSFETs | 2 | Q1 and Q2, both CSD13380F3 |
| ESD diodes | 9 | D1–D9 TPD1E10B06DPYR |
| Gas discharge | 9 | D12–D20, TDK S30-A90X, B88069X9231T203 |
| LEDs | 2 | D10 NF2W757G-F1, D11 SFH 4053 |
| Inductors | 2 | L1 0.47 µH, L2 1.0 µH |
| Pads | 5 | J1 USB-C, J2 LiPo (3 pads), J3 FSR, J5 ECG, J6 EDA |
| Resistors | 53 | 48 of 0402, plus R32–R36 in 2512 |
| Capacitors | 57 | 54 of 0402, plus C44, C45, C47 in 0603 |

PWR_FLAG symbols are schematic-only and are not in this count.

## Rails

USB VBUS feeds the MCP73831 and the CP2102N VBUS pin. There is no
power-path diode or switch. The cell and the charger share VBAT.
The TPS63802 makes 3.3 V from VBAT. The board does not run from USB
if the cell is missing.

| Net | Source | Loads |
| --- | --- | --- |
| VBUS | USB-C | MCP73831 VDD, CP2102N VBUS and VREGIN, the VBUS divider |
| VBAT | MCP73831 and J2 | TPS63802, TPS61240, MAX17048 |
| +3V3 | TPS63802 | ESP32, AFE4900 RX and IO, AD5940, ADS1292R, 3.3 V I2C, sensors, the white LED, the 860 nm LED resistor |
| TX_5V | TPS61240 | AFE4900 TX_SUP and the SFH 7072 anodes. Enable is expander P3 |
| +1V8 | TPS7A2018 | AS7341 VDD, PCA9306 VREF1, MLX90632 SDA/SCL pull-ups |
| VDD_CP2102 | CP2102N internal regulator | CP2102N VDD bypass only. Not tied to +3V3 |

R3 is 10 kΩ, so the MCP73831-2 charges at 100 mA while Q2 is on.
STAT goes to the expander with a pull-up. There is no charge LED.
Q2 is in series with R3. DS20001984H §5.2.2: letting PROG float disables
charge. The expander powers up with P7 as an input, R59 holds the gate
low, and charge stays off until firmware has read the NTC and drives
CHG_EN high. That is the safe default. Charging is not enabled at power-up.

TPS61240 output is 4.9–5.1 V. AFE4900 TX_SUP must be 3.0–5.25 V, so
this sits inside that window. Green LED Vf max in the SFH 7072 v1.6
sheet is 2.8 V at 20 mA.

## I2C and SPI

Addresses, chip selects, and the expander port map are in `PINMAP.md`.

One PCA9306 (U9) translates the 1.8 V sensor bus. It is an extra IC
relative to the original sensor list: AS7341 VDD is 1.7–2.0 V, and an
ESP32 input is not guaranteed to see 1.8 V as a high.

## Assumptions that are not in a public full datasheet

- AFE4900 TX_SUP headroom, ECG common-mode range, and the 100 nF / 10 MΩ
  ECG coupling are not in the public short-form. The coupling values are
  an inference.
- AS7341 NIR is characterised at 940 nm. Response at 860 nm (SFH 4053)
  was not confirmed. LDR headroom with a white LED Vf near 2.9 V from
  a 3.3 V rail is tight and was not confirmed against the full AS7341 sheet.
- MAX17048 ball map and the NF2W757 land were drawn without a downloaded
  mechanical PDF. See `lib/SOURCES.md`.
- W25Q512 8-pad pinout follows the JV-family WSON. The 512 Mbit PDF
  returned 404.
- SFH 7072 pad pitch is reconstructed from the 7.5 × 3.9 mm body and a
  2 × 6 land. Confirm pin 1 before fabrication.
- ADS1292R C34 is 47 nF. SBAS502C Fig 73 note (1): "When using the
  ADS1292R and the channel 1 respiration function, this capacitor must
  be 47 nF." The PGA section also says 4.7 nF is recommended; the
  figure note is the one that says "must" for respiration. C35 stays
  4.7 nF. R32 and R33 are now 51 kΩ instead of 49.9 kΩ. Fig 68's
  electrode node is still on the IC side of that resistor, so the
  modulation impedance is 40.2 kΩ + 51 kΩ = 91.2 kΩ. Equation 10
  scales the respiration current by that impedance. The current is
  about 1.2% lower than it was at 49.9 kΩ. The modulation path is
  unchanged apart from that existing series resistor. Fig 68's note
  is only "Patient and input protection circuitry not shown."
- S30-A90X recommended-land dimensions were read from a drawing whose
  text did not extract. Pad size and pitch are approximate. TDK says
  solder must not close the gap under the tube.
- The S30 impulse ratings are 8/20 µs and 10/1000 µs. They are not an
  IEC 60601-2-27 defibrillator-waveform energy rating.
- C54 (15 nF, 50 V, 0402) and C55 (470 nF, 0402) still sit on the EDA
  pad side of the series parts. Until a gas-discharge tube fires they
  can see the pad voltage. Their voltage ratings do not cover a
  500 V impulse sparkover. The 1 kΩ EDA network was not changed.

## Firmware profile is out of date

`firmware/esp32/profiles/hw_v1.yaml` and `config/hardware.example.yaml`
were not edited. They still describe an ESP32-S3 board with different
GPIO, a MAX86141, an MLX90637, and an ICM-42670-P. This PCB is the
classic ESP32 map in `PINMAP.md`.

## Sheets

1. Power and charging, including links to the other sheets
2. USB-UART
3. MCU and FSR
4. AFE4900 PPG, SFH 7072, and the 5 V boost
5. AD5940 EDA
6. ADS1292R ECG and respiration
7. I2C sensors, the second TMP117, and the AS7341 LEDs

## Original parts still on the board

From the board this round started from. J4 was removed because the SFH 7072
replaces it. U3 is the same reference with a new device.

| Ref | Part |
| --- | --- |
| U1 | ESP32-WROOM-32E-N8R2 |
| U2 | MCP73831T-2ACI/OT |
| U3 | was XC6206P332MR, now TPS63802DLAR |
| U4 | TPS7A2018PDBVR |
| U5 | CP2102N-A02-GQFN28R |
| U6 | AFE4900YZR |
| U7 | AD5940BCBZ-RL7 |
| U8 | ADS1292RIRSMT |
| U9 | PCA9306DCUR |
| U10 | AS7341-DLGM |
| U11 | TMP117AIDRVR |
| U12 | MLX90632SLD-DCB-100-SP, 1.8 V I2C variant, VDD still 3.3 V |
| U13 | BME280 |
| U14 | LSM6DSV80XTR |
| J1 | USB-C |
| J2 | LiPo pads, now BAT+, BAT−, and NTC |
| J3 | FSR pads |
| J5 | ECG pads, now five pads |
| J6 | EDA pads |
| R1–R3, R5–R22 | previous resistors, including R8 and R10 |
| C1–C4, C6–C11, C13–C16, C18–C30, C32–C43 | previous capacitors |

R4, C5, C12, C17, C31, TP1, and TP2 were already absent. They were not
put back.
