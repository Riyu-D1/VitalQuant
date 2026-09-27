# VitalQ hw_v1 firmware interface

Classic ESP32-WROOM-32E-N8R2. GPIO16 is PSRAM inside the module and is not
a symbol pin. GPIO6–11 are the module flash bus and are not brought out.
Do not use them.

`firmware/esp32/profiles/hw_v1.yaml` and `config/hardware.example.yaml`
were not edited. They still say ESP32-S3, I2C on GPIO 8/9, SPI on
GPIO 12/11/13, MAX86141, MLX90637 at 0x3B, and ICM-42670-P at 0x68.
This page is the board that was built.

## ESP32 GPIO

| GPIO | Module pin | Net | Function |
| --- | --- | --- | --- |
| EN | 3 | ESP_EN | 10 kΩ pull-up to +3V3 (R5) |
| 36 / SENSOR_VP | 4 | FSR_ADC | FSR402 divider, ADC1 only |
| 39 / SENSOR_VN | 5 | NTC_ADC | ADC1_CH3. R60 10 kΩ from +3V3, cell NTC on J2 pin 3 to GND. R61 is the DNP on-board 10 kΩ NTC |
| 34 | 6 | ADS1292_DRDY | input |
| 35 | 7 | EXP_INT | TCA6408 /INT, 10 kΩ pull-up (R53). Input only |
| 32 | 8 | LSM6_INT1 | |
| 33 | 9 | TMP117_ALERT | U11 only. R65 is the 10 kΩ pull-up. U20 ALERT is open |
| 25 | 10 | open | was ADS1292 PWDN; that net moved to the expander |
| 26 | 11 | CS_FLASH | W25Q512 chip select, 10 kΩ pull-up (R52). Not a strap |
| 27 | 12 | open | was AD5940 RESET; that net moved to the expander |
| 14 | 13 | AD5940_GPIO0 | |
| 12 | 14 | open | strap. Leave low |
| 13 | 16 | AFE4900_ADC_RDY | |
| 15 | 23 | CS_AD5940 | strap. 10 kΩ pull-up (R7) so it is high at reset |
| 2 | 24 | open | strap. Leave low |
| 0 | 25 | ESP_IO0 | strap. 10 kΩ pull-up (R6) |
| 4 | 26 | CS_ADS1292 | |
| 17 | 28 | open | was AFE4900 RESETZ; that net moved to the expander |
| 5 | 29 | CS_AFE4900 | |
| 18 | 30 | SPI_SCK | shared |
| 19 | 31 | SPI_MISO | shared |
| 21 | 33 | I2C_SDA | 3.3 V bus |
| 3 / U0RX | 34 | ESP_RX | from CP2102 TXD |
| 1 / U0TX | 35 | ESP_TX | to CP2102 RXD |
| 22 | 36 | I2C_SCL | 3.3 V bus |
| 23 | 37 | SPI_MOSI | shared |

ADS1292R START (device pin 16) is tied to GND. Start conversions with the
SPI START opcode (SBAS502). The 3.3 V buck-boost enable is tied to its
own VIN, not to a GPIO. The rail has to exist before the expander can run.

## SPI

One bus. Each device has its own chip select. Modes are not the same;
switch the ESP32 SPI mode when changing CS.

| Signal | GPIO | Devices |
| --- | --- | --- |
| SCK | 18 | ADS1292R, AFE4900, AD5940, W25Q512 |
| MISO | 19 | same |
| MOSI | 23 | same |
| CS_ADS1292 | 4 | ADS1292R |
| CS_AFE4900 | 5 | AFE4900. I2C_SPI_SEL is tied to +3V3 |
| CS_AD5940 | 15 | AD5940. Pull-up holds the GPIO15 strap high |
| CS_FLASH | 26 | W25Q512. /WP and /HOLD tied to +3V3. Pull-up holds CS high while GPIO26 is an input |

## I2C

3.3 V bus, GPIO21 SDA and GPIO22 SCL, 4.7 kΩ pull-ups (R19, R20).

| Address | Device | How it is set |
| --- | --- | --- |
| 0x20 | TCA6408A U19 | ADDR = GND |
| 0x36 | MAX17048 U17 | fixed |
| 0x48 | TMP117 U11 | ADD0 = GND |
| 0x49 | TMP117 U20 | ADD0 = +3V3 |
| 0x76 | BME280 U13 | SDO = GND, CSB high |
| 0x6A | LSM6DSV80X U14 | SDO, SDx, SCx = GND, CS high |

1.8 V bus, behind PCA9306 U9. Pull-ups R21 and R22, 4.7 kΩ to +1V8.

| Address | Device | How it is set |
| --- | --- | --- |
| 0x39 | AS7341 U10 | fixed. INT is open; poll the device |
| 0x3A | MLX90632 U12 | ADDR = GND. VDD is still +3V3. SDA and SCL are 1.8 V |

No two devices on the same bus share an address.

## TCA6408A pins

/RESET is tied to +3V3. Both VCCI and VCCP are +3V3. The device powers up
with ports as inputs (Hi-Z). R9, R14, R11, R25, R48 and R59 pull the driven
nets down, so the front ends, the 5 V boost, the 860 nm LED, and the
charger stay off until firmware writes the port.

| Port | Pin | Net | What it drives | Idle |
| --- | --- | --- | --- | --- |
| P0 | 2 | AFE4900_RESETZ | AFE4900 RESETZ | R9 10 kΩ to GND |
| P1 | 3 | ADS1292_PWDN | ADS1292R PWDN | R14 10 kΩ to GND |
| P2 | 4 | AD5940_RESET | AD5940 RESET | R11 10 kΩ to GND |
| P3 | 5 | TX5_EN | TPS61240 EN | R25 100 kΩ to GND |
| P4 | 7 | IR_GATE | CSD13380F3 gate, SFH 4053 | R48 100 kΩ to GND |
| P5 | 8 | CHG_STAT | MCP73831 STAT | R49 10 kΩ to +3V3. Open drain |
| P6 | 9 | VBUS_DET | VBUS divider | R50 100 kΩ from VBUS, R51 200 kΩ to GND |
| P7 | 10 | CHG_EN | Q2 gate, MCP73831 PROG pull-down | R59 100 kΩ to GND. Low or Hi-Z floats PROG and charge stays off |
| /INT | 11 | EXP_INT | ESP32 GPIO35 | R53 10 kΩ to +3V3 |

VBUS_DET is about 3.33 V at 5.0 V VBUS (200/300). At 5.25 V it is 3.50 V,
under the expander absolute maximum of VCC + 0.5 V. At 4.75 V it is 3.17 V,
above VIH (0.7 × 3.3 V).

## Other fixed pins

| Device | Pin | Tied to |
| --- | --- | --- |
| TPS63802 | EN | VBAT (not the expander) |
| TPS63802 | MODE | GND (power save) |
| TPS63802 | PG | open |
| TPS63802 | EP (pad 11) | GND |
| TPS7A2018 | EN | IN (+3V3) |
| AFE4900 | I2C_SPI_SEL | +3V3 |
| AFE4900 | CONTROL1 | GND |
| AFE4900 | RLD_OUT | open. AFE RLD is unused |
| ADS1292R | START | GND |
| ADS1292R | CLKSEL | DVDD |
| ADS1292R | CLK | open |
| CP2102N | ~RSTb | R62 1 kΩ to VDD_CP2102. VBUS sense is R63/R64, not the 5 V pin |
