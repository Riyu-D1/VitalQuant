# Passives

Every resistor and capacitor, the pin it serves, and the datasheet
section that set the value. 0402 unless the row says 0603. Manufacturer
part numbers are in `vitalq_hw_v1_bom.csv`.

This is a research prototype. The electrode network is not an IEC 60601
patient-leakage or defibrillator-proof claim.

## Power

| Ref | Value | From | To | Why |
| --- | --- | --- | --- | --- |
| C1 | 4.7 µF | VBUS | GND | BQ25170 CIN. SLUSDJ8A asks for at least 1 µF; 4.7 µF was already on VBUS |
| C2 | 4.7 µF | VBAT | GND | BQ25170 COUT. Same minimum; 4.7 µF was already on VBAT |
| R3 | 3.0 kΩ | CHG_ISET | GND | SLUSDJ8A: KISET / RISET, KISET = 300 AΩ. 3.0 kΩ is 100 mA (spec 90–110 mA at OUT = 3.8 V) |
| R75 | 27.0 kΩ | CHG_VSET | GND | SLUSDJ8A Table 7-1: 27 kΩ sets a 1-cell Li-Ion to 4.20 V |
| R59 | 100 kΩ | CHG_DIS | GND | Holds Q2 off. Charge stays enabled until firmware drives P7 high |
| R60 | 10 kΩ | +3V3 | CHG_PG | BQ25170 /PG is open drain. GPIO27 reads it |
| R74 | 10 kΩ | +3V3 | GAUGE_ALRT | MAX17048 ALRT is open drain. GPIO25 reads it |
| R61 | 10 kΩ NTC, DNP | TS | GND | On-board NCU15XH103F6SRC. Fit this or the cell NTC, not both. Not an ADC divider |
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

## Charge temperature cut-off

U2 is a BQ25170DSGR. It replaces the MCP73831T-2ACI/OT. The MCP73831
has no TS pin, so a flat cell with the old default-off FET never
started a charge: the expander that drove PROG needed 3.3 V, and 3.3 V
needed the cell. The BQ25170 charges whenever IN is valid and TS is
inside its hardware window. No firmware is in that path.

SLUSDJ8A: IN operating range is 3.0–6.6 V (USB is inside that; IN abs
max is 30 V). Fast charge is KISET / RISET with KISET = 300 AΩ typical
(270–330). R3 = 3.0 kΩ sets 100 mA. Precharge is 20% of that (20 mA)
and termination is 10% (10 mA). R75 = 27.0 kΩ sets 4.20 V (Table 7-1,
1-cell Li-Ion). STAT is open-drain, low while charging. /PG is
open-drain and low when input power is good.

The TS pin sources about 38 µA (36.5–39.5 µA). A 10 kΩ NTC from TS to
GND is the cold/hot window. The datasheet's example is a Semitec
103AT-2. Cold is about 1.04 V (~0 °C) and hot is about 188 mV (~45 °C)
for that beta. VTS_ENZ is 50 mV typical (40–60 mV); pulling TS below
that disables charge. Do not hang an ESP32 ADC on TS. The ADC is not
high-Z and it would steal the 38 µA bias. GPIO39 is open.

R61 is Murata NCU15XH103F6SRC, 0402, 10 kΩ ±1% at 25 °C, B25/50 ≈ 3380 K.
That is close to the 103AT-2 (B ≈ 3435 K) and not the same part, so the
window can shift by a few degrees. It is DNP. Fit R61 or the cell NTC
on J2 pin 3, not both. Both at once halves the resistance.

Q2 stays. It is a CSD13380F3. Gate is CHG_DIS, source is GND, drain is
TS. R59 holds the gate low, so Q2 is off at power-up and charge is
enabled. Firmware can drive TCA6408 P7 high to short TS below VTS_ENZ
and stop charge. That polarity is the opposite of the old CHG_EN net
(high used to mean charge on). VGS is 0 V or 3.3 V; absolute maximum
is 8 V. The body diode is reverse-biased while TS is positive and the
source is GND.

Dissipation at 100 mA from 5 V into a cell near 4.2 V is about
(5 − 4.2) × 0.1 = 80 mW, not 0.18 W. At the start of fast charge, with
VIN at 5.25 V and the cell near the 2.8 V precharge handoff, it can
reach about (5.25 − 2.8) × 0.1 = 0.25 W. U2 sits on the bottom next to
the LiPo pads, about 35 mm from the two TMP117s.

## MCU, USB, and straps

| Ref | Value | From | To | Why |
| --- | --- | --- | --- | --- |
| C7 | 10 µF | +3V3 | GND | ESP32 module bulk, Espressif hardware design guideline |
| C8 | 100 nF | +3V3 | GND | ESP32 module high-frequency bypass |
| C9 | 4.7 µF | VDD_CP2102 | GND | CP2102N VDD bulk. Not the 3.3 V rail |
| C62 | 100 nF | VDD_CP2102 | GND | CP2102N Rev 1.5: 100 nF beside the 4.7 µF on VDD |
| C10 | 1 µF | VBUS | GND | Extra VREGIN bypass |
| C63 | 100 nF | VBUS | GND | CP2102N Rev 1.5: 100 nF on VREGIN. C1 is the 4.7 µF |
| R1 | 5.1 kΩ | USB CC1 | GND | USB Type-C Rd |
| R2 | 5.1 kΩ | USB CC2 | GND | USB Type-C Rd |
| R5 | 10 kΩ | +3V3 | ESP_EN | ESP32 EN pull-up. WROOM-32E: 10 kΩ with 1 µF |
| C64 | 1 µF | ESP_EN | GND | Espressif EN delay. DevKitC uses 0.1 µF; this board follows the module guide |
| R70 | 10 kΩ | CP_DTR | Q3 base 1 | Auto-program, EN transistor. Espressif DevKitC cross-couple |
| R71 | 10 kΩ | CP_RTS | Q3 base 2 | Auto-program, GPIO0 transistor |
| R72 | 1 kΩ | ESP_TX | CP_RX | Series on UART TX so a powered CP2102 cannot back-feed the ESP32 |
| R73 | 1 kΩ | CP_TX | ESP_RX | Series on UART RX, same reason |
| R6 | 10 kΩ | +3V3 | ESP_IO0 | GPIO0 strap pull-up |
| R7 | 10 kΩ | +3V3 | CS_AD5940 | GPIO15 must be high at reset |
| R52 | 10 kΩ | +3V3 | CS_FLASH | Keeps the flash deselected while GPIO26 is an input |
| R53 | 10 kΩ | +3V3 | EXP_INT | TCA6408 /INT is open drain |
| R62 | 1 kΩ | VDD_CP2102 | CP_RST | CP2102N Rev 1.5: RSTb pull-up, required in all cases |
| R63 | 22.1 kΩ | VBUS | CP_VBUS | CP2102N Fig 2.5 divider, top |
| R64 | 47.5 kΩ | CP_VBUS | GND | Fig 2.5 divider, bottom. Pin sees 3.41 V at 5.0 V |
| R65 | 10 kΩ | +3V3 | TMP117_ALERT | SNOSD82D: ALERT is open-drain and needs a pull-up |
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
| R44 | 100 kΩ | AFE_P_AC | AFE_P_SER | TIDUDO6B board schematic, series between the clamp and the input |
| R46 | 100 kΩ | AFE_N_AC | AFE_N_SER | Same, negative input |
| C56 | 100 nF | AFE_P_SER | AFE_INP | AC couple. Kept; see the note under this table |
| C57 | 100 nF | AFE_N_SER | AFE_INM | AC couple, negative input |
| R43 | 5.11 MΩ | RLDOUT | AFE_INP | TIDUDO6B Fig 2-10 bias from the body-drive node |
| R45 | 5.11 MΩ | RLDOUT | AFE_INM | Same, negative input |

TIDUDO6B (TIDA-01580 Rev B) Fig 2-10 biases each three-electrode input
from RLD_OUT through 5.11 MΩ, and the guide text says those electrodes
are DC-coupled. Fig 2-9, the two-electrode case, uses a 10 MΩ divider
to midsupply and AC coupling. The board schematic in that guide also
shows 100 kΩ in series with each ECG input. Two things on this board
differ from Fig 2-10, on purpose:

- AFE4900 RLD_OUT (ball C2) stays open. ADS1292R already drives the one
  RLD pad. A second RLD amplifier on that electrode would fight it.
  R43 and R45 take their bias from ADS RLDOUT, which is the same
  body-bias node Fig 2-10 calls RLD_OUT.
- The 100 nF coupling stays. Dry chest electrodes carry a DC offset.
  Fig 2-9 uses AC coupling, and the guide tells you to keep the input
  impedance high for dry electrodes. The high-pass corner with 100 nF
  and 5.11 MΩ is about 0.31 Hz.

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
| R17 | 1 MΩ | RLDOUT | RLDINV | RLD integrator, Fig 35 / Fig 68 |
| C65 | 1.5 nF | RLDOUT | RLDINV | Across R17. Fig 35, Fig 36, and Fig 40 (CEXT). The figures call it a typical value |
| C66 | 1 µF | RLDREF | GND | Bypass on the R15/R16 divider. SBAS502C does not give this value; see the note below |
| R26 | 10 MΩ | +3V3 | IN1P | Fig 68 bias |
| R27 | 10 MΩ | IN1P | GND | Fig 68 bias |
| R28 | 10 MΩ | +3V3 | IN1N | Fig 68 bias |
| R29 | 10 MΩ | IN1N | GND | Fig 68 bias |
| C48 | 2.2 nF | IN1P | GND | Fig 68 |
| C49 | 2.2 nF | IN1N | GND | Fig 68 |
| C50 | 100 nF | ECG_P | IN1P_AC | Fig 68, respiration sense from the electrode node |
| R67 | 10 kΩ | IN1P_AC | IN1P | After the coupling cap, so defibrillator current does not use the ADS input diodes |
| C51 | 100 nF | ECG_N | IN1N_AC | Fig 68 |
| R68 | 10 kΩ | IN1N_AC | IN1N | Same, negative input |
| R69 | 10 kΩ | RLD_CLAMP | RLDOUT | Between the TPD node and the RLD pin. SBAS502C input limit is ±100 mA momentary |
| C52 | 2.2 nF | ECG_P | GND | Fig 68 |
| C53 | 2.2 nF | ECG_N | GND | Fig 68 |
| R30 | 40.2 kΩ | RESP_MODP | ECG_P | Fig 68 modulation |
| R31 | 40.2 kΩ | RESP_MODN | ECG_N | Fig 68 modulation |

The earlier "1.2%" note was wrong about the measured baseline, and it
is withdrawn. Equation 10 only sets the modulation current:
(VREFP − AVSS) / (R30 + R32), and the same on the negative side. The
PGA still measures the voltage between IN1P and IN1N, which includes
the drop across both 51 kΩ surge resistors and the body. That baseline
is about 51 kΩ + Zbody + 51 kΩ. With a low body impedance it is near
102 kΩ. SBAS502C §6.5 lists the respiration impedance range as
2000–10,000 Ω at IRESP = 30 µA. 102 kΩ is outside that range. The
51 kΩ parts were already in this loop before the DPCR swap; changing
49.9 kΩ to 51 kΩ did not create the problem and does not fix it.

R67 and R68 are on the IC side of C50 and C51. They are not in the
respiration current loop. R30 and R31 still land on ECG_P and ECG_N.
C34 stays 47 nF (Fig 73/74 note 1). C35 stays 4.7 nF. The respiration
topology is unchanged on purpose. Options and a recommendation are in
`VERIFICATION.md`. Do not treat the present loop as a working
respiration measurement.

C65, 1.5 nF across R17, is the capacitor Fig 35 and Fig 36 draw, and
Fig 40 labels CEXT = 1.5 nF with REXT = 1 MΩ. The figure note says
those are typical values for an example. C66 is 1 µF from RLDREF to
GND. SBAS502C Fig 35, 36, 40, 68, and 73 do not specify that bypass.
The R15/R16 divider is 1 MΩ / 1 MΩ, so the Thevenin resistance is
500 kΩ. 1 µF puts the corner near 0.32 Hz, under the ECG band.
100 nF would sit near 3.2 Hz, inside it. 1 µF is an engineering choice,
not a figure callout.

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
| R76 | 51 kΩ, 2512 | EDA_CE_PAD | CE_SURGE | First part on the CE pad. DPCR, same family as R32–R36 |
| C54 | 15 nF | CE_SURGE | CE_ISO | CISO1, Fig 54. Now behind R76, not across the pad |
| R39 | 1 kΩ | CE_ISO | CE0 | RLIMIT, Fig 54 / AN-1557. Still 1 kΩ |
| R77 | 51 kΩ, 2512 | EDA_SE_PAD | SE_SURGE | First part on the SE pad |
| R40 | 1 kΩ | SE_SURGE | SE_ISO | SE series, kept at 1 kΩ |
| C55 | 470 nF | SE_ISO | SE0 | CISO2, Fig 54. Behind R77 and R40 |
| R78 | 51 kΩ, 2512 | EDA_RE_PAD | RE_SURGE | First part on the RE pad |
| R41 | 1 kΩ | RE_SURGE | RE0 | RE series |
| R79 | 51 kΩ, 2512 | EDA_DE_PAD | DE_SURGE | First part on the DE pad |
| R42 | 1 kΩ | DE_SURGE | DE0 | DE series |

R39 stays the AN-1557 RLIMIT of 1 kΩ. R76 is in front of it, on the
pad, so a defibrillator current is dropped in the pulse resistor
before it reaches C54 or the 1 kΩ. Skin impedance for this measurement
is roughly 20 kΩ to 10 MΩ. At a 0.5 V excitation, 51 kΩ plus 20 kΩ is
about 7 µA, and 51 kΩ plus 10 MΩ is about 50 nA. Both are inside what
the AD5940 can measure, and the 51 kΩ is a known series term to
calibrate out. A high-current EIS sweep is not: 100 µA through
51 kΩ + 20 kΩ needs about 7 V, and the AD5940 runs from 3.3 V.

C54 is a 50 V 0402 (GRM155R71H153KA12), behind R76. Nothing clamps the
pad itself, so a slow pulse can charge C54 toward the pad voltage.
50 V does not cover that. C55 is GRM155R61A474KE15, 10 V, on the clamp
side of R77 and R40. The TPD1E10B06 clamps at 10 V max for 1 A
(8/20 µs) and 14 V at 5 A, so 10 V on C55 is tight if the clamp is
driven hard. Neither cap was upsized.

## Electrode protection

Order on every patient line, from the skin toward the IC: electrode
pad, DPCR2512 51 kΩ, TPD1E10B06 to GND, then the second series resistor
into the IC pin. EDA uses the same pad-side 51 kΩ first (R76–R79),
then the existing 1 kΩ. There is no gas-discharge tube. The TPD is not
across the pad. A 5.5 V diode on the pad would take the defibrillator
current.

SBAS502C §6.1 limits current into any pin except the supplies to
±10 mA continuous and ±100 mA momentary. If the TPD node sits near
15 V and the rail is 3.3 V, 10 kΩ leaves about 1.2 mA, under that
momentary limit. The 10 kΩ parts are R67, R68, and R69. TIDUDO6B uses
100 kΩ on the AFE4900 ECG inputs (R44, R46); that is the value on
those two lines. ILEAK of the TPD1E10B06 is 100 nA max at VRWM 5.5 V.
On the ADS bias, 100 nA across 10 MΩ is 1 V, so the TPD stays on the
electrode side of R67/R68 and is not across R26–R29.

| Ref | Value | Line |
| --- | --- | --- |
| R32 | 51 kΩ, 2512 | ECG1 pad to the Fig 68 node ECG_P |
| R33 | 51 kΩ, 2512 | ECG2 pad to the Fig 68 node ECG_N |
| R34 | 51 kΩ, 2512 | RLD pad to RLD_CLAMP. R69 (10 kΩ) continues to RLDOUT |
| R35 | 51 kΩ, 2512 | AFE ECG+ pad to AFE_P_AC. R44 (100 kΩ) continues toward the pin |
| R36 | 51 kΩ, 2512 | AFE ECG− pad to AFE_N_AC. R46 (100 kΩ) continues toward the pin |
| R67 | 10 kΩ, 0402 | IN1P_AC to IN1P, after C50 |
| R68 | 10 kΩ, 0402 | IN1N_AC to IN1N, after C51 |
| R69 | 10 kΩ, 0402 | RLD_CLAMP to RLDOUT |
| R44, R46 | 100 kΩ, 0402 | AFE clamp node to the coupling cap |
| D1–D5 | TPD1E10B06 | ECG_P, ECG_N, RLD_CLAMP, AFE_P_AC, AFE_N_AC |
| D6–D9 | TPD1E10B06 | CE_ISO, SE_ISO, RE_SURGE, DE_SURGE |

R32–R36 are TT Electronics / Welwyn DPCR2512-51KJT18. The DPCR series
is 2512 only, which is the smallest package in that family. The
datasheet standard values include 51 kΩ ±5% (the nearest listed value
to 50 kΩ). Power is 1.5 W at 70 °C. Continuous limiting-element voltage
is 500 V. Dielectric withstand, coating to board, is also 500 V; that
is not the pulse rating. The defibrillation pulse test is 100 pulses,
5 kV peak, ΔR max 1%. ESD is 15 kV air / 8 kV contact. Body 6.5 × 3.2 mm,
termination gap 4.4 mm minimum. Ordering code follows the datasheet
example DPCR2512-20KJT18 with the 51 kΩ value. Future Electronics also
lists this family as tested to IEC 60601-2-27 at 5 kV peak; the rating
used here is the manufacturer's pulse test, not a system type test.
Vishay CRCW-HP, Panasonic ERJ-P, and Yageo HV were not used. Their
published working-voltage numbers are not a 5 kV millisecond defibrillator
pulse test. A Yageo HV2512's 3000 V working voltage applies only when
R is at or above the critical resistance, which 51 kΩ is not.

Creepage of about 4 mm was the target between electrode-side copper and
other copper on the same layer. It is met across each DPCR: the official
R_2512 land leaves about 4.7 mm of copper gap between its own pads.
Four 0.8 mm slots, one in each gap between the facing electrode pads,
make the path through the board 4.0 mm on a 1.6 mm stackup. It is not
met as a straight surface gap in these places:

- J5 pads are on a 1.70 mm pitch. Adjacent pad copper is about 0.55 mm apart. A slot needs about 1.4 mm of gap. Spreading J5 to that pitch, or to a 4 mm centre pitch, runs into the antenna keep-out or the skin cluster. The footprint was not changed.
- J6 pads are on a 2.20 mm pitch. Adjacent pad copper is about 1.05 mm apart, for the same reason.
- Adjacent DPCR electrode pads (R32–R36 and R76–R79) are about 3.10 mm apart. Five pads of 3.35 mm plus four 4 mm gaps do not fit in 36.5 mm once the antenna keep-out is reserved. The slots are the 4 mm path.
- R39–R42 and C54 are still 0402, behind the pad-side 51 kΩ. The gap across each 0402 is about 0.4–0.5 mm. The pulse voltage is meant to land on R76–R79.

## I2C and optical

| Ref | Value | From | To | Why |
| --- | --- | --- | --- | --- |
| R18 | 200 kΩ | +3V3 | VREF2 | PCA9306 EN tied to VREF2. SCPS113O §8.1.2 specifies 200 kΩ, not 200 Ω |
| R66 | 301 kΩ | +1V8 | GND | §8.1.7 bleed so the TPS7A2018 is not back-driven. See the note below |
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

SCPS113O §8.1.2 ties EN to VREF2 and pulls that node up to VCC2 through
200 kΩ. §8.1.8 allows a much smaller resistor if the only goal is FET
current, and 200 Ω would do that, but it is the wrong setup: the bias
into VREF1 becomes (3.3 − 2.4) / 200 Ω ≈ 4.5 mA, which a TPS7A2018
cannot sink. With 200 kΩ the bias is (3.3 − 2.4) / 200 kΩ = 4.5 µA.
§8.1.7: if the LDO cannot sink, VREF1 charges up to about VCC2 − Vth
≈ 2.7 V. AS7341 VDD absolute maximum is 2.2 V (DS000504). Equation 3
sets Rpulldown = 1.8 V / 4.5 µA = 400 kΩ. Equation 4 multiplies by 0.75,
which is 300 kΩ. R66 is 301 kΩ (RC0402FR-07301KL). The extra current
in the 1.8 V rail is about 6 µA.
