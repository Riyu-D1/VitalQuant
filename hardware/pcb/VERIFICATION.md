# VitalQ hw_v1 verification

Research prototype. This is not a medical device. Nothing here is a claim
about safety, sterility, biocompatibility, defibrillator withstand, or
regulatory clearance. IEC 60601-1 is not claimed. The 0.26% bio-impedance
accuracy figure does not apply with 51 kΩ leads.

Baseline for this round is commit 87e94a46 (4-wire chest respiration on
the AD5940, gas-discharge tubes already removed). KiCad 9.0.9
`kicad-cli`, schematic parity on.

The board is **not fully routed**. Do not send it to fabrication as a
finished layout.

## ERC and DRC

ERC, severity-all, 2026-09-27: **0 errors**. Warnings: `pin_to_pin` 11,
`lib_symbol_issues` 5, `lib_symbol_mismatch` 3.

DRC, severity-all, schematic parity, same date, after removing the GND
via that overlapped a J8 alignment hole:

| Severity | Type | Count |
| --- | --- | --- |
| error | unconnected_items | 433 |
| error | clearance | 44 |
| error | copper_edge_clearance | 9 |
| warning | via_dangling | 21 |
| warning | text_height | 15 |
| warning | text_thickness | 14 |
| warning | isolated_copper | 13 |
| warning | silk_overlap | 8 |
| warning | silk_edge_clearance | 3 |
| warning | footprint_symbol_mismatch | 28 |
| warning | lib_footprint_mismatch | 2 |
| warning | net_conflict | 2 |
| warning | track_dangling | 2 |
| warning | nonmirrored_text_on_back_layer | 1 |

No shorts, no solder-mask bridges, no starved thermals, no hole-clearance
errors, no hole-to-hole errors. `check.sh` still fails: it allows only
`unconnected_items` as an error and it requires silk overlap and silk
edge clearance to be zero. Those checks were not weakened.

The 44 clearance errors are the custom rule `HV electrode to other nets`
(1.500 mm). One measured example is 0.529 mm. Adjacent electrode pads
are closer than that rule on purpose: J5 gap 0.55 mm, J6 and J7 gaps
1.05 mm. The 1.5 mm rule is not clean.

The 9 copper-edge errors are autorouted copper against Edge.Cuts,
including the 1.0 mm slots: I2C_SDA_1V8 on B.Cu (five segments, actual
0.15–0.29 mm), RLD_CLAMP on In1.Cu (two segments), and TX_5V_RAW on
F.Cu (a track and a via).

## Routing that remains

433 unconnected items, 155 nets. Each count is one missing connection
on that net. Freerouting 2.1.0 ran 40 passes on a DSN with the pours
removed (pours in the DSN stall the router) and stopped at 549 unrouted
in its own score. The imported SES is about 105 wires (273 tracks and
vias on the board, including stitch and via-in-pad). A later maze
router shorted +3V3 to GND and AFE balls to ESP_IO0 and was discarded.
Plane vias that tried to tie the sense nets onto the load pours also
shorted and were discarded.

Why it stopped: 36.5 × 70 mm, 245 parts, electrode keep-outs, a 0.127 mm
clearance outside the BGA, and the autorouter stall. USB D+/D− were not
length-matched. The PPG guard and the symmetric high-voltage pairs were
not finished. SW_L1 and SW_L2 are still open, so the switcher loops are
not routed. Decoupling is not generally within 1 mm of the pin (C7 is
about 3.6 mm from ESP32 pin 2).

AFE4900 (U6) sits on the bottom under the ESP32. Through vias in those
inner balls short the module. Vias were removed at TX1 (24.2, 14.0),
PD2_INM (24.6, 13.2), +3V3_ANA (24.2, 13.6), and a GND via at
(24.2, 13.2). Those inner balls have no escape. 24 via-in-pad vias
remain (0.30 mm copper, 0.20 mm drill) on balls that do not hit the
module. W25Q exposed pad has no exposed vias.

| Net | Open items |
| --- | --- |
| GND | 66 |
| +3V3 | 46 |
| +3V3_ANA | 20 |
| I2C_SCL | 9 |
| I2C_SDA | 9 |
| VBUS | 9 |
| VBAT | 8 |
| VDD_CP2102 | 8 |
| +1V8 | 7 |
| ESP_EN | 7 |
| TX_5V | 7 |
| ESP_IO0 | 5 |
| SPI_MISO | 5 |
| SPI_MOSI | 5 |
| SPI_SCK | 5 |
| VBAT_SYS | 5 |
| ECG_N | 4 |
| ECG_P | 4 |
| IN1P | 4 |
| RLDOUT | 4 |
| USB_DM | 4 |
| USB_DP | 4 |
| +3V3_ESP | 3 |
| ADS1292_DRDY | 3 |
| AFE_P_AC | 3 |
| CHG_STAT | 3 |
| CS_AD5940 | 3 |
| CS_ADS1292 | 3 |
| CS_FLASH | 3 |
| ESP_RX | 3 |
| ESP_TX | 3 |
| EXP_INT | 3 |
| I2C_SCL_1V8 | 3 |
| I2C_SDA_1V8 | 3 |
| IN1N | 3 |
| RLDREF | 3 |
| +1V8_LDO | 2 |
| AD5940_RESET | 2 |
| ADS1292_PWDN | 2 |
| AFE4900_ADC_RDY | 2 |
| AFE4900_RESETZ | 2 |
| AFE_INM | 2 |
| AFE_INP | 2 |
| AFE_N_AC | 2 |
| CE_ISO | 2 |
| CHG_DIS | 2 |
| CHG_PG | 2 |
| CP_DTR | 2 |
| CP_RTS | 2 |
| CS_AFE4900 | 2 |
| FN_ISO | 2 |
| FP_ISO | 2 |
| GAUGE_ALRT | 2 |
| IN1P_AC | 2 |
| IOVDD | 2 |
| IR_GATE | 2 |
| RLDINV | 2 |
| SE_ISO | 2 |
| SP_ISO | 2 |
| TMP117_ALERT | 2 |
| TS | 2 |
| TX5_EN | 2 |
| VREF2 | 2 |
| AD5940_GPIO0 | 1 |
| ADS_GPIO1 | 1 |
| ADS_GPIO2 | 1 |
| AFE_BG | 1 |
| AFE_CLK | 1 |
| AFE_N_PAD | 1 |
| AFE_N_SER | 1 |
| AFE_P_PAD | 1 |
| AFE_P_SER | 1 |
| AIN4_LPF0 | 1 |
| AVDD_REG | 1 |
| BIOZ_FN | 1 |
| BIOZ_FN_PAD | 1 |
| BIOZ_FP | 1 |
| BIOZ_FP_PAD | 1 |
| BIOZ_SN | 1 |
| BIOZ_SN_PAD | 1 |
| BIOZ_SP | 1 |
| BIOZ_SP_PAD | 1 |
| CE0 | 1 |
| CE_SURGE | 1 |
| CHG_ISET | 1 |
| CHG_VSET | 1 |
| CP_RST | 1 |
| CP_RX | 1 |
| CP_TX | 1 |
| CP_VBUS | 1 |
| DE0 | 1 |
| DE_SURGE | 1 |
| DVDD_REG | 1 |
| ECG1_PAD | 1 |
| ECG2_PAD | 1 |
| EDA_CE_PAD | 1 |
| EDA_DE_PAD | 1 |
| EDA_RE_PAD | 1 |
| EDA_SE_PAD | 1 |
| FB_3V3 | 1 |
| FP_SURGE | 1 |
| FSR_ADC | 1 |
| IN1N_AC | 1 |
| IR_AN | 1 |
| IR_K | 1 |
| LDR | 1 |
| LED_A | 1 |
| LSM6_INT1 | 1 |
| MISO_AD | 1 |
| MISO_ADS | 1 |
| MISO_AFE | 1 |
| MISO_FL | 1 |
| PD2_INM | 1 |
| PD2_INP | 1 |
| PD_INM | 1 |
| PD_INP | 1 |
| PGA1N | 1 |
| PGA1P | 1 |
| PGA2N | 1 |
| PGA2P | 1 |
| Q3_B1 | 1 |
| Q3_B2 | 1 |
| RC0_0 | 1 |
| RC0_1 | 1 |
| RCAL0 | 1 |
| RCAL1 | 1 |
| RE0 | 1 |
| RESP_MODN | 1 |
| RESP_MODP | 1 |
| RE_SURGE | 1 |
| RLD_CLAMP | 1 |
| RLD_PAD | 1 |
| SE0 | 1 |
| SN_ISO | 1 |
| SN_SURGE | 1 |
| STATUS_LED | 1 |
| SW_L1 | 1 |
| SW_L2 | 1 |
| TX1 | 1 |
| TX2 | 1 |
| TX3 | 1 |
| TX4 | 1 |
| TX_5V_RAW | 1 |
| TX_SW | 1 |
| USB_CC1 | 1 |
| USB_CC2 | 1 |
| VBIAS0 | 1 |
| VBIAS_CAP | 1 |
| VBUS_DET | 1 |
| VCAP1 | 1 |
| VCAP2 | 1 |
| VREFP | 1 |
| VREF_1V82 | 1 |
| VREF_2V5 | 1 |
| VZERO0 | 1 |

## Review items

| Item | Status |
| --- | --- |
| A1 MAX17048 TDFN | Done. Official land, LCSC C2682616, MPN MAX17048G+T10. See pin-1. |
| A2 ADS1292 GPIO1/GPIO2 | Done. R100 and R101, 10 kΩ to GND, LCSC C25744. |
| A3 TMP117 paste | Done. U11 and U20 use `vitalq:TMP117_DRV_NOPASTE`. Pad 7 is F.Cu and F.Mask only. |
| A4 VBUS capacitor voltage | Done. C1 is 4.7 µF 25 V 0603 CL10A475KA8NQNC (C69335). C10 is 1 µF 25 V 0603; LCSC left blank. C63 is 100 nF 16 V and was left as-is. |
| A5 L1 vs U4 | Done. Schematic L1 at (300, 48). SW_L1 is L1.1 and U3.9. SW_L2 is L1.2 and U3.7. Neither net touches +3V3 or U4. Board L1 is at (27.55, 2.40) top; U4 is at (34.10, 13.95) top. |
| A6 Labels inside the frame | Done. ERC has 0 errors after the move. |
| A7 Electrode keep-out and 1.5 mm | Rule and keep-outs are in the board. The 1.5 mm clearance is **not met** (44 DRC errors). Adjacent electrode gaps stay 0.55 mm and 1.05 mm. Keep-out is on every copper layer except the pad's own layer. |
| A8 BOM LCSC and consign | Done. Columns include LCSC and Consign/Global sourcing. DNP excluded from the JLC BOM. |
| A9 Panel | **Not built.** No KiKit CLI. Order note below. |
| A10 Via-in-pad | Partial. 24 vias, 0.30 / 0.20 mm, ENIG, POFV intent. Inner AFE balls under the ESP32 have no through-via escape. W25Q EP has no exposed vias. BGA pads that take a via are 0.25 mm with local mask margin 0.025 mm. |
| B1 USBLC6 | Connected. U21 at (33.5, 31.5) top, about 9 mm from J1. J1's courtyard covers x 26.98–36.49, y 16.84–27.57 and overlaps the ESP32, so the array cannot sit on the connector. Pins 1 and 6 USB_DP, 3 and 4 USB_DM, 5 VBUS, 2 GND. LCSC C7519. USB_DP and USB_DM are still unrouted. |
| B2 C7 22 µF | Done electrically. C7 at (11.5, 33.25) top, about 3.6 mm from ESP32 pin 2. The 1 mm target is not met. C8 100 nF stays. LCSC C59461. |
| B3 SW1 | Done. EN to GND, (10.8, 68.0) top, TS-1088, LCSC C720477. |
| B4 SW2 | Done. IO0 to GND, (17.4, 68.0) top. |
| B5 GPIO17 LED | Done. R88 1 kΩ, D25 KT-0603G at (22.6, 68.0). Only IO16 is the PSRAM pin. |
| B6 CS pull-up | Done. R98 10 kΩ, (16.5, 64.2). |
| B7 DRDY pull-up | Done. R99 100 kΩ, (18.8, 64.2). |
| B8 MISO 0 Ω | Done electrically. R89–R92 are in the debug band at y = 61.5, not next to the drivers. LCSC C17168, MPN 0402WGF0000TCE. |
| B9 Current links | Done electrically. R93 VBAT to VBAT_SYS (0603, C21189) at (12.0, 63.0). R94 +3V3 to +3V3_ESP. R95 +3V3 to +3V3_ANA. R96 TX_5V_RAW to TX_5V. R97 +1V8_LDO to +1V8. Sense nets are not poured onto the load side. The nets themselves are not fully routed. |
| B10 Test pads | Done. TP1–TP27, 1.0 mm, no paste, grid from (8.0, 55.0). Individual 1.0 mm silk names do not fit on every pad. |
| B11 Tag-Connect | Done. J8 at (29.4, 67.5) top. Pin 1 +3V3, 2 GND, 3 TX, 4 RX, 5 EN, 6 IO0. Excluded from the JLC BOM and CPL. One GND stitch via that hit an alignment hole was removed. |
| B12 Fiducials | Done. Local `vitalq:Fiducial_1mm`, 1.0 mm copper, 2.0 mm mask, no paste. FID1 (35.0, 56.4) top, FID2 (8.2, 48.6) top, FID3 (35.0, 64.8) top, FID4 (34.6, 46.8) bottom, FID5 (22.0, 64.0) bottom, FID6 (14.5, 68.5) bottom. |
| B13 Mounting holes | Done. H1 NPTH 2.2 mm at (9.0, 62.5) bottom. H2 at (33.6, 61.5) bottom. Copper keep-out radius 2.1 mm. H1 is outside the antenna strip. |
| B14 Silk | Partial. Rev text and JLCJLCJLCJLC were placed only where they cleared pads. Pin-1 dots were skipped where they would overlap. J2 already has a "+" on the BAT+ side. BAT− and NTC silk stay 0.45 mm because 1.0 mm text hits the creepage slot. Silk warnings remain (8 overlap, 3 edge, 15 height, 14 thickness, 1 non-mirrored back text). |
| B15 Isolation slots | Partial. Creepage slots and the AS7341 barrier are 1.0 mm. The TMP117 moat was widened outward to 1.0 mm. The MLX left slot is only the short segment above R47; a full-height 1.0 mm slot does not fit between R47 and the MLX pads. Nine copper-edge errors remain. |

## Pin-1

MAX17048 official TDFN, measured in `TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm`
(Maxim package 21-0168): pad 1 (−0.9875, −0.75), pad 2 (−0.9875, −0.25),
pad 3 (−0.9875, 0.25), pad 4 (−0.9875, 0.75), pad 5 (0.9875, 0.75),
pad 6 (0.9875, 0.25), pad 7 (0.9875, −0.25), pad 8 (0.9875, −0.75),
pad 9 (0, 0) size 0.8 × 1.2 mm. Positive Y is up, so pin 1 is the bottom
of the left column, pins 1–4 go up that edge, and pins 5–8 go down the
right edge. Symbol pins: 1 CTG, 2 CELL, 3 VDD, 4 GND, 5 ALRT, 6 QSTRT,
7 SCL, 8 SDA, 9 EP. CTG, QSTRT, GND, and EP are GND. CELL and VDD are
VBAT. ALRT is GPIO25 with R74. SCL and SDA are the 3.3 V bus.

W25Q512 local copy of `WSON-8-1EP_8x6mm_P1.27mm_EP3.4x4.3mm`: pad 1
(−3.75, −1.905), pads 1–4 up the left edge, pad 5 (3.75, 1.905). EP
3.4 × 4.3 mm, copper and mask, no exposed vias. Pins 1 /CS, 2 DO,
3 /WP, 4 GND, 5 DI, 6 CLK, 7 /HOLD, 8 VCC. /WP and /HOLD are +3V3,
which matches QE = 1 on the IQ die. The review text calls pin 1
top-left on the datasheet drawing; this report uses the official KiCad
land and does not mirror it.

SFH 7072: pads 0.9 × 1.0 mm, centres x = ±0.6, ±1.8, ±3.0, y = ±1.25.
Top view pin 1 is top-right, then counter-clockwise. Symbol functions
match datasheet pin numbers. No centre slot. Silk dot outside pad 1.

Nichia NF2W757G-F1: pad 1 anode at x = −0.775, size 0.60 × 2.30 mm;
pad 2 cathode at x = 1.200, size 1.45 × 2.30 mm. This is the review
extraction. The Nichia PDF was not re-fetched. Do not treat the land
as verified.

## Accepted warnings

- Library silk and fab text under 1.0 mm or 0.15 mm thickness (text_height 15, text_thickness 14), including BAT− and NTC at 0.45 mm.
- Silk overlap 8 and silk edge clearance 3. Real, not hidden.
- One non-mirrored text on the back layer.
- 21 dangling vias and 2 dangling tracks from the partial route.
- 13 isolated copper islands from the partial route and the keep-outs.
- Footprint/symbol BOM-exclude mismatch on TP1–TP27 and J8 (28). They are excluded from the JLC BOM on purpose. H1 and H2 report `net_conflict` because the NPTH pad number is empty.
- U6 and U7 `lib_footprint_mismatch`: the board pads were resized to 0.25 mm for via-in-pad and no longer match the snap library copy.
- ERC `pin_to_pin`, `lib_symbol_issues`, and `lib_symbol_mismatch` warnings. No ERC errors.
- BGA via-in-pad: 0.30 mm vias on a 0.4 mm pitch cannot meet a 0.5 mm hole-to-hole rule or a 0.45 mm POFV-to-PTH spacing. The `bga_fanout` rule allows 0.09 mm clearance and 0.2 mm hole-to-hole inside that area. Default netclass clearance is 0.09 mm so the BGA exception is reachable; outside that area the custom rule requires 0.127 mm.
- C54 is 15 nF 50 V and C55 is 470 nF 10 V. With the gas tubes gone, a slow pulse can charge C54 toward the pad voltage. 50 V does not cover that.
- Murata NCU15XH103F6SRC (R61, DNP) is not the Semitec 103AT-2. Beta differs, so the TS window can shift.
- AFE4900 ball map is still the short-form mapping. The 8-page sheet has no ball map.
- MAX17048 address 0x36 was not re-read from the Maxim PDF this round.
- Breathing ΔZ versus the noise floor is a bench question, not a layout result.

## Fabrication notes

Do not order this revision as a finished board. The gerbers match the
partial route.

- Layers: 4. Stackup JLC04161H-7628, 1.6 mm, 1 oz outer, 0.5 oz inner.
- Finish: ENIG.
- Via-in-pad: POFV (filled and capped) on the 24 BGA escape vias. Do not add through vias under the ESP32.
- Assembly: standard PCBA, double-sided.
- Outline: 36.5 × 70.0 mm. Edge.Cuts includes 1.0 mm isolation slots.
- Panel: no panel file in this repo. Ask JLCPCB to panel 2×2 with 5 mm rails, mouse bites off the left (antenna) edge and off the electrode-pad edges, 2.0 mm NPTH tooling holes, and 1.0 mm fiducials 3.85 mm from the rail edges. The board is not ≥ 70 × 70 mm, so 2×2 is the requested array.
- CPL rotation: bottom parts use (180 − KiCad degrees) mod 360, then −90° for SOT-23, SOT-23-5, and SOT-23-6. Other footprints have no extra correction. BGAs, the WSON flash, and the USB-C receptacle need tape-orientation confirmation at order time. Y origin is the KiCad board origin (lower left, Y up).
- JLC BOM and CPL omit DNP (R61), fiducials, test pads, mounting holes, J8, and the bare pad groups J2, J3, J5, J6, J7.

Consigned or global-source parts (LCSC blank):

| Parts | Note |
| --- | --- |
| R32–R36, R76–R83 | DPCR2512-51KJT18 |
| D10 | NF2W757G-F1. Land not re-verified against the Nichia PDF |
| U12 | MLX90632SLD-DCB-100-SP, kept on the 1.8 V bus |
| U14 | LSM6DSV80XTR |
| U18 | W25Q512JVEIQ |
| R66 | 301 kΩ RC0402FR-07301KL, extended library, value unchanged |
| C10 | 1 µF 25 V 0603. Voltage change only. LCSC not confirmed |

Low stock that was left in place for a five-board lot: AD5940 C650308,
ADS1292R C882777, TCA6408A C2649390, SFH 7072 C2655172.

## Still open, and not a layout pass

Firmware files were not edited. `firmware/esp32/profiles/hw_v1.yaml`
and `config/hardware.example.yaml` still describe an ESP32-S3, I2C on
GPIO 8/9, SPI on GPIO 12/11/13, a MAX86141, an MLX90637 at 0x3B, and an
ICM-42670-P at 0x68. The board is the classic ESP32 map in `PINMAP.md`.
