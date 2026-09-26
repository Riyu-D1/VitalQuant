# Symbol and footprint sources

KiCad 9 official libraries are the ones installed with the `kicad` package
(`/usr/share/kicad/symbols`, `/usr/share/kicad/footprints`). Local symbols and
the four local footprints are produced by `make_lib.py` and live in this
directory. `make_lib.py` also reads an EasyEDA cache at `/tmp/cad/ee` for two
lands. That cache is not in the repository, so CI uses the committed library
and does not re-run `make_lib.py`.

A symbol is "local" when the official KiCad symbol is missing, has the wrong
package, or (for TMP117) swaps two pins. The land pattern is still the official
KiCad footprint whenever that land matches the package.

## Parts on the board

| Part | Symbol | Footprint | Source |
| --- | --- | --- | --- |
| ESP32-WROOM-32E-N8R2 | KiCad `RF_Module:ESP32-WROOM-32E-R2` | KiCad `RF_Module:ESP32-WROOM-32E` | https://gitlab.com/kicad/libraries/kicad-symbols and https://gitlab.com/kicad/libraries/kicad-footprints (module `RF_Module`). Espressif's module drawing is https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf |
| MCP73831T-2ACI/OT | KiCad `Battery_Management:MCP73831-2-OT` | KiCad `Package_TO_SOT_SMD:SOT-23-5` | KiCad libraries. Datasheet https://ww1.microchip.com/downloads/en/DeviceDoc/20001984g.pdf |
| XC6206P332MR-G | KiCad `Regulator_Linear:XC6206PxxxMR` | KiCad `Package_TO_SOT_SMD:SOT-23-3` | KiCad symbol matches the Torex SOT-23 pinout (1 GND, 2 VOUT, 3 VIN). Datasheet https://www.torexsemi.com/file/xc6206/XC6206.pdf |
| TPS7A2018PDBVR | Local symbol, TI DBV pinout | KiCad `Package_TO_SOT_SMD:SOT-23-5` | KiCad only ships the X2SON TPS7A20 symbol. Pins follow https://www.ti.com/lit/ds/symlink/tps7a20.pdf (SBVS338): 1 IN, 2 GND, 3 EN, 4 NC, 5 OUT |
| CP2102N-A02-GQFN28R | KiCad `Interface_USB:CP2102N-Axx-xQFN28` | KiCad `Package_DFN_QFN:QFN-28-1EP_5x5mm_P0.5mm_EP3.35x3.35mm` | KiCad libraries. Datasheet https://www.silabs.com/documents/public/data-sheets/cp2102n-datasheet.pdf |
| AFE4900YZR | Local symbol | Local `vitalq:AFE4900YZR` | **Hand-drawn land, last resort.** YZ0030-C01 from SBAS861B page 5 (0.4 mm pitch, 0.23 mm NSMD, A1 top-left). The public PDF has no ball-name table. https://www.ti.com/lit/ds/symlink/afe4900.pdf |
| AD5940BCBZ | Local symbol | Local `vitalq:AD5940BCBZ` | Ball names from AD5940/AD5941 Rev G, checked against the LCSC C650308 symbol. Land is that LCSC BGA with Y flipped so A1 is top-left. https://www.analog.com/media/en/technical-documentation/data-sheets/AD5940-5941.pdf . LCSC model https://www.lcsc.com/product-detail/C650308.html |
| ADS1292RIPBSR | Local symbol | KiCad `Package_QFP:TQFP-32_5x5mm_P0.5mm` | Pin numbers from SBAS502C pages 4-5. https://www.ti.com/lit/ds/symlink/ads1292r.pdf |
| PCA9306DCUR | KiCad `Interface:PCA9306DC` | KiCad `Package_SO:VSSOP-8_2.3x2mm_P0.5mm` | **Extra IC.** See the README. https://www.ti.com/lit/ds/symlink/pca9306.pdf |
| AS7341-DLGM | KiCad `Sensor_Optical:AS7341DLG` | KiCad `Package_LGA:AMS_OLGA-8_2x3.1mm_P0.8mm` | KiCad libraries. Datasheet https://look.ams-osram.com/m/24266a3e333a7d03/original/AS7341-DS000504.pdf |
| TMP117AIDRVR | Local symbol | KiCad `Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm` | DRV pinout from SNOSD82 Table 5-1. The KiCad TMP117 symbol and LCSC C699536 swap ADD0 and ALERT, so they were not used. https://www.ti.com/lit/ds/symlink/tmp117.pdf |
| MLX90632SLD-DCB-000-RE | Local symbol | Local `vitalq:MLX90632` | **Hand-drawn 5-pad land.** Pads at x = -1, -0.5, 0, 0.5, 1 mm, y = -1.35 mm, 0.40 x 0.55 mm, from the SLD land in Melexis DOC 3901090632. LCSC C7541116 adds a bogus pad 6. https://www.melexis.com/en/documents/documentation/datasheets/datasheet-mlx90632 |
| BME280 | KiCad `Sensor:BME280` | KiCad `Package_LGA:Bosch_LGA-8_2.5x2.5mm_P0.65mm_ClockwisePinNumbering` | KiCad libraries. https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf |
| LSM6DSV80XTR | Local symbol | Local `vitalq:LSM6DSV80X` | Symbol from DS14764 Table 2. Pins 10 and 11 are NC on this device. The land is the LGA-14 of the sibling LSM6DSV16X (LCSC C5267406), same 3.0 x 2.5 mm body. https://www.st.com/resource/en/datasheet/lsm6dsv80x.pdf |
| FSR402 header | KiCad `Connector:Conn_01x02_Pin` | KiCad `Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical` | KiCad libraries |
| USB-C | KiCad `Connector:USB_C_Receptacle_USB2.0_16P` | KiCad `Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12` | KiCad libraries. Pad numbers match the symbol |
| LiPo | KiCad `Connector:Conn_01x02_Pin` | KiCad `Connector_JST:JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal` | KiCad libraries. Pin 1 is positive |
| ECG, EDA, PPG headers | KiCad `Conn_01x03_Pin`, `Conn_01x04_Pin`, `Conn_01x06_Pin` | KiCad 1.27 mm horizontal pin headers | KiCad libraries |
| Passives | KiCad `Device:R` and `Device:C` | KiCad 0402 | KiCad libraries |
| Test points | Local `TestPoint` | KiCad `TestPoint:TestPoint_Pad_D1.5mm` | KiCad footprint |
| M2 holes | Local `MountingHole` | KiCad `MountingHole:MountingHole_2.2mm_M2` | KiCad footprint. The NPTH pad number is empty, so schematic parity reports "no pad for pin 1". The hole is still on the board |

## Downloads that were blocked

These were tried without a login and did not return CAD:

- SnapEDA / SnapMagic search API: https://www.snapeda.com/api/v1/parts/search returns "not logged in". Part pages such as https://www.snapeda.com/parts/TPS7A2018PDBVR/Texas+Instruments/view-part/ return 403.
- Component Search Engine / SamacSys: https://componentsearchengine.com/part-preview/TMP117AIDRVR/Texas%20Instruments redirects to a registration page. Same for the other TI and ADI parts.
- Ultra Librarian search (https://www.ultralibrarian.com/) returned 404 for the part lookups tried from this environment.
- GitHub code search for manufacturer KiCad repos returned 401.
- The Torex XC6206 PDF at https://www.torexsemi.com/file/xc6206/XC6206.pdf returned 403 from this environment. The KiCad symbol was checked against the published Torex SOT-23 pin order instead.

EasyEDA / LCSC JSON is reachable with a browser user agent and was used only as noted above. LCSC C2651595 is not an AFE4900 and was not used.
