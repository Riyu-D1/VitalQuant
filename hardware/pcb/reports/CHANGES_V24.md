# CHANGES_V24 — BOM/flag revision over fix_v23

KiCad CLI 10.0.4. All LCSC/JLC stock and price figures read from the
published pages on 2026-10-25 (label: **checked**). *inferred* = reasoned
from related evidence; *guess* = unverified.

**No copper, track, via, zone, footprint-position or footprint-geometry
changes were made.** Only schematic fields (Value/MPN/LCSC/Manufacturer),
DNP + in_bom + exclude-from-position flags, matching PCB footprint
fields/attributes, exports and docs. Verified: 16/16 gerber layers,
drills, all 302 footprints' geometry and all 8898 board primitives are
identical to fix_v23 (`verify/COPPER_EQUIV.md`).

## D1 — U14 IMU: LSM6DSV80XTR -> LSM6DSV320XTR (C26633007)

| Ref | Field | Old | New |
|---|---|---|---|
| U14 | Value | LSM6DSV80XTR | LSM6DSV320XTR |
| U14 | MPN | LSM6DSV80XTR | LSM6DSV320XTR |
| U14 | LCSC | C46898862 | C26633007 |
| U14 | Footprint | vitalq:QFN_LSM6DSV80XTR_STM | **unchanged** |

Reason: 80X = 0 stock everywhere; 320X is the same LGA-14L 2.5x3x0.83 mm
package, same pin functions, same WHO_AM_I 0x73, same low-g/gyro
registers the firmware uses (firmware runs +/-8 g, high-g channel off —
see `_inputs/HW_FIT_CHECK.md`).
Footprint vs 320X package drawing (DS14623/TN0018): the board land was
drawn for the 80X's identical LGA-14L outline — pinout, pad pitch and
package dimensions are the same per HW_FIT_CHECK. Land-pattern equality
is **inferred** (datasheet drawings not re-measured from text); the
footprint itself was **not** edited. **checked** (page), **inferred**
(land).
Stock: LCSC 0 / JLC 0 on 25 Oct — see BUY_NOTES. Fallback LSM6DSV16XTR
(C5267406, LCSC stock 6393) documented but **not applied**: it needs a
one-line WHO_AM_I firmware change and loses the (unused) high-g channel.

## D2 — D10 white illuminator: revert to Nichia NF2W757G-F1, consigned

| Ref | Field | Old | New |
|---|---|---|---|
| D10 | Value | JB3030UWT-B-H50EA0000-1Z000001 | NF2W757G-F1 |
| D10 | MPN | (JB3030 Cree) | NF2W757G-F1 |
| D10 | Manufacturer | Cree/JSLED | Nichia |
| D10 | LCSC | C17610390 | **blank** |

Reason: the `vitalq:NF2W757G` land is drawn for the Nichia part; the Cree
JB3030 does not match it (HW_FIT_CHECK). C17610390 is also 0 stock at
both LCSC and JLC. Jufei/Cree alternatives were rejected — different land
pattern would risk a footprint change. Handling mirrors D12 exactly:
blank LCSC, in BOM_JLC (MPN column filled) and in CPL_JLC so JLC places
it from customer consigned stock or it is hand-fit. **checked**.
See `CONSIGNED_PARTS.md` and `BUY_NOTES.md`.

## D3 — J12 / U24 / U25: kept fitted, restored to exports

| Ref | Part | LCSC | Change |
|---|---|---|---|
| J12 | FH12-14S-0.5SH(55) FFC | C5378715 | stays fitted; now exported |
| U24 | RV-3028-C7 RTC | C2829066 | stays fitted; now exported |
| U25 | IM69D130 mic | C536262 | stays fitted; now exported |

v23's export step removed these by hand (`jlc_dnp_not_assembled`). Owner
decision: keep them fitted so exports are truthful. All three now appear
in BOM_JLC.csv and CPL_JLC.csv. Stock is thin/absent — all three are
listed "hand-fit or consign" in BUY_NOTES.md. **checked**.

## D4 — MX-ECG chain DNP + 51 kOhm corrections

Netlist proof (checked): `MX_ECG_INP` connects only D29.1, R118.2;
`MX_ECG_INM` only D30.1, R119.2; the other ends go to J13 (already DNP)
and GND. U22 = MAX86141 has no ECG inputs -> chain is dead, safe to DNP.

| Ref | Value | Footprint | Nets | Fitted/DNP | LCSC old -> new |
|---|---|---|---|---|---|
| D29 | TPD1E10B06 | TPD1E10B06_DPY | GND, MX_ECG_INP | **DNP** | C48260 (unchanged) |
| D30 | TPD1E10B06 | TPD1E10B06_DPY | GND, MX_ECG_INM | **DNP** | C48260 (unchanged) |
| J13 | MX ECG pads | Pads_1x02 | J13_INM/INP | DNP (was already) | — |
| R32 | 51k | R_2512 | ECG1_PAD, ECG_P | fitted | C18009 -> C30412 |
| R33 | 51k | R_2512 | ECG2_PAD, ECG_N | fitted | C18009 -> C30412 |
| R34 | 51k | R_2512 | RLD_CLAMP, RLD_PAD | fitted | C18009 -> C30412 |
| R35 | 51k | R_2512 | AFE_P_AC, AFE_P_PAD | fitted | C18009 -> C30412 |
| R36 | 51k | R_2512 | AFE_N_AC, AFE_N_PAD | fitted | C18009 -> C30412 |
| R76 | 51k | R_2512 | CE_SURGE, EDA_CE_PAD | fitted | C18009 -> C30412 |
| R77 | 51k | R_2512 | EDA_SE_PAD, SE_SURGE | fitted | C18009 -> C30412 |
| R78 | 51k | R_2512 | EDA_RE_PAD, RE_SURGE | fitted | C18009 -> C30412 |
| R79 | 51k | R_2512 | DE_SURGE, EDA_DE_PAD | fitted | C18009 -> C30412 |
| R80 | 51k | R_2512 | BIOZ_FP_PAD, FP_SURGE | fitted | C18009 -> C30412 |
| R81 | 51k | R_2512 | BIOZ_FN_PAD, FN_SURGE | fitted | C18009 -> C30412 |
| R82 | 51k | R_2512 | BIOZ_SP_PAD, SP_SURGE | fitted | C18009 -> C30412 |
| R83 | 51k | R_2512 | BIOZ_SN_PAD, SN_SURGE | fitted | C18009 -> C30412 |
| R118 | 51k | R_2512 | J13_INP, MX_ECG_INP | **DNP** | C18009 -> C30412 |
| R119 | 51k | R_2512 | J13_INM, MX_ECG_INM | **DNP** | C18009 -> C30412 |
| R120 | 51k | R_2512 | J11_WE, SWEAT_WE | fitted | C18009 -> C30412 |
| R121 | 51k | R_2512 | J11_RE, SWEAT_RE | fitted | C18009 -> C30412 |
| R122 | 51k | R_2512 | J11_CE, SWEAT_CE | fitted | C18009 -> C30412 |

All 18 verified individually: value `51k`, footprint `R_2512_6332Metric`,
nets unambiguous. C30412 = 25121WF5102T4E 51 kOhm 1% 1 W 2512 — correct
replacement for the bogus C18009 (4.3 kOhm 1206). **checked**.
DNP'd refs keep correct fields but drop out of BOM_JLC/CPL_JLC. LCSC
stock 0 / JLC 818 for C30412 — see BUY_NOTES.

## D5 — J2–J11 pad connectors excluded

| Ref | Value | Action |
|---|---|---|
| J2 | LiPo pads | exclude_from_bom + exclude_from_pos_files |
| J3 | speaker pads | same |
| J5, J6, J7 | electrode pad blocks | same |
| J9 | I2C tail pads | same |
| J10 | aux pads | same |
| J11 | sweat-electrode pads | same |

Bare `vitalq:Pads_*` lands; schematic `in_bom` unticked and board attrs
set, matching the existing J8 (TC2030) convention. No J4 exists; J1
(USB-C), J12 and J13 untouched; copper unchanged. **checked**.

## D6 — LCSC corrections and verified swaps

Every row verified against the LCSC page (MPN, value, package, tolerance,
voltage, stock, price) before applying — **checked** unless noted.

| Ref | Field | Old | New | Note |
|---|---|---|---|---|
| C21,C22,C23,C55,C68,C69,C70 | LCSC | C188347 (Y5V -20/+80%) | C47339 CL05A474KP5NNNC | X5R +/-10% 10 V 0402; all nets <=3.3 V (C21 VREF_2V5, C22 VBIAS_CAP, C23 DVDD_REG, C55 SE_ISO, C68-70 BIOZ *_ISO) |
| C35 | LCSC | C23691 (delisted) | C84705 CL05B472KB5NNNC | 4.7 nF X7R 0402 |
| C48,C49,C52,C53 | LCSC | C22299 (delisted) | C84703 CL05B222KB5NNNC | 2.2 nF X7R 0402 |
| C54,C67 | LCSC | C22314 (delisted) | C114405 CC0402KRX7R9BB153 | 15 nF X7R 0402 |
| C65 | LCSC | C1528 (160 pF!) | C23967 CL05B152KB5NNNC | 1.5 nF X7R 0402 |
| Q3 | MPN | "BC847BS,115" | BC847BS-7-F | MPN text only; LCSC C156292 unchanged |
| R3 | LCSC | C22879 (147 ohm 0603!) | C25867 0402WGF1501TCE | 1.5 kOhm 1% 0402, BQ25170 ISET |
| R12 | LCSC | C25513 (4x68 array!) | C25077 0402WGF100JTCE | 10 ohm 1% 0402 |
| R15,R16,R17 | LCSC | C25797 (6.81 Mohm!) | C26083 0402WGF1004TCE | 1 Mohm 1% 0402 |
| R18 | LCSC | C26015 (1.5k 0805!) | C25764 0402WGF2003TCE | 200 kOhm 1% 0402 |
| R23 | LCSC | C25840 (1.8k 0201!) | C132339 0402WGF5603TCE | 560 kOhm 1% 0402, TPS63802 FB; LCSC 0 / JLC 40 |
| R26–R29 | LCSC | C17413 (107 ohm 0805!) | C26082 0402WGF1005TCE | 10 Mohm 1% 0402 |
| R32..R36,R76–R83,R118–R122 | LCSC | C18009 (4.3k 1206!) | C30412 25121WF5102T4E | 51 kOhm 1% 1 W 2512 — see D4 |
| R43,R45 | LCSC | C3017614 (5.1M 5%) | C407678 0402WGF5114TCE | exact 5.11 Mohm 1% 0402 |
| R47,R109 | LCSC | C25084 (18 ohm!) | C25076 0402WGF1000TCE | 100 ohm 1% 0402 |
| R63 | LCSC | C853138 (22.1k 0.1%, JLC 4) | C43473 0402WGF2212TCE | 22.1 kOhm 1% 0402, CP2102N VBUS divider |
| R64 | LCSC | C25896 | C25896 (unchanged) | 47.5 kOhm; LCSC 0 / JLC 75 — flagged |
| R66 | LCSC | C6097400 (0 stock) | C102962 RTT023013FTH | 301 kOhm 1% 0402 |
| R75 | LCSC | C25779 (33k!) | C25771 0402WGF2702TCE | 27.0 kOhm 1% 0402, BQ25170 VSET |
| U8 | Value+LCSC | ADS1292RIRSMT / C882777 | ADS1292RIRSMR / C456281 | same die QFN-32; R = larger reel |
| U10 | LCSC | C2649486 AS7341-DLGM | unchanged | JLC stock thin (4) — flagged |
| U12 | Value+LCSC | MLX90632SLD-DCB-100-SP / C5349491 | MLX90632SLD-DCB-100-RE / C17235844 | same part, -100 = 1.8 V I2C variant; ADDR=GND -> 0x3A |
| U14 | see D1 | | | |
| U19 | LCSC | C2649390 TCA6408ARSVR | unchanged | JLC stock 2 — flagged |
| U22 | LCSC | C5328762 MAX86141ENP+ | unchanged | JLC stock 2 — flagged |
| U23 | LCSC | C5221601 SHT45-AD1B(-R2) | unchanged | LCSC page delisted; JLC stock 692 — flagged |
| U24 | see D3 | | | |
| U25 | see D3 | | | IM69D130V01; LCSC delisted, JLC 73 — flagged |
| U26 | LCSC | C1882550 SN74AXC2T245RSWR | unchanged | JLC stock 3 — flagged |
| U1 | LCSC | (missing on PCB) | C2913202 ESP32-S3-WROOM-1-N16R8 | PCB LCSC field added (already in schematic) |

D6 note: `R64` C25896 and the U19/U22/U26 rows were already correct;
they are listed only for the stock flags in BUY_NOTES.

### Rejected / needs owner

- **bio-econ audit M23** (set DNP on J12/U24/U25): **overridden by owner
  decision D3** — they stay fitted and in exports.
- **bio-econ audit M22 variant** (Jufei/Cree JB3030 alternative C17246500
  for D10): rejected — land pattern does not match `vitalq:NF2W757G`;
  owner chose consigned Nichia instead.
- **BOM_rebuilt_JLC.csv** retained C882777 (U8), C5349491 (U12),
  C17610390 (D10), C46898862 (U14): superseded by the stock_swaps/D1/D2
  proposals — the applied codes are the *proposed* ones, not the stale
  in-file ones.
- **FFC-connector alternates for J12** (LCSC-listed alternatives
  C19273929 / C262663 14-pos 0.5 mm): **not applied** — land-pattern
  equivalence to the Hirose FH12 land cannot be verified from page data.
  Listed in BUY_NOTES as unverified candidates; J12 kept as C5378715 and
  flagged hand-fit/consign.
- **U24 RV-3028-C7**: no second-source exists; flagged consign/hand-fit.
- **Q1/Q2/Q4 CSD13380F3T** (JLC 3 < 9 needed): TI alternates seen
  (CSD17585F5) are not verified drop-ins — flagged only.

### NOT DONE

- Nothing was stopped for copper/geometry reasons. Every requested change
  was achievable in fields/flags only.
- U14 fallback to LSM6DSV16XTR deliberately not implemented (documented
  only, per spec — it would need a firmware change).
- D7 firmware doc issue: logged, no code touched. `FIRMWARE_NOTES.md`
  (workspace) says AS7341/IMU/TMP117/PMIC share `I2C_1V8`; the board
  actually has only U10 and U12 on the 1.8 V island behind U9 — IMU is on
  the 3.3 V bus. The repo's `hardware/pcb/docs/
  FIRMWARE_V2_CTO_PROMPT.md` already records the same finding; no repo
  text fix was needed.

## Export diffs vs fix_v23 (assembly outputs)

- BOM_JLC: 79 rows both revs; content differs per D1/D2/D3/D4/D6.
- CPL_JLC: 247 rows (v23: 248). Net +3 (J12,U24,U25 restored) -4
  (D29,D30,R118,R119 DNP) ... = -1 net change; v23 also listed the 3
  jlc-DNP rows in CPL.csv but not CPL_JLC.
- `board_exclude_from_bom` now reports 45 refs (J2–J11 pads + FID/TP/H
  markers whose schematic in_bom flags were synced to the board — v23's
  board had not been field-synced). Truthful accounting, no copper effect.
