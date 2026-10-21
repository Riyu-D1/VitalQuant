# Round 7 — JLC assembly DFM review + single-fault analysis + firmware-note HW mitigations

## A. Hardware mitigations for the three firmware notes (per prompt)

### 1. U6 AFE4900 OSC_ENABLE (CLK tied to GND via R10 1 k)
- Status: `AFE_CLK` = U6.CLK + R10 1 k→GND only; CLK low = internal-oscillator mode per SBAS857.
- HW-safety assessment: a pull-down is exactly what the DS requires for OSC mode — already implemented (R10). Residual risk is FW writing OSC_ENABLE=0 while CLK floats low = AFE dead. **A pull-down is already present; nothing else can be made "hardware-safe"** — enabling an external-clock strap would need a clock source that doesn't exist on board. Verdict: correctly strapped; no change. Documented: firmware must keep OSC_ENABLE=1.

### 2. U24 RV-3028 CLKOE (CLKOUT pin 1 unconnected)
- Status: pin1 NC; CLKOE is a register bit (CLKOUT enabled only if FW sets it), output push-pull to VDD.
- HW option assessed: the DS-recommended way to kill CLKOUT is the CLKOE register bit OR leaving pin1 open — both already true. A pulldown on CLKOUT is not required (unused push-pull output can float; Micro Crystal app manual shows NC acceptable). Adding a strap is infeasible (pin inside RTC keepout corner). Verdict: leave NC + FW must keep CLKOE=0; documented.

### 3. U16 SFH7072 LED DC-limit note
- Status: 4 LED anodes on TX_5V, cathodes to U6 TX1–TX4 (AFE4900 sinks) and U22 LED*_K.
- HW option assessed: firmware already limits LED duty; a series resistor on TX_5V would cap worst-case current but adds voltage drop the AFE needs for compliance; per-LED series R would change calibrated optical output — rejected (harms function). The correct protection already exists in silicon (AFE LED-current DAC + current limiting) — FW limit stands, documented.

## B. JLC assembly DFM review
- **Tombstoning (0402 passives)**: sampled 0402s — pads are symmetric 0402-metric, both pads reach same-width copper; C63's new site: pad1→0.197 mm track to U2 pad, pad2→GND stub/via — thermal balance slightly asymmetric (pad2 near via) but via is 0.45 mm, acceptable; flagged as low risk.
- **Thermal-pad paste (U2 WSON-8, U8 VQFN-32, U26 UQFN-10, U3 VSON-HR)**: F_Paste gerber shows segmented EP windows; voiding risk typical for JLC — recommend 50–70% paste coverage, already segmented in footprint.
- **Fine-pitch bridging**: finest pitch = U26 0.4 mm UQFN, U6/U7/U22 WLP 0.4 mm ball pitch — JLC-capable; NSMD pads verified in footprint defs.
- **Component spacing/rework**: tightest cluster = U2 corner (TP18 2.4 mm, vias 1.9 mm) and U9 via ring (2.2 mm) — rework with hot air feasible but cramped; noted.
- **Polarity marks**: diode/LED refs have silk dots; verified present for D*/U16/U21.
- **Fiducials**: 6 FID marks, 3+ per side, corners — JLC requirement met.
- **Heights vs enclosure**: tallest parts (J1 USB-C, U1 module, battery pads) are within case_v8 verified clearances (136/136 checks passed again this round).

## C. Single-fault analysis (per IC class)

| IC | Rotated | Missing | Pin open | Pin bridged |
|----|---------|---------|----------|-------------|
| U1 ESP32 | asymmetric pinout → won't boot, no damage | board dead, safe | depends — worst: EN open → no boot | adjacent GND/GPIO → no rail damage |
| U2 BQ25170 | keyed asymmetric WSON → no charge, safe | no charging, system runs on battery | IN open → no charge; OUT open → battery disconnect (safe) | IN-GND bridge → VBUS short: **J1-side ESD/polyfuse absent — risk accepted** (USB port current-limited by host); OUT-VBAT bridge = same net, no effect |
| U3 TPS63802 | won't regulate | no +3V3 → board dead (safe state) | FB open → VOUT runs away: internal OVP caps at ~5.5 V, parts on +3V3 rated ≥5.5 V abs — **accepted** | VOUT-GND → overcurrent limit, safe |
| U4 TPS7A20 | dead 1V8 rail | 1V8 sensors off, safe | EN open → EN floats: may latch off, safe | OUT-IN bridge → 3.3 V on 1V8 rail: damages U9-side parts — mitigated by LDO SOA? **accepted risk, noted** |
| U5 CP2102N | no UART | dead console only | VDD open → dead | REGIN-VBUS bridge = same net |
| U6 AFE4900 | ball-mapped WLP can't rotate blind | no optical data, safe | LED sink open → dark channel | RX_SUP-GND → rail sag, recoverable |
| U7 AD5940 | asymmetric WLP | no EIS data | electrode open → lost channel | AVDD-GND → +3V3_ANA short: **shared rail risk** — local damage confined to AFE |
| U8 ADS1292R | asymmetric VQFN | no ECG | ECG input open → noise channel | AVDD-GND → rail short, confined |
| U9 PCA9306 | symmetric-ish: pin1 mark critical | 1V8 I2C dead, 3V3 side OK | VREF1 open → bus stuck | VREF1-VREF2 bridge → 1V8-3V3 contention through pass-gate; R18 200 k limits → safe |
| U15 TPS61240 | can't (WLP-6 keyed) | no TX_5V → LEDs off, safe | EN open → off | VOUT-GND → boost OCP, safe |
| U22 MAX86141 | keyed WLP | no PPG2 | PD open → dark | LED_SUP-GND → TX_5V short through boost OCP — safe |
| U26 SN74AXC2T245 | asymmetric UQFN | no SDO return | OE open → output float | VCCA-VCCB bridge → 1V8-3V3 short: R12 10Ω limits — **accepted** |

**Worst cascades identified**: (a) U4 OUT-IN bridge putting 3.3 V onto +1V8 rail — damages U9/U12/U10 domain ICs rated ≤3.6 V abs → they survive (≤3.6), so cascade contained. (b) Any +3V3/1V8 short via U26 — limited by R12. No skin-exposure cascade found: all patient-contact pins are electrode inputs with series R/C + rule areas — a single IC fault cannot place rail voltage on skin lands (verified: every electrode path has ≥1 series element).

## D. Residual accepted risks
VBUS has no dedicated polyfuse (host-side current limit relied on); documented, matches R5/R6 position.
