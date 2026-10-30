# VitalQ fix_v23 – hardware fit check for BOM swap candidates

Read-only check, 25 Oct 2026. Nothing on the board, the repo or any order was changed.
Sources: board files from main dba96670 (`vitalq_v2.kicad_sch` / `.kicad_pcb`, parsed with Python because kicad-cli is not on the box), datasheets (LCSC mirror PDFs in `/workspace/ds/`), LCSC part pages, and the firmware on `feature/vitalq-firmware-v2` (PR #16, read only).
Labels: **checked** = I read it in the datasheet or board file. **inferred** = reasoned from checked facts. **guess** = not verified.

## Summary table

| Ref | Candidate | Verdict | Label |
|---|---|---|---|
| U14 | LSM6DSV320XTR C26633007 | Drop-in for footprint, pinout and firmware (same WHO_AM_I 0x73, same low-g registers). LCSC stock 0; bio econ saw JLC 40 | checked (pins, ID, regs); pad geometry inferred |
| U14 | LSM6DSV16XTR C5267406 | Drop-in for footprint and pinout. Firmware needs a **one-line change** (accept WHO_AM_I 0x70). No high-g channel, but firmware doesn't use it | checked |
| U14 | LSM6DSO32XTR C3663738 | Fits the footprint and pinout, **not firmware**: different register map, WHO_AM_I 0x6C, new driver needed | checked |
| D10 | JB3030AWT-P-U50 C17246500 | Polarity matches. **Not a clean drop-in**: the land is drawn for Nichia, not the Cree recommended land. Probably solderable | land: checked from drawing; solderability: inferred |
| U12 | MLX90632SLD-DCB-100-RE C17235844 | Same part, packing suffix only. Drop-in | checked (variant, VDD, address); "-SP" meaning inferred |
| U8 | ADS1292RIRSMR C456281 | Same part. R = 3000 pcs on 330 mm reel, T = 250 pcs on 180 mm reel (both are reels, not cut tape). Drop-in | checked (TI package addendum) |
| R66 | C102962 RTT023013FTH | 301 kΩ, 1%, 0402. Drop-in | checked |
| R63 | C43473 1% | Fine: CP2102N VBUS-sense divider on a DNP USB-UART path | checked (values); "unused path" inferred |
| C21–23, C55, C68–70 | C47339 X5R | Use X5R. Y5V C188347 is wrong for these nets | checked |
| R43/R45 | C407678 5.11 MΩ 1% | Matches the schematic value exactly. Use it, not C3017614 (5.1 MΩ 5%) | checked |
| R3 / R75 / R23 / 51 k 2512 group | C25867 / C25771 / C132339 / C30412 | All correct for value and footprint. C132339 and C30412 have 0 LCSC stock; JLC stock not confirmed | checked |

## U14 – IMU (board footprint `vitalq:QFN_LSM6DSV80XTR_STM`, LGA-14)

Board pad nets (checked):

| Pad | Net |
|---|---|
| 1 | GND (SA0, gives 0x6A) |
| 2, 3 | GND |
| 4 | LSM6_INT1 |
| 5 | +3V3 (VDDIO) |
| 6, 7 | GND |
| 8 | +3V3 (VDD) |
| 9 | NC (INT2) |
| 10, 11 | NC |
| 12 | +3V3 (CS high, so I²C) |
| 13 | I2C_SCL |
| 14 | I2C_SDA |

Pin tables, checked in each datasheet (DSV80X DS14764 r1, DSV320X DS14623 r1, DSV16X DS13510 r4, DSO32X DS13607 r1):
- Pins 1–9 and 12–14 have the same function on all four parts: SDO/SA0, SDx, SCx, INT1, VDDIO, GND, GND, VDD, INT2, CS, SCL, SDA.
- Pins 10/11: NC on the 80X; OCS_aux/SDO_aux on the 320X and 16X ("connect to VDDIO or leave unconnected"); NC on the DSO32X. Leaving them unconnected is allowed on all four.
- SDx/SCx tied to GND is allowed in mode 1 on all four.
- So the 0x6A strap (SA0 = GND) is the same on all four.

Package:
- All four are 2.5 × 3.0 mm LGA-14L with 0.5 mm pitch.
- 80X/320X: 0.83 mm thick, drawing 1021587.
- 16X/DSO32X: 0.86 mm thick, drawing 0249496.
- ST points all of them to the same land note, TN0018.
- Pad-for-pad fit is **inferred**. The package drawings are images, so I couldn't read their terminal dimensions as text.

WHO_AM_I (0x0F), checked:

| Part | WHO_AM_I |
|---|---|
| DSV80X | 0x73 |
| DSV320X | 0x73 |
| DSV16X | 0x70 |
| DSO32X | 0x6C |

Firmware (PR #16 branch, `vq_drv_imu.c`), checked:
- Uses only the low-g accel at ±8 g (FS_XL = 10b in CTRL8 0x17) and the gyro.
- 52 Hz comes from HAODR (CTRL1/CTRL2 = 0x15, HAODR_CFG 0x62 band 3). FIFO is used, wake-up threshold is weighted through INACTIVITY_DUR 0x54.
- Its header says "the ±80 g high-g channel is unused".
- It checks `WHO_AM_I_VAL 0x73` exactly.
- `hw_v2.yaml` asks for accel + gyro at 52 Hz with no high-g outputs.
- **Project need for 80 g: none found** in the firmware or profiles. BOM_V22's "±80 g" is only a part description. I found no FIRMWARE_NOTES file on main or on the PR #16 branch.

Registers:
- DSV80X vs DSV320X (checked): same register addresses, including the high-g ones (CTRL1_XL_HG 0x4E, CTRL2_XL_HG 0x4D, UI_OUT*_HG 0x34–0x39). The only difference is the FS_XL_HG codes: 80X 000/001/010 = 32/64/80 g; 320X 000–100 = 32/64/128/256/320 g. Firmware leaves high-g off, so this makes no difference.
- DSV16X vs DSV80X (checked): CTRL8 FS_XL[1:0] coding is the same (00 = ±2 g, 01 = ±4 g, 10 = ±8 g, 11 = ±16 g). HAODR_CFG 0x62, INACTIVITY_DUR 0x54 (WU_INACT_THS_W) and WAKE_UP_THS 0x5B sit at the same addresses.
  - **The firmware comment that the 16X "FS_XL encoding differs" is wrong** according to the datasheets.
  - The only blocker is the exact 0x73 ID check.
  - The 16X has no high-g channel, plus Qvar/analog-hub pins, which aren't used here.
  - Register-for-register equivalence of the HAODR ODR table and FIFO tags on the 16X is **inferred**. I checked the register addresses but not every bit table.
- DSO32X (checked): older LSM6DSO register map. ODR and FS live in CTRL1_XL 0x10, FS coding differs, there is no HAODR (52 Hz is a native ODR), and the ID is 0x6C. A new driver is needed.

**Recommendation:**
1. Use the 320X if JLC really has ≥ 2 + attrition; no firmware change is needed.
2. Otherwise use the 16X (much cheaper, ~£2.7 vs ~£10). Firmware must accept 0x70 as well as 0x73; that's a firmware decision and outside this task.
3. Avoid the DSO32X.

## D10 – AS7341 white illuminator

Function (checked):
- Anode on +3V3 (pad 1); cathode on net LDR, which goes to U10 pin 4 (AS7341 LDR current sink). It is the white light source for the AS7341 spectral and NIR readings.
- PINMAP says firmware must keep LED_DRIVE ≤ 40 mA; the AS7341 can sink up to 258 mA.

Mismatch (checked):
- Schematic Value = NF2W757G-F1.
- PCB Value and LCSC field = JB3030UWT-B-H50 (C17610390, 0 stock).
- FIX_V19 says the land was drawn for the Nichia land and that the Nichia part should be consigned.

Board land:
- Pad 1 (anode): 0.89 × 1.95 mm.
- Pad 2 (cathode/LDR): 1.28 × 2.40 mm.
- Gap 0.66 mm; total span 2.83 mm.

Cree JB3030 (datasheet p32, checked; C17246500 and C17610390 use the same package):
- Package terminals: cathode 1.428 × 2.2 mm (large), gap 0.69 mm, anode 0.48 mm.
- Recommended land: cathode 1.60 × 2.40 mm, gap 0.55 mm, anode 0.60 × 2.40 mm (notched).

Fit:
- **Polarity matches**: the large pad is the cathode on both, and the board's large pad is LDR.
- Fit is marginal (inferred). The Cree cathode terminal (1.428 mm) is wider than the board pad (1.28 mm), so about 0.15–0.25 mm overhangs into the gap. The anode pad is shorter than the terminal (1.95 vs 2.2 mm).
- It is probably solderable, with a self-alignment and tilt risk. There is no short to the anode pad: about 0.4 mm clearance, inferred.

Electrical (checked):
- Vf at 65 mA: JB3030AWT-P 2.81 V typ, 3.0 V max; UWT 2.70–2.79 V.
- DC current: 240 mA max.
- At ≤ 40 mA, Vf is lower still. That leaves roughly 0.4–0.7 V across the AS7341 LDR sink at 3.3 V.
- I did **not** confirm the AS7341 LDR minimum compliance voltage (AS7341 datasheet not read). The same question applies to the Nichia part.

Note on C17246500:
- Its full MPN is JB3030AWT-P-U50EA0000-N0000001, brand **Cree LED** (bio econ's sheet says "Jufei").
- It is 5000 K with a 90 CRI minimum ("U"). The board value is 80 CRI ("H"), so the spectrum is not identical. AS7341 white-reference calibration would need redoing (inferred).

**Recommendation, in order:**
1. Revert the BOM/PCB value to the Nichia NF2W757G-F1 and consign it. This matches the drawn land, and is what FIX_V19 decided.
2. If going with Cree, redraw the footprint to the Cree recommended land (1.60 / 0.55 / 0.60 × 2.40 mm).
3. Fitting C17246500 on the current land is possible but not recommended.

Whichever you pick, make the schematic and PCB values agree.

## U12 – MLX90632 (checked against Melexis datasheet Rev 13)

- SLD-**DCB-100**: medical-accuracy variant, 1.8 V I²C level. Option digit "1" = 1V8, "0" = 3V3.
- VDD is 3.0–3.6 V for every variant. The board has VDD = +3V3 (pad 2) and SDA/SCL on the I2C_SDA_1V8 / I2C_SCL_1V8 nets, which is correct. (The brief's "runs it on +1V8" is wrong: only the bus is at 1.8 V.)
- ADDR (pad 5) = GND gives 0x3A, the factory default. (hw_v1.yaml's 0x3B is wrong; hw_v2 is not affected.)
- The datasheet ordering table only lists "-RE" (reel). "-SP" is not defined there; I take it to be a small or partial pack of the same part (**inferred**).
- Verdict: -RE is a drop-in.

## U8 – ADS1292R

- TI package option addendum (checked): RIRSMR = 3000 pcs on a 330 mm reel; RIRSMT = 250 pcs on a 180 mm reel. Same VQFN-32 RSM package, same lead finish and MSL.
- So it is packaging only, and both are full reels rather than cut tape.
- The board footprint QFN40P400X400X100-33N is unchanged. Drop-in.

## Passives (board value and footprint checked; LCSC parameters checked)

| Ref | Board | Net / role | Proposed | Verdict |
|---|---|---|---|---|
| R66 | 301 k 0402 | +1V8 → GND (bleed/load on the 1V8 rail) | C102962 RTT023013FTH 301 k 1% 0402 | Drop-in |
| R63 | 22.1 k 0402 | VBUS → CP_VBUS. With R64 47.5 k, sets the CP2102N VBUS-sense level at about 3.4 V from 5 V | C43473 1% | Fine. 1% moves the node by < 0.05 V, far inside CP2102N VBUS-detect margin (inferred). The CP2102N USB path is DNP (CONNECTION_AUDIT) |
| C21 | 470 n 0402 | AD5940 VREF_2V5 decoupling | C47339 X5R ±10% 10 V | Use X5R |
| C22 | 470 n 0402 | VBIAS_CAP | C47339 | Use X5R |
| C23 | 470 n 0402 | DVDD_REG | C47339 | Use X5R |
| C55 | 470 n 0402 | Series isolation, SE0 (EDA/bio-Z) | C47339 | Use X5R |
| C68 | 470 n 0402 | Series isolation, BIOZ_FN | C47339 | Use X5R |
| C69 | 470 n 0402 | Series isolation, BIOZ_SP | C47339 | Use X5R |
| C70 | 470 n 0402 | Series isolation, BIOZ_SN | C47339 | Use X5R |
| R43/R45 | 5.11 M 0402 | RLDOUT → AFE_INP/INM (AFE4900 input bias) | C407678 5.11 M 1% | Exact match |
| R3 | 1.5 k 0402 | BQ25170 ISET | C25867 1.5 k 1% 0402 | Correct |
| R75 | 27.0 k 0402 | BQ25170 VSET | C25771 27 k 1% 0402 | Correct |
| R23 | 560 k 0402 | TPS63802 FB top (with R24 100 k) | C132339 560 k 1% 0402 | Correct; LCSC stock 0 |
| R32–36, R76–83, R118–122 | 51 k 2512 | Electrode HV-boundary series resistors | C30412 25121WF5102T4E 51 k 1% 1 W 2512 | Correct; LCSC stock 0 |
| R12 | 10 Ω 0402 | — | C25077 0402WGF100JTCE (bio econ extra item) | LCSC lists it as 10 Ω 1% 0402. Correct |

Notes:
- **Y5V C188347** (−20/+80 %, large DC-bias and temperature loss) is unsuitable on all seven 470 nF nets: reference decoupling and measurement-path coupling.
- **R43/R45:** don't use C3017614 (5.1 M, 5%, 200 ppm).

**Not confirmed:** JLC (as opposed to LCSC) stock for C132339 and C30412. Check this before ordering. If they are out, any 0402 560 k 1% and any 2512 51 k 1% (≥ 1 W, ≥ 200 V) thick-film part is a drop-in.

## Decisions list

1. **J12 / U24 / U25** (FH12-14S FFC, RV-3028 RTC, IM69D130 mic).
   - Now: the schematic and PCB both have dnp = no and in_bom = yes. They were removed from BOM_JLC/CPL by hand only. BOM_V22 lists all three as DNP.
   - To fix: set "Do not populate" on each schematic symbol, then update the PCB from the schematic so the footprints get the dnp attribute.
   - Effect: KiCad BOM/CPL exports and the fab scripts then drop them automatically, and the fab drawing marks them DNP.
   - Electrical effect, inferred from BOM_V22 roles:
     - No RTC means no absolute wall-clock time; the firmware's RV-3028 probe just reports it absent.
     - No mic channel.
     - The tails run through the J9/J10/J11 discrete pads instead of the FFC.
   - The parts stay drawn, so they can be fitted later.
2. **D29 / D30 / R118 / R119 (MX-ECG chain).**
   - Netlist (checked): J13 (DNP) → R118/R119 51 k → MX_ECG_INP/INM, with D29/D30 TVS to GND. Those two nets connect to nothing else. U22 is now a MAX86141, which has no ECG inputs.
   - So the chain is **electrically dead**. The schematic is the version that's out of date; BOM_V22 ("chain unpopulated") reflects the intent.
   - Recommend marking D29/D30/R118/R119 DNP: this saves 2× 2512 + 2× TVS per board and is harmless (inferred).
   - Also note that R118/R119 still carry the wrong LCSC C18009 in the schematic.
3. **J2–J11 bare land pads** (LiPo, FSR, ECG, EDA, bio-Z, I²C/optical tails, sweat site).
   - Now: in_bom = yes with no LCSC part, so they appear as empty BOM lines and JLC flags them.
   - Fix: untick "in BOM" (and "exclude from position files"), as was already done for J8 (TC2030). No electrical effect.
   - There is no J4 on the board.
4. **D10:** Nichia consigned on the existing land (recommended), or a Cree footprint redraw. Then make the schematic, PCB and BOM values agree.
5. **U14:** 320X (no firmware change; low stock) or 16X (WHO_AM_I accept 0x70 in firmware). The firmware's "16X FS_XL differs" comment is wrong and should be corrected whenever firmware is touched.

## Could not confirm

- LGA-14 terminal dimensions (the drawings are images) and ST TN0018. Pad-for-pad fit of the 16X/DSO32X versus the 80X is inferred from the identical 2.5 × 3 mm, 0.5 mm pitch package.
- AS7341 LDR sink compliance voltage.
- The Nichia NF2W757G-F1 land and Vf (Nichia datasheet not fetched).
- JLC live stock: I relied on bio econ's 25 Oct 2026 figures; my LCSC reads were the same day.
- The meaning of the Melexis "-SP" suffix.
- Every bit-field of the 16X HAODR/FIFO tables versus the 80X.
