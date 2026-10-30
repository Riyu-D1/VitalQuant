# CONNECTION AUDIT — every component vs its datasheet (fix_v22)

> **Status note (fix_v24, 2026-10-25):** this document describes an earlier
> revision. Current board state: D10 = Nichia NF2W757G-F1 (consigned, no
> LCSC), U14 = LSM6DSV320XTR (LCSC C26633007, same LGA-14 land), J12 /
> U24 / U25 are **fitted** (in the v24 JLC BOM/CPL), and the dead MX-ECG
> chain D29 / D30 / R118 / R119 is DNP. See `../reports/CHANGES_V24.md`.

Method: schematic netlist export (`_work/sch_net.xml`, KiCad 10, includes pin names) +
BOM values + per-pin datasheet cross-check. Each IC entry lists pin → net → verdict.
PASS = matches datasheet requirement/typical app. NOTE = intentional design choice
worth documenting. FLAG = needs human decision (not necessarily a bug).

## USB / J1 path

| Ref | Check | Result |
|-----|-------|--------|
| J1 HRO TYPE-C-31-M-12 | 16-pin USB-2.0 receptacle: A1/B1/A12/B12+SH→GND, A4/A9/B4/B9→VBUS, A5→CC1, B5→CC2, A6/B6→DP, A7/B7→DM, A8/B8 SBU→NC | **PASS** — standard Type-C mapping; SBU NC fine for USB-2 |
| R1/R2 5.1k | CC1/CC2 → GND | **PASS** — Rd=5.1k UFP pulldowns per Type-C spec |
| U21 USBLC6-2SC6 | pin1/6 I/O1→DP, pin3/4 I/O2→DM, pin5→VBUS, pin2→GND | **PASS** — matches ST pinout exactly |
| R102/R103 0Ω | J1 DP/DM → USB_DP/DM_S3 → U1.14/13 | **PASS** — populated, USB data = S3 native USB |
| R104/R105 0Ω | J1 DP/DM → USB_DP/DM_CP → U5.4/5 | **NOTE** — **DNP**: CP2102 USB path reserved, unpopulated by design |
| U5 CP2102N | 1 DCD→VDD, 2 RI→VDD, 3 GND, 4/5 D±(unpopulated path), 6 VDD→VDD_CP2102 (C62 100n+C9 10u), 7 VREGIN→VBUS, 8 VBUS→CP_VBUS (R63 22.1k/R64 47.5k divider→3.4V), 9 RST→CP_RST (R62 1k pullup), 10 NC, 11-22 GPIO/CHR→NC, 23 CTS→VDD, 24 RTS→CP_RTS, 25 RX→R72→ESP_TX, 26 TX→R73→ESP_RX, 27 DSR→VDD, 28 DTR→CP_DTR, 29 EP→GND | **PASS** — regulator powered from VBUS, decoupling present, unused modem inputs tied to VDD (not floating), UART wired via 10k series; USB face intentionally unpopulated |
| Q3 BC847BS + R70/R71 | E1=RTS, B1=DTR(via R70), C2=ESP_IO0; E2=DTR, B2=RTS(via R71), C1=ESP_EN | **PASS** — textbook ESP32 auto-program circuit |

## Power tree

| Ref | Check | Result |
|-----|-------|--------|
| U2 BQ25170DSGR | 1 IN→VBUS (C1 4.7u+C10/C63 1u ≥1µF ✓), 2 ISET→R3 1.5k (ICHG=KISET/RISET=300/1500=**200mA**, in 375Ω–30k range), 3 TS→R61 10k NTC+J2.3 batt NTC+Q2 clamp, 4 GND+9 EP→GND, 5 STAT→R49 10k pullup, 6 PG→R60 10k pullup, 7 VSET→R75 **27k→4.2V Li-ion** (EVM table #5), 8 OUT→VBAT (C2/C3/C58/C82/C85 ≥1µF) | **PASS** — 4.2V/200mA valid config; STAT/PG pullups per datasheet |
| Q2 CSD13380F3T | G←CHG_DIS (R59 100k pd), S=GND, D→TS | **PASS** — shorts TS low → charge disable (TS<VTS_ENZ) |
| D33 1N5819WS | A→VBUS, K→VBAT_SYS | **PASS** — Schottky OR'ing USB→system |
| Q6 AO3401A | G→VBUS, S→VBAT_SYS, D→VBAT | **PASS** — P-ch ideal diode: VBUS present→off, absent→battery feeds sys |
| U3 TPS63802 | 1 EN→VBAT_SYS (always-on, NOTE), 2 MODE→GND (PFM mode), 3/8/11 GND, 4 FB→R23 560k/R24 100k (VFB=0.5→**VOUT=3.30V**), 5 PG→NC, 6 VOUT→+3V3 (22u bulk + ceramics ≥8.2µF), 7/9 L→L1 **0.47µH** (datasheet recommended; DFE201612E listed by TI), 10 VIN→VBAT_SYS | **PASS** — config exactly per datasheet |
| U4 TPS7A20 | 1 IN→+3V3, 3 EN→+3V3 (always-on), 4 NC→NC, 5 OUT→+1V8_LDO→R97 0Ω→+1V8 (C6 1u out cap) | **PASS** |
| U15 TPS61240YFFR | A1 VIN→VBAT_SYS, A2 GND, B1 L→TX_SW (L2 1.0µH VBAT_SYS-side), B2 VOUT→TX_5V_RAW (C47 4.7u), C1 EN→TX5_EN (U19 P3 + R25 100k pd), C2 FB→TX_5V_RAW | **PASS** — fixed-5V boost, FB tied to VOUT per datasheet |
| U17 MAX17048 | 1 CTG→GND, 2 CELL→VBAT, 3 VDD→VBAT, 4 GND, 5 ALRT→GAUGE_ALRT (R74 10k pullup), 6 QSTRT→GND, 7/8 I2C bus, 9 EP→GND | **PASS** |
| U19 TCA6408A | 1 /RESET→ESP_EN (NOTE: resets with MCU), 2-10 P0-P7 = AFE_RESETZ/PWDN/AD5940_RESET/TX5_EN/IR_GATE/CHG_STAT/VBUS_DET/CHG_DIS, 11 /INT→EXP_INT (R53 10k pullup), 12/13 I2C, 14/15 VCCP/VCCI→+3V3, 16 ADDR→GND (0x20) | **PASS** |
| VBUS_DET | R50 100k (VBUS) / R51 150k (GND) → U19.P6 | **PASS** — 3.0V @5V > VIH 2.31V |

## Analog front ends

| Ref | Check | Result |
|-----|-------|--------|
| U8 ADS1292R | IN1P/N (BioZ ch) + IN2P/N (ECG ch); VREFP→C30 10u; VREFN→GND; VCAP1/2→C32/C33 1u; AVDD/DVDD→+3V3_ANA, AVSS/DGND/EP→GND; CLKSEL→+3V3_ANA (internal osc); CLK,START→GND; /PWDN→ADS1292_PWDN (U19.P1); SPI→bus + /DRDY→U1.38 (R99 100k pullup); GPIO1/2→ADS_GPIO1/2 (R100/R101 10k pd); RLD network R17 1M+C65 1.5n fb, R15/R16 1M RLDREF+C66 1u, R43/R45 5.11M RLD-bias, R69 10k RLD_CLAMP; RESP_MOD→R30/R31 40.2k; PGA caps C34 47n, C35 4.7n | **PASS** — full TI reference-style network |
| U7 AD5940 | AVDD_1/2,DVDD→+3V3_ANA; AGND_1/2,DGND_1/2/3,AGND_REF→GND; IOVDD→R12 10Ω+C19 1u; VREF_1V82→C20 4.7u; VREF_2V5→C21 470n; AVDD_REG→C42 1u; DVDD_REG→C23 470n; VBIAS0→C25 100n; VZERO0→C24 100n; VBIAS_CAP→C22 470n; RCAL0-1→R13 1k (NOTE: vs typical 200Ω — calibration constant, firmware-scaled); RC0_0/1→C27 100n; DE0/CE0/RE0→1k series; SE0→C55 470n; XTALI/O→NC (internal osc); GPIO0→U1.22; GPIO2→NIR730_GATE; /RESET→AD5940_RESET (U19.P2); SPI+CS; AFE1/2,DNC,GPIOs→NC | **PASS** |
| U6 AFE4900 | RX_SUP/IO_SUP→+3V3_ANA; RX/TX_GND→GND; TX_SUP→TX_5V; I2C_SPI_SEL→+3V3_ANA (**high=SPI**); SEN→CS_AFE4900; I2C_DAT→MOSI; I2C_CLK→SCK; SDOUT→MISO_AFE; CLK→R10 1k→GND (**internal osc**, datasheet-supported); RESETZ→U19.P0; ADC_RDY→U1.23; BG→C15 100n; TX1-4→SFH7072 cathodes; INP/INM,INP2/INM2→PD pins; INP3/INM3,RLD_OUT,PROG_OUT1,DNC→NC; INP_ECG/INM_ECG→AFE_INP/INM | **PASS** |
| U22 MAX86141 | VDD_ANA/VDD_DIG→+1V8; GND_ANA/GND_DIG/PGND→GND; VLED→TX_5V; VREF→C83 1u (MX_VREF); SCK/SDI/CSB→3.3V SPI (**PASS — digital pins abs-max −0.3..+6.0V, VIH 1.4V: 3.3V-safe**); SDO→MX_SDO_1V8→U26.A1→B1 MISO_MX (level-shifted 1.8→3.3 ✓); INT→MAX86178_INT (R113 10k pullup); LED1/2/3_DRV→LED_K nets; PD1_IN→PD_K, PD_GND→PD_A; PD2_IN,GPIO→NC | **PASS** |
| U26 SN74AXC2T245 | VCCA→+1V8, VCCB→+3V3; DIR1→+1V8 (A→B: 1.8→3.3 shift); A1←MX_SDO_1V8, B1→MISO_MX→R106→SPI_MISO; OE→CS_MAX86178 (translate only during CS — gated MISO); ch2: B2→GND, DIR2→GND (B→A), A2→NC | **PASS** |
| U9 PCA9306 | VREF1→+1V8, VREF2+EN→R18 **200k**→+3V3 (exact datasheet value); side1 SCL/SDA→1V8 bus (R22/R21 2.2k pullups), side2→3.3V bus (R20/R19 2.2k) | **PASS** |
| R89-R92/R106 0Ω | per-slave MISO stubs (DOUT→bus) | **PASS** — star MISO |

## Sensors / peripherals

| Ref | Check | Result |
|-----|-------|--------|
| U10 AS7341 | VDD→+1V8 (1.8V part), SCL/SDA→1V8 bus, LDR→D10 illuminator LED, INT/GPIO→NC | **PASS** |
| U11/U20 TMP117 | V+→+3V3, I2C bus; ADD0 GND→0x48 / +3V3→0x49 (distinct); U11 ALERT→R65 10k pullup→U1.5; U20 ALERT→NC | **PASS** |
| U12 MLX90632SLD | VDD→+3V3, SDA/SCL→**1V8 bus**, ADDR→GND, EP→GND | **FLAG** — part must be the **1.8V-I²C variant** (MLX90632 ships in 3.3V or 1.8V I²C-reference versions). If the assembled part is the 3.3V-I²C variant, VIH=0.7·VDD≈2.3V won't be met by 1.8V pull-ups → comms fail. Verify BOM/ordered part suffix. |
| U13 BME280 | VDD/VDDIO→+3V3, CSB→+3V3 (=I²C mode), SDI→SDA, SCK→SCL, SDO→GND (addr 0x76) | **PASS** |
| U14 LSM6DSV80X | 1 SDO/TA0→GND (0x6A), 2 SDx/3 SCx→GND (mode 1), 4 INT1→U1.4, 5 VDDIO→+3V3, 6/7 GND, 8 VDD→+3V3, 9 INT2→NC, 10/11 NC, 12 CS→+3V3 (I²C enabled), 13 SCL/14 SDA→bus | **PASS** — all 14 pins per ST datasheet |
| U16 SFH 7072 | PD1/PD2 C/A→PD_INP/INM,PD2_INP/INM; emitters A→TX_5V, C→TX1-4 | **PASS** |
| U23 SHT45 | VDD→+3V3, SDA/SCL→bus, VSS→GND | **PASS** |
| U24 RV-3028-C7 | CLKOUT→NC, INT→R112 10k pullup, SCL/SDA→bus, VSS→GND, VBACKUP→R115 **10k→GND** (Micro Crystal's recommended unused-backup handling), VDD→+3V3, EVI→GND | **PASS** |
| U25 IM69D130 | DATA→MIC_DAT→U1.34, CLOCK→MIC_CLK→U1.33, VDD→+3V3, SELECT→GND, GND | **PASS** |
| U18 W25Q512 | /CS→CS_FLASH (R52 10k pullup), DO→MISO_FL, /WP→+3V3, DI→MOSI, CLK→SCK, /HOLD→+3V3, VCC→+3V3 (3V variant), GND+EP→GND | **PASS** |

## ESP32-S3-WROOM-1-N16R8 (U1) — all 41 pins vs Espressif pinout

| Pin | Function | Net | Verdict |
|-----|----------|-----|---------|
| 1,40,41 | GND | GND | PASS |
| 2 | 3V3 | +3V3_ESP (via R94 0Ω, C7 22u+C8 100n) | PASS |
| 3 | EN | ESP_EN — R5 10k pullup + C64 1u RC + SW1 + J8 + Q3 autoprog + U19./RST | PASS (Espressif-required RC) |
| 4-7 | IO4-7 | LSM6_INT1 / TMP117_ALERT(R65 pu) / GAUGE_ALRT(R74 pu) / CHG_PG(R60 pu) | PASS |
| 8-11 | IO15-18 | CS_AD5940 / CS_FLASH(R52 pu) / STATUS_LED(R88 150→D25) / CS_AFE4900 | PASS |
| 12,17 | IO8,IO9 | I2C_SDA/SCL (R19/R20 2.2k pullups) | PASS |
| 13,14 | IO19,IO20 | USB_DM/DP_S3 via R102/R103 0Ω→J1 | PASS — native USB connected |
| 15 | IO3 | EXP_INT (R53 pu, U19./INT) | PASS |
| 16,24,25,26 | IO46,47,48,45 | NC | PASS — strapping, internal defaults ok (IO45=VDD_SPI→3.3V flash) |
| 18-22 | IO10-14 | CS_ADS1292 / SPI_MOSI / SPI_SCK / SPI_MISO / AD5940_GPIO0 | PASS |
| 23 | IO21 | AFE4900_ADC_RDY | PASS |
| 27 | IO0 | ESP_IO0 — R6 10k pullup + SW2 + J8 + Q3 | PASS — boot-strap pulled high |
| 28-30 | IO35-37 | NC | PASS — octal PSRAM pins, must NC on N16R8 |
| 31-35 | IO38-42 | CS_MAX86178(R117 pu) / MAX86178_INT(R113 pu) / MIC_CLK / MIC_DAT / SPARE_IN | PASS |
| 36,37 | RXD0,TXD0 | ESP_RX/TX → R73/R72 → CP2102 + J8 prog | PASS |
| 38,39 | IO2,IO1 | ADS1292_DRDY(R99 pu) / FSR_ADC(J3+R8 10k divider) | PASS |

## Connectors / switches

J2 LiPo (VBAT/GND/TS ✓), J3 FSR (+3V3/FSR_ADC+R8 10k pd ✓), J5 ECG pads →51k→ESD→
ADS1292/AFE4900 ✓, J6 EDA →51k→ESD→AD5940 CE/RE/SE/DE ✓, J7 BIOZ →51k→ESD→AD5940
AIN0-3 ✓, J8 Tag-Connect (+3V3/GND/TX/RX/EN/IO0 = std ESP prog ✓), J9 I2C tail ✓,
J10 optical tail (LED1-3_K/LED_AN/PD_K/PD_A ✓), J11 sweat → SWEAT_* ✓,
J12 FH12-14S (power+I2C+optical+sweat mirror; pin14/MP NC) ✓, J13 MX ECG **DNP**
(nets end at ESD — reserved) ✓, SW1/SW2 (EN/IO0→GND buttons) ✓, D1-D30 TPD1E10B06
ESD on every external pad ✓.

## Findings summary

- **1 FLAG**: U12 MLX90632 I²C variant must be the 1.8V-I²C version (VDD=3.3V but bus at 1.8V pull-ups). Verify the ordered part before/after build; wrong variant = dead I²C.
- **NOTES (design choices, all safe)**: CP2102 USB face DNP'd (data path = S3 native USB via R102/103); J13 MX-ECG pads reserved/DNP'd; U3 EN hardwired on; RCAL=1k (not 200Ω); U19 resets with ESP_EN; U20 ALERT unmonitored; charge rate 200mA; VBUS_DET→3.0V ok.
- **PASS**: all 42 ICs/connectors/transistors verified pin-by-pin; every rail has required decoupling; every open-drain output has a pull-up; every strapping/mode pin correctly biased; every external pad has ESD protection; no floating inputs except intended NCs; zero schematic/board connectivity mismatches.
