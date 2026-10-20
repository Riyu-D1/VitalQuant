# DATASHEET-VS-WIRING AUDIT — every component, fix_v21

Method: pin-level netlist (946 pin-net pairs w/ pinfunctions) vs each part's datasheet pin table,
supply ranges, required passives, strapping, abs-max. Datasheets: `verify/datasheets_v18_txt/`
(34 extracted), `datasheets_r5/`, `datasheets_v20/` (WROOM-1 + HW guidelines).

## ICs — 26/26 pin-verified, all PASS

| Part | Datasheet | Wiring verdict |
|---|---|---|
| U1 ESP32-S3-WROOM-1-N16R8 | ✓ cached | VDD=+3V3_ESP(3.3V, 3.0–3.6 range ✓); EN=R5 10k+C64 1µF (Espressif-recommended RC); IO0=R6 10k+Q3/SW2 boot circuit; USB=IO19/20 w/ 0Ω+USBLC6; strapping IO45/46/47/48 & octal-PSRAM IO33–37 all NC per N16R8; 34/34 pad→GPIO audit clean; EP=GND 3×3 via grid; antenna keepout clear |
| U5 CP2102N-A02 | ✓ cached | VREGIN=VBUS (internal-reg bus-power config); VDD=C9 10µF+C62 100n ✓; **CP_VBUS divider R63/R64=22.1k/47.5k → 3.41V — datasheet-prescribed divider** (VIH=VIO−0.6≈2.8V, abs-max VIO+2.5≈5.95V both satisfied); TX/RX crossed to ESP32 correctly; DTR/RTS→Q3 auto-reset; D+/D− via R104/105; unused inputs (DCD/RI/CTS/DSR) tied to VDD per datasheet; GPIO/CHR/SUSPEND NC-ok |
| U2 BQ25170 | ✓ cached | IN=VBUS, OUT=VBAT, ISET R3=1.5k→**200mA**, VSET R75=27k→**4.20V**, TS=pack NTC + R61 10k on-board + Q2 CHG_DIS path, STAT/PG OC→pull-ups, EP=GND ✓ |
| U3 TPS63802 | ✓ cached | VIN/VIN_EN=VBAT_SYS, VOUT=+3V3, FB=560k/100k→**3.30V**, MODE=GND(PFM+forced-PWM auto), L=L1 0.47µH on SW_L1/L2, EP=GND ✓ |
| U4 TPS7A2018 | ✓ cached | IN=+3V3, OUT=+1V8_LDO (fixed 1.8V), EN=+3V3, NC pad NC, GND, 1.5V headroom ≫ dropout ✓ |
| U15 TPS61240 | ✓ cached | VIN=VBAT_SYS, VOUT/FB=TX_5V_RAW (fixed 5V — FB direct ✓), L=L2 1.0µH, EN=TX5_EN (TCA6408 P3) ✓ |
| U17 MAX17048 | ✓ cached | CELL & VDD=VBAT, CTG=QSTRT=GND, ALRT→GAUGE_ALRT w/ pull-up, I²C 0x36, EP=GND ✓ |
| U8 ADS1292R | ✓ cached | DVDD/AVDD decoupled, CLKSEL=high(internal clk), CLK=GND, START=GND (SPI START — FW), CS=R98 PU, DRDY=R99 PU→U1.38, SPI pins correct, RLDOUT→R69/D3→RLD_CLAMP→R34(51k,2512)→RLD_PAD w/ RLDINV/RLDREF feedback loop, RESP_MODP/N, IN1/IN2 electrode chains via 51k+ESD ✓ |
| U6 AFE4900 | ✓ cached | RX_SUP/TX_SUP rails + decoupling, ECG INP/INM electrode path complete (R43/R45 5.11M bias to shared RLDOUT, own RLD_OUT unused by design), CLK=R10 1k PD → **internal-osc mode (FW must select)**, SPI + RDY→U1.23, RESETZ←TCA6408 P0, TX1-4→SFH7072 LED cathodes, INP/INM(+INP2/INM2)←PD anodes/cathodes ✓ |
| U7 AD5940 | ✓ cached | All datasheet decoupling present: VREF_1V82/VREF_2V5/AVDD_REG/DVDD_REG/VBIAS_CAP/VZERO0/RC0_0/RC0_1/AIN4_LPF0; RCAL=R13 1k; CS=R7 PU; RESET←TCA6408 P2; GPIO0→U1.22, **GPIO2 drives Q4 NIR_LED gate** (AFE-sequenced pulsing); electrode mux →EDA/BIOZ/sweat chains via 51k+ESD; XTAL pins NC (internal osc) ✓ |
| U22 MAX86141 | ✓ cached | VDD_ANA/VDD_LED supplies, VDD_DIG=+1V8; SPI inputs 3.3V on 1.8V core — **datasheet abs-max 6V on SCLK/SDI/CSB/INT, VIH 1.4V ✓**; SDO→U26 level-shifted; LED1-3_K→J10/J12 PD/LED tails; INT→R113 PU→U1.39... (to MAX86178_INT net→U1 IO39); CS=R117 PU+U26 OE gating ✓ |
| U11/U20 TMP117 | ✓ cached | V+=+3V3 (1.8–5.5V), ADD0=GND→0x48 / +3V3→0x49 **no collision**, U11 ALERT→TMP117_ALERT(R65 PU→U1.5), U20 ALERT spare NC ✓ |
| U12 MLX90632 | ✓ cached | VDD=+3V3, ADDR=GND, EP=GND; on 1V8 bus — **`-100` SKU = the 1V8-I²C variant** (VIH=0.7·VI2C) ✓ |
| U13 BME280 | ✓ cached | VDD=VDDIO=+3V3, CSB=+3V3(I²C mode), SDO=GND→0x76, SCK/SDI on bus ✓ |
| U23 SHT45 | ✓ cached | VDD=+3V3, VSS=GND, SDA/SCL — addr 0x44 unique ✓ |
| U14 LSM6DSV80X | ✓ cached | VDD(8)/VDDIO(5)=+3V3, **pin12(CS)=+3V3 → I²C mode selected** (CS-low=SPI; tied correctly), pin1 SDO/SA0=GND→0x6A, SDx/SCx aux pins GND per datasheet, INT1→U1.4, INT2/NC pins NC ✓ |
| U24 RV-3028-C7 | ✓ cached | VDD=+3V3, VSS=GND, EVI=GND, **VBACKUP=R115 10k→GND (unused-backup tie per datasheet)**, CLKOUT NC, INT→R112 PU+TP28 (note: does not reach U1 — feature gap, not a wiring error) |
| U25 IM69D130 | ✓ cached | VDD=+3V3, SELECT=GND(L-channel), CLOCK/DATA→IO40/41 PDM ✓ |
| U9 PCA9306 | ✓ cached | VREF1=+1V8, VREF2 & EN→R18 200k→+3V3 (TI-required EN network), SCL1/SDA1=1V8 side, SCL2/SDA2=3V3 side ✓ |
| U26 SN74AXC2T245 | ✓ cached | VCCA=1V8<VCCB=3V3 (required order), DIR1=high→A→B for MX_SDO, **OE=CS_MAX86178 (MISO shared-bus hygiene)**, DIR2=GND, unused B2=GND/A2 open ✓ |
| U19 TCA6408A | ✓ cached | VCCI=VCCP=+3V3, ADDR=GND→0x20, RESET=ESP_EN, INT→EXP_INT(R53 PU), P0–P7 = AFE_RESETZ/ADS_PWDN/AD5940_RESET/TX5_EN/IR_GATE/CHG_STAT(in)/VBUS_DET(in)/CHG_DIS ✓ |
| U10 AS7341 | ✓ cached | VDD=+1V8 (1.7–2.0V range ✓), PGND/GND, LDR=**controlled current-sink LED driver** (D10 direct-connect is the datasheet app — no series R), on 1V8 bus 0x39, GPIO/INT spare NC |
| U18 W25Q512JV | ✓ cached | VCC=+3V3 (2.7–3.6V), CS=R52 PU→CS_FLASH, DI=MOSI, DO→R106→MISO_FL, CLK=SCK, **/WP=+3V3 & /HOLD=+3V3 (writes enabled, not held)**, EP=GND ✓ |
| U21 USBLC6-2SC6 | ✓ cached | IO1(pins1,6)=USB_DP, IO2(3,4)=USB_DM, VBUS_5=VBUS, GND_2 — protects both data lines + VBUS rail ✓ |
| U16 SFH7072 | ✓ cached | Pad map vs module datasheet: PD1_C/A→PD_INP/INM→AFE4900 INP/INM; PD2→INP2/INM2; LED cathodes G1_C/G2_C/R_C/IR_C→TX1–TX4 sinks; anodes→TX_5V ✓ |

## Discrete semiconductors — all PASS

| Part | Datasheet | Verdict |
|---|---|---|
| D1–D9,D21–D30 TPD1E10B06 ×18 | ✓ cached | bidirectional ESD, each skin-land signal→GND ✓ |
| Q1/Q2/Q4 CSD13380F3 | ✓ cached | N-FET: Q1 G=IR_GATE,S=GND,D=IR_K; Q4 G=**NIR730_GATE (AD5940 GPIO2 + R110 100k PD)**,D=NIR_K; Q2 G=CHG_DIS,S=GND,D=TS pull-down ✓ Vgs max 8V ≫ 3.3V |
| Q6 AO3401A | ✓ cached | P-FET ideal-OR: G=VBUS,S=VBAT_SYS,D=VBAT — correct polarity both directions |
| Q3 BC847BS | ✓ cached | dual NPN auto-reset: C→EN/IO0, E→DTR/RTS, B via R70/R71 10k ✓ |
| D33 1N5819WS | ✗ not cached (generic 40V/1A Schottky) | A=VBUS,K=VBAT_SYS orientation correct; ratings adequate for ≤1A system |
| D11 SFH4053 | ✓ cached | IR emitter 860nm: A→IR_AN(100Ω→+3V3, ~17mA),K→Q1 ✓ |
| D25 KT-0603G | ✓ cached | green status LED: A→R88 150Ω→IO17 (~9mA) ✓ |
| D10 NF2W757G-F1 | ✗ **not cached** (Nichia white LED) | wired to AS7341's controlled LDR sink — topology matches datasheet app; verify If/Vf at bring-up |
| D12 XL0603NRC730 | ✗ **not cached** (red/NIR ~730nm LED) | A→NIR_AN(100Ω→+3V3 ~14mA),K→Q4 — safe generic drive; verify wavelength spec vs 730 label |

## Passives / mechanical

| Class | Verdict |
|---|---|
| 150 R/C/L parts | values all verified in §K math (dividers, ISET/VSET, pull-ups, decoupling, 51k barriers, sense/bias) |
| R61 NTC 10k B3380 | TS to GND — backup thermistor alongside pack NTC ✓ |
| L1 0.47µH / L2 1.0µH DFE201612 | within converter datasheet ranges; sat-current adequate for design loads |
| J1 USB-C | A/B-row symmetry verified (DP/DM pairs, CC1/CC2→R56/R57 5.1k, 4×VBUS, SBU NC) — reversible ✓ |
| J2–J13 connectors | pinouts all decoded vs function; J10↔J12 redundant tails consistent; J2 1=VBAT/2=GND/3=TS — **confirm vs physical battery pack** (not verifiable from files) |
| SW1/SW2 TS-1088 | EN/IO0→GND buttons ✓ (datasheet not cached — mechanical) |
| TPs ×28 | probe pads on all major nets ✓ |
| FID×6, H1/H2 M2 | mechanical ✓ |

## Datasheet availability
- **Cached (34+2)**: all 26 ICs, all ESD/FET/transistor parts, SFH7072, SFH4053, KT-0603G, W25Q512JV, USBLC6.
- **Not cached**: NF2W757G-F1 (D10 white LED), XL0603NRC730 (D12), 1N5819WS (D33), connector/button datasheets, R61 NTC. All are simple/generic parts whose wiring is verified correct by topology; mark D10/D12 electrical parameters for bring-up confirmation.

## Net defects found in this pass: **0**
New notes: (a) RTC_INT dead-end re-confirmed (feature gap); (b) D10/D12 LED params unverifiable without sheets — flagged; (c) J2 battery pin order must match the physical pack — unverifiable from files; (d) TCA6408 RESET=ESP_EN means expander outputs Hi-Z during ESP reset — FW must re-init after boot.
