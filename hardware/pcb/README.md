# VitalQ hw_v1

Research prototype. This is not a medical device. Nothing here is a claim
about safety, sterility, biocompatibility, or regulatory clearance.

The schematic is drawn explicitly (every part is on a sheet, with its
reference and value). The PCB is an unrouted starting placement for a
wearable: **37 × 30 mm**, 4 layers, parts on both sides. The ESP32-WROOM-32E
shield plus its antenna keep-out ends near x = 26 mm, and the USB-C
receptacle needs about 11 mm beside that, so 37 × 30 is as small as those
two official footprints allow. 35 × 30 mm collides the module courtyard
with the USB-C courtyard.

Regenerate and check with `hardware/pcb/check.sh` (KiCad 9).

## Extra part

**U9 PCA9306DCUR is not on the requested IC list.** It is the one extra IC.

AS7341 VDD is 1.7–2.0 V, so it cannot sit on the 3.3 V rail. The 1.8 V
TPS7A2018 powers it. An ESP32 input is not guaranteed to see 1.8 V as a
high (VIH is about 0.75 × 3.3 V). One PCA9306 translates SDA and SCL.
AS7341 INT is left open; firmware should poll 0x39.

MLX90632SLD-DCB-100-SP uses that same 1.8 V bus for SDA and SCL. The
option-code digit "1" selects 1.8 V I2C. VDD is still 3.3 V. No second
translator was added.

No other IC was added. Test points and the decoupling and bias passives
below are the datasheet minimum. There are no pin headers and no
mounting holes.

## Part-number changes

| Was | Is | Why |
| --- | --- | --- |
| ADS1292RIPBSR (TQFP-32) | **ADS1292RIRSMT** (VQFN-32) | Smaller package. SBAS502 pin numbers 1–32 are the same. Exposed pad pin 33 is AVSS. |
| MLX90632SLD-DCB-000-RE | **MLX90632SLD-DCB-100-SP** | Same pinout. The "1" option is 1.8 V I2C, so SDA and SCL moved to the PCA9306 1.8 V side. Address with ADDR = GND is still 0x3A. VDD stays on +3V3. |

## Part count

**86 parts** on the board.

| Group | Qty | Parts |
| --- | --- | --- |
| ICs | 14 | U1 ESP32-WROOM-32E-N8R2, U2 MCP73831T-2ACI/OT, U3 XC6206P332MR-G, U4 TPS7A2018PDBVR, U5 CP2102N-A02-GQFN28R, U6 AFE4900YZR, U7 AD5940BCBZ-RL7, U8 ADS1292RIRSMT, U9 PCA9306DCUR, U10 AS7341-DLGM, U11 TMP117AIDRVR, U12 MLX90632SLD-DCB-100-SP, U13 BME280, U14 LSM6DSV80XTR |
| Connectors | 6 | J1 USB-C, J2 LiPo pads, J3 FSR pads, J4 PPG pads, J5 ECG pads, J6 EDA pads |
| Passives | 64 | 22 resistors and 42 capacitors, all 0402 |
| Other | 2 | TP1 EN, TP2 IO0 |

PWR_FLAG symbols are schematic-only and are not in this count. TP1 and
TP2 are `in_bom no`, matching the KiCad test-point footprint.

There is no MPU6050, no BME680, and no MAX86141. U14 is the LSM6DSV80X.

## Why TPS7A2018

Every IC supply except the AS7341 runs at 3.3 V. AS7341 VDD is 1.7–2.0 V,
so the TPS7A20 is the 1.8 V fixed variant **TPS7A2018PDBVR** (SOT-23-5, DBV).
EN is tied to IN. The input is the XC6206 3.3 V output. KiCad's TPS7A20
symbol is the X2SON package, so the symbol is local and the footprint is
the official SOT-23-5.

## Rail plan

USB VBUS feeds the MCP73831 and the CP2102N VBUS pin directly. There is
no power-path diode, switch, or ideal diode.

The cell and the charger output share VBAT. The XC6206 input is VBAT, so
the 3.3 V rail is the cell. USB charges the cell and runs the UART. The
board does not run from USB if the cell is missing.

| Net | Source | Loads |
| --- | --- | --- |
| VBUS | USB-C | MCP73831 VDD, CP2102N VBUS and VREGIN |
| VBAT | MCP73831 and J2 | XC6206 input, AFE4900 TX_SUP, J4 VBAT pad |
| +3V3 | XC6206P332MR-G | ESP32, AFE4900 RX_SUP and IO_SUP, AD5940 and ADS1292R supplies, 3.3 V I2C, IMU, TMP117, MLX90632 VDD, BME280, FSR top |
| +1V8 | TPS7A2018PDBVR | AS7341 VDD, PCA9306 VREF1, MLX90632 SDA/SCL pull-ups |
| VDD_CP2102 | CP2102N internal regulator | CP2102N VDD bypass only. Not tied to +3V3 |
| IOVDD | +3V3 through 10 Ω | AD5940 IOVDD |

R3 is 10 kΩ, so the MCP73831-2 charges at 100 mA. STAT is open. There is
no charge LED.

## I2C

One 4.7 kΩ pull-up pair per bus, not per sensor.

| Bus | Device | Address | Address pins |
| --- | --- | --- | --- |
| 3.3 V, GPIO21/22 | TMP117 | 0x48 | ADD0 = GND |
| 3.3 V | BME280 | 0x76 | SDO = GND, CSB high |
| 3.3 V | LSM6DSV80X | 0x6A | SDO, SDx, SCx = GND, CS high |
| 1.8 V, behind U9 | AS7341 | 0x39 | INT, LDR, LED open |
| 1.8 V, behind U9 | MLX90632 | 0x3A | ADDR = GND. VDD is +3V3 |

PCA9306: VREF1 = 1.8 V, EN tied to VREF2, VREF2 to 3.3 V through 200 Ω,
pull-ups on both sides.

TMP117 pin 3 is ALERT and pin 4 is ADD0 (DRV Table 5-1). The previous
local symbol had those two swapped.

## ESP32-WROOM-32E-N8R2 pin map

GPIO16 is PSRAM on the N8R2 and is not brought out. GPIO6–11 are the
module flash bus and have no symbol pins. GPIO2 and GPIO12 are open so
those straps stay low. GPIO15 must be high at reset, so it is the only
chip-select with a pull-up.

| GPIO | Symbol pin | Net | Function |
| --- | --- | --- | --- |
| EN | 3 | ESP_EN | 10 kΩ to +3V3, TP1 |
| 36 / SENSOR_VP | 4 | FSR_ADC | FSR divider, ADC1 |
| 39 / SENSOR_VN | 5 | open | |
| 34 | 6 | ADS1292_DRDY | |
| 35 | 7 | open | |
| 32 | 8 | LSM6_INT1 | |
| 33 | 9 | TMP117_ALERT | |
| 25 | 10 | ADS1292_PWDN | |
| 26 | 11 | ADS1292_START | |
| 27 | 12 | AD5940_RESET | |
| 14 | 13 | AD5940_GPIO0 | |
| 12 | 14 | open | strap low |
| 13 | 16 | AFE4900_ADC_RDY | |
| 15 | 23 | CS_AD5940 | 10 kΩ to +3V3 |
| 2 | 24 | open | strap low |
| 0 | 25 | ESP_IO0 | 10 kΩ to +3V3, TP2 |
| 4 | 26 | CS_ADS1292 | |
| 16 | 27 | inside the module | PSRAM, no connect |
| 17 | 28 | AFE4900_RESETZ | |
| 5 | 29 | CS_AFE4900 | |
| 18 | 30 | SPI_SCK | shared |
| 19 | 31 | SPI_MISO | shared |
| 21 | 33 | I2C_SDA | 3.3 V bus |
| 3 / U0RX | 34 | ESP_RX | from CP2102 TXD |
| 1 / U0TX | 35 | ESP_TX | to CP2102 RXD |
| 22 | 36 | I2C_SCL | 3.3 V bus |
| 23 | 37 | SPI_MOSI | shared |

SPI mode for the AFE4900: I2C_SPI_SEL tied to RX_SUP. AD5940 and ADS1292R
share SCK, MOSI, and MISO.

## Solder pads

Nothing on the board is a pin header. J2–J6 are flat SMD pads with the
net function in silkscreen.

| Ref | Pads |
| --- | --- |
| J2 LiPo | BAT+, BAT- |
| J3 FSR | 3V3, FSR. One 10 kΩ (R8) from FSR_ADC to GND |
| J4 PPG | VBAT, TX1, TX2, TX3, PD+, PD-. LEDs and the photodiode are off the board |
| J5 ECG | IN+, IN-, RLD |
| J6 EDA | CE, RE, SE, DE. No series RC |

## Placement

Top: ESP32-WROOM-32E with the antenna on the left edge and the keep-out
covering x = 0 to 6.5 mm. USB-C opens on the right edge, with the CP2102N
under it. The charger and both LDOs fill the rest of that column. BME280
and the IMU sit above the module so the air sensor is not against skin.
LiPo and FSR pads are on the bottom edge.

Bottom, skin side: AFE4900, AS7341, MLX90632, and TMP117 are one group
under the module, clear of the module's through-hole ground pads and of
the antenna. PPG pads sit above that group. ECG and EDA pads sit below
it. AD5940 and ADS1292R are on the lower right, away from the antenna
and from the USB plug. Their passives are on the same side.

The board is not routed. JLCPCB 4-layer numbers are in
`vitalq_hw_v1.kicad_pro`: 0.09 mm track and clearance, 0.2 / 0.45 mm
vias, 0.3 mm copper-to-edge, 0.1 mm solder-mask web, mask expansion 0.
Reference designators are hidden. Pad function text is 0.45 mm so it
fits the 2.2 mm pad pitch.

## Assumptions

- AFE4900 CLK is held low with 1 kΩ so the internal oscillator is used.
  Ball names are the SnapMagic symbol.
- AD5940 has no crystal. XTAL pins are open. Electrodes are wired
  straight to CE0, RE0, SE0, and DE0.
- ADS1292R channel 2 is tied to channel 1. Respiration parts are not
  fitted. CLKSEL is tied to DVDD and CLK is open. The exposed pad is AVSS.
- ADS1292R digital inputs should stay low until the supplies are up
  (SBAS502). CS_AD5940 cannot be low at reset because GPIO15 must be
  high. That pull-up is R7.
- LSM6DSV80X pins 10 and 11 are NC. The Ultra Librarian land is the
  nominal QFN_LSM6DSV80XTR_STM body. Pins 6 and 7 are ground.
- MLX90632SLD-DCB-100-SP pin order matches the 000-RE. Only the I2C
  voltage changed.

## Firmware profile is out of date

`firmware/esp32/profiles/hw_v1.yaml` and `config/hardware.example.yaml`
were not edited. They still describe a different board: ESP32-S3,
I2C on GPIO 8/9, SPI on GPIO 12/11/13, MAX86141, MLX90637 at 0x3B, and
ICM-42670-P at 0x68.

This PCB is a classic ESP32-WROOM-32E. I2C is GPIO 21/22 on the 3.3 V
side of the PCA9306. SPI is GPIO 19/23/18. PPG is the AFE4900, not a
MAX86141. MLX90632 is 0x3A on the 1.8 V side, and TMP117 is 0x48 on
3.3 V. Motion is LSM6DSV80X at 0x6A. FSR sense is ADC1 GPIO36. AS7341
is 0x39 on the 1.8 V side. The firmware profile has no ECG or EDA entries.

## Open items

- Route the board. DRC unconnected-item errors are expected until then.
- The LSM6 3D model is a box. Ultra Librarian did not supply a STEP file.

## Sheets

1. Power and charging, including links to the other sheets
2. USB-UART
3. MCU and FSR
4. AFE4900 PPG
5. AD5940 EDA
6. ADS1292R ECG
7. I2C sensors and IMU

Symbol and footprint sources are in `lib/SOURCES.md`.
