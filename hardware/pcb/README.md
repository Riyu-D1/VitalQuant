# VitalQ hw_v1

Research prototype. This is not a medical device. Nothing here is a claim
about safety, sterility, biocompatibility, or regulatory clearance.
The defibrillator parts are not an IEC 60601-2-27 type-test claim.
The 51 kΩ pulse resistors are a research-prototype attempt to follow
the TI pulse guidance. They do not make this a defibrillator-proof
medical input. The gas-discharge tubes were removed.

The schematic is drawn explicitly. The PCB is a partial route on a
**36.5 × 70.0 mm** rectangle, **4 layers**, parts on both sides.
Six layers were not required. The stackup intent is JLCPCB
JLC04161H-7628, 1.6 mm, 1 oz outer and 0.5 oz inner, ENIG, with POFV
via-in-pad on the BGA balls that can escape. Routing is not finished.
`VERIFICATION.md` lists the nets that are still open.

Regenerate and check with `hardware/pcb/check.sh` (KiCad 10.0.4).

The firmware pin map is `PINMAP.md`. Every resistor and capacitor is
in `PASSIVES.md`. Orderable part numbers are in `vitalq_hw_v1_bom.csv`.
Footprint sources are in `lib/SOURCES.md`.

This branch also carries the `hw_v2` specification (`HW_V2_SPEC.md`).
The files in this directory still describe hw_v1 until the regenerated
schematic and board land; see the hw_v2 paragraph in the Board section
and `VERIFICATION.md` §hw_v2.

## What changed from the previous board

One part was replaced. The XC6206P332MR (U3) is now a TI TPS63802
buck-boost, 3.3 V, with the datasheet 0.47 µH inductor and the 10 µF /
22 µF 0603 capacitors. EN is tied to VBAT so the rail exists before the
I/O expander runs. MODE is grounded (power save). The exposed pad is GND.

J4, the off-board PPG pads, is gone. The ams OSRAM SFH 7072 is on the
board, on the skin side. Anodes go to the TPS61240 5 V rail (TX_5V).
Cathodes go to AFE4900 LED1–LED4. The two photodiodes go to two AFE4900
PD inputs. The package has the optical barrier. The footprint does not
cut a slot through the centre: the 0.9 mm pads at ±0.6 mm would overlap
a slot of 0.8 mm or wider. A window outline stays on User.1.

Everything else that was on the previous board is still here, including
R8 and R10. The list is at the bottom of this file.

## Board

The module antenna keep-out is x = 0 to 6.5 mm, every copper layer.
Pads start at x ≥ 6.55 mm. USB-C is rotated so the opening is the right
edge. The buck-boost, the 5 V boost, and the CP2102 sit on
the top, on the right, next to USB. The charger sits on the bottom,
next to the LiPo pads, away from the TMP117s. The flash, the IMU, the BME280, and
the expander are also on the top, below the module. Analog parts are on
the bottom, away from that switcher column.

Skin side, one cluster: SFH 7072, the
AS7341 with the white LED and the 860 nm LED about 5 mm from the
aperture, and the MLX90632. A 1.0 mm slot separates those LEDs from the
AS7341. J5 and J6 sit on the bottom and top edges of that cluster.
U11 is on a slotted island beside the LEDs. U20 is on the top at the
same XY, outside the module body. The island neck faces the cluster.

Above that cluster, five 2512 pulse resistors (R32–R36) are on the
bottom and four more (R76–R79) are on the top at the same X. The LiPo
pads and the charger are on the bottom, past that row. Four more 2512s
(R80–R83) are the chest bio-impedance leads, on the bottom past the
charger, because a turned 2512 does not fit in the gap that is already
there. There is no gas-discharge tube.

Electrodes are flat solder pads. No headers. J5 is five pads: ADS1292R
positive, ADS1292R negative, RLD, AFE4900 positive, AFE4900 negative.
J6 is the four EDA pads. The EDA cable is a flex tail to the shoulder.
The electrodes on that tail must land at least 5 cm from the ECG
electrodes. That distance is on the harness, not on this board.
J7 is four chest pads for the AD5940 4-wire respiration measurement
(F+, F−, S+, S−). They are not tied to J5 or J6.

The creepage slots between the 2512 electrode pads are 1.0 mm wide,
including the R35–R36 gap and the three gaps between R80–R83. On a
1.6 mm board the path down one face, across the slot, and up the other
face is 4.2 mm. The straight copper gap between adjacent 2512 pads is
still 3.10 mm: five 3.35 mm pads and four wider gaps do not fit in the
36.5 mm width once the antenna keep-out is reserved. J5 is still 0.55 mm
between pads, and J6 and J7 are still 1.05 mm. A custom rule asks for
1.5 mm from each high-voltage electrode net to unrelated copper. DRC
still reports that rule. Adjacent electrode pads are not 1.5 mm apart.

Placement was repacked after review: all 245 parts kept, none removed.
Every skin-facing sensor stays on the bottom in one cluster — the ECG
pads (J5), SFH 7072, MLX90632, AS7341, both LEDs, and the TMP117 pair
sit inside a ~16 × 16 mm window around the same skin spot. The 860 nm
LED (D11) was moved beside the white LED so both emitters light the
same aperture. Each IC's passives were pulled tight against it: the
AFE4900 supply/reference network (C16–C27, C42, R11–R13) wraps U7 on
the bottom, the ADS1292R input/RLD network (R14–R31, R67–R69,
C28–C35, C43, C48–C53, C65–C66) hugs U8, and the AD5940 loop plus the
isolated electrode path (C56–C57, C15, R43–R45, C54–C55, R39–R42,
D6–D9) sit at U6 and along the J6 corridor. U9's I2C pull-ups now
flank the expander, and the USB-UART series resistors moved beside the
connector row. Placement gates are clean: 0 courtyard clashes, 0
pad-to-slot/edge hits, 0 SMD-vs-through-hole hits, 0 missing pads.

The board is partly routed. Freerouting stalled, and a later maze pass
that shorted nets was discarded. `VERIFICATION.md` lists every remaining
unconnected net. Design rules are in `vitalq_hw_v1.kicad_pro` and
`vitalq_hw_v1.kicad_dru`: 0.09 mm is the BGA floor, 0.127 mm is the
clearance outside that area, copper-to-edge is 0.3 mm, and the mask web
is 0.1 mm. Reference designators are hidden. Fab reference text is 0.35 mm.

hw_v2 is specified on this branch (`HW_V2_SPEC.md`, authoritative) and is
**placed but unrouted**; everything above still describes hw_v1. In
short: U1 becomes an ESP32-S3-MINI-1U-N4R2 with a U.FL antenna — the
x < 6.5 mm keep-out strip goes away — and native USB on GPIO19/20 takes
over J1 through the fitted R102/R103 0 Ω straps, while the CP2102 console
stays on UART0 and can take the USB pair instead through the DNP R104/R105
straps (never fit both pairs). Adds: a MAX86178 WLP-49 synchronised ECG/PPG/BioZ
AFE that also drives a satellite-PPG tail (J10), an SHT45 skin RH + temp
sensor at 0x44 on the bottom face, a 730 nm LED (D12) paired with the
AS7341 NIR channel, a distal-temperature tail (J9, sensor at 0x4A), a
RESEARCH-GRADE sweat-electrode site (J11), a DNP unified 14-pos FFC tail
(J12), a DNP ECG pad pair for the MAX86178 (J13), a DNP RTC (0x52), a DNP
PDM mic, and a foam insulation dome over U20 for
the core-temperature trend channel (assembly note only). Nothing is
removed — the defib ladder, TP1–TP28, J8, and the whole power tree stay.
The placed outline is **40.0 × 62.0 mm** (291 parts): it grew 8 mm east
from the ~32 mm target to host the J11/J13 HV-boundary protection row
— J10 and J11 sit on the new east edge, and their 0 Ω cut-points
R118–R122 are 2512 HV-boundary parts like the defib ladder. `check.sh`
passes end-to-end (ERC 0, placement gates 0, 499 unconnected nets plus
9 waived intra-package clearances) but the board is **unrouted and not
fabrication-ready**. Full design doc:
`../../docs/10-hw-v2-upgrade.md`; BOM delta: `BOM_V2_ADDENDUM.md`.

## Part count

**245 parts.** U2 is a BQ25170DSGR in place of the MCP73831. Q3 is
the auto-program pair. D12–D20, the gas-discharge tubes, are gone.
The chest bioZ path adds J7, R80–R87, C67–C70, and D21–D24.
This prefab round adds U21, D25, SW1, SW2, J8, R88–R101, TP1–TP27,
FID1–FID6, H1, and H2.

| Group | Qty | Parts |
| --- | --- | --- |
| ICs | 21 | U1–U21. U2 is BQ25170DSGR. U21 is USBLC6-2SC6 |
| MOSFETs | 2 | Q1 and Q2, both CSD13380F3T |
| Transistor | 1 | Q3 BC847BS,115, SOT-363 |
| ESD diodes | 13 | D1–D9 and D21–D24, TPD1E10B06DPYR. U21 is the USB array, counted above |
| LEDs | 3 | D10 NF2W757G-F1, D11 SFH 4053, D25 KT-0603G |
| Inductors | 2 | L1 0.47 µH, L2 1.0 µH |
| Switches | 2 | SW1 EN, SW2 IO0, both TS-1088-AR02016 |
| Connectors and pads | 7 | J1 USB-C, J2 LiPo (3 pads), J3 FSR, J5 ECG, J6 EDA, J7 chest bioZ, J8 Tag-Connect |
| Resistors | 93 | 0402, two 0603 0 Ω links (R93, R94), and R32–R36, R76–R79, R80–R83 in 2512. R61 is DNP |
| Capacitors | 66 | 0402, plus the 0603 parts C1, C7, C10, C44, C45, C47 |
| Test, fiducials, holes | 35 | TP1–TP27, FID1–FID6, H1, H2 |

PWR_FLAG symbols are schematic-only and are not in this count.

## Rails

USB VBUS feeds the BQ25170 and the CP2102N VREGIN pin. The CP2102N
VBUS sense pin is the Fig 2.5 divider (R63 22.1 kΩ, R64 47.5 kΩ), not
the 5 V rail. There is no power-path diode or switch. The cell and the
charger share VBAT.
The TPS63802 makes 3.3 V from VBAT. Charging starts from USB with a
flat cell. The 3.3 V rail still needs the cell or a charged battery;
the board does not run the ESP32 from USB alone.

| Net | Source | Loads |
| --- | --- | --- |
| VBUS | USB-C | BQ25170 IN, CP2102N VREGIN, USBLC6, the VBUS divider |
| VBAT | BQ25170 OUT and J2 | MAX17048, then R93 (0 Ω) into VBAT_SYS |
| VBAT_SYS | R93 | TPS63802 and TPS61240 |
| +3V3 | TPS63802 | Digital loads, then R94 into +3V3_ESP and R95 into +3V3_ANA |
| +3V3_ESP | R94 | ESP32 module pin 2, C7, C8 |
| +3V3_ANA | R95 | AFE4900, AD5940, ADS1292R supplies and their local decoupling |
| TX_5V_RAW | TPS61240 | R96 (0 Ω) into TX_5V |
| TX_5V | R96 | AFE4900 TX_SUP and the SFH 7072 anodes. Enable is expander P3 |
| +1V8_LDO | TPS7A2018 | R97 (0 Ω) into +1V8 |
| +1V8 | R97 | AS7341 VDD, PCA9306 VREF1, MLX90632 SDA/SCL pull-ups |
| VDD_CP2102 | CP2102N internal regulator | CP2102N VDD bypass only. Not tied to +3V3 |

R93–R97 are current-sense links. Do not pour VBAT_SYS, +3V3_ESP,
+3V3_ANA, TX_5V_RAW, or +1V8_LDO onto the load-side copper. That would
bypass the 0 Ω parts.

U2 is a BQ25170DSGR, the replacement for the MCP73831. R3 is 1.5 kΩ
(200 mA). R75 is 27.0 kΩ (4.20 V). R61 stays DNP. The cell on J2 must
include a 10 kΩ NTC and a protection PCM. Charge is on when USB is
present and TS is inside the hardware window. P7 high (CHG_DIS) turns
Q2 on and shorts TS, which stops charge. STAT still goes to the
expander. /PG goes to GPIO27. There is no charge LED. Q1 and Q2 are
CSD13380F3T.

TPS61240 output is 4.9–5.1 V. AFE4900 TX_SUP must be 3.0–5.25 V, so
this sits inside that window. Green LED Vf max in the SFH 7072 v1.6
sheet is 2.8 V at 20 mA.

## I2C and SPI

Addresses, chip selects, and the expander port map are in `PINMAP.md`.

One PCA9306 (U9) translates the 1.8 V sensor bus. It is an extra IC
relative to the original sensor list: AS7341 VDD is 1.7–2.0 V, and an
ESP32 input is not guaranteed to see 1.8 V as a high.

## Assumptions that are not in a public full datasheet

- AFE4900 TX_SUP headroom and ECG common-mode range are not in the
  public short-form. The ECG bias follows TIDUDO6B Fig 2-10 (5.11 MΩ
  from the body-drive node, 100 kΩ series). AC coupling is kept, and
  the AFE's own RLD amplifier stays off, because ADS1292R already
  drives the RLD pad. See `PASSIVES.md`.
- AS7341 NIR is characterised at 940 nm. Response at 860 nm (SFH 4053)
  was not confirmed. LDR headroom with a white LED Vf near 2.9 V from
  a 3.3 V rail is tight and was not confirmed against the full AS7341 sheet.
- MAX17048G+T10 uses the official KiCad TDFN-8 land from Maxim 21-0168
  (`Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm`). Pad 1 is at
  (−0.9875, −0.75) mm, the bottom of the left column. The address 0x36
  was not re-read from the Maxim PDF in this round.
- The Nichia NF2W757 land is the review extraction: pad 1 anode
  0.60 × 2.30 mm at x = −0.775, pad 2 cathode 1.45 × 2.30 mm at
  x = 1.200. The STS-DA7-7098 text used for that extraction was garbled
  and was not re-fetched. Do not treat this land as verified against the PDF.
- W25Q512 8-pad pinout follows the JV-family WSON. The 512 Mbit PDF
  returned 404.
- SFH 7072 pin names match datasheet v1.6 page 19 (pin 1 is the broadband
  photodiode cathode). The land is pads 0.9 × 1.0 mm at x = ±0.6, ±1.8,
  ±3.0 mm and y = ±1.25 mm, pin 1 at the top-right in the top view,
  continuing counter-clockwise. The centre optical slot was removed.
- ADS1292R C34 is 47 nF. SBAS502C Fig 73 note (1): "When using the
  ADS1292R and the channel 1 respiration function, this capacitor must
  be 47 nF." The PGA section also says 4.7 nF is recommended; the
  figure note is the one that says "must" for respiration. C35 stays
  4.7 nF. The 51 kΩ surge resistors sit inside the respiration loop.
  The PGA therefore sees a baseline near 102 kΩ, outside the
  2000–10,000 Ω range in SBAS502C §6.5. Equation 10 only describes the
  modulation current. That earlier "1.2% and it still works" claim is
  withdrawn. That network is still on the board. It is not the
  breathing measurement. Chest breathing is the AD5940 4-wire path
  on J7 (AIN1, AIN0, AIN3, AIN2). See `PASSIVES.md`.
- C54 (15 nF, 50 V) sits behind R76. With the gas tube gone, a slow
  pulse can charge C54 toward the pad voltage; 50 V does not cover
  that. C55 (470 nF, 10 V) sits on the clamp side of the SE surge
  resistor. The TPD1E10B06 clamps at 10 V (1 A) and 14 V (5 A). The
  1 kΩ RLIMIT (R39) is unchanged.

## Firmware profile is out of date

`firmware/esp32/profiles/hw_v1.yaml` and `config/hardware.example.yaml`
were not edited. They still describe an ESP32-S3 board with different
GPIO, a MAX86141, an MLX90637, and an ICM-42670-P. This PCB is the
classic ESP32 map in `PINMAP.md`. GPIO39 is open (the NTC is the
BQ25170 TS pin). GPIO25 is the fuel-gauge alert. GPIO27 is charger
power-good. GPIO17 is the status LED. GPIO4 (CS_ADS1292) has a 10 kΩ
pull-up. Expander P7 is CHG_DIS, active high to stop charge.
DTR/RTS auto-program the ESP32. Firmware must cap the AFE4900 LED
current at 100–150 mA total, and cap the AS7341 LED_DRIVE at 40 mA
(the white LED absolute maximum is 100 mA and the register can reach
258 mA). Chest respiration is the existing AD5940 chip select.

## Sheets

1. Power and charging, including links to the other sheets
2. USB-UART
3. MCU and FSR
4. AFE4900 PPG, SFH 7072, and the 5 V boost
5. AD5940 EDA and chest respiration
6. ADS1292R ECG (channel-1 modulation left in place)
7. I2C sensors, the second TMP117, and the AS7341 LEDs
8. Debug: USB ESD, buttons, status LED, test points, Tag-Connect, fiducials, 0 Ω links

## Original parts still on the board

From the board this round started from. J4 was removed because the SFH 7072
replaces it. U3 is the same reference with a new device.

| Ref | Part |
| --- | --- |
| U1 | ESP32-WROOM-32E-N8R2 |
| U2 | BQ25170DSGR (replaces MCP73831T-2ACI/OT) |
| U3 | was XC6206P332MR, now TPS63802DLAR |
| U4 | TPS7A2018PDBVR |
| U5 | CP2102N-A02-GQFN28R |
| U6 | AFE4900YZR |
| U7 | AD5940BCBZ-RL7 |
| U8 | ADS1292RIRSMT |
| U9 | PCA9306DCUR |
| U10 | AS7341-DLGM |
| U11 | TMP117AIDRVR |
| U12 | MLX90632SLD-DCB-100-SP, 1.8 V I2C variant, VDD still 3.3 V |
| U13 | BME280 |
| U14 | LSM6DSV80XTR |
| J1 | USB-C |
| J2 | LiPo pads, now BAT+, BAT−, and NTC |
| J3 | FSR pads |
| J5 | ECG pads, now five pads |
| J6 | EDA pads |
| R1–R3, R5–R22 | previous resistors, including R8 and R10 |
| C1–C4, C6–C11, C13–C16, C18–C30, C32–C43 | previous capacitors |

R4, C5, C12, C17, and C31 were already absent. They were not put back.
TP1–TP27 on this board are the new 1.0 mm test pads, not a restoration
of the old missing TP1 and TP2.
