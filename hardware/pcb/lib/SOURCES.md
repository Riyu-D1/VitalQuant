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
| AFE4900YZR | `snap` `AFE4900YZR` | `snap:BGA30N40P5X6_260X210X50` | Ball names match the previous map. A1 is the library origin (bottom-left of the frame). The vendor 0.102 mm solder-mask margin bridged the 0.17 mm pad gap, so that margin was removed; mask expansion is the board setting (0). The vendor STEP did not show up in the board render, so the footprint uses a generic WCSP box, 2.10 × 2.60 × 0.50 mm, seated on z = 0 (`lib/vitalq.3d/AFE4900_WCSP.wrl`). That is the 5 × 6, 0.40 mm body, not an official STEP. |
| AD5940BCBZ-RL7 | `snap` `AD5940BCBZ-RL7` | `snap:BGA56C40P8X7_416X356X55` | Not Y-flipped. DNC balls are left open. STEP is already Z-up, so the model transform is identity. |
| ADS1292RIRSMT | `snap` `ADS1292RIRSMT` | `snap:QFN40P400X400X100-33N-D` | Was ADS1292RIPBSR (TQFP-32). TI SBAS502 numbers 1–32 are the same on the RSM VQFN. Pin 33 is the exposed pad and is tied to AVSS. KiCad 9 imports this STEP already Z-up, so the model rotation is identity and the body is the flat 4 × 4 × 1 mm QFN. C43 (1 µF, AVDD to GND) is the bulk capacitor SBAS502 §11.1 asks for next to the pin. |
| AS7341-DLGM | `snap` `AS7341-DLGM` | `snap:AS7341DLGT` | Replaces the KiCad OLGA land. STEP is Y-up: rotate X by −90, Z offset 0, so the 3.1 × 2.0 × 1.1 mm body sits on the copper. |
| MLX90632SLD-DCB-100-SP | `snap` `MLX90632SLD-DCB-100-SP` | `snap:MLX90632SLDDCB100SP` | Was MLX90632SLD-DCB-000-RE. Pins are unchanged (1 SDA, 2 VDD, 3 GND, 4 SCL, 5 ADDR, 6 EP to GND). Ordering digit "1" is the 1.8 V I2C option; VDD stays 3.3 V. |
| TMP117AIDRVR | `snap` `TMP117AIDRVR` | `snap:SON65P200X200X80-7N` | DRV Table 5-1: 1 SCL, 2 GND, 3 ALERT, 4 ADD0, 5 V+, 6 SDA, 7 EP to GND. The old local symbol had ADD0 and ALERT swapped. |
| LSM6DSV80XTR | Ultra Librarian `LSM6DSV80XTR` | `snap:QFN_LSM6DSV80XTR_STM` | UL nominal QFN variant (LGA-14L). No STEP file was supplied. `lib/snap.3d/LSM6DSV80XTR.wrl` is a 3.0 × 2.5 × 0.83 mm body (3.0 mm along X, matching the land). KiCad reads VRML in units of 0.1 inch, so the file is written in those units and offset +0.415 mm in Z. Pins 6 and 7 were `power_out` in the UL file and are `power_in` so they do not fight the GND flag. |
| MCP73831T-2ACI/OT | `snap` `MCP73831T-2ACI_OT` | `snap:SOT95P280X145-5N` | Vendor file typed VBAT as `output`. Import sets it to `power_out` so it drives the XC6206. A courtyard rectangle was added around the pads; the copper is unchanged. STEP is Y-up: rotate X by −90 and Z by 90 so the 2.9 mm length follows the SOT-23-5 pads. Z offset is 0. |

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
| USB-C | KiCad `Connector:USB_C_Receptacle_USB2.0_16P` | KiCad `Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12` | KiCad. This install has no `Connector_USB.3dshapes` package, so the board build points the model at `lib/vitalq.3d/USB_C_Receptacle_HRO_TYPE-C-31-M-12.step`, the same official STEP that ships with the KiCad royalblue demo. |
| LiPo, FSR, ECG, EDA | KiCad `Conn_01x02_Pin`, `Conn_01x05_Pin`, `Conn_01x04_Pin` | Local `vitalq:Pads_LiPo`, `Pads_FSR`, `Pads_ECG`, `Pads_EDA` | Flat SMD pads. ECG is five pads at 1.70 mm pitch. The 3D body is a 0.15 mm slab the size of the copper. PPG pads (`Pads_PPG`) are no longer placed. |
| Passives | KiCad `Device:R` and `Device:C` | KiCad `R_0402_1005Metric`, `C_0402_1005Metric`, and `C_0603_1608Metric` for C44, C45, C47 | KiCad |

No pin headers and no mounting holes are on the board.

## Chest-board parts

`lib/add_chest.py` adds these. Official KiCad footprints are used when the library has the land. Where it does not, the land is drawn from the package drawing and called out here.

| Part | Package | Footprint | 3D | Land source |
| --- | --- | --- | --- | --- |
| TPS63802DLAR | VSON-HR 10, 2.0 × 3.0 × 1.0 mm, the only package | KiCad `Package_SON:WSON-10-1EP_2x3mm_P0.5mm_EP0.84x2.4mm` | Official STEP | Same body as the TPS62177 land KiCad generated. Pads 0.25 × 0.75 mm vs the TI example 0.25 × 0.60 mm. Pin 11 is the exposed pad and is grounded. |
| TPS61240YFFR | DSBGA-6 YFF, 0.86 × 1.26 mm, 0.4 mm pitch. Smaller than the DRV WSON | `vitalq:TPS61240_YFF`, copied from KiCad `Texas_DSBGA-6_0.855x1.255mm_Layout2x3_P0.4mm_LevelC` | `TPS61240_YFF.wrl`, 0.86 × 1.26 × 0.50 mm box. No STEP in this KiCad install | Official KiCad YFF0006 land |
| TCA6408ARSVR | RSV UQFN-16, 1.8 × 2.6 mm, 0.4 mm pitch | `vitalq:TCA6408A_RSV`, copied from KiCad `UQFN-16_1.8x2.6mm_P0.4mm` | `TCA6408A_RSV.wrl`, 1.80 × 2.60 × 0.50 mm | Official KiCad land (TI qfnd836). The YZP DSBGA is smaller and has no KiCad symbol or land, so the RSV is the smallest official QFN |
| W25Q512JVEIQ | WSON-8 8 × 6 mm. This density has no 6 × 5 WSON | `vitalq:W25Q512_WSON8`, copied from KiCad `WSON-8-1EP_8x6mm_P1.27mm_EP3.4x4.3mm` | `W25Q512_WSON8.wrl`, 8.0 × 6.0 × 0.80 mm, X along the 8 mm fab axis | Official KiCad land. Pinout is the JV-family 8-pad WSON (1 ~CS, 2 DO, 3 ~WP, 4 GND, 5 DI, 6 CLK, 7 ~HOLD, 8 VCC, 9 EP) |
| MAX17048G+T10 | 8-bump WLP, about 0.86 × 1.61 × 0.40 mm | `vitalq:MAX17048_WLP` | `MAX17048_WLP.wrl` | Drawn here. The Maxim PDF did not download. Balls used: A1 CTG, A2 CELL, A3 VDD, A4 GND, B1 SDA, B2 SCL, B3 QSTRT, B4 ALRT. NSMD 0.25 mm, 0.4 mm pitch. Confirm against the full datasheet before fab |
| TPD1E10B06DPYR | DPY X1SON 1.0 × 0.6 mm | `vitalq:TPD1E10B06_DPY`, copied from KiCad `Texas_DPY0002A_0.6x1mm_P0.65mm` | `TPD1E10B06_DPY.wrl`, 1.00 × 0.60 × 0.40 mm | Official KiCad land. Matches the datasheet example pads 0.30 × 0.50 mm at ±0.35 mm |
| CSD13380F3 | PicoStar 0.69 × 0.60 mm | KiCad `Texas_PicoStar_DFN-3_0.69x0.60mm` | Official STEP | Official. Pins 1 G, 2 S, 3 D |
| SFH 7072 | 7.5 × 3.9 × 0.9 mm module | `vitalq:SFH7072` | `SFH7072.wrl`, 7.50 × 3.90 × 0.90 mm | Drawn from the v1.6 package outline (body 7.5 × 3.9) and a 2 × 6 land. Pad 0.55 × 0.70 mm. Centre gap holds an 0.8 mm edge-cut slot. The datasheet dimension callouts are vector art, so the pitch is reconstructed, not traced. Confirm pin 1 against the mark before fab. Window outline is on User.1, not on F.CrtYd |
| NF2W757G-F1 | 3030, 3.0 × 3.0 × 0.7 mm | `vitalq:NF2W757G` | `NF2W757G.wrl` | Drawn. Pads 1.10 × 2.10 mm at x = ±0.95 mm, pin 1 anode. The Nichia PDF was not fetched |
| SFH 4053 | 0402 LED, anode is the marked end | KiCad `LED_SMD:LED_0402_1005Metric` | Official STEP | Custom `LED_AK` symbol so pin 1 is the anode |
| DFE201612E | 2.0 × 1.6 × 1.2 mm | `vitalq:L_DFE201612E`, copied from KiCad `L_Murata_DFE201610P` | `L_DFE201612E.wrl` is 1.2 mm tall. The 201610 STEP is 1.0 mm, so it is not used | Official 2.0 × 1.6 land. Height follows the Toko/Murata 201612 body |
| Inductor and bulk caps | 0603 where the datasheet table is 0603 | KiCad `C_0603_1608Metric` | Official STEP | C44 10 µF, C45 22 µF, C47 4.7 µF. A 22 µF X5R will not fit a reliable 0402 |
