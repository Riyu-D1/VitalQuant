# VitalQ hw_v1 schematic

Research-prototype wiring for the wearable biosensing board. This is the
electrical netlist, not a placement and not a route. Open the KiCad project,
place the parts, and route them yourself.

This is not a medical device. Nothing here is a claim of safety, sterility,
biocompatibility, or regulatory compliance. The electrode networks follow
datasheet application circuits for a bench prototype.

The source of truth is SKiDL (`vitalq_hw_v1.py` and the `@subcircuit` modules).
Generated KiCad files are checked in so the project opens without rerunning
the generator, and `check.sh` rebuilds them.

## Regenerate

KiCad 9 (`kicad-cli` and the `pcbnew` Python module) and SKiDL 2.3 are required.

```bash
python3 -m venv ~/skidl-venv
~/skidl-venv/bin/pip install -r hardware/pcb/requirements.txt
hardware/pcb/check.sh
```

`check.sh` uses `~/skidl-venv` when it exists. Set `SKIDL_PYTHON` to point at
another interpreter. The script:

1. Rebuilds `lib/vitalq.kicad_sym` and `lib/vitalq.pretty`.
2. Runs SKiDL `ERC()`, writes `vitalq_hw_v1.net`, `vitalq_hw_v1_bom.csv`, the
   hierarchical schematic, and the unrouted board.
3. Runs `kicad-cli sch erc` and fails if that report contains errors.
4. Exports a schematic PDF, schematic SVGs, a board SVG, and a board render.

SKiDL warnings on import about `fp-lib-table` are from the library search
path. They are not ERC results. Footprints are loaded by nickname from the
KiCad install and from `fp-lib-table` in this directory.

## Open in KiCad

Open `hardware/pcb/vitalq_hw_v1.kicad_pro`. The root sheet is
`vitalq_hw_v1.kicad_sch`. Sub-sheets match the SKiDL blocks: `power`, `usb`,
`mcu`, `spi_afes` (with `afe4900`, `ad5940`, `ads1292`), `i2c_sensors`, and
`contact`.

The board file already contains every footprint and the ratsnest. Coordinates
are an import grid so the nets are visible. Move every part before routing.
The silk legend on the board says the same thing.

`PWR_FLAG` symbols are schematic-only. They are removed from
`vitalq_hw_v1.net` so pcbnew is not asked to load a footprint for them. A
power flag on a resistor-biased net (FET gate, ADC divider, `EN`, CP2102
`VBUS` sense and `~RST`, PCA9306 `VREF2`, AS7341 interrupt) marks that net as
driven for ERC. It is not a power source.

To rebuild the board from the netlist inside KiCad: Schematic Editor → Export
netlist, then PCB Editor → Tools → Update PCB from Schematic, using
`vitalq_hw_v1.net` if you want the SKiDL netlist specifically. Prefer editing
the Python and rerunning `check.sh` so the schematic and the board stay in
step.

## Rails

| Net | Source | Loads |
| --- | --- | --- |
| `VBUS` | USB-C | MCP73831 VDD, CP2102N VREGIN, charge LED, Schottky into `VSYS` |
| `VBAT` | Cell, JST PH pin 1, before the switch | MCP73831 battery pin |
| `VBAT_SW` | `VBAT` through SW1 | AO3401 drain, battery divider |
| `VSYS` | Schottky from `VBUS`, or the cell through Q1 | All LDO inputs, AFE4900 `TX_SUP` |
| `+3V3` (`3V3_D`) | U13 AP2112K-3.3TRG1 | ESP32, strap pull-ups, auto-program pull-ups |
| `+3.3VA` (`3V3_A`) | U12 TPS7A2033PDBVR | AD5940 AVDD/DVDD, ADS1292R AVDD/DVDD, AFE4900 `RX_SUP` and `IO_SUP` |
| `3V3_S` | U11 XC6206P332MR-G | I2C sensors, FSR top, 3.3 V side of the PCA9306 |
| `+1V8` | U14 XC6206P182MR-G, fed from `3V3_S` | AS7341, 1.8 V side of the PCA9306 |
| `VDD_CP2102` | CP2102N internal regulator | CP2102 only. Not tied to `+3V3`. The QFN-28 has no VIO pin. |
| `IOVDD_AD5940` | `+3.3VA` through 10 Ω, 1 µF to GND | AD5940 IOVDD. Datasheet wants DVDD up before IOVDD; the RC is a prototype delay of about 10 µs. |

LDO `EN` pins are tied to their inputs so the rails rise together. Firmware
should still avoid driving SPI until `+3.3VA` is up.

Q1 (AO3401A) source is `VSYS` and drain is `VBAT_SW`, so the body diode does
not charge the cell from USB. SW1 is on the battery side of Q1: with the
switch off, USB still powers the board and the charger still sees the cell.
Gate bias is 100 kΩ to GND and 100 kΩ to `VBUS`.

MCP73831T-2ACI/OT charges to 4.20 V. `RPROG` is 10 kΩ, which is 100 mA.
The charge LED is on `STAT` (on while charging) from `VBUS` through 1 kΩ.

## Buses

SPI, one shared `SPI_SCK` / `SPI_MOSI` / `SPI_MISO`, idle-high chip-selects:

| Device | CS | Mode |
| --- | --- | --- |
| AD5940 | GPIO15 `CS_AD5940` | Mode 0 |
| AFE4900 | GPIO5 `CS_AFE4900` | SPI (`I2C_SPI_SEL` tied to `RX_SUP`). The I2C address 0x5B is the address with `SEN` low and is unused. |
| ADS1292R | GPIO4 `CS_ADS1292R` | Mode 1 (CPOL=0, CPHA=1). Firmware has to switch mode per device. |

I2C bus A, 3.3 V, 4.7 kΩ to `3V3_S`, ESP32 GPIO21/GPIO22:

| Device | Address | Strap |
| --- | --- | --- |
| MLX90632SLD-DCB-000-RE | 0x3A | `ADDR` to GND |
| TMP117 | 0x48 | `ADD0` to GND |
| LSM6DSV80XTR | 0x6A | `SDO/TA0` to GND, mode-1 I2C (`SDx` and `SCx` to GND, `CS` high) |
| BME280 | 0x76 | `SDO` to GND, `CSB` high |

I2C bus B, 1.8 V, behind U16 PCA9306 (`VREF1` = 1.8 V, `EN` tied to `VREF2`,
`VREF2` to `3V3_S` through 200 Ω):

| Device | Address |
| --- | --- |
| AS7341-DLGM | 0x39 (fixed) |

AS7341 `INT` is 1.8 V open-drain. Q2 (BSS138) level-shifts it to GPIO35.
There is no address conflict on bus A.

## ESP32-WROOM-32E-N8R2 pin map

Module is the N8R2 (8 MB flash, 2 MB PSRAM). GPIO16 is inside the PSRAM and
the symbol pin is left no-connect. GPIO6–11 are the internal flash and stay
no-connect. ADC2 is not used.

| GPIO | Pin | Net | Direction | Notes |
| --- | --- | --- | --- | --- |
| EN | 3 | `ESP_EN` | in | 10 kΩ to `+3V3`, 1 µF to GND, button SW2, auto-program |
| 0 | 25 | `ESP_IO0` | strap / boot | 10 kΩ to `+3V3`, button SW3, auto-program |
| 1 | 35 | `UART_TX` | out | ESP32 TX → CP2102 RXD |
| 2 | 24 | — | strap | Left open. Download mode wants it low or floating. |
| 3 | 34 | `UART_RX` | in | ESP32 RX ← CP2102 TXD |
| 4 | 26 | `CS_ADS1292R` | out | 10 kΩ to `+3V3` |
| 5 | 29 | `CS_AFE4900` | out | 10 kΩ to `+3V3` |
| 6–11 | module | — | flash | Do not use |
| 12 | 14 | — | strap | Left open so MTDI stays low (3.3 V flash) |
| 13 | 16 | `AFE4900_ADC_RDY` | in | |
| 14 | 13 | `AD5940_GPIO0` | in | Interrupt from AD5940 |
| 15 | 23 | `CS_AD5940` | out | 10 kΩ to `+3V3` so the MTDO strap is high |
| 16 | 27 | — | PSRAM | Do not use |
| 17 | 28 | `AFE4900_RESETZ` | out | 10 kΩ to `+3.3VA`, active low |
| 18 | 30 | `SPI_SCK` | out | |
| 19 | 31 | `SPI_MISO` | in | |
| 21 | 33 | `I2C_SDA` | bidir | 4.7 kΩ to `3V3_S` |
| 22 | 36 | `I2C_SCL` | out | 4.7 kΩ to `3V3_S` |
| 23 | 37 | `SPI_MOSI` | out | |
| 25 | 10 | `ADS1292_PWDN` | out | 10 kΩ to `+3.3VA`, active low |
| 26 | 11 | `ADS1292_START` | out | |
| 27 | 12 | `AD5940_RESET` | out | 10 kΩ to `IOVDD_AD5940`, active low |
| 32 | 8 | `LSM6_INT1` | in | |
| 33 | 9 | `TMP117_ALERT` | in | 10 kΩ to `3V3_S`, open-drain |
| 34 | 6 | `ADS1292_DRDY` | in | Input only |
| 35 | 7 | `AS7341_INT` | in | Input only, level-shifted |
| 36 | 4 | `FSR_SENSE` | analog | ADC1_CH0 |
| 39 | 5 | `VBAT_SENSE` | analog | ADC1_CH3. Divider is 100 kΩ / 100 kΩ from `VBAT_SW`, so 4.2 V reads as 2.1 V. |

Auto-program matches the Espressif DevKit cross-coupled NPN circuit, with
MMBT3904 and no base resistors. CP2102 `~RTS` drives the EN transistor base;
`~DTR` drives the GPIO0 transistor base.

USB-C is a 16-pin USB 2.0 receptacle (HRO TYPE-C-31-M-12 land). Each CC pin
has 5.1 kΩ 1% to GND. D+/D− go through USBLC6-2SC6. The CP2102 VBUS divider
is 22.1 kΩ from VBUS and 47.5 kΩ to GND, about 3.4 V, which meets VIH.
`~RST` has 1 kΩ to `VDD_CP2102`.

## Assumptions

Parts the component list had not selected, and the choices made here:

| Item | Choice |
| --- | --- |
| LiPo connector J3 | JST PH B2B, pin 1 positive (`JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical`) |
| FSR connector J1 | 2-pin 2.54 mm header. The FSR402 is off-board. |
| ECG header J5 | 2.54 mm 1x3, LA / RA / RL, for the ADS1292R only |
| AFE4900 ECG header J6 | 2.54 mm 1x3, INP / INM / RLD. Separate pair from J5. |
| EDA header J4 | 2.54 mm 1x4, CE / RE / SE / DE |
| USB receptacle | HRO TYPE-C-31-M-12 footprint |
| Power switch SW1 | C&K PCM12 footprint used as SPST (pads 1 and 2). Pad 2 is assumed to be the common pin. Pad 3 is open. Unnumbered frame pads on that footprint are tied to GND in the board file. |
| EN / BOOT buttons | TL3342-style tactile switch |
| Charge LED D1 | Generic red 0603 |
| PPG LEDs D2 D3 D4 | Generic 1206, targets ~525 nm (TX1), ~940 nm (TX3), ~660 nm (TX2). Not optically specified parts. Common anode on `VSYS`. |
| PPG photodiode D6 | Generic Si PIN on `D_SOD-123`. Cathode to `INP` (A2), anode to `INM` (A3). |
| AS7341 LED D7 | Generic 0603, anode on 1.8 V, cathode on `LDR`. Current is the AS7341 register, so there is no series resistor. |
| AD5940 crystal Y1 | DNP. 2-pin 3.2x1.5 mm, 16 MHz, 12 pF load caps for an assumed 8 pF crystal. Internal oscillator is the one to use. |
| AD5940 `AVDD_REG` capacitor | 1 µF. The pin table does not state the value. |
| AD5940 electrodes | Series 1 kΩ plus the Rev G Z-measurement capacitors: 15 nF on CE, 470 nF on RE, SE, and DE. This is an AC impedance network. It blocks DC. |
| AD5940 `RCAL` | 1.00 kΩ 0.1% between `RCAL0` and `RCAL1` |
| AFE4900 `CLK` | 1 kΩ to GND so the pin is not floating before `OSC_ENABLE`. Not a hard short. |
| ADS1292R electrodes | Fig 68 values (10 MΩ bias, 2.2 nF, 0.1 µF and 40.2 kΩ into `RESP_MOD`, 1 MΩ RLD). Extra 10 kΩ series resistors on LA, RA, and RL are a prototype choice, not a safety network. Channel 1 and channel 2 share LA/RA. |
| MLX90632 VDD capacitor | 100 nF. Fig 27a says the capacitor is within 10 mm and does not print the value. |
| Battery divider | 100 kΩ / 100 kΩ after the switch, 100 nF on the tap |
| CP2102 unused modem inputs | `~CTS`, `~DSR`, `~DCD`, and `~RI/CLK` tied to `VDD_CP2102` |

Stock-symbol footprint overrides, because the inherited footprint was wrong:
ESP32 land is `RF_Module:ESP32-WROOM-32E` (the symbol default is the 32D).
USBLC6-2SC6 is SOT-23-6, not SOT-666. SS14 is SMA, not DO-41. PCA9306DC,
MMBT3904, and BSS138 footprints are set explicitly.

## Custom symbols and footprints

See `lib/SOURCES.md` for the datasheet page behind each local library part.
Short version:

| Part | Why it is local |
| --- | --- |
| AFE4900YZR | No KiCad symbol. Land is YZ0030-C01 from SBAS861B p. 5 (0.23 mm NSMD). The public 8-page PDF has no ball list; ball names are from the full pin-function table. `ALDO_1V8` and `DLDO_1V8` are internal nodes, not balls. |
| AD5940BCBZ | No KiCad symbol. Ball map is AD5940/AD5941 Rev G. Pad diameter 0.22 mm is an assumption. Body 3.6 × 4.2 mm, 0.4 mm pitch, A1 top-left. |
| ADS1292R | No KiCad symbol. Footprint is the official TQFP-32. Pinout is SBAS502C pp. 4–5. |
| LSM6DSV80X | No KiCad symbol. Footprint pin 1 follows DS14764 Rev 2 Fig 5 (bottom view, pin 1 on the right), not the stock LGA-14. Terminals from Fig 24 p. 158. TN0018 is the official land pattern and was not copied. |
| MLX90632 | No KiCad symbol. One row of 5 pins, 0.50 mm, DOC 3901090632 Rev 13 Table 17 p. 46. No center pad. |
| TPS7A2033PDBVR | KiCad only has the X2SON symbol. Pins from SBVS338H p. 3. Footprint is the official SOT-23-5. |
| XC6206 | KiCad `XC6206PxxxMR` extends a symbol whose pin numbers are swapped versus Torex (pin 1 VSS, pin 2 VIN, pin 3 VOUT). Footprint is the official SOT-23. |
| TMP117 | Stock symbol matches SNOSD82D Table 5-1 p. 4 but has no exposed-pad pin and points at SOT-563. Pin 7 is GND so WSON pad 7 is connected. |

AS7341 uses the official `AS7341DLG` symbol. Pinout matches DS000504 v3-00 p. 6.

## ERC

SKiDL `ERC()` is zero errors. Accepted warnings:

- `TMP117_ALERT`: open-collector `ALERT` meets ESP32 GPIO33, which the symbol marks bidirectional.
- `VDD_CP2102`: `~RI/CLK` is bidirectional and is tied to the local 3.3 V, so it meets the power flag.
- `GND`: BME280 `SDO` is bidirectional and is strapped to GND (address 0x76), so it meets the GND power flag.

The generated sheets are labels and KiCad power symbols. The SKiDL wire
router does not finish on this netlist, so `vitalq_hw_v1.py` marks every net
as a stub before `generate_schematic`. `auto_stub` stays off: that pass snaps
two-pin parts onto each other and the resulting schematic shorts rails that
are separate in the netlist. NC pins are no-connect flags, not a shared label.

`kicad-cli sch erc` on the generated sheets is 0 errors and 172 warnings
(KiCad 9.0.9). `check.sh` writes the full report to `build/erc_sch.rpt` and
fails only on errors. The warnings are:

- 168 `lib_symbol_mismatch`. SKiDL embeds a copy of each symbol in the sheet.
  KiCad compares that copy to the library file (`Device:R`, `power:PWR_FLAG`,
  the local `vitalq` symbols, and the other stock symbols) and they are not
  byte-identical. The pin numbers used for the netlist are the ones in the
  sheet.
- 2 `endpoint_off_grid`. AD5940 ball D6 (`GPIO1`, left open) and AFE4900 ball
  E1 (`RESETZ`) are not on the 50 mil schematic grid. The schematic netlist
  still has `RESETZ` on `AFE4900_RESETZ`. `GPIO1` stays unconnected.
- 2 `pin_to_pin`. BME280 `SDO` (bidirectional, strapped low for address 0x76)
  meets the GND power flag, and CP2102N `~RI/CLK` (bidirectional, tied to the
  local 3.3 V) meets the `VDD_CP2102` power flag. Same two nets SKiDL warns
  about, aside from `TMP117_ALERT`, which KiCad does not flag.

Intentional unconnected pads, not missing connections:

- ESP32 flash / PSRAM / GPIO2 / GPIO12, marked no-connect.
- AFE4900 `DNC`, unused photodiode inputs, `TX4`, `PROG_OUT1`.
- AD5940 `DNC` and unused AFE/AIN/GPIO pins. `RC0_2` is left open, which the datasheet allows.
- LSM6DSV80X pins 10 and 11 (datasheet: solder them, leave them electrically open) and `INT2`.
- ADS1292R `CLK` (3-state when the internal oscillator is selected), `GPIO1`, `GPIO2`. Firmware should not leave GPIO1/GPIO2 as inputs.
- USB SBU1/SBU2, power-switch pad 3, CP2102 pin 10 and the unused GPIO/suspend/charge pins.
- Paste-only apertures on the QFN and WSON exposed pads are not copper and are not nets.

ADS1292R digital inputs should be held low until the supplies are up (SBAS502C
power-up note, p. 65). Chip-selects are pulled up so they idle high. That
disagrees with the power-up note for `CS`. Firmware should keep SPI quiet
until `+3.3VA` is valid rather than pulling `CS` down and fighting the strap
on GPIO15.

## Firmware profile drift

`firmware/esp32/profiles/hw_v1.yaml` and `config/hardware.example.yaml` were
not edited. They still describe a different board:

| Profile field | Profile today | This schematic |
| --- | --- | --- |
| `soc` | `esp32s3` | ESP32-WROOM-32E (not S3) |
| I2C | GPIO 8 / 9 | GPIO21 SDA, GPIO22 SCL |
| SPI | MISO 12, MOSI 11, SCLK 13 | MISO 19, MOSI 23, SCLK 18 |
| PPG | MAX86141, 660/880 nm | AFE4900, green / red / IR, SPI |
| Temperature | MLX90637 at 0x3B | MLX90632 at 0x3A, plus TMP117 at 0x48 |
| Motion | ICM-42670-P at 0x68 | LSM6DSV80X at 0x6A |
| FSR | `adc1_ch0` | Still ADC1 channel 0, which is GPIO36 on this module |

The profile has no ADS1292R, AD5940, AFE4900, or AS7341-on-1.8 V entries.
AS7341 at 0x39 and BME280 at 0x76 match the profile. The pin numbers do not.

## Open items

- Pick real PPG LEDs and a photodiode. The 1206 and SOD-123 lands are placeholders.
- Pick the electrode connectors. The 2.54 mm headers are placeholders.
- Confirm the LiPo connector. JST PH is the assumption.
- Confirm the PCM12 pin that is common, and whether the frame pads should be GND.
- AD5940 pad diameter 0.22 mm, `AVDD_REG` 1 µF, and the series CISO network are assumptions. DC skin conductance needs a different electrode network; the one fitted blocks DC.
- LSM6 land should be replaced with the TN0018 pattern before fabrication.
- MLX90632 capacitor value should be confirmed on Fig 27a of the current datasheet.
- AFE4900 ball list should be checked against a complete TI datasheet. SBAS861B as published does not contain it.
- No USB-C ESD on VBUS beyond the USBLC6 VBUS pin, and no series ferrite. Add them if the bring-up needs them.
- Crystal stays DNP unless the internal AD5940 oscillator is not used.
