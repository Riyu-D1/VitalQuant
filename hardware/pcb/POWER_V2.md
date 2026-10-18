# hw_v2 power tree

The hw_v1 power tree is kept whole (spec: "BQ25170 + MAX17048 +
TPS63802 power tree: KEPT — no PMIC swap"). This file describes the
tree and an estimated current budget per rail for the hw_v2 load set.
Numbers taken from the hw_v1 docs are quoted as-is; everything else is
a typical-datasheet figure marked "(est.)" or "# VERIFY" — the board
has not been measured, and the MAX86178 public datasheet is a
short-form under NDA with no pin descriptions.

## Tree

```
USB-C 5 V (VBUS)
  ├─ BQ25170 (U2) charger ── VBAT ── LiPo on J2 (must have 10 kΩ NTC + PCM)
  │                            ├─ MAX17048 (U17) gauge
  │                            ├─ MAX86178 LED_DRV_SUP (ball F3) ── LED drivers
  │                            └─ R93 0 Ω ── VBAT_SYS
  │                                 ├─ TPS63802 (U3) buck-boost ── +3V3
  │                                 │      ├─ R94 ── +3V3_ESP ── ESP32-S3-MINI-1U
  │                                 │      ├─ R95 ── +3V3_ANA ── AFE4900, AD5940,
  │                                 │      │                     ADS1292R, MAX86178 AVDD
  │                                 │      │                     (ball F1, NDA VERIFY)
  │                                 │      ├─ digital loads (below)
  │                                 │      └─ TPS7A2018 (U4) ── +1V8_LDO ── R97 ── +1V8
  │                                 │              (AS7341, PCA9306 VREF1, 1.8 V pull-ups)
  │                                 └─ TPS61240 (U15) boost ── TX_5V_RAW ── R96 ── TX_5V
  │                                        (AFE4900 TX_SUP + SFH 7072 anodes
  │                                         + R114 0 Ω ── LED_AN → J10 satellite
  │                                         anodes, moved off VBAT for Vf
  │                                         headroom)
  ├─ CP2102N VREGIN ── VDD_CP2102 (internal ~3.45 V reg; UART side only
  │                     in the default strap — see PINMAP "USB and console path")
  └─ VBUS_DET divider R50/R51 → expander P6
```

There is no power-path diode or switch. The cell and charger share
VBAT. Charging starts from USB with a flat cell; the 3.3 V rail still
needs the cell — the board does not run the S3 from USB alone. The
TS window is hardware (BQ25170 sources ~38 µA into the 10 kΩ NTC);
firmware stops charge only by driving expander P7 (CHG_DIS) high
through Q2.

## Regulators

| Part | Rail | Set point / capability | Quiescent |
| --- | --- | --- | --- |
| BQ25170DSGR (U2) | VBAT | R3 1.5 kΩ → 200 mA fast charge, 40 mA precharge, 20 mA termination; R75 27.0 kΩ → 4.20 V (SLUSDJ8A, hw_v1 values). IN range 3.0–6.6 V | TS bias ~38 µA while charging; BAT-side leakage (est. ~1 µA class, verify SLUSDJ8A) |
| TPS63802 (U3) | +3V3 | R23/R24 = 560 kΩ/100 kΩ → 3.30 V; L1 0.47 µH, C44/C45 0603s per datasheet Table 10-2. Buck-boost, so +3V3 holds over the whole cell range; output capability is well above the ~0.5 A peak budget below (verify datasheet curves vs VIN) | (est. ~11 µA, verify SLVSEU9) |
| TPS61240 (U15) | TX_5V | Fixed 5 V (4.9–5.1 V); L2 1.0 µH, C46/C47. SLVS806D recommended output 200 mA, switch limit ~600 mA. EN = expander P3, held off by R25 | (est. ~30 µA operating, ~1 µA shutdown, verify SLVSAR3) |
| TPS7A2018 (U4) | +1V8 | 300 mA LDO from +3V3, EN tied to IN. Bleed R66 301 kΩ exists because it cannot sink the PCA9306 bias (SCPS113O §8.1.7) | (est. ~10 µA, verify SBVS338) |
| CP2102N internal | VDD_CP2102 | ~3.45 V from VREGIN = VBUS. Zero drain on the cell; off whenever USB is unplugged | (est. ~9.5 mA enumerated, suspend ~100–200 µA, from USB not the cell) |

## Current budget per rail (estimates unless quoted)

### VBAT_SYS (from the cell, through R93)

Feeds U3 and U15 only. Its draw is the +3V3 and TX_5V budgets divided
by efficiency — roughly 85–95 % for the buck-boost and ~80–90 % for the
boost over the cell range (est.). MAX17048 sits on VBAT before R93
(~23 µA operating, est.) and the BQ25170 TS bias is ~38 µA only while
charging. The VBUS_DET divider is ~17 µA at 5 V and zero unplugged.
New for hw_v2, also upstream of R93 on VBAT itself: the MAX86178
LED_DRV_SUP pin (ball F3) — the satellite LED driver supply draws
straight from the cell. The J10 LED_AN shared-anode rail, by
contrast, was moved **off** VBAT to TX_5V through R114: emitter Vf
(~3 V class) plus driver compliance had no headroom on a 3.0–4.2 V
rail — resolved by the rail change.

### +3V3 (TPS63802 output)

| Load | Typ (est.) | Peak | Note |
| --- | --- | --- | --- |
| ESP32-S3-MINI-1U (via R94, +3V3_ESP) | ~30–90 mA Wi-Fi active avg; modem-sleep far lower | ~350 mA class RF TX peaks — verify vs module datasheet | Deep-sleep module level ~10 µA class (est.). USB/JTAG active adds a few mA (est.) |
| W25Q512 | standby ~10–20 µA | ~10–25 mA program/erase (est.) | CS_FLASH on GPIO16 |
| TCA6408A | ~10 µA (est.) | — | open-drain /INT to GPIO3 |
| TMP117 ×2 (U11, U20) | ~3.5 µA each @ 1 Hz (datasheet class) | ~150 µA conversion (est.) | |
| TMP117/TMP119 on J9 tail | ~3.5 µA @ 1 Hz | ~150 µA conversion (est.) | plus tail-wire capacitance on SDA/SCL |
| SHT45-AD1B | ~0.4 µA idle, ~2 µA avg @ 1 meas/s (est.) | ~1 mA measuring (est.) | new, skin face |
| BME280 | ~3.6 µA @ 1 Hz forced (est.) | ~0.7 mA (est.) | |
| LSM6DSV80X | ~20 µA low-power to ~1 mA high-perf accel+gyro (est.) | — | |
| MLX90632 VDD | ~1 mA measuring (est.) | ~1.4 mA (est.) | VDD is 3.3 V even though I2C is 1.8 V |
| RV-3028-C7 RTC (DNP) | ~0.05–1 µA timekeeping (est., verify) | — | VBACKUP reaches VSS through R115 10 kΩ (DNP with U24) — resolved |
| IM69D130 mic (DNP) | ~0.7–1.6 mA active (est.) | — | PDM on GPIO40/41, footprint only |
| D10 white LED (AS7341 LDR) | 0 / per-frame | ≤ 40 mA firmware cap | abs max 100 mA; register can reach 258 mA — cap stands |
| D11 SFH 4053 860 nm | 0 | ~15–20 mA via R47 (hw_v1 number) | Q1 gate = expander P4 |
| D12 730 nm (new) | 0 | ~15–20 mA via R109 (100 Ω 0402, D11/R47 pattern) | Q4 gate = AD5940 GPIO2/E1 (NIR730_GATE) |
| D25 status LED | ~1–3 mA when on (est.) | — | GPIO17 |
| Pull-ups / misc. | < 1 mA | — | |
| +1V8_LDO input to U4 | the +1V8 column below | — | LDO input adds it here |

Rough continuous service total: **~50–120 mA** with Wi-Fi on and one
optical acquisition running (est., dominated by the module). RF TX
peaks push toward **~0.5 A** (est.) — inside the buck-boost's
capability; the 22 µF C45 plus local module bulk (C7) carry the edges.

### +3V3_ANA (via R95)

| Load | Typ (est.) | Peak | Note |
| --- | --- | --- | --- |
| AFE4900 RX | ~1–3 mA (est., verify vs short-form) | — | TX_SUP is TX_5V, not this rail |
| AD5940 | ~1–5 mA measuring (est.) | — | hibernate ~6.5 µA class (est.) |
| ADS1292R | ~1 mA AVDD+DVDD (est.) | — | |
| MAX86178 | ~1–5 mA ECG+PPG+BioZ active (est.) | # VERIFY | AVDD sits here (ball F1), DVDD on +3V3 (F2); LED_DRV_SUP is on VBAT (F3) — see the VBAT_SYS section. Ball refs and the possible internal ~1.1–1.8 V core are # VERIFY vs the NDA datasheet |

### TX_5V (via R96, only when expander P3 is high)

| Load | Typ | Peak | Note |
| --- | --- | --- | --- |
| AFE4900 LED path (SFH 7072) | pulsed | **≤ 150 mA firmware cap** (hw_v1 number) | recommended rail output is 200 mA (SLVS806D) |
| AFE4900 TX_SUP idle | (est. < 1 mA) | — | |

**Peak rule:** the AFE4900 LED path plus the J10 satellite anode
rail ride this boost — LED_AN was moved to TX_5V through R114 (the
emitter-Vf headroom fix); the MAX86178 LED-driver supply itself
stays on VBAT (LED_DRV_SUP, ball F3). The AFE4900 150 mA firmware cap
plus the satellite-anode envelope decide the
TX_5V envelope against the 200 mA recommended output (L2 Isat is
~600 mA-class, DFE201612E-1R0M). The driver-side budget stays in the
VBAT_SYS section. D10/D11/D12 are on +3V3, not on this rail;
nothing else joins TX_5V.

### +1V8 (via R97, TPS7A2018 300 mA)

AS7341 VDD (~0.2–0.3 mA active, est.), PCA9306 VREF1 bias ~4.5 µA plus
the R66 301 kΩ bleed ~6 µA (hw_v1 numbers), and the 1.8 V I2C pull-ups
(few µA while clocking). Total **< 1 mA** — far under the LDO's 300 mA.

## Battery-life posture — duty-cycle profile needed, not computed

An always-on sepsis watch cannot run the full AFE set and Wi-Fi
continuously from a small LiPo; the budget above is a peak/continuous
envelope, not a life estimate. Battery life needs a duty-cycle profile
decided at the system level — described here, not computed:

- **Dominant terms:** LED pulse energy (TX_5V slots × current × duty —
  AFE4900 LEDs plus the J10 satellite anodes — and the MAX86178
  LED-driver draw on VBAT) and radio on-time (S3
  Wi-Fi is ~100× the sensor standby draw). The MAX86178 adds a third
  synchronized channel — its driver slots draw from VBAT, its anodes
  from TX_5V.
- **Profile shape:** a low-rate always-on loop (IMU + skin temp +
  periodic ECG/PPG/BioZ windows into the AFEs' FIFOs, S3 in
  modem/light sleep) with Wi-Fi burst upload; LED duty and radio duty
  are the two knobs that move the answer, so the numbers above are
  left per-load for whoever owns that profile.
- **Floor:** with P3 low, the sleep-state draw is the S3 deep-sleep
  module current (~10 µA class, est.) plus sensor sleep currents and
  the ~50 µA of always-on pull-ups/regulator quiescents — ~60–80 µA
  class (est.). MAX17048 and the RTC (if fitted) are inside that.
  CP2102 draws nothing without USB.

## Open VERIFY items (power)

- MAX86178 supply pin names and rail voltages as drawn (AVDD +3V3_ANA,
  DVDD +3V3, LED_DRV_SUP VBAT, possible internal ~1.1–1.8 V core) —
  ball map is NDA-datasheet dependent; # VERIFY vs the released pin
  table.
- The J10 LED_AN anode rail moved to TX_5V through R114 — resolved
  for emitter-Vf plus driver-compliance headroom (it had been drawn
  on VBAT). The MAX86178 LED-driver supply itself stays on VBAT
  (LED_DRV_SUP, ball F3); its driver-side compliance on the cell
  rail remains # VERIFY vs the NDA datasheet.
- D12 emitter choice/value confirmation — R109 is 100 Ω per the
  D11/R47 pattern; confirm current class vs the selected 730 nm part.
- RTC backup-supply arrangement: resolved — the RV-3028-C7 datasheet
  wants an unused VBACKUP to reach VSS through 10 kΩ (R115, DNP with
  U24), not a direct GND short. No backup cell is specified.
- S3 RF-peak and sleep currents vs the module datasheet before any
  battery-life number is published.
