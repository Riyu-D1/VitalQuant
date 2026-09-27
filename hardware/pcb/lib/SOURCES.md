# Symbol and footprint sources

Project libraries live in this directory and are registered from
`hardware/pcb/sym-lib-table` and `hardware/pcb/fp-lib-table`.

| Nickname | Path | What it is |
| --- | --- | --- |
| `vitalq` | `lib/vitalq.kicad_sym`, `lib/vitalq.pretty` | Local symbols (passives, power, connectors, ESP32, regulators, CP2102, PCA9306, BME280) and the solder-pad footprints |
| `snap` | `lib/snap.kicad_sym`, `lib/snap.pretty`, `lib/snap.3d` | SnapMagic and Ultra Librarian CAD imported by `import_snap.py` |

`import_snap.py` is not part of CI. It copies the attached vendor archive,
repoints each footprint property to `snap:NAME`, points 3D models at
`${KIPRJMOD}/lib/snap.3d/`, and applies the two electrical fixes below.
`make_lib.py` still knows how to draw the older stand-in lands. Those files
remain in `vitalq.pretty` and are not placed on this board.

KiCad official libraries are the ones shipped with KiCad 9
(`/usr/share/kicad/symbols`, `/usr/share/kicad/footprints`).

## Vendor CAD

SnapMagic / Ultra Librarian, from the attached `vitalq_libs` archive
(`MANIFEST.md`: SnapMagic KiCad unless noted):

| Part | Symbol | Footprint | Notes |
| --- | --- | --- | --- |
| AFE4900YZR | `snap` `AFE4900YZR` | `snap:BGA30N40P5X6_260X210X50` | Ball names match the previous map. A1 is the library origin (bottom-left of the frame). The vendor 0.102 mm solder-mask margin bridged the 0.17 mm pad gap, so that margin was removed; mask expansion is the board setting (0). |
| AD5940BCBZ-RL7 | `snap` `AD5940BCBZ-RL7` | `snap:BGA56C40P8X7_416X356X55` | Not Y-flipped. DNC balls are left open. |
| ADS1292RIRSMT | `snap` `ADS1292RIRSMT` | `snap:QFN40P400X400X100-33N-D` | Was ADS1292RIPBSR (TQFP-32). TI SBAS502 numbers 1–32 are the same on the RSM VQFN. Pin 33 is the exposed pad and is tied to AVSS. |
| AS7341-DLGM | `snap` `AS7341-DLGM` | `snap:AS7341DLGT` | Replaces the KiCad OLGA land. |
| MLX90632SLD-DCB-100-SP | `snap` `MLX90632SLD-DCB-100-SP` | `snap:MLX90632SLDDCB100SP` | Was MLX90632SLD-DCB-000-RE. Pins are unchanged (1 SDA, 2 VDD, 3 GND, 4 SCL, 5 ADDR, 6 EP to GND). Ordering digit "1" is the 1.8 V I2C option; VDD stays 3.3 V. |
| TMP117AIDRVR | `snap` `TMP117AIDRVR` | `snap:SON65P200X200X80-7N` | DRV Table 5-1: 1 SCL, 2 GND, 3 ALERT, 4 ADD0, 5 V+, 6 SDA, 7 EP to GND. The old local symbol had ADD0 and ALERT swapped. |
| LSM6DSV80XTR | Ultra Librarian `LSM6DSV80XTR` | `snap:QFN_LSM6DSV80XTR_STM` | UL nominal QFN variant (LGA-14L). No STEP file was supplied. `lib/snap.3d/LSM6DSV80XTR.wrl` is a 3.0 × 2.5 × 0.83 mm body (3.0 mm along X, matching the land). KiCad reads VRML in units of 0.1 inch, so the file is written in those units and offset +0.415 mm in Z. Pins 6 and 7 were `power_out` in the UL file and are `power_in` so they do not fight the GND flag. |
| MCP73831T-2ACI/OT | `snap` `MCP73831T-2ACI_OT` | `snap:SOT95P280X145-5N` | Vendor file typed VBAT as `output`. Import sets it to `power_out` so it drives the XC6206. A courtyard rectangle was added around the pads; the copper is unchanged. |

## KiCad official footprints, checked against the datasheet

| Part | Symbol | Footprint | Check |
| --- | --- | --- | --- |
| XC6206P332MR-G | KiCad `Regulator_Linear:XC6206PxxxMR` (copied into `vitalq`) | KiCad `Package_TO_SOT_SMD:SOT-23-3` | Torex SOT-23: 1 GND, 2 VOUT, 3 VIN. https://www.torexsemi.com/file/xc6206/XC6206.pdf |
| TPS7A2018PDBVR | Local `vitalq:TPS7A2018PDBVR` | KiCad `Package_TO_SOT_SMD:SOT-23-5` | KiCad only ships the X2SON TPS7A20 symbol. DBV: 1 IN, 2 GND, 3 EN, 4 NC, 5 OUT. https://www.ti.com/lit/ds/symlink/tps7a20.pdf |
| CP2102N-A02-GQFN28R | KiCad `Interface_USB:CP2102N-Axx-xQFN28` | KiCad `Package_DFN_QFN:QFN-28-1EP_5x5mm_P0.5mm_EP3.35x3.35mm` | 5 × 5 mm QFN-28, exposed pad 3.35 mm. https://www.silabs.com/documents/public/data-sheets/cp2102n-datasheet.pdf |
| PCA9306DCUR | KiCad `Interface:PCA9306DC` | KiCad `Package_SO:VSSOP-8_2.3x2mm_P0.5mm` | Extra IC. VSSOP-8, 2.3 × 2 mm, 0.5 mm pitch. https://www.ti.com/lit/ds/symlink/pca9306.pdf |
| BME280 | KiCad `Sensor:BME280` | KiCad `Package_LGA:Bosch_LGA-8_2.5x2.5mm_P0.65mm_ClockwisePinNumbering` | Bosch LGA-8, 2.5 × 2.5 mm, clockwise numbering. https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf |
| ESP32-WROOM-32E-N8R2 | KiCad `RF_Module:ESP32-WROOM-32E-R2` | KiCad `RF_Module:ESP32-WROOM-32E` | Antenna is the local −Y end. Rotation 90 puts that end on the left board edge. https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf |

## Other footprints

| Part | Symbol | Footprint | Source |
| --- | --- | --- | --- |
| USB-C | KiCad `Connector:USB_C_Receptacle_USB2.0_16P` | KiCad `Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12` | KiCad. This is a connector, not a pin header. |
| LiPo, FSR, PPG, ECG, EDA | KiCad `Conn_01x02_Pin`, `Conn_01x06_Pin`, `Conn_01x03_Pin`, `Conn_01x04_Pin` | Local `vitalq:Pads_LiPo`, `Pads_FSR`, `Pads_PPG`, `Pads_ECG`, `Pads_EDA` | Flat SMD pads, 2.2 mm pitch, 1.15 × 1.70 mm. Silkscreen names the function (BAT+, IN+, CE, TX1, …). There is no official footprint for a labelled solder-pad array. |
| Passives | KiCad `Device:R` and `Device:C` | KiCad `R_0402_1005Metric`, `C_0402_1005Metric` | KiCad |
| TP1, TP2 | Local `TestPoint` | KiCad `TestPoint:TestPoint_Pad_D1.5mm` | KiCad footprint excludes the pad from the BOM. The symbol matches (`in_bom no`). |

No pin headers and no mounting holes are on the board.
