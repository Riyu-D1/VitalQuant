# Local symbol and footprint sources

These parts are not used from the stock KiCad libraries, or the stock symbol
disagrees with the manufacturer pinout. Official KiCad footprints are reused
where the land pattern matches.

| Part | Symbol | Footprint | Datasheet evidence |
| --- | --- | --- | --- |
| AFE4900YZR | local | local `vitalq:AFE4900YZR` | Ball functions from the full AFE4900 pin table (the public SBAS861B PDF is 8 pages and has no pin list). Land: SBAS861B p. 5, YZ0030-C01, 0.4 mm pitch, 0.23 mm NSMD, A1 top-left. ALDO_1V8 and DLDO_1V8 are internal, not balls. |
| AD5940BCBZ | local | local `vitalq:AD5940BCBZ` | AD5940/AD5941 Rev G pin-function table, package CB-56-3, 3.6 x 4.2 mm, 0.4 mm. Top view, ball side down, A1 top-left. Pad diameter 0.22 mm is an assumption. |
| ADS1292R | local | official `Package_QFP:TQFP-32_5x5mm_P0.5mm` | SBAS502C pp. 4-5 pinout. No ADS1292R symbol in KiCad 9. |
| LSM6DSV80X | local | local `vitalq:LSM6DSV80X` | DS14764 Rev 2 Table 2 pp. 9-11, Fig 5 p. 9 bottom view (pin 1 on the right), Fig 24 p. 158 terminals. TN0018 (p. 157) is the official land pattern and was not copied; the stock KiCad LGA-14 pin-1 corner does not match Fig 5. |
| MLX90632 | local | local `vitalq:MLX90632` | DOC 3901090632 Rev 13 Table 5 p. 8 (pin names) and Table 17 p. 46 (one row of 5 pins, 0.50 mm). Fig 25 bottom view: pins 1-5 left to right. No center pad. |
| TPS7A2033PDBVR | local | official `SOT-23-5` | SBVS338H pin functions p. 3 (1 IN, 2 GND, 3 EN, 4 N/C, 5 OUT). KiCad only has the X2SON symbol. |
| XC6206 | local | official `SOT-23` | Torex pin 1 VSS, pin 2 VIN, pin 3 VOUT. KiCad `XC6206PxxxMR` extends a symbol whose pin numbers are swapped. |
| TMP117 | local | official `WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm` | SNOSD82D Table 5-1 p. 4. Pin 7 added so the exposed pad is grounded. The stock symbol's footprint field points at SOT-563. |
