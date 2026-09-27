# Passives

Every resistor and capacitor, the pin it serves, and the datasheet
section that set the value. 0402 unless the row says 0603. Manufacturer
part numbers are in `vitalq_hw_v1_bom.csv`.

This is a research prototype. The 49.9 kΩ electrode resistors limit a
short to the 3.3 V rail to about 66 µA. That is not an IEC 60601
patient-leakage claim.

## Power

| Ref | Value | From | To | Why |
| --- | --- | --- | --- | --- |
| C1 | 4.7 µF | VBUS | GND | MCP73831 input bypass, DS20001984 Fig 2-1 |
| C2 | 4.7 µF | VBAT | GND | MCP73831 battery bypass, same figure |
| R3 | 10 kΩ | PROG | GND | MCP73831-2, IREG = 1000 V / RPROG = 100 mA |
| R49 | 10 kΩ | +3V3 | CHG_STAT | STAT is open drain. Pull-up so the expander can read it |
| C3 | 1 µF | VBAT | GND | Local VBAT bypass kept from the previous rail |
| C4 | 1 µF | +3V3 | GND | Local 3.3 V bypass |
| C44 | 10 µF 0603 | VBAT | GND | TPS63802 CIN. SLVSEU9 Table 10-2, GRM188R61A106ME69, 10 V. 0603 because that table is 0603 |
| C45 | 22 µF 0603 | +3V3 | GND | TPS63802 COUT. Same table, GRM188R61A226ME15. 22 µF X5R is not a reliable 0402 |
| L1 | 0.47 µH | L1 pin | L2 pin | DFE201612E-R47M. Smallest inductor in Table 10-2 with Isat 5.5 A |
| R23 | 560 kΩ | +3V3 | FB | TPS63802 FB divider. VOUT = 0.5 × (1 + R23/R24) = 3.30 V |
| R24 | 100 kΩ | FB | GND | R2 in the datasheet must not exceed 100 kΩ |
| C6 | 1 µF | +1V8 | GND | TPS7A2018 output bypass. SBVS338 recommended 1 µF |
| R50 | 100 kΩ | VBUS | VBUS_DET | Divider top |
| R51 | 200 kΩ | VBUS_DET | GND | 5 V × 200/300 = 3.33 V into the expander |
| C58 | 100 nF | VBAT | GND | MAX17048 VDD bypass, 19-4688 typical application |
| C46 | 2.2 µF | VBAT | GND | TPS61240 CIN. SLVSAR3 typical application, 0402 is allowed at 2.2 µF |
| C47 | 4.7 µF 0603 | TX_5V | GND | TPS61240 COUT. Datasheet specifies 0603 |
| L2 | 1.0 µH | LX | TX_5V | DFE201612E-1R0M. Isat covers the 600 mA switch limit |
| R25 | 100 kΩ | TX5_EN | GND | Holds the boost off until the expander drives EN |
| C7 | 10 µF | +3V3 | GND | ESP32 module bulk, Espressif hardware design guideline |
| C8 | 100 nF | +3V3 | GND | ESP32 module high-frequency bypass |
| C9 | 4.7 µF | VDD_CP2102 | GND | CP2102N VDD bypass. Not the 3.3 V rail |
| C10 | 1 µF | VBUS | GND | CP2102N VREGIN bypass |
| R1 | 5.1 kΩ | USB CC1 | GND | USB Type-C Rd |
| R2 | 5.1 kΩ | USB CC2 | GND | USB Type-C Rd |
| R5 | 10 kΩ | +3V3 | ESP_EN | ESP32 EN pull-up |
| R6 | 10 kΩ | +3V3 | ESP_IO0 | GPIO0 strap pull-up |
| R7 | 10 kΩ | +3V3 | CS_AD5940 | GPIO15 must be high at reset |
| R52 | 10 kΩ | +3V3 | CS_FLASH | Keeps the flash deselected while GPIO26 is an input |
| R53 | 10 kΩ | +3V3 | EXP_INT | TCA6408 /INT is open drain |
| R8 | 10 kΩ | FSR_ADC | GND | FSR402 divider bottom. Top of the FSR is +3V3 |
| C59 | 100 nF | +3V3 | GND | W25Q512 VCC bypass |
| C60 | 100 nF | +3V3 | GND | TCA6408 VCC bypass |

## AFE4900

| Ref | Value | From | To | Why |
| --- | --- | --- | --- | --- |
| C11 | 1 µF | +3V3 | GND | RX_SUP bypass. Public short-form shows 1 µF on RX_SUP |
| C13 | 100 nF | +3V3 | GND | IO_SUP bypass |
| C14 | 1 µF | TX_5V | GND | Local TX_SUP capacitor. TX_SUP is the TPS61240 5 V rail, not VBAT |
| C15 | 100 nF | AFE_BG | GND | Bandgap bypass |
| R9 | 10 kΩ | RESETZ | GND | Holds reset until expander P0 drives it. Was a pull-up |
| R10 | 1 kΩ | AFE_CLK | GND | CLK low selects the internal oscillator |
| C56 | 100 nF | AFE_P_AC | AFE_INP | AC couple, AFE ECG positive. Value is not in the public short-form |
| C57 | 100 nF | AFE_N_AC | AFE_INM | AC couple, AFE ECG negative |
| R43 | 10 MΩ | +3V3 | AFE_INP | Bias, IC side of the coupling cap |
| R44 | 10 MΩ | AFE_INP | GND | Bias return |
| R45 | 10 MΩ | +3V3 | AFE_INM | Bias |
| R46 | 10 MΩ | AFE_INM | GND | Bias return |

## ADS1292R

SBAS502C Fig 68 is the respiration network. Its note is only "Patient
and input protection circuitry not shown." Section 8.3.10.4 does not
set CFILTER. Channel 1 is respiration. Channel 2 is ECG. The inputs are
not tied together. C34 is 47 nF because Fig 73 and Fig 74 note (1), on
the PGA1 capacitor, says: "When using the ADS1292R and the channel 1
respiration function, this capacitor must be 47 nF." The PGA section
also says a 4.7 nF capacitor is recommended for respiration. This board
follows the "must" note. C35, on PGA2, stays 4.7 nF.

| Ref | Value | From | To | Why |
| --- | --- | --- | --- | --- |
| C43 | 1 µF | +3V3 (AVDD) | GND | SBAS502 §11.1 AVDD bulk |
| C28 | 100 nF | +3V3 | GND | AVDD high-frequency |
| C29 | 100 nF | +3V3 | GND | DVDD high-frequency |
| C30 | 10 µF | VREFP | GND | VREFP reservoir, §11.1 |
| C32 | 1 µF | VCAP1 | GND | VCAP1, §11.1 |
| C33 | 1 µF | VCAP2 | GND | VCAP2, §11.1 |
| C34 | 47 nF | PGA1N | PGA1P | PGA1 filter. Fig 73 note (1): must be 47 nF with channel-1 respiration |
| C35 | 4.7 nF | PGA2N | PGA2P | PGA2 filter |
| R14 | 10 kΩ | PWDN | GND | Held in power-down until expander P1 |
| R15 | 1 MΩ | +3V3 | RLDREF | RLD reference divider, Fig 68 |
| R16 | 1 MΩ | RLDREF | GND | RLD reference divider |
| R17 | 1 MΩ | RLDOUT | RLDINV | RLD integrator, Fig 68 |
| R26 | 10 MΩ | +3V3 | IN1P | Fig 68 bias |
| R27 | 10 MΩ | IN1P | GND | Fig 68 bias |
| R28 | 10 MΩ | +3V3 | IN1N | Fig 68 bias |
| R29 | 10 MΩ | IN1N | GND | Fig 68 bias |
| C48 | 2.2 nF | IN1P | GND | Fig 68 |
| C49 | 2.2 nF | IN1N | GND | Fig 68 |
| C50 | 100 nF | ECG_P | IN1P | Fig 68, respiration sense from the electrode node |
| C51 | 100 nF | ECG_N | IN1N | Fig 68 |
| C52 | 2.2 nF | ECG_P | GND | Fig 68 |
| C53 | 2.2 nF | ECG_N | GND | Fig 68 |
| R30 | 40.2 kΩ | RESP_MODP | ECG_P | Fig 68 modulation |
| R31 | 40.2 kΩ | RESP_MODN | ECG_N | Fig 68 modulation |

## AD5940

Fig 54 / AN-1557. R13 remains the RCAL resistor. It is not reused as RLIMIT.

| Ref | Value | From | To | Why |
| --- | --- | --- | --- | --- |
| C16 | 1 µF | +3V3 | GND | AVDD |
| C18 | 100 nF | +3V3 | GND | DVDD |
| C42 | 1 µF | AVDD_REG | GND | AVDD_REG, datasheet decoupling |
| C19 | 1 µF | IOVDD | GND | After R12 |
| C20 | 4.7 µF | VREF_1V82 | GND | 1.82 V reference |
| C21 | 470 nF | VREF_2V5 | GND | 2.5 V reference |
| C22 | 470 nF | VBIAS_CAP | GND | VBIAS |
| C23 | 470 nF | DVDD_REG | GND | DVDD_REG |
| C24 | 100 nF | VZERO0 | GND | VZERO |
| C25 | 100 nF | VBIAS0 | GND | VBIAS0 |
| C26 | 1 µF | AIN4_LPF0 | GND | Low-pass pin |
| R12 | 10 Ω | +3V3 | IOVDD | IOVDD series resistor, datasheet |
| R11 | 10 kΩ | RESET | GND | Held in reset until expander P2 |
| R13 | 1 kΩ | RCAL0 | RCAL1 | RCAL. AN-1557 |
| C27 | 100 nF | RC0_0 | RC0_1 | RC0 filter |
| R39 | 1 kΩ | CE0 | CE_ISO | RLIMIT, Fig 54. 1 kΩ |
| C54 | 15 nF | CE_ISO | EDA_CE_PAD | CISO1, Fig 54 |
| C55 | 470 nF | SE0 | SE_ISO | CISO2, Fig 54 |
| R40 | 1 kΩ | SE_ISO | EDA_SE_PAD | SE series. Same 1 kΩ as the electrode resistor, not a second RLIMIT |
| R41 | 1 kΩ | RE0 | EDA_RE_PAD | RE series |
| R42 | 1 kΩ | DE0 | EDA_DE_PAD | DE series |

A second large series resistor on CE0 would sit on top of RLIMIT and
change the excitation. The EDA lines therefore use 1 kΩ, and the ECG
lines use 49.9 kΩ.

## Electrode protection

TPD1E10B06, ILEAK 100 nA max, VRWM 5.5 V. The diode is at the pad, not
across the 10 MΩ bias. 100 nA across 10 MΩ would be 1 V.

| Ref | Value | Line |
| --- | --- | --- |
| R32 | 49.9 kΩ | ECG1 pad to ADS1292R IN2P |
| R33 | 49.9 kΩ | ECG2 pad to ADS1292R IN2N |
| R34 | 49.9 kΩ | RLD pad to ADS1292R RLDOUT |
| R35 | 49.9 kΩ | AFE ECG+ pad to the AC coupling cap |
| R36 | 49.9 kΩ | AFE ECG− pad to the AC coupling cap |
| D1–D5 | TPD1E10B06 | ECG1, ECG2, RLD, AFE+, AFE− pads to GND |
| D6–D9 | TPD1E10B06 | EDA CE, SE, RE, DE pads to GND |

## I2C and optical

| Ref | Value | From | To | Why |
| --- | --- | --- | --- | --- |
| R18 | 200 Ω | +3V3 | VREF2 | PCA9306 EN/VREF2, TI application circuit |
| R19 | 4.7 kΩ | +3V3 | I2C_SDA | 3.3 V pull-up |
| R20 | 4.7 kΩ | +3V3 | I2C_SCL | 3.3 V pull-up |
| R21 | 4.7 kΩ | +1V8 | I2C_SDA_1V8 | 1.8 V pull-up |
| R22 | 4.7 kΩ | +1V8 | I2C_SCL_1V8 | 1.8 V pull-up |
| C36 | 100 nF | +1V8 | GND | AS7341 VDD |
| R47 | 100 Ω | +3V3 | SFH 4053 anode | Sets about 17 mA at Vf 1.6 V. Cathode is the MOSFET drain |
| R48 | 100 kΩ | IR_GATE | GND | LED off until expander P4 |
| C37 | 100 nF | +3V3 | GND | BME280 |
| C38 | 100 nF | +3V3 | GND | LSM6DSV80X |
| C39 | 100 nF | +3V3 | GND | MLX90632 VDD |
| C40 | 100 nF | +3V3 | GND | TMP117 U11, inside the thermal island |
| C41 | 100 nF | +3V3 | GND | spare sensor bypass, placed with the 1.8 V group |
| C61 | 100 nF | +3V3 | GND | TMP117 U20, inside the thermal island, top side |

D10, the Nichia white LED, has no series resistor. Its anode is +3V3 and
its cathode is the AS7341 LDR current sink. LDR absolute maximum is 3.6 V,
so the anode is not VBAT.
