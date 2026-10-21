# U14 — LSM6DSV80XTR (6-axis IMU + high-g, LGA-14) — Round 6 full audit

Datasheet: `verify/datasheets/lsm6dsv80x.pdf` — ST LSM6DSV80X, 180 pp (verified real).
Reference: DS §2 pin description, §3 abs-max (VDD 4.8 V, Vin ≤VDDIO+0.3), §5 app hints (100 nF/1 µF on VDD, CS→VDDIO for I2C).

## Pin-by-pin audit

| Pin | Pad | DS requirement | Actual net | Parts | Decap | Verdict |
|-----|-----|----------------|-----------|-------|-------|---------|
| 1 | SDO/SA0 | addr LSB: GND→0x6A | GND | — | — | OK — 0x6A unique |
| 2 | SDx | aux I2C data; tie GND unused | GND | — | — | OK |
| 3 | SCx | aux I2C clk; tie GND unused | GND | — | — | OK |
| 4 | INT1 | int out | IMU_INT1 | →U1 | — | OK — push-pull output |
| 5 | VDDIO | 1.62–3.6 V; decap | +3V3 | U14.5 | C84 1µ @1.60 mm; C76 100n @2.78 mm | OK |
| 6 | GND | gnd | GND | — | — | OK |
| 7 | GND | gnd | GND | — | — | OK |
| 8 | VDD | 1.71–3.6 V; 100 nF + bulk | +3V3 | U14.8 | C4 1µ @2.01 mm; C84 @2.76 mm | OK |
| 9 | INT2 | int out (unused OK) | NC | — | — | OK — output left floating is safe |
| 10 | NC | leave NC | NC | — | — | OK |
| 11 | NC | leave NC | NC | — | — | OK |
| 12 | CS | 1=I2C mode, VDDIO ref | +3V3 | U14.12 | C45 22µ @1.84 mm (rail bulk) | OK — tied to VDDIO net → I2C |
| 13 | SCL | I2C clk, ≤VDDIO+0.3 | I2C_SCL | R20 2.2k→+3V3 | — | OK |
| 14 | SDA | I2C data | I2C_SDA | R19 2.2k→+3V3 | — | OK |

## Absolute-max vs power states
VDD ≤4.8 V, logic pins ≤VDDIO+0.3=3.6 V; I2C 3.3 V ✓. All rails same net → no state exceeds. Battery-flat all off. Skin-fault n/a.

## Sequencing / passives
- VDD=VDDIO same rail; CS=3.3 V selects I2C before POR (power-up: CS must be high within tBD — tied high satisfies).
- C4/C84 1 µF + C45 22 µF bulk on +3V3 within 4 mm — exceeds DS app-hint. R19/R20 2.2k pull-ups std.

## Findings
None. 14/14 pads verified; NC pins correctly left unconnected per DS (they are internally test pins — grounding is also permitted but NC matches DS text).
