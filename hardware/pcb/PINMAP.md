# VitalQ hw_v2 firmware interface

ESP32-S3-MINI-1U-N4R2 (U1), replacing the WROOM-32E. 65 module pins,
15.4 × 15.4 mm, U.FL external antenna — the x < 6.5 mm module keep-out
strip is gone. On the N4R2, module pin 26 (IO26) connects to the
embedded PSRAM and is not available. Module pins 28–29 and 31–33
(IO33–IO37) are the SPI0/1 flash/PSRAM-related pads; their default
function is eFuse-decided and they stay unused. Module pins 35–38
(IO39–IO42) are the pads-JTAG port (MTCK/MTDO/MTDI/MTMS); the GPIO3
strap keeps JTAG on the USB Serial/JTAG controller, so those four are
usable GPIO — see the strapping notes.

The firmware profile said esp32s3 with I2C on GPIO 8/9 all along — on
hw_v2 that is finally true (the hw_v1 classic-ESP32 board was the drift,
not the profile). `firmware/esp32/profiles/hw_v2.yaml` and
`config/hardware.example.yaml` now carry this map, including the
spec's SPI pinout (MOSI 11 / SCK 12 / MISO 13). The old `hw_v1.yaml`
still lists the stale sensor set (MAX86141, MLX90637 at 0x3B,
ICM-42670-P at 0x68) and a `spi1` line with SCK/MISO swapped relative
to the spec — it describes a board that was never built; this page is
the board.

## ESP32-S3 GPIO

| GPIO | Module pin | Net | Function |
| --- | --- | --- | --- |
| EN | 45 | ESP_EN | R5 10 kΩ to +3V3 and C64 1 µF to GND (module-guide RC). TCA6408 /RESET and the Q3 collector also sit here. SW1 shorts it to GND |
| 0 | 4 | ESP_IO0 | Boot strap. R6 10 kΩ to +3V3. Q3 pulls it for auto-program. SW2 shorts it to GND. Boot-pad test points are TP9 (ESP_EN) and TP10 (ESP_IO0); TP24 is on ADS1292_DRDY, same as hw_v1 |
| 1 | 5 | FSR_ADC | FSR402 divider, ADC1_CH0. R8 is the divider bottom |
| 2 | 6 | ADS1292_DRDY | Timestamped input. R99 is 100 kΩ to +3V3 |
| 3 | 7 | EXP_INT | TCA6408 /INT, open drain, R53 10 kΩ pull-up. Strap pin — see strapping notes |
| 4 | 8 | LSM6_INT1 | Input |
| 5 | 9 | TMP117_ALERT | U11 only. R65 is the 10 kΩ pull-up. U20 ALERT is open |
| 6 | 10 | GAUGE_ALRT | MAX17048 ALRT, open drain, R74 10 kΩ to +3V3 |
| 7 | 11 | CHG_PG | BQ25170 /PG, open drain, R60 10 kΩ to +3V3. Low means USB power is good |
| 8 | 12 | I2C_SDA | 3.3 V bus. Matches the firmware profile |
| 9 | 13 | I2C_SCL | 3.3 V bus. " |
| 10 | 14 | CS_ADS1292 | Also the FSPI CS0 pad. R98 is 10 kΩ to +3V3 |
| 11 | 15 | SPI_MOSI | FSPID. Shared SPI host |
| 12 | 16 | SPI_SCK | FSPICLK. Shared SPI host |
| 13 | 17 | SPI_MISO | FSPIQ. Shared SPI host |
| 14 | 18 | AD5940_GPIO0 | AD5940 GPIO0 on ball F5 — one of two AD5940 sideband GPIOs now in use (ball E1 / GPIO2 drives NIR730_GATE — see below) |
| 15 | 19 | CS_AD5940 | R7 10 kΩ to +3V3, high at reset |
| 16 | 20 | CS_FLASH | W25Q512 chip select, R52 10 kΩ to +3V3. On the S3, IO16 is a plain GPIO — the N4R2 PSRAM sits on IO26, not here |
| 17 | 21 | STATUS_LED | R88 1 kΩ → D25 (KT-0603G) → GND. (The spec table's "R54" is a stale designator — the schematic keeps R88.) |
| 18 | 22 | CS_AFE4900 | AFE4900 I2C_SPI_SEL is tied to +3V3 |
| 19 | 23 | USB_DM_S3 | Native USB D− → J1 USB_D− through R103 (fitted 0 Ω strap). See "USB and console path" |
| 20 | 24 | USB_DP_S3 | Native USB D+ → J1 USB_D+ through R102 (fitted 0 Ω strap) |
| 21 | 25 | AFE4900_ADC_RDY | Timestamped input |
| 26 | 26 | — | Not a net. Embedded PSRAM on the N4R2; do not use |
| 38 | 34 | CS_MAX86178 | New. Fifth CS on the shared SPI host |
| 39 | 35 | MAX86178_INT | New, timestamped input. MTCK if pads-JTAG is ever selected — see strapping notes |
| 40 | 36 | MIC_CLK | DNP PDM mic (IM69D130-class). MTDO if pads-JTAG is selected |
| 41 | 37 | MIC_DAT | DNP PDM mic data. MTDI if pads-JTAG is selected |
| 42 | 38 | SPARE_IN | MTMS — digital-only, no SAR-ADC channel on the S3 (ADC1 = GPIO1–10, ADC2 = GPIO11–20). Spare input; R107/R108 DNP divider is a logic-level VBAT-present detect option only, never analog. Battery SoC stays with MAX17048 |
| 43 / TXD0 | 39 | ESP_TX | UART0 TX → CP2102 RXD through R72, 1 kΩ |
| 44 / RXD0 | 40 | ESP_RX | UART0 RX ← CP2102 TXD through R73, 1 kΩ |
| 45 | 41 | open | Strap (VDD_SPI). Leave unstrapped, no loads — see strapping notes |
| 46 | 44 | open | Strap, input-only. Leave unstrapped, no loads — see strapping notes |

GPIO22–25, 27–32, and 47–48 are either absent from the module or
flash/PSRAM-adjacent; none are brought out. GPIO33–37 exist on module
pins 28–29 and 31–33 with eFuse-decided defaults and are left open.

### Strapping notes (GPIO0 / 3 / 45 / 46)

- **GPIO0 (module pin 4)** — boot select. High (R6 pull-up) at reset =
  SPI boot; low = download boot (with GPIO46 low). SW2 holds it low and
  the Q3 auto-program pair drives it from CP2102 DTR/RTS. Boot test
  pads: TP10 on IO0, TP9 on EN (TP24 stays on ADS1292_DRDY per the
  schematic, same as hw_v1).
- **GPIO3 (module pin 7)** — JTAG signal-source strap. High at reset
  selects the internal USB Serial/JTAG controller; low selects the
  external JTAG pads on GPIO39–42. EXP_INT is acceptable on this strap
  because R53 (10 kΩ to +3V3) reads it high, which keeps JTAG on the
  USB path — the same connector the board already uses — and leaves
  GPIO39–42 free for MAX86178_INT, MIC_CLK, MIC_DAT, and SPARE_IN.
  The strap cannot be fought during reset: the TCA6408's /RESET is tied
  to ESP_EN, so while the S3 is in reset the expander is held in reset
  too and its open-drain /INT is Hi-Z. Whether the strap pin is
  consulted at all is eFuse-controlled (STRAP_JTAG_SEL); the default
  eFuse configuration already routes JTAG to USB_SERIAL_JTAG
  (verify vs datasheet/eFuse state if external JTAG is ever wanted).
- **GPIO45 (module pin 41)** — VDD_SPI strap. The N4R2's in-package
  flash and PSRAM are fixed-voltage 3.3 V parts; the module ships with
  the flash-voltage configuration already set (verify vs datasheet /
  eFuse). Any external pull would only risk a misread at reset, so the
  pin is left unstrapped with no loads, per spec.
- **GPIO46 (module pin 44)** — input-only, and a second boot-mode strap
  with GPIO0. The combination GPIO0 = 0 + GPIO46 = 1 is unsupported, so
  nothing may ever pull it up; leave it unstrapped with no loads, per
  spec.

## SPI

One host (FSPI / SPI2 function pins). Each device has its own chip
select. Modes are not the same across devices; switch the SPI mode when
changing CS.

| Signal | GPIO | Devices |
| --- | --- | --- |
| SCK | 12 | ADS1292R, AFE4900, AD5940, W25Q512, MAX86178 |
| MISO | 13 | same |
| MOSI | 11 | same |
| CS_ADS1292 | 10 | ADS1292R. Native FSPI CS0 pad. R98 pull-up |
| CS_AFE4900 | 18 | AFE4900. I2C_SPI_SEL is tied to +3V3 |
| CS_AD5940 | 15 | AD5940. R7 pull-up holds CS high at reset |
| CS_FLASH | 16 | W25Q512. /WP and /HOLD tied to +3V3. R52 pull-up |
| CS_MAX86178 | 38 | MAX86178, new. SPI per the schematic (SCK/MOSI/MISO_MX/SEN); the public MAX86178 datasheet is a short-form under NDA — # VERIFY SPI mode (CPOL/CPHA) and max clock vs the released pin table |

Chest respiration does not add a GPIO. It is the AD5940 high-speed
loop, selected by CS_AD5940 and the on-chip switch matrix. Shoulder EDA
keeps CE0, SE0, RE0, and DE0. The chest force and sense lines are
separate balls: AIN1 (F+), AIN0 (F−), AIN3 (S+), AIN2 (S−). Firmware
must not drive D5 (CE0) while the chest measurement is running. J5 and
J6 are not paralleled onto these nets. The new J11 sweat channel
(below) adds AD5940 mux inputs on spare balls — it is RESEARCH-GRADE
and must never share a measurement window with EDA or chest bioZ
without a firmware interlock (verify channel availability vs
datasheet).

## I2C

3.3 V bus, GPIO8 SDA and GPIO9 SCL, 4.7 kΩ pull-ups (R19, R20). SHT45
and the J9 tail share the same pull-ups — do not add pull-ups on the
tail cable.

| Address | Device | How it is set |
| --- | --- | --- |
| 0x20 | TCA6408A U19 | ADDR = GND |
| 0x36 | MAX17048 U17 | fixed |
| 0x44 | SHT45-AD1B (new) | fixed. DFN-4 1.5 × 1.5 mm on the bottom skin face |
| 0x48 | TMP117 U11 | ADD0 = GND |
| 0x49 | TMP117 U20 | ADD0 = +3V3 |
| 0x4A | Distal TMP117/TMP119 on J9 tail | ADD0 = SDA per the TMP117 address table (verify vs the tail flex) |
| 0x52 | RV-3028-C7 RTC (DNP) | fixed. VBACKUP reaches VSS through R115 (10 kΩ, DNP) per the datasheet — no dead short to GND |
| 0x76 | BME280 U13 | SDO = GND, CSB high |
| 0x6A | LSM6DSV80X U14 | SDO, SDx, SCx = GND, CS high |

1.8 V bus, behind PCA9306 U9. Pull-ups R21 and R22, 4.7 kΩ to +1V8.

| Address | Device | How it is set |
| --- | --- | --- |
| 0x39 | AS7341 U10 | fixed. INT is open; poll the device |
| 0x3A | MLX90632 U12 | ADDR = GND. VDD is still +3V3. SDA and SCL are 1.8 V |

No two devices on the same bus share an address. 0x52 stays free when
the RTC is DNP.

## TCA6408A pins

Unchanged from hw_v1 — P0–P7 keep their nets. /RESET is tied to ESP_EN,
so an S3 reset clears the expander. Both VCCI and VCCP are +3V3. Ports
power up as inputs (Hi-Z). R9, R14, R11, R25 and R48 hold the front
ends, the 5 V boost, and the 860 nm LED off. Charge does not wait on
this port: R59 holds Q2 off, and the charger runs whenever USB is
present and the NTC is in range.

| Port | Pin | Net | What it drives | Idle |
| --- | --- | --- | --- | --- |
| P0 | 2 | AFE4900_RESETZ | AFE4900 RESETZ | R9 10 kΩ to GND |
| P1 | 3 | ADS1292_PWDN | ADS1292R PWDN | R14 10 kΩ to GND |
| P2 | 4 | AD5940_RESET | AD5940 RESET | R11 10 kΩ to GND |
| P3 | 5 | TX5_EN | TPS61240 EN | R25 100 kΩ to GND |
| P4 | 7 | IR_GATE | CSD13380F3 gate, SFH 4053 | R48 100 kΩ to GND |
| P5 | 8 | CHG_STAT | BQ25170 STAT | R49 10 kΩ to +3V3. Open drain. Low means charging |
| P6 | 9 | VBUS_DET | VBUS divider | R50 100 kΩ from VBUS, R51 200 kΩ to GND |
| P7 | 10 | CHG_DIS | Q2 gate. High shorts the charger TS pin and stops charge | R59 100 kΩ to GND. Low or Hi-Z leaves charge enabled |
| /INT | 11 | EXP_INT | S3 GPIO3 (module pin 7) | R53 10 kΩ to +3V3 |

The spec's "unchanged" parenthetical lists a different-looking port
map (ADS1292_RESET, AFE4900_RESET, AD5940_RESET, FLASH_WP/RESET,
IR_GATE, GRN_GATE, KEY_DETECT — seven names for eight ports). The
table above is the hw_v1 map that "unchanged" refers to: those seven
names do not exist as nets on the hw_v1 board, and CHG_DIS / VBUS_DET /
CHG_STAT / TX5_EN must not be dropped. Flagged for the spec owner.

VBUS_DET is about 3.33 V at 5.0 V VBUS (200/300). At 5.25 V it is 3.50 V,
under the expander absolute maximum of VCC + 0.5 V. At 4.75 V it is
3.17 V, above VIH (0.7 × 3.3 V).

## USB and console path

The CP2102N (U5) is kept, wired to UART0 exactly as hw_v1: ESP_TX
(GPIO43) → R72 1 kΩ → CP_RX, and CP_TX → R73 1 kΩ → ESP_RX (GPIO44).
DTR/RTS still drive Q3, the BC847BS auto-program pair on EN and IO0.
VREGIN is on VBUS, so the CP2102 draws nothing when USB is unplugged;
VDD is the internal regulator output (VDD_CP2102, ~3.45 V), never tied
to +3V3.

New for hw_v2 is which USB D+/D− pair reaches connector J1, chosen by
two pairs of 0 Ω strap resistors (the spec's "SJ" links — on the
schematic they are R102–R105):

- **Default strap (fitted):** R102/R103 (0 Ω) connect J1 USB_D+/D− →
  USB_DP_S3/USB_DM_S3 → S3 GPIO20/GPIO19, the native USB peripheral.
  This gives the console, flashing (USB-OTG download), and USB
  Serial/JTAG debugging on the same connector, and is why the GPIO3
  pull-up matters.
- **Alternate strap (DNP):** R104/R105 (0 Ω) connect J1 USB_D+/D− →
  USB_DP_CP/USB_DM_CP → the CP2102, for out-of-band console recovery
  if the native-USB path or its firmware is dead. **Never fit both
  pairs — two USB devices cannot share one D+/D− pair.** In this mode
  S3 GPIO19/20 float; firmware should not enable the USB peripheral.

The CP2102's UART side is wired in both strap positions; only its USB
side is disconnected in the default strap. U21 (USBLC6-2SC6) stays on
the connector-side D+/D−. J8 (TC2030) still carries UART0 + EN + IO0
for a strap-independent console, see below.

## Tail connectors (hw_v2 additions)

All flat solder pads unless noted; assembly details belong to the
layout agent. J12 is a DNP alternative to populating J9–J11 as discrete
pads.

### J9 — distal-temp tail (4 pads)

Flex-mounted TMP117/TMP119, I2C 0x4A.

| Pad | Net |
| --- | --- |
| 1 | +3V3 |
| 2 | I2C_SDA |
| 3 | I2C_SCL |
| 4 | GND |

### J10 — satellite-PPG tail (6 pads)

Off-board SFH7050A, driven natively by the MAX86178 — same-site
synchronous acquisition with the main PPG.

| Pad | Net | Note |
| --- | --- | --- |
| 1 | LED1_K | LED driver 1 → LED1 cathode (U22 ball A3) |
| 2 | LED2_K | LED driver 2 → LED2 cathode (U22 ball A4) |
| 3 | LED3_K | LED driver 3 → LED3 cathode (U22 ball A5) |
| 4 | LED_AN | Shared anode rail — tied to **TX_5V** through R114 (0 Ω). Was VBAT in the first hw_v2 schematic; moved to TX_5V because VBAT lacks headroom for a ~3 V Vf emitter plus driver compliance (resolved — see POWER_V2.md) |
| 5 | PD_K | Photodiode cathode (U22 ball A2) |
| 6 | PD_A | Photodiode anode (U22 ball A1) |

### J11 — sweat-electrode site (3 pads) — RESEARCH-GRADE

Amperometric sweat-lactate research channel into spare AD5940 mux
inputs. **Labelled RESEARCH-GRADE wherever it appears, including the
schematic note (spec rule).**

| Pad | Net | Note |
| --- | --- | --- |
| 1 | J11_WE | Working electrode → R120 (0 Ω 2512 FP_HV cut-point) → SWEAT_WE → AD5940 AIN6 (ball C5), LPTIA input — VERIFY vs datasheet switch matrix |
| 2 | J11_RE | Reference electrode → R121 → SWEAT_RE → AD5940 AFE3 (ball A2), P-switch sense — VERIFY vs switch matrix |
| 3 | J11_CE | Counter electrode → R122 → SWEAT_CE → AD5940 AFE4 (ball A1), D-switch drive — VERIFY vs switch matrix |

The connector-side nets are J11_WE/RE/CE; the device-side
SWEAT_WE/RE/CE nets start on the far side of the R120–R122 0 Ω
cut-points (2512 FP_HV in the east protection column) and carry the
DNP TPD1E10B06 clamp footprints D26–D28 to GND. The same for J12:
the FFC carries the device-side SWEAT_* nets. J11 sits on the new
east edge at (38.8, 55.0).

Schematic (design.py) assigned the hw_v1-open analog balls: C5 (AIN6),
A2 (AFE3), A1 (AFE4). The internal routing (LPTIA/P/D switch mux) is
# VERIFY against the AD5940 datasheet switch matrix; the ball refs
themselves are datasheet-confirmed.

### J12 — unified FFC tail (DNP, FH12-14S-0.5SH, 14-position)

Carries the J9+J10+J11 lines as one assembly option. 13 signals — the
spec's 12-position class did not fit, so the schematic uses the
smallest standard connector that covers them: FH12-14S-0.5SH, 14
positions, pin 14 open.

| Pos | Net | Pos | Net |
| --- | --- | --- | --- |
| 1 | +3V3 | 8 | LED_AN |
| 2 | I2C_SDA | 9 | PD_K |
| 3 | I2C_SCL | 10 | PD_A |
| 4 | GND | 11 | SWEAT_WE |
| 5 | LED1_K | 12 | SWEAT_RE |
| 6 | LED2_K | 13 | SWEAT_CE |
| 7 | LED3_K | 14 | open / spare |

### J13 — MAX86178 ECG pad pair (2 pads, DNP)

Optional second ECG input pair on the MAX86178 bio-potential inputs.

| Pad | Net |
| --- | --- |
| 1 | J13_INP |
| 2 | J13_INM |

The pad nets are connector-side J13_INP/INM; R118/R119 (0 Ω 2512
FP_HV cut-points) join them to MX_ECG_INP/INM on U22 balls A6/A7,
with D29/D30 DNP clamps to GND on the IC side. J13 stays on the
bottom edge at (29.6, 60.4).

## MAX86178 notes

WLP-49 (7 × 7 bumps, 2.77 × 2.57 mm), new sheet — schematic agent's
part. From the pinmap side:

- CS_MAX86178 = GPIO38, MAX86178_INT = GPIO39 (timestamped input like
  DRDY; /INT is open-drain — R113 10 kΩ pulls it up to +3V3).
- SPI MISO reaches the host through the R106 0 Ω link on net MISO_MX
  (same isolation pattern as R89–R92).
- Supplies as drawn in design.py: AVDD on +3V3_ANA (ball F1), DVDD on
  +3V3 (F2), LED_DRV_SUP on **VBAT** (F3), reference pin MX_VREF (F4)
  bypassed with C83 1 µF. Ball names/numbers follow the MAX86176
  family — the public MAX86178 sheet is NDA-short with no pin
  descriptions, so every ball ref is # VERIFY against the released
  pin table. The LED_DRV_SUP = VBAT choice (not TX_5V) leaves a real
  headroom question for green/IR emitters on a 3.0–4.2 V rail —
  # VERIFY vs emitter Vf and driver compliance (see POWER_V2.md).
- J10 uses LED1/LED2/LED3 cathodes plus the shared LED_AN anode rail
  (TX_5V through R114 — moved off VBAT) and one PD pair; the part
  supports up to 6 LEDs
  / 4 PDs and has ECG + BioZ front ends per the public short form.
  The spare ECG pair lands on J13 (DNP).

## MAX86141 substitution (U22, hw_v2.1)

U22 is now **MAX86141ENP+** (LCSC C5328762) — a 20-bump WLP optical PPG
AFE (package N201A2+1, outline 21-100134, 2.048 × 1.848 mm, 5 × 4 grid,
0.4 mm pitch, Ø0.24 mm NSMD lands, footprint `vitalq:MAX86141_WLP20`).
Same refdes, same schematic/footprint UUIDs, same locked board position
(B.Cu, 26.1/45.05, rot 180). The MAX86178 ECG/BioZ front ends are gone —
J13's MX_ECG_INP/INM nets no longer reach U22 (D29/D30 clamps and
R118/R119 cut-points remain in place but are now unused).

U22 pin → net map (as netlisted):

| Ball | Pin | Net |
| --- | --- | --- |
| A1 | VLED | TX_5V (bypass C89 10 µF) |
| A2 | SCLK | SPI_SCK |
| A3 | SDO | MX_SDO_1V8 → U26 A1 (1.8 V side) |
| A4 | SDI | SPI_MOSI |
| A5 | CSB | CS_MAX86178 (GPIO38 — net name kept) |
| B1 | LED3_DRV | LED3_K |
| B2 | INT | MAX86178_INT (GPIO39 — net name kept) |
| B3 | GPIO1 | no connect |
| B4 | GPIO2 | no connect |
| B5 | VREF | MX_VREF (bypass C83 1 µF) |
| C1 | LED2_DRV | LED2_K |
| C2 | VDD_DIG | +1V8 (bypass C88 100 nF) |
| C3 | GND_DIG | GND |
| C4 | GND_ANA | GND |
| C5 | PD_GND | PD_A |
| D1 | LED1_DRV | LED1_K |
| D2 | VDD_ANA | +1V8 (bypass C86 100 nF + C87 10 µF) |
| D3 | PGND | GND |
| D4 | PD2_IN | no connect |
| D5 | PD1_IN | PD_K |

**U26 = SN74AXC2T245RSWR** (LCSC C1882550, RSW0010A UQFN-10 1.4 × 1.8 mm)
translates the 1.8 V SDO into the shared 3.3 V SPI MISO. Wiring:
A1 ← MX_SDO_1V8 (pin 8), B1 → MISO_MX (pin 5, into R106 as before),
DIR1 = +1V8 (pin 10, A→B), DIR2 = GND (pin 1, B→A), A2 NC (pin 9),
B2 = GND (pin 4), OE = **CS_MAX86178** (pin 2 — MISO tri-states while
the chip is deselected), GND (pin 3), VCCA = +1V8 (pin 7, bypass C90
100 nF), VCCB = +3V3 (pin 6, bypass C91 100 nF). INT stays direct:
open-drain with existing R113 10 kΩ pull-up to +3V3.

New bypass caps C86–C91 (all parked off-board for Quilter placement):
C86/C88/C90/C91 = 100 nF 0402 (C1525), C87 = 10 µF 0402 6.3 V (C15525),
C89 = 10 µF 0603 10 V (C19702). C29 keeps its footprint but is no longer
a U22 bypass cap (its +3V3_ANA role ended with the old F7 ball).

## AD5940 spare GPIO → NIR730_GATE

D12 is the new 730 nm LED (LED_0402) for the tissue-StO₂ ratio against
the AS7341 NIR channel. Anode: +3V3 through R109 (100 Ω 0402, the
D11/R47 pattern ≈ 15–20 mA). Cathode: new Q4, a CSD13380F3T clone of
Q1. Gate: net **NIR730_GATE**, driven by an AD5940 spare GPIO —
**not** an S3 pin; firmware toggles it through the AD5940 GPIO
registers over SPI.

Which ball: GPIO0 (ball F5) is already AD5940_GPIO0 → S3 GPIO14.
**Schematic (design.py) chose GPIO2 on ball E1** — one of the
GPIO-capable balls hw_v1 left open. The BCBZ ball map is
datasheet-confirmed: E1 = GPIO2, F5 = GPIO0, D6 = GPIO1, F4 = GPIO6
(and for J11: A1 = AFE4, A2 = AFE3, C5 = AIN6). Gate pull-down R110
(100 kΩ to GND, Q1/R48 pattern) keeps D12 off until the AD5940 is
configured; AD5940 GPIOs are open-drain-capable, so firmware must
configure push-pull drive or the pull-down defines the idle (verify
GPIO drive options vs datasheet).

## Other fixed pins

| Device | Pin | Tied to |
| --- | --- | --- |
| TPS63802 | EN | VBAT_SYS, the same net as VIN (not the expander) |
| TPS63802 | MODE | GND (power save) |
| TPS63802 | PG | open |
| TPS63802 | EP (pad 11) | GND |
| TPS7A2018 | EN | IN (+3V3) |
| AFE4900 | I2C_SPI_SEL | +3V3 |
| AFE4900 | CONTROL1 | GND |
| AFE4900 | RLD_OUT | open. Bias for the AFE ECG inputs comes from ADS RLDOUT |
| ADS1292R | START | GND |
| ADS1292R | GPIO1 (pin 26) | GND through R100, 10 kΩ |
| ADS1292R | GPIO2 (pin 25) | GND through R101, 10 kΩ |
| ADS1292R | CLKSEL | DVDD |
| ADS1292R | CLK | open |
| CP2102N | ~RSTb | R62 1 kΩ to VDD_CP2102. QFN-28 VDD is VIO. VBUS sense is R63/R64 |
| CP2102N | DTR, RTS | Q3, the BC847BS auto-program pair, through R70 and R71 (10 kΩ) |
| CP2102N | D+, D− | J1 only through R104/R105 (DNP alternate strap); left unconnected while R102/R103 route the pair to the S3 |
| BQ25170 | TS | J2 pin 3, or R61 if the cell has no NTC. Not both |
| MAX86178 | INT | GPIO39, open-drain — R113 10 kΩ to +3V3 |
| MAX86178 | supply pins | AVDD +3V3_ANA, DVDD +3V3, LED_DRV_SUP VBAT, MX_VREF bypassed by C83 — see "MAX86178 notes" (ball refs # VERIFY, NDA datasheet) |

ADS1292R START (device pin 16) is tied to GND. Start conversions with
the SPI START opcode (SBAS502). The 3.3 V buck-boost enable is tied to
its own VIN, not to a GPIO. The rail has to exist before the expander
can run.

## LED current

TPS61240 (U15) feeds SFH 7072 plus the J10 shared anode rail — the
hw_v2 schematic puts the MAX86178 LED-driver supply (LED_DRV_SUP) on
**VBAT**, but the LED_AN rail (through R114) is on **TX_5V**, moved
off VBAT because the cell lacks emitter-Vf plus driver-compliance
headroom. SLVS806D
recommended output current is 200 mA. Firmware must keep the AFE4900
LED current at 100–150 mA total, including the case where two LEDs
are on in one slot. The boost switch limit is higher than that and is
not a reason to program 200 mA. MAX86178 LED drive is
8-bit-programmable per the public short form; its driver pulses come
straight out of the cell on VBAT (LED_DRV_SUP = F3), while the
satellite anodes draw from TX_5V — budget the pulse envelope in
POWER_V2.md. The
AS7341 white LED (D10) is on +3V3, not on this boost; its datasheet
current limit is 100 mA. The AS7341 LED_DRIVE register can reach
258 mA. Firmware must keep LED_DRIVE at or below 40 mA. The 860 nm LED
(D11) is about 15–20 mA through R47 (100 Ω) when Q1 is on; the new
730 nm LED (D12) is the same class through R109 (100 Ω) when Q4
(NIR730_GATE) is on.

## Current-sense links

| Ref | From | To |
| --- | --- | --- |
| R93 | VBAT | VBAT_SYS |
| R94 | +3V3 | +3V3_ESP |
| R95 | +3V3 | +3V3_ANA |
| R96 | TX_5V_RAW | TX_5V |
| R97 | +1V8_LDO | +1V8 |
| R89 | MISO_ADS | SPI_MISO |
| R90 | MISO_AFE | SPI_MISO |
| R91 | MISO_AD | SPI_MISO |
| R92 | MISO_FL | SPI_MISO |
| R106 | MISO_MX | SPI_MISO |
| R114 | TX_5V | LED_AN |

R93 and R94 are 0603. The others are 0402. The MISO links sit in the
debug band, not next to the drivers. hw_v2 adds R106 — the fifth MISO
link — for the MAX86178 (its isolated MISO stub is named **MISO_MX**),
and R114, the 0 Ω TX_5V→LED_AN anode link on the J10 rail.

## Tag-Connect J8

TC2030-NL, top side. The footprint is not in the JLCPCB BOM. Unchanged;
still valid with the CP2102 recovery path because it taps UART0, not
the USB pair.

| Pin | Net |
| --- | --- |
| 1 | +3V3 |
| 2 | GND |
| 3 | ESP_TX |
| 4 | ESP_RX |
| 5 | ESP_EN |
| 6 | ESP_IO0 |

SW1 shorts ESP_EN to GND. SW2 shorts ESP_IO0 to GND.
