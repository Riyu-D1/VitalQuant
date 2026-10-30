# VitalQuant firmware v2: hardware-interface brief for the WROOM board (fix_v22)

> **Status note (fix_v24, 2026-10-25):** this document describes an earlier
> revision. Current board state: D10 = Nichia NF2W757G-F1 (consigned, no
> LCSC), U14 = LSM6DSV320XTR (LCSC C26633007, same LGA-14 land), J12 /
> U24 / U25 are **fitted** (in the v24 JLC BOM/CPL), and the dead MX-ECG
> chain D29 / D30 / R118 / R119 is DNP. See `../reports/CHANGES_V24.md`.

**For:** the CTO / firmware author. **From:** the hardware side. This is a specification and prompt, not code.

**Board:** `fix_v22` (8-layer, ESP32-S3-WROOM-1-N16R8). Facts come from the fix_v22 schematic netlist (`kicad-cli sch export netlist`), `fab/BOM.csv`, `FIRMWARE_NOTES.md` and the earlier verification reports.

**Labels used throughout:**
- **[C]** checked in the fix_v22 files.
- **[I]** inferred: datasheet knowledge or a derivation. Confirm it against the datasheet before relying on it.
- **[G]** guess or open question.

> This is a non-medical research prototype. Nothing here is a claim of medical safety, clinical accuracy or regulatory compliance (IEC 60601, ISO 80601 or similar). The 238-patient sepsis simulation is a feasibility model only.

---

## 0. Your task
Write firmware v2 for this board (ESP-IDF 5.x recommended; Arduino-ESP32 3.x acceptable). It must:
1. Bring up every peripheral listed below, using exactly the pins, buses, addresses and limits in this document.
2. Respect every "MUST NOT" in §10.
3. Provide the self-tests in §9.
4. Meet the acceptance criteria in §11.

Do not change any GPIO assignment. If a pin seems wrong, raise it with hardware; do not work around it silently. Update `firmware/esp32/profiles/` with a new `hw_v3.yaml` (WROOM). Do not overwrite `hw_v2.yaml`. Fix the discrepancies listed in §1.3.

## 1. What changed from v1/v2 firmware assumptions

### 1.1 Module
- **[C]** U1 changed from the ESP32-S3-MINI-1-N4R2 (4 MB flash, 2 MB quad PSRAM) to the **ESP32-S3-WROOM-1-N16R8**: 16 MB quad flash, 8 MB **octal (OPI)** PSRAM, on-board PCB antenna.
- **[C]** GPIO numbers are **unchanged** for every used net (verified pad by pad against the netlist; table in §2).
- **[C]** **GPIO35, GPIO36 and GPIO37** (module pads 28–30) are used internally by the octal PSRAM. They are not connected on the board. **Firmware must never configure them.**
- **[C]** GPIO45, GPIO46, GPIO47 and GPIO48 are not connected. GPIO26–32 (internal flash/PSRAM SPI) are not exposed.
- **[C]** Antenna: the WROOM PCB antenna overhangs the board's −y (north) edge, with an all-layer keep-out. Nothing firmware can change. **[I]** Expect the RF range to change inside the case; do not hard-code TX power above what range tests justify.

### 1.2 Board changes since hw_v2.yaml that matter to firmware
- **[C]** J1 (USB-C) moved to x 29.5 for plug clearance. The USB wiring is electrically unchanged.
- **[C]** The fab BOM now lists U1 = ESP32-S3-WROOM-1-N16R8. Flash and PSRAM config must match (§6).

### 1.3 Discrepancies: the existing firmware files vs this board
| # | File / claim | Board reality | Action |
|---|---|---|---|
| D1 | `hw_v2.yaml`: `i2c0 hz: 400000` | **[C]** The pull-ups are R19/R20 2.2 kΩ to +3V3, with 14 nodes on the bus. The earlier sim gave a rise time of about 503 ns, which fails 400 kHz (tr ≤ 300 ns) and passes 100 kHz. | **Run I²C at 100 kHz.** |
| D2 | `hw_v2.yaml` / `hw_v1.yaml`: module MINI-1U-N4R2 with a U.FL antenna | **[C]** WROOM-1-N16R8 with a PCB antenna | New profile `hw_v3.yaml` |
| D3 | `hw_v2.yaml`: `ppg_satellite model: MAX86178` | **[C]** U22 is a **MAX86141ENP+**. The net names `CS_MAX86178` and `MAX86178_INT` are legacy labels only. | Use the MAX86141 register map |
| D4 | `hw_v1.yaml`: `spi1 {miso: 12, sclk: 13}` | **[C]** MOSI = GPIO11, SCK = GPIO12, MISO = GPIO13 (hw_v2.yaml is correct) | Use the hw_v2 mapping |
| D5 | `hw_v2.yaml`: RTC `dnp: true`, mic `dnp: true`, J12 "DNP" | **[C]** `fab/BOM.csv` lists U24 RV-3028-C7, U25 IM69D130 and J12 FH12-14S as **fitted** (DNP column empty). The only schematic DNP parts are J13, R93, R104, R105, R107 and R108. | Treat the RTC and mic as present, but probe them at boot and degrade gracefully. **[G]** Confirm with the owner whether U24/U25/J12 are actually to be assembled. |
| D6 | `FIRMWARE_NOTES.md` §13: "CP2102N + Q3 auto-reset unchanged; esptool auto-download works as before" | **[C]** R104/R105 (USB D± to the CP2102N) are **DNP**. The CP2102N gets power from VBUS but has **no USB data connection**. So USB auto-download goes through the S3's **native USB-Serial/JTAG**, not the CP2102N. | §5 |
| D7 | `FIRMWARE_NOTES.md` §3: "AS7341, IMU, TMP117 … share I2C_1V8" | **[C]** This dates from fix_v10. Now only **AS7341 (U10)** and **MLX90632 (U12)** sit on the 1.8 V island, behind the PCA9306 (U9). Everything else is on the 3.3 V bus. | §3 |
| D8 | `FIRMWARE_NOTES.md` §5: deep sleep ~38 µA via a PMIC | **[C]** There is no PMIC; that was fix_v10. The V21 verify modelled about **110 µA** board standby. | §4.4 |
| D9 | `FIRMWARE_NOTES.md` §5 says the cell has **no integrated PCM**, but §12 says "plain 2-wire protected LiPo" | **[G]** Contradiction | **Assume NO protection.** Firmware must enforce the cut-off (§4.2) until the owner confirms the exact cell. |

## 2. Full U1 GPIO table (every module pad) [C unless marked]
Pulls are external, on the board. Internal pulls should stay off where an external pull exists.

| Pad | GPIO | Net | Connected to | Dir (ESP) | Board pull | Active / notes |
|---|---|---|---|---|---|---|
| 1, 40, 41 | GND | GND | — | — | — | Pad 41 is the thermal EP |
| 2 | 3V3 | +3V3_ESP | via R94 0 Ω from +3V3; C7 22 µF, C8 100 nF | — | — | Module supply |
| 3 | EN | ESP_EN | R5 10k to +3V3, C64 1 µF to GND, SW1 (reset to GND), Q3 auto-reset, J8.5 (Tag-Connect), TP9, **U19 TCA6408 ~RESET** | in | 10k up / 1 µF | Low = reset. **[C] It also resets the I/O expander.** |
| 4 | IO4 | LSM6_INT1 | U14 LSM6DSV80X INT1 | in | none | **[I]** Push-pull, active-high by default |
| 5 | IO5 | TMP117_ALERT | U11 TMP117 ALERT | in | R65 10k up | Open-drain, active-low by default **[I]** |
| 6 | IO6 | GAUGE_ALRT | U17 MAX17048 ALRT | in | R74 10k up | Open-drain, active-low **[I]**. Used for the low-SOC / low-V cut-off (§4.2). |
| 7 | IO7 | CHG_PG | U2 BQ25170 ~PG | in | R60 10k up | Low = input power good (VBUS present) **[I]** |
| 8 | IO15 | CS_AD5940 | U7 AD5940 ~CS | out | R7 10k up | Active-low |
| 9 | IO16 | CS_FLASH | U18 W25Q512JV ~CS | out | R52 10k up | Active-low |
| 10 | IO17 | STATUS_LED | R88 150 Ω → D25 green LED → GND | out | — | **Active-high.** About 7–9 mA at 3.3 V **[I]** |
| 11 | IO18 | CS_AFE4900 | U6 AFE4900 SEN | out | R116 10k up to **+3V3_ANA** | Active-low |
| 12 | IO8 | I2C_SDA | 3V3 bus (§3) | od | R19 2.2k up | — |
| 13 | IO19 | USB_DM_S3 | R103 0 Ω → USB_DM → U21 USBLC6 → J1 | USB | — | Native USB D− |
| 14 | IO20 | USB_DP_S3 | R102 0 Ω → USB_DP → U21 → J1 | USB | — | Native USB D+ |
| 15 | IO3 | EXP_INT | U19 TCA6408 ~INT | in | R53 10k up | Active-low, open-drain. **Strapping pin** (JTAG source select; only matters if eFuse STRAP_JTAG_SEL is burnt; **do not burn it**). |
| 16 | IO46 | NC | — | — | none (internal pull-down) | Strapping pin (ROM log / download). Leave it unconfigured. |
| 17 | IO9 | I2C_SCL | 3V3 bus | od | R20 2.2k up | — |
| 18 | IO10 | CS_ADS1292 | U8 ADS1292R ~CS | out | R98 10k up | Active-low |
| 19 | IO11 | SPI_MOSI | U18 DI, U22 SDI, U6 I2C_DAT (used as SPI SDIN), U7 MOSI, U8 DIN | out | — | Shared SPI2/FSPI; IOMUX pin |
| 20 | IO12 | SPI_SCK | U18 CLK, U22 SCLK, U6 I2C_CLK (SCLK), U7 SCLK, U8 SCLK | out | — | Shared |
| 21 | IO13 | SPI_MISO | via 0 Ω links: R89 (ADS), R90 (AFE), R91 (AD5940), R92 (flash), R106 (MAX86141 via U26) | in | — | One MISO per device, joined by 0 Ω links |
| 22 | IO14 | AD5940_GPIO0 | U7 GPIO0 (ball F5) | in | none | AD5940 interrupt output. Program its polarity in the AD5940. |
| 23 | IO21 | AFE4900_ADC_RDY | U6 ADC_RDY | in | none | **[I]** Active-high pulse; timestamp it in the ISR |
| 24 | IO47 | NC | — | — | — | Do not use |
| 25 | IO48 | NC | — | — | — | Do not use |
| 26 | IO45 | NC | — | — | none (internal pull-down) | **Strapping pin: VDD_SPI.** It must read 0 at reset, giving 3.3 V flash, which is correct for N16R8. **Never pull it high, and never burn the VDD_SPI eFuse to 1.8 V.** |
| 27 | IO0 | ESP_IO0 | R6 10k up, SW2 (BOOT to GND), Q3 auto-reset, J8.6, TP10 | in | 10k up | Strapping pin: low at reset = download mode |
| 28–30 | IO35–37 | NC | used by the internal octal PSRAM | — | — | **RESERVED: never touch** |
| 31 | IO38 | CS_MAX86178 | U22 MAX86141 CSB **and** U26 SN74AXC2T245 OE | out | R117 10k up | Active-low. It also enables the MISO level shifter (§3.2). |
| 32 | IO39 | MAX86178_INT | U22 MAX86141 INT | in | R113 10k up | Open-drain, active-low **[I]** |
| 33 | IO40 | MIC_CLK | U25 IM69D130 CLOCK | out | — | PDM clock |
| 34 | IO41 | MIC_DAT | U25 IM69D130 DATA | in | — | PDM data. SELECT = GND; **[I]** check which clock edge from the datasheet |
| 35 | IO42 | SPARE_IN | R107/R108 100k divider of VBAT, **both DNP** | in | none fitted | Floating as built. Enable the internal pull-down or leave it unused. **Not ADC-capable.** |
| 36 | RXD0 (GPIO44) | ESP_RX | J8.4 Tag-Connect; R73 10k ← CP2102N TXD; TP12 | in | — | UART0 console |
| 37 | TXD0 (GPIO43) | ESP_TX | J8.3; R72 10k → CP2102N RXD; TP11 | out | — | UART0 console |
| 38 | IO2 | ADS1292_DRDY | U8 ~DRDY | in | R99 100k up | **Active-low.** Timestamp it in the ISR. |
| 39 | IO1 | FSR_ADC | J3 (FSR between +3V3 and the node), R8 10k to GND | analogue in | 10k down | ADC1_CH0 **[I]**; usable alongside Wi-Fi |

### Strapping and boot summary
- **[C]** IO0 has a 10k pull-up, so normal boot is SPI flash boot. Holding SW2 (IO0 low) while pulsing SW1 (EN) enters download mode.
- **[C]** IO45 and IO46 are floating; the internal pull-downs give 3.3 V VDD_SPI and the normal ROM log.
- **[C]** IO3 is pulled up and harmless.
- **[I]** The ESP_EN RC (10k / 1 µF) gives τ ≈ 10 ms, which meets Espressif's EN-after-supply guidance.

## 3. Buses and peripherals

### 3.1 I²C: one controller, GPIO8 = SDA, GPIO9 = SCL, **100 kHz maximum**
**3.3 V side** (pull-ups R19/R20 2.2k to +3V3). Addresses come from the strap wiring [C]; the address values are [I] from each datasheet's address table.

| Ref | Part | Strap → address | INT to ESP | Notes |
|---|---|---|---|---|
| U11 | TMP117AIDRVR (skin) | ADD0 = GND → **0x48** | IO5 (ALERT) | Burst conversions, ≥1 s period (self-heating, per FIRMWARE_NOTES §3) |
| U20 | TMP117AIDRVR | ADD0 = V+ → **0x49** | none (ALERT NC) | Poll it |
| J9 / J12 tail | TMP117/119 off-board | **0x4A** per hw_v2.yaml | none | Optional; probe for it **[G]** |
| U13 | BME280 | SDO = GND → **0x76** | — | CSB tied high, so I²C mode |
| U14 | LSM6DSV80X | SA0 (pin 1) = GND → **0x6A** | IO4 (INT1) | CS (pin 12) = +3V3, so I²C mode |
| U17 | MAX17048G+T10 | fixed **0x36** | IO6 (ALRT) | Powered from VBAT, so always on |
| U19 | TCA6408ARSVR expander | ADDR = GND → **0x20** | IO3 (INT) | ~RESET = ESP_EN. All pins come up as inputs after any ESP reset (see the pin map below). |
| U23 | SHT45-AD1B | fixed **0x44** | — | Skin-side RH/temperature |
| U24 | RV-3028-C7 RTC | fixed **0x52** | RTC_INT → TP28 + R112 only; **not wired to the ESP** | Poll its flags. VBACKUP has R115 10k to GND, so no backup cell **[C]**. |

**1.8 V island** (via U9 PCA9306; pull-ups R21/R22 2.2k to +1V8; PCA9306 EN = VREF2 through R18 200k to +3V3 [C]). It is the same logical bus, so all addresses must be unique, and they are.

| Ref | Part | Address | Notes |
|---|---|---|---|
| U10 | AS7341-DLGM | **0x39** [I] | INT and GPIO are NC. Its LDR pin drives the on-chip LED driver. |
| U12 | MLX90632 | ADDR = GND → **0x3A** [I] | Consigned part. `hw_v1.yaml`'s 0x3B is wrong for this strap. |

**Address-map check [I]:** 0x20, 0x36, 0x39, 0x3A, 0x44, 0x48, 0x49, (0x4A), 0x52, 0x6A, 0x76. No clashes.

**TCA6408 port map** [C] (the external pulls fix the state while the pins are inputs after reset):

| Pin | Net | Function | Board pull | Firmware direction / meaning |
|---|---|---|---|---|
| P0 | AFE4900_RESETZ | AFE4900 reset | R9 10k down | Output. Low = in reset (default). Drive high to run. |
| P1 | ADS1292_PWDN | ADS1292R ~PWDN/~RESET | R14 10k down | Output. Default low = powered down. |
| P2 | AD5940_RESET | AD5940 ~RESET | R11 10k down | Output. Default low = in reset. |
| P3 | TX5_EN | TPS61240 EN (TX_5V LED boost) | R25 100k down | Output. Default off. Enable **only for LED windows**. |
| P4 | IR_GATE | Q1 low-side switch for an IR LED (IR_K) | R48 100k down | Output, active-high |
| P5 | CHG_STAT | BQ25170 STAT | R49 10k up | Input. Low = charging **[I]** |
| P6 | VBUS_DET | VBUS divider (R50 100k / R51 150k), about 3.0 V when VBUS = 5 V | — | Input. High = USB present. |
| P7 | CHG_DIS | Q2 pulls TS low, which **disables charging** | R59 100k down | Output. High = charge inhibit (TS < 50 mV per FIRMWARE_NOTES §12). |

### 3.2 SPI: one SPI2/FSPI host. MOSI GPIO11, SCK GPIO12, MISO GPIO13. Five chip-selects, all active-low with pull-ups.
| Device | CS | Mode | Max SCLK to use | Interrupt / ready | Reset / power control | Notes |
|---|---|---|---|---|---|---|
| U8 ADS1292R (ECG + resp) | IO10 | **Mode 1** (CPOL 0, CPHA 1) [I] | ≤ 4 MHz recommended [I]; the datasheet allows more, but respect its multi-byte decode timing | IO2 DRDY, active-low | PWDN/RESET via expander P1 | **[C]** START is tied to GND, so conversions must be started with the START opcode. **[C]** CLKSEL is high, so it uses the internal oscillator. 500 sps per FIRMWARE_NOTES §9. Use RDATAC or RDATA. Lead-off is on. |
| U6 AFE4900 (main PPG + ECG ch) | IO18 | Mode 0 [I] | ≤ 8 MHz [I] | IO21 ADC_RDY | RESETZ via expander P0 | **[C]** I2C_SPI_SEL is tied to +3V3_ANA, and MOSI/SCK go to its I2C_DAT/I2C_CLK balls. The design intent is SPI mode. **[G]** Confirm the select polarity in the AFE4900 datasheet. SDOUT goes via R90. Its I/O supply is **+3V3_ANA**. |
| U7 AD5940 (EDA/BioZ/sweat) | IO15 | Mode 0 [I] | ≤ 8 MHz [I] | IO14 = AD5940 GPIO0 | ~RESET via expander P2 | **[C]** AD5940 GPIO2 drives Q4 (NIR730_GATE → D12 730 nm LED from +3V3 through R109 100 Ω). XTAL is not fitted, so use the internal HFOSC. Subtract the 51 kΩ series resistors from the measured |Z| (FIRMWARE_NOTES §9/§10). |
| U18 W25Q512JV (64 MB data flash) | IO16 | Mode 0 or 3 [I] | ≤ 40 MHz [I] | — | /WP and /HOLD tied to +3V3 | **[I]** It is 512 Mbit, so it needs **4-byte addressing** (4B opcodes or enter-4-byte mode). This is separate from the module's internal 16 MB flash. |
| U22 MAX86141 (satellite PPG on J10) | IO38 | Mode 0 [I] | **≤ 8 MHz** ([C] the cached datasheet text gives fSCLK 8 MHz; FIRMWARE_NOTES §10) | IO39 INT, active-low | soft reset by register | **[C]** It runs on 1.8 V (VDD_DIG and VDD_ANA = +1V8). SDO returns through U26 SN74AXC2T245 (A side 1.8 V, B side 3.3 V; DIR fixed). **[I]** U26's OE is active-low and tied to the same CS, so the translator drives MISO only while CS38 is low. Use its FIFO plus the INT threshold. |

**Shared-MISO rule:** assert only one CS at a time. Every CS must idle high (each has a pull-up). **[I]** Each device tri-states its output when deselected. Run a MISO bus-contention self-test (§9).

### 3.3 Other I/O
- **[C]** Mic: U25 IM69D130 PDM on IO40 (CLK) / IO41 (DATA), SELECT = GND. Use the ESP-IDF I2S PDM-RX driver. **[I]** Clock range is about 1.0–3.25 MHz for normal mode; stop the clock to put it in sleep.
- **[C]** FSR: on IO1, ADC1_CH0 [I]. It is a divider (FSR to +3V3, 10k to GND), so the reading is an ordinal contact level only. Use 12-bit with 11 dB attenuation; sample at 20 Hz per the profile.
- **[C]** Status LED: IO17, active-high.
- **[C]** Optical LEDs:
  - U16 SFH7072, with anodes on TX_5V and cathodes on AFE4900 TX1–TX4.
  - The J10 tail LEDs, driven by MAX86141 LED1–3 with anode LED_AN = TX_5V through R114.
  - The IR LED via Q1/IR_GATE (expander P4).
  - D12, 730 nm, via Q4 from AD5940 GPIO2.

## 4. Power

### 4.1 Rails [C]
| Rail | Source | Limit | Notes |
|---|---|---|---|
| VBUS | USB-C J1 (CC1/CC2 5.1k pull-downs: R1, R2) | USB 2.0 default 500 mA **[I]** | ESD: U21 USBLC6-2SC6 |
| VBAT | J2 LiPo pads (pin 3 = TS); target cell **402535, about 300 mAh** (FIRMWARE_NOTES) | — | Charged by U2 BQ25170 |
| VBAT_SYS | Q6 AO3401A from VBAT, or D33 1N5819 from VBUS (load sharing) | — | About 4.7 V with USB, about VBAT without |
| +3V3 | U3 **TPS63802** buck-boost (EN = VBAT_SYS, so **always on**; FB 560k/100k) | V21 verify: worst-case DC load 471.6 mA vs the ≥0.5 A rating it used; typical 116 mA | Feeds the WROOM (via R94), all 3V3 I²C parts, and +3V3_ANA via R95 0 Ω. **Firmware cannot switch it off.** |
| +3V3_ANA | R95 0 Ω from +3V3 | — | ADS1292R, AFE4900 RX/IO, AD5940. **Not switchable.** Use each IC's power-down. |
| +1V8 | U4 TPS7A2018 (EN = +3V3, always on) via R97 | 300 mA LDO **[I]** | MAX86141, AS7341, PCA9306 low side, U26 |
| TX_5V | U15 **TPS61240** boost from VBAT_SYS, **EN = TX5_EN (expander P3)** | About 400 mA practical output (V21 verify) | LED anodes only |

### 4.2 Battery and charging: firmware duties
- **[C] Charger:**
  - BQ25170 with R_ISET = 1.5 kΩ, giving a **200 mA charge current** (verified in sim).
  - R_VSET = 27 kΩ, giving **4.20 V** CV (sim 4.17–4.23 V).
  - TS goes to R61, a 10k B3380 NTC on the board. Charging is suspended below about 0 °C and above about 45 °C board temperature (FIRMWARE_NOTES §12).
- **[C]** Charge status: CHG_STAT (expander P5) and CHG_PG (IO7). Inhibit charging with CHG_DIS (P7).
- **[G] Assume the cell has NO protection circuit (D9).** Firmware MUST:
  - Configure the MAX17048 VALRT low threshold at about **3.2 V** [G] and the SOC alert at about 5 %.
  - On GAUGE_ALRT (IO6) or VCELL ≤ 3.2 V: stop all LEDs and RF, save the logs, and enter deep sleep with only the gauge alert as a wake source.
  - Treat **≤ 3.0 V** [G] as an emergency shutdown. Never run the boost (TX_5V) below 3.3 V [G].
  - Hardware cannot disconnect the cell. The board standby draw (about 110 µA) keeps draining it, so tell the owner a protected cell is strongly preferred.
- **[C/I] USB budget:** 200 mA charge plus the system load can exceed **500 mA** on current-limited hosts (V21 finding m4). While VBUS is present, firmware SHOULD avoid running Wi-Fi TX bursts and the LED boost at the same time as charging, or set CHG_DIS during heavy bursts.

### 4.3 LED pulse budget: REQUIRED [C, from V21 finding m3 and FIRMWARE_NOTES §2]
- If all chains fired together (AFE4900 about 200 mA + MAX86141 about 200 mA + SFH7072 about 500 mA), the peak would be about **900 mA**. The TPS61240 can only supply about 400 mA.
- **Firmware MUST interleave:** at most one LED chain active in any time slot, and the global LED duty for 400 mA-class pulses must be ≤ 1 %.
- Enable TX_5V only around LED windows. Switch TX_5V off on watchdog timeout, on brown-out risk, and on low battery.
- Schedule the AS7341 LED (about 91 mA), the IR (Q1) and the NIR730 (D12) in their own slots.

### 4.4 Sleep [C/I]
- **[C]** The V21 verify modelled about **110 µA total board standby**: about 110 days of theoretical standby on 300 mAh.
- **[C]** The "29 µA" figure in the reports is the **modelled single-fault patient leakage** (versus the numeric 100 µA BF limit). It is not a sleep current and not a compliance claim.
- **[C]** The rails cannot be switched off (except TX_5V), so before deep sleep firmware must:
  - Put the ADS1292R into PWDN (P1 low).
  - Hold the AFE4900 in reset (P0 low) or use its power-down register.
  - Hold the AD5940 in reset or hibernate.
  - Put the MAX86141 into SHDN by register.
  - Put the IMU, BME280, SHT45, TMP117s, AS7341 and MLX90632 into their sleep/one-shot modes.
  - Stop the mic clock and turn TX_5V off (P3 low).
  - Put the W25Q512 into power-down (B9h) [I].
- **The TCA6408 keeps its outputs during ESP deep sleep** [I], because it is on the always-on +3V3. Any EN/reset that pulses low (SW1, brown-out) resets it: all lines then fall to their pulled-down "off" states, which is the safe default.
- **GPIO hold in deep sleep** (`gpio_hold_en` + `gpio_deep_sleep_hold_en`):
  - Hold all five CS lines high (the pull-ups help, but hold them anyway).
  - Hold STATUS_LED (IO17) low and MIC_CLK (IO40) low.
  - **[I]** Wake sources: EXT1 on GAUGE_ALRT (IO6), EXP_INT (IO3; USB plug-in via VBUS_DET/CHG_STAT), and optionally IO4 (IMU wake-on-motion). These are all RTC-capable GPIOs [I].
- **[I]** The CP2102N is powered only from VBUS, so it draws nothing on battery. R72/R73 limit back-injection to about 0.3 µA (V21 sim).

### 4.5 Thermal [C/I]
- **[C]** V21 sim: no over-temperature in the models.
- **[I]** Firmware SHOULD read the TMP117s and the ESP32 internal sensor. Throttle Wi-Fi and LED duty if the board is above about 40 °C [G] (skin-worn device), and stop charging-heavy loads above 45 °C (the charger's own TS cut-off is also at about 45 °C).

## 5. USB, flashing and console
- **[C] Native USB:** J1 D+/D− → U21 ESD → R102/R103 0 Ω → **GPIO20 (D+) / GPIO19 (D−)**. Use the S3's built-in **USB-Serial/JTAG** (CDC console plus JTAG) by default, or TinyUSB OTG if the CTO needs other device classes. The two cannot both own the pins at runtime.
- **[C] CP2102N (U5)** is fitted and powered from VBUS, but its USB D± links R104/R105 are **DNP**, so it is **not reachable over USB** as built. Its UART is wired to UART0 through R72/R73 10k, and its RTS/DTR drive the Q3 auto-reset, but nothing will drive them. **Never fit R102/R103 and R104/R105 together.**
- **Flashing:**
  - (a) **USB-C via native USB-Serial/JTAG**, `esptool.py --chip esp32s3 --port <cdc>`. Auto-reset works over the native USB in most cases [I]. If the app has crashed or USB was disabled, hold **BOOT (SW2)**, tap **RESET (SW1)**, release BOOT, then flash.
  - (b) **Tag-Connect J8 (TC2030)**: 3V3, GND, ESP_TX, ESP_RX, ESP_EN, ESP_IO0, for a UART0 programmer with DTR/RTS-style reset.
- **Firmware MUST NOT:**
  - disable USB-Serial/JTAG permanently (eFuse);
  - repurpose GPIO19/20;
  - burn the JTAG or VDD_SPI strap eFuses.
  Keep a recovery path always available.
- **[C] USB electrical notes:** full-speed only. The pair has about 12 mm skew and is long (DP about 130 mm, DM about 118 mm). That is fine for 12 Mb/s, but there is no high-speed capability.

## 6. Flash / PSRAM / partition configuration
- **[C/I] Module:** 16 MB **quad** flash at 3.3 V, plus 8 MB **octal** PSRAM.
- **Arduino-ESP32:** board "ESP32S3 Dev Module"; Flash Size 16MB; Flash Mode QIO 80 MHz; **PSRAM "OPI PSRAM"**; Partition Scheme custom (below); USB CDC On Boot "Enabled" for the native-USB console.
- **ESP-IDF sdkconfig, suggested [I]:**
  ```
  CONFIG_IDF_TARGET="esp32s3"
  CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
  CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
  CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
  CONFIG_SPIRAM=y
  CONFIG_SPIRAM_MODE_OCT=y
  CONFIG_SPIRAM_SPEED_80M=y          # 120M only if validated across temperature
  CONFIG_SPIRAM_USE_MALLOC=y
  CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
  CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y   # secondary console on UART0 optional
  CONFIG_ESP_BROWNOUT_DET=y
  CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7=y    # ~2.7 V [I]
  CONFIG_PARTITION_TABLE_CUSTOM=y
  ```
- **Do not select quad PSRAM.** N16R8 PSRAM is octal only [C, FIRMWARE_NOTES §13].
- **Suggested partition table** (16 MB) [G]:

  | Partition | Size |
  |---|---|
  | nvs | 24 KB |
  | otadata | 8 KB |
  | phy_init | 4 KB |
  | **ota_0** | 4 MB |
  | **ota_1** | 4 MB |
  | **model** (data, raw; INT8 weights) | 4 MB |
  | **storage** (LittleFS/FAT; config and logs) | about 3.9 MB |
  | coredump | 64 KB |

  Bulk sample logging goes to the external 64 MB W25Q512 (§3.2), with wear levelling and CRC-protected atomic records (FIRMWARE_NOTES §5).

## 7. Sampling and timing limits
| Channel | Part | Rate to use (from earlier notes/profile) | Limit [I] |
|---|---|---|---|
| ECG + resp | ADS1292R | 500 sps (PAT windows) | 125 sps–8 ksps |
| Main PPG | AFE4900 | 200 Hz; PRF ≥ 500 Hz in PAT windows, inside the LED budget | Programmable PRF |
| Satellite PPG | MAX86141 | 200 Hz | Up to several kHz; use FIFO + INT |
| EDA / BioZ / sweat | AD5940 | Per measurement sequence | Internal HFOSC |
| IMU | LSM6DSV80X | 52 Hz (motion gating, **required** for alarms) | Up to kHz ODR |
| Skin temperature | TMP117 ×2 (+ tail) | 1 Hz burst; ≥1 s period | Self-heating |
| IR temperature | MLX90632 | 1 Hz | 0.5–64 Hz refresh |
| RH / temperature | SHT45 | 1 Hz | — |
| Environment | BME280 | 0.5 Hz | — |
| Spectral | AS7341 | 2 Hz, dark frame per sequence | 6-channel ADC plus mux, so frames are not simultaneous |
| Contact | FSR (IO1 ADC) | 20 Hz | — |
| Gauge | MAX17048 | 0.2 Hz | — |
| Mic | IM69D130 | Optional | PDM |

- **Timestamps:** stamp DRDY, ADC_RDY, INT and GPIO0 edges in the ISR, not at FIFO pop. A single esp_timer clock domain is used for PAT (about 1 ms resolution target).
- **The I²C bus total must fit in 100 kHz.** Budget the transactions; do not poll faster than these rates.

## 8. RF / antenna
- **[C]** On-board PCB antenna with an all-layer keep-out. The antenna end overhangs the north edge.
- Firmware cannot fix detuning. Leave the antenna configuration at its defaults.
- **[I]** Wi-Fi TX peaks of about 350–500 mA hit the +3V3 rail (TPS63802), so avoid TX bursts while the LED boost is active, and space bursts out. Prefer BLE for streaming.

## 9. Bring-up self-tests the firmware must provide (a console command each, plus one "run all")
1. **Rails and boot:**
   - reset reason, eFuse/strap readback (VDD_SPI = 3.3 V, flash 16 MB, PSRAM 8 MB octal detected);
   - PSRAM march test (≥1 MB);
   - internal flash CRC.
2. **I²C scan at 100 kHz:** expect 0x20, 0x36, 0x39, 0x3A, 0x44, 0x48, 0x49, 0x52, 0x6A and 0x76 (0x4A optional). Read the WHO_AM_I / ID registers: TMP117 0x0F, LSM6 0x0F, BME280 0xD0, AS7341, MLX90632, SHT45 serial, MAX17048 VERSION, RV-3028. Report missing and unexpected addresses.
3. **Expander:** walk P0, P1, P2, P3, P4 and P7 one at a time and read back. Check that P5/P6 track USB plug and unplug.
4. **SPI, per device:** read the ID or a known register: ADS1292R ID, AFE4900 register readback, AD5940 CHIPID, W25Q512 JEDEC ID (EF 40 20) [I], MAX86141 PART_ID. Do this at 1 MHz, then at the operating clock. Also run the **MISO contention test**: with all CS high, MISO must float (read with the internal pull-up and pull-down in turn).
5. **Interrupt lines:** trigger each one (ADS DRDY, AFE ADC_RDY, MAX INT, AD5940 GPIO0, IMU INT1, TMP117 ALERT, GAUGE_ALRT, EXP_INT) and measure the latency.
6. **Power:**
   - TX_5V on/off with one LED chain at minimum current, never two;
   - MAX17048 VCELL/SOC versus a DMM;
   - charger state machine with USB in and out;
   - CHG_DIS test.
7. **USB:** native CDC enumeration, plus a reconnect after deep sleep.
8. **Deep-sleep current test mode:** everything in its lowest state, as in §4.4. The target is about 110 µA board total, measured at J2.
9. **Optical:** one chain at a time; check the photodiode response, the AS7341 dark/light frames, and the NIR730 via AD5940 GPIO2.
10. **ECG/BioZ:** ADS1292R internal test signal, lead-off detection, AD5940 RCAL measurement.
11. **Mic:** PDM capture of 1 s, then compute RMS.
12. **FSR:** raw ADC at rest and pressed.

## 10. Firmware MUST NOT
- Configure GPIO35/36/37, or drive GPIO45/46/47/48 or GPIO26–32.
- Pull IO45 high, or burn the VDD_SPI, JTAG or USB-disable eFuses.
- Run I²C above **100 kHz**, or MAX86141 SPI above **8 MHz**. Keep all the other SPI devices at or below the §3.2 values until they are validated on the bench.
- Assert two SPI chip-selects at once.
- Fire LED chains at the same time, exceed the ≤ 1 % duty for 400 mA-class pulses, or leave TX_5V on outside LED windows.
- Run the TX_5V boost or Wi-Fi TX below the low-battery threshold, or keep discharging below the cut-off (§4.2).
- Drive GPIO19/20 as GPIO, or disable the native USB recovery path.
- Make medical claims in the UI or logs ("sepsis detected" and similar). Use wording like "early-warning score trend" plus a research-prototype disclaimer.
- Change the RF or antenna settings to work around range problems without range-test data.

## 11. Acceptance criteria for firmware v2
1. Builds reproducibly for esp32s3 with the §6 configuration. The boot log shows 16 MB flash, 8 MB octal PSRAM and VDD_SPI 3.3 V.
2. `hw_v3.yaml` matches §2/§3 exactly (I²C 100 kHz, correct part numbers, addresses, CS and INT pins). D1–D9 are resolved or documented.
3. The full §9 self-test suite passes on a bench board, with a pass/fail report over USB CDC.
4. The LED scheduler can be shown never to overlap chains: trace TX_5V current on a scope with all channels running.
5. Low-battery cut-off is demonstrated with a bench supply on J2: it ramps down, the alert fires, the device shuts down safely, and it does not boot-loop.
6. Deep-sleep current with everything held as in §4.4 is ≤ 150 µA board total [G target], measured.
7. Continuous sensing for 24 h with Wi-Fi/BLE bursts runs with no watchdog resets and no I²C/SPI error-rate growth. Timestamps are monotonic, and the DRDY/ADC_RDY jitter is reported.
8. Charging works on a 500 mA host without USB brown-out (logs show VBUS_DET/CHG state stable).
9. Recovery: the device can always be re-flashed via native USB with BOOT+RESET, and via J8.
10. No medical or compliance claims appear in any output.

## 12. Open questions for hardware/owner
- **Q1 (D9):** Is the 402535 cell protected (has a PCM)? Until this is answered, firmware enforces the cut-off.
- **Q2 (D5):** Are U24 (RTC), U25 (mic) and J12 (FFC) to be assembled? The BOM says yes; the older profile says DNP.
- **Q3:** AFE4900 I2C_SPI_SEL is tied high. Confirm that this selects SPI.
- **Q4:** RTC_INT is not wired to the ESP, so the RTC cannot wake it. Is polling acceptable?
- **Q5:** SPARE_IN (IO42) is floating, because the VBAT divider is DNP. Is it intentionally unused?
- **Q6:** The CP2102N is fitted but not connected over USB. Keep it (UART console via R104/R105 rework), or make it DNP in the next spin?
