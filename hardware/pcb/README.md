# VitalQ hw_v1

Research prototype. This is not a medical device. Nothing here is a claim
about safety, sterility, biocompatibility, or regulatory clearance.

The schematic is drawn explicitly (every part is on a sheet, with its
reference and value). The PCB is an unrouted starting placement for a
wearable: 52 × 40 mm, 4 layers, parts on both sides. 35 × 30 mm does not
fit the ESP32-WROOM-32E (18 × 25.5 mm) plus a USB-C receptacle and the
analog parts, so the outline is the next size that keeps the antenna
keep-out off the left edge and the blocks in clusters.

Regenerate and check with `hardware/pcb/check.sh` (KiCad 9).

## Extra part

**U9 PCA9306DCUR is not on the requested IC list.** It is the one extra IC.

AS7341 VDD is 1.7–2.0 V, so it cannot sit on the 3.3 V rail. The 1.8 V
TPS7A2018 powers it. An ESP32 input is not guaranteed to see 1.8 V as a
high (VIH is about 0.75 × 3.3 V). One PCA9306 translates SDA and SCL.
AS7341 INT is left open; firmware should poll 0x39. The level shifter
that was removed from the old design is not otherwise replaced.

No other IC was added. Test points, M2 holes, connectors, and the
decoupling and bias passives below are the datasheet and connector
minimum.

## Part count

**88 parts** on the board.

| Group | Qty | Parts |
| --- | --- | --- |
| ICs | 14 | U1 ESP32-WROOM-32E-N8R2, U2 MCP73831T-2ACI/OT, U3 XC6206P332MR-G, U4 TPS7A2018PDBVR, U5 CP2102N-A02-GQFN28R, U6 AFE4900YZR, U7 AD5940BCBZ, U8 ADS1292RIPBSR, U9 PCA9306DCUR, U10 AS7341-DLGM, U11 TMP117AIDRVR, U12 MLX90632SLD-DCB-000-RE, U13 BME280, U14 LSM6DSV80XTR |
| Connectors | 6 | J1 USB-C, J2 LiPo (JST PH), J3 FSR402, J4 PPG, J5 ECG, J6 EDA |
| Passives | 64 | 22 resistors and 42 capacitors, all 0402 |
| Other | 4 | TP1 EN, TP2 IO0, H1 and H2 M2 |

PWR_FLAG symbols are schematic-only and are not in this count. H1 and H2
are `in_bom no`, matching the KiCad mounting-hole footprint. The CSV
`vitalq_hw_v1_bom.csv` lists every netlist component, including the holes.

There is no MPU6050. U14 is the LSM6DSV80X.

## Why TPS7A2018

Every IC except the AS7341 runs at 3.3 V. AS7341 VDD is 1.7–2.0 V, so the
TPS7A20 is the 1.8 V fixed variant **TPS7A2018PDBVR** (SOT-23-5, DBV).
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
| VBAT | MCP73831 and J2 | XC6206 input, AFE4900 TX_SUP, J4 pin 1 |
| +3V3 | XC6206P332MR-G | ESP32, AFE4900 RX_SUP and IO_SUP, AD5940 and ADS1292R supplies, 3.3 V I2C, IMU, TMP117, MLX90632, BME280, FSR top |
| +1V8 | TPS7A2018PDBVR | AS7341 VDD, PCA9306 VREF1 |
| VDD_CP2102 | CP2102N internal regulator | CP2102N VDD bypass only. Not tied to +3V3 |
| IOVDD | +3V3 through 10 Ω | AD5940 IOVDD |

R3 is 10 kΩ, so the MCP73831-2 charges at 100 mA. STAT is open. There is
no charge LED.

## I2C

One 4.7 kΩ pull-up pair per bus, not per sensor.

| Bus | Device | Address | Address pins |
| --- | --- | --- | --- |
| 3.3 V, GPIO21/22 | TMP117 | 0x48 | ADD0 = GND |
| 3.3 V | MLX90632 | 0x3A | ADDR = GND |
| 3.3 V | BME280 | 0x76 | SDO = GND, CSB high |
| 3.3 V | LSM6DSV80X | 0x6A | SDO, SDx, SCx = GND, CS high |
| 1.8 V, behind U9 | AS7341 | 0x39 | INT, LDR, LED open |

PCA9306: VREF1 = 1.8 V, EN tied to VREF2, VREF2 to 3.3 V through 200 Ω,
pull-ups on both sides.

## ESP32-WROOM-32E-N8R2 pin map

GPIO16 is PSRAM on the N8R2 and is not brought out. GPIO6–11 are the
module flash bus and have no symbol pins. GPIO2 and GPIO12 are open so
those straps stay low. GPIO15 must be high at reset, so it is the only
chip-select with a pull-up.

| GPIO | Symbol pin | Net | Function |
| --- | --- | --- | --- |
| EN | 3 | ESP_EN | 10 kΩ to +3V3, TP1 |
| 36 / SENSOR_VP | 4 | FSR_ADC | FSR402 divider, ADC1 |
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

## Connectors

| Ref | Pins |
| --- | --- |
| J2 LiPo | 1 VBAT, 2 GND |
| J3 FSR402 | 1 +3V3, 2 FSR_ADC. One 10 kΩ (R8) from FSR_ADC to GND |
| J4 PPG | 1 VBAT, 2 TX1, 3 TX2, 4 TX3, 5 PD_INP, 6 PD_INM. LEDs and the photodiode are off the board |
| J5 ECG | 1 IN1P, 2 IN1N, 3 RLDOUT |
| J6 EDA | 1 CE0, 2 RE0, 3 SE0, 4 DE0. No series RC |

## Placement

Top side: ESP32 with the antenna off the left edge, USB-UART at the
top-right, charger and both LDOs under the USB block, ADS1292R and
AD5940 at the lower right, BME280 and the IMU along the top edge so the
air sensor is not against skin. Bottom side, not under the module can:
AFE4900, the PPG header, AS7341, TMP117, MLX90632, and the PCA9306.
AD5940's reference capacitors sit on the bottom, under the BGA, because
the top side next to the TQFP is the ADS1292R cluster. M2 holes are H2
under the module's bottom edge (clear of the antenna keep-out) and H1
at the lower right.

The board is not routed. JLCPCB 4-layer numbers are in
`vitalq_hw_v1.kicad_pro`: 0.09 mm track and clearance, 0.2 / 0.45 mm
vias, 0.3 mm copper-to-edge.

## Assumptions that need a datasheet pass

- AFE4900 CLK is held low with 1 kΩ so the internal oscillator is used.
  The public SBAS861B PDF has no ball list. The footprint is hand-drawn
  from page 5 and is flagged in `lib/SOURCES.md`.
- AD5940 has no crystal. XTAL pins are open. Electrodes are wired
  straight to CE0, RE0, SE0, and DE0.
- ADS1292R channel 2 is tied to channel 1. Respiration parts are not
  fitted. CLKSEL is tied to DVDD and CLK is open. IN2 sharing IN1 keeps
  the unused channel from floating.
- ADS1292R digital inputs should stay low until the supplies are up
  (SBAS502C). CS_AD5940 cannot be low at reset because GPIO15 must be
  high. That pull-up is R7.
- LSM6DSV80X pins 10 and 11 are NC. The land is the sibling DSV16X
  LGA-14, not a DSV80X-specific download.
- MLX90632 is a 5-pad land drawn from the datasheet. LCSC's model has a
  sixth pad and was not used.

## Firmware profile is out of date

`firmware/esp32/profiles/hw_v1.yaml` and `config/hardware.example.yaml`
were not edited. They still describe a different board: ESP32-S3,
I2C on GPIO 8/9, SPI on GPIO 12/11/13, MAX86141, MLX90637 at 0x3B, and
ICM-42670-P at 0x68.

This PCB is a classic ESP32-WROOM-32E. I2C is GPIO 21/22. SPI is GPIO
19/23/18. PPG is the AFE4900, not a MAX86141. Temperature is MLX90632
at 0x3A plus TMP117 at 0x48. Motion is LSM6DSV80X at 0x6A. FSR sense is
ADC1 GPIO36. AS7341 is 0x39 on the 1.8 V side of the PCA9306. The
firmware profile has no ECG or EDA entries.

## Open items

- Route the board. DRC unconnected-item errors are expected until then.
- Confirm the AFE4900 ball names against a full mechanical table. The
  public PDF does not include one.
- Replace the hand-drawn AFE4900 and MLX90632 lands if the manufacturer
  CAD can be downloaded (see `lib/SOURCES.md` for the login walls).
- KiCad schematic parity warns that the M2 NPTH pad has no number, so
  pin 1 of H1/H2 is "missing". The holes are on the board. The footprint
  was not edited, so it stays the official KiCad model.
- Silkscreen overlaps the parts. Reference text is 0.6 mm so it fits
  the 0402s. That is a placement starting point, not finished legend.
- Decide whether the JST PH and the 1.27 mm headers are the final
  wearable connectors. They are the smallest stock KiCad parts that
  make the nets usable.

## Sheets

1. Power and charging, including links to the other sheets
2. USB-UART
3. MCU and FSR
4. AFE4900 PPG
5. AD5940 EDA
6. ADS1292R ECG
7. I2C sensors and IMU

Symbol and footprint URLs are in `lib/SOURCES.md`.
