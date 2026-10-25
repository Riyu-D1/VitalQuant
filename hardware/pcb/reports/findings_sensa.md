# Findings — SENSOR group A (U10 AS7341, U11/U20 TMP117, U12 MLX90632, U13 BME280)

Revision: **fix_v19** @ `b9002188`. Sources: `reports/01_netlist_pinmap.csv`,
`audit_netlist.xml` (Eeschema export of `vitalquant_v2.kicad_sch`), `vitalquant_v2.kicad_pcb`,
`vitalquant.pretty/*.kicad_mod`, `fab/BOM_JLC.csv`, datasheets in `reports/datasheets/`.

## I2C bus architecture (as built)

Two physical segments joined into **one logical bus** by U9 `PCA9306DCUR`
(VREF1=+1V8 pin2, VREF2=+3V3 pin7, EN pin8 tied to VREF2 via R18 200k — standard
PCA9306 topology). Firmware `board_pins.h` agrees ("same logical bus", 100 kHz,
2.2k pulls).

| Segment | Pull-ups | Members |
|---|---|---|
| 3.3 V (`I2C_SDA`/`I2C_SCL`) | R19/R20 2.2k→+3V3 ✓ | U1 ESP32 (IO8/IO9 = pins 12/13 — master), U11, U13, U14, U17, U19, U20, U23, U24, J9 tail pads, J12 FFC, U9 side-2 |
| 1.8 V (`I2C_SDA_1V8`/`I2C_SCL_1V8`) | R21/R22 2.2k→+1V8 ✓ | U10, U12, U9 side-1 |

## Computed 7-bit address map (strap-derived)

| Addr | Device | Ref | Strap | Source |
|---|---|---|---|---|
| 0x20 | TCA6408A | U19 | ADDR(p16)=GND | TCA6408A DS: 010000+ADDR |
| 0x36 | MAX17048G+ | U17 | fixed | DS |
| 0x39 | AS7341 | U10 | fixed | DS000504 §9.1 |
| 0x3A | MLX90632 | U12 | ADDR(p5)=GND | DS Fig 11 / Table 6 SA=3A |
| 0x44 | SHT45-AD1B | U23 | fixed | DS |
| 0x48 | TMP117 | U11 | ADD0(p4)=GND → 1001000x | SNOSD82D Table 7-2 |
| 0x49 | TMP117 | U20 | ADD0(p4)=V+ → 1001001x | SNOSD82D Table 7-2 |
| 0x4A | (TMP117 tail) | J9 | reserved — off-board probe | firmware `VQ_ADDR_TMP117_TAIL` |
| 0x52 | RV-3028-C7 | U24 | fixed | DS |
| 0x6A | LSM6DSV80X | U14 | SDO/TA0(p1)=GND; CS(p12)=+3V3→I²C | ST DS14764 Table 2 |
| 0x76 | BME280 | U13 | SDO(p5)=GND → 1110110 | BST-BME280-DS001-23 §6.1 |

**No collisions** on either segment or on the merged logical bus. Firmware
`board_pins.h` matches every entry (U11=skin@0x48, U20=dome@0x49,
MLX90632 comment "strap ADDR=GND — hw_v1's 0x3B is wrong" → now correct).

---

## BLOCKER
none

## HIGH
none

## MEDIUM

### M1 — U12 MLX90632: EP land pattern deviates from vendor recommendation
Pad 6 (EP, net GND) is a single 2.5×2.1 mm Cu pad with **full-area B.Paste and
no via array**. Melexis datasheet Fig 26 (§13.2 "PCB footprint", p.47)
recommends a **0.25×0.25 mm windowpane stencil** over the EP and **8×Ø0.2 mm
PTH vias, tented or plugged**. Full-area paste on a 2.5×2.1 mm pad risks solder
float/skew and voiding on a 0.5 mm-pitch part; missing vias reduce thermal
anchor for a sensor whose die temperature is the measured quantity (Ta).
Recommend: windowpane the EP stencil and add the 8-via array in next rev; flag
to JLC for stencil review of the current rev.
Refs: `vitalquant.pretty/MLX90632SLDDCB100SP.kicad_mod` pad 6; DOC3901090632
Fig 26 + Table 17 (D2=2.5, E2=2.1).

### M2 — U10 AS7341: LDR flash-LED headroom is Vf-marginal at the 92 mA setpoint
Pin 4 LDR (current sink, abs-max 3.6 V) drives D10 = Nichia NF2W757G-F1 white
LED whose anode is **+3V3**. Firmware `as7341.c` sets LED_DRIVE code 44 →
I_LED ≈ 4+2·44 = **92 mA**. At that current the NF2W757G Vf ≈ 2.9 V typ,
**rank max 3.3 V @65 mA** (Nichia STS-DA1-5056, §Electrical + Ranks) — and Vf
only rises with current, so at 92 mA expect ≈3.0–3.5 V ⇒ V_LDR ≈ −0.2 to +0.3 V.
The internal sink needs positive compliance; at ~0 V the LED on a high-Vf bin
delivers far under the programmed current (the sink saturates; the loop can't
pull more voltage than the rail gives it). Abs-max is safe (3.3 < 3.6 V), and
92 mA is under the LED's 100 mA abs-max / is pulsed ~25 % duty — no damage
path — but the dark-vs-illuminated CLEAR self-test could see a weaker delta.
Recommend: verify bench flash current/brightness, or give the anode more
headroom (e.g., TX_5V with a series R, or a lower-Vf bin LED).
Refs: DS000504 §4 (VANA_MAX 3.6 V), §10 reg 0x74 (4–258 mA); NF2W757G
datasheet Vf 2.9 typ / 2.4–3.3 rank; `vq_drv_island/src/as7341.c` LED_CODE=44;
net `LDR` = {D10.2, U10.4}, D10.1 = +3V3.

## COSMETIC

### C1 — U11/U20 TMP117 footprint vs TI DRV0006B example
Footprint pads 0.6×0.35 mm on columns ±1.05 mm (2.1 mm) vs TI example
0.45×0.30 mm at 1.95 mm (SNOSD82D §12, DRV0006B land pattern, p.40).
The **inner pad edge (0.75 mm) is preserved**; the deviation is +0.15 mm toe
and +0.05 mm width — benign for reflow/hand rework. EP Cu 1.1×1.7 mm vs
package paddle 1.3×1.6 mm; deliberately **no paste** on pad 7 — matches TI
§10.1 ("do not solder down the thermal pad" for best accuracy; if soldered it
must float or be grounded — net is GND, so both cases compliant). Pitch
0.65 mm ✓, pad count 6+EP ✓.

### C2 — U10 AS7341 GPIO pin 6 floating
GPIO is a digital input (DI). Left unconnected; safe because GPIO_IN_EN
(reg 0xBE bit2) defaults to 0 = input buffer disabled (datasheet: "If set,
the GPIO pin accepts a non-floating input"). Tie to GND if a future rev wants
belt-and-suspenders hygiene. INT pin 7 NC is fine (open-drain output, only
needs a 1.8 V pull-up when used — §3.2).

### C3 — Ordering-code suffix: BOM "…-100-SP" vs JLC "…-100-RE"
`fab/BOM_JLC.csv` quotes part MLX90632SLD-DCB-100-SP but LCSC line says
"-RE". Datasheet Table 1 lists only "RE" packing. Silicon is identical;
paperwork tidy-up only. (The **-1**00 I²C-level digit is what matters — see V4.)

### C4 — TMP117 ALERT pull-up 10k vs 5k example
U11 ALERT → `TMP117_ALERT` w/ R65 10k→+3V3. Datasheet §8.2.1 recommends 5 kΩ
"can be lower or higher" — fine. U20 ALERT left floating — explicitly legal
("can be left floating or connected to ground when not in use").

## VERIFIED / informational

- **V1 — Supply domains all legal**: U10 VDD=+1V8 ∈ 1.7–2.0 V (DS §5); U11/U20
  V+=+3V3 ∈ 1.7–5.5 V (§6.3); U12 VDD=+3V3 ∈ 3.0–3.6 V (Table 6); U13
  VDD/VDDIO=+3V3 ∈ 1.71–3.6 / 1.2–3.6 V (§1). +1V8 rail = TPS7A2018 fixed 1.8 V
  via R97 0Ω.
- **V2 — Pull-ups on correct rail side**: 3V3 bus R19/R20→+3V3; 1V8 bus
  R21/R22→+1V8. No device sees pull-ups above/below its interface rail.
- **V3 — Strap arithmetic**: U11 ADD0=GND→0x48; U20 ADD0=V+→0x49 (both static
  straps — the SDA/SCL strap options were *not* used; those are legal per
  Table 7-2 but would make ADD0 ride a driven pin). U12 ADDR=GND→0x3A. U13
  SDO=GND→0x76 **and CSB tied to VDDIO (+3V3) — I²C mode selected, not
  floating** (§6.1/Fig 17: "A direct connection between CSB and VDDIO is
  required" ✓; "SDO cannot be left floating" ✓).
- **V4 — U12 variant match**: BOM -DCB-**1**00 ⇒ option digit "1" = **1V8 I²C**
  (Table 2 legend) ⇒ correct for the 1.8 V segment (VI2C 1.65–1.95 V; VIH
  0.7·VI2C = 1.26 V < 1.8 V). **Procurement note:** if JLC ever substitutes
  -DCB-**0**00 (3V3-I²C, VIH ≥ 2.31 V), the 1.8 V bus can never reach VIH —
  dead part. Keep the -100 part locked.
- **V5 — Protocol quirks** (task checklist): AS7341 uses **standard** register
  write (START + chip-addr + reg-addr + data — §9.2), not the AS726x virtual
  register scheme ✓. MLX90632 frames **16-bit BE register address then 16-bit
  BE data** (Fig 12/14) and needs HALT/STEP before EEPROM ops (§9.1) ✓.
  BME280 CSB/SDO strap rules per §6.1 ✓ (see V3). TMP117 ALERT is open-drain —
  wired/left-floating legally ✓.
- **V6 — Footprint geometry**: AS7341 OLGA-8 pads are a **verbatim match** to
  DS Fig 93 (8 pads 0.575×0.488 ±0.05, cols ±0.638, 0.8 mm pitch — OCR-verified
  from DS000504 p.60). BME280 = KiCad `Bosch_LGA-8_2.5x2.5mm_P0.65mm_
  ClockwisePinNumbering`; dims consistent with Fig 21 (pad h 0.5, inner edge
  0.775, span 2.50). MLX90632 pads match Fig 26 (0.5 mm pitch; EP sized to
  D2/E2) except the via/stencil detail in M1.
- **V7 — Decoupling**: C36 100 nF 1.66 mm from U10 (+1V8); C61 100 nF 0.54 mm
  from U12 (DS wants C1 <10 mm ✓); C40 100 nF 2.85 mm from U13 (DS C1/C2 100 nF
  ✓); C41 100 nF ~4.2 mm from U11/U20 (DS: "as close as possible" — OK).
- **V8 — ESP32 strap compatibility**: I2C_SCL is on IO9 (ESP32-S3-MINI-1 pin
  13, a boot-strapping pin) — R20 2.2k holds it high → normal flash boot.
- **V9 — J9/J12 external I2C**: J9 "I2C tail pads" (+3V3/SDA/SCL/GND) is where
  the optional TMP117 tail @0x4A lives; J12 FFC also exports the bus — external
  devices must not reuse the on-board addresses listed above.

## UNVERIFIABLE
- MLX90632 Fig 26 exact stencil-aperture percentages (raster figure; only
  0.25×0.25 windowpane note legible) — but M1 stands on the missing vias +
  full-paste alone.
- NF2W757G-F1 Vf at the actual 92 mA drive point (datasheet bins given at
  65 mA; Vf-vs-If curve is graphical) — M2's headroom number is therefore an
  estimate band (−0.2…+0.3 V), not a datasheet limit.
- AS7341 DS reached via ams-osram.com mirror `look.ams-osram.com/…/AS7341-
  DS000504.pdf` (v3-00, 2020-Jun-25); hash recorded in manifest_sensa.csv.
