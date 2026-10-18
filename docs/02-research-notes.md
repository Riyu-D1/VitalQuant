# 02 — Research Notes (Step 2)

Datasheet-grounded sensor facts, processing literature anchors, dataset survey, and the
quantum-model audit. Sources cited inline. These notes constrain every downstream doc.

## A. Sensors

### Optical / cardiovascular

**MAX30102 (current V0 part)** — integrated module: red (660 nm) + IR (880 nm) LEDs and
photodiode in a 5.6×3.3 mm package; I²C fixed addr `0x57`; internal 32-sample FIFO;
programmable sample rate (up to ~3.2 ksps) and LED current; sub-mW HR operation.
Source: Analog Devices product page/datasheet.
*Implication:* simplest path to a working PPG channel; but fixed optics (cannot change
LED/PD geometry or add wavelengths).

**MAX86141 (custom-PCB candidate)** — optical AFE, not a module: 3 programmable LED
drivers (up to 6 LEDs via external 3×2:1 mux; master–slave pair drives up to 12), **2
simultaneous optical readout channels**, 19-bit ADC, ambient-light cancellation (ALC),
"picket-fence" LED-failure detect-and-replace, 128-word FIFO, **SPI** interface.
Source: Analog Devices datasheet DS 19-100187 rev.5.
*Implications:* (a) supports the custom-optics plan (external LEDs + PDs + geometry);
(b) two independent PD channels enable two wavelengths *simultaneously* — better than
time-muxed LEDs for reflectance SpO₂ under motion; (c) SPI + different register model →
firmware driver ≠ MAX30102 driver; abstraction must cover both.

**Abstraction derived:** `OpticalFrontend { channels[], wavelength_nm[], led_current[],
sample_rate, ambient_cancellation, fifo }` — capabilities queried from config, not code.

### Spectral — AS7341 / AS7341-DLGM

From the ams OSRAM datasheet (DS000504): **11 channels** —
`F1 415 nm/26`, `F2 445/30`, `F3 480/36`, `F4 515/39`, `F5 555/39`, `F6 590/40`,
`F7 630/50`, `F8 680/52` (centre/FWHM nm), plus `CLEAR` (unfiltered Si), `NIR` (~910 nm),
and a `FD` flicker-detection channel (50/60 Hz flags, external calculation up to 2 kHz).
Only **6 channels are digitised in parallel**; the remainder go through an internal mux —
so a "frame" is at least two sequential read groups (SMUX-configured). Gain `AGAIN`
0.5×–512× and integration `ATIME/ASTEP` are per-measurement config.
**AS7341-DLGM is a reel/delivery variant of the same die** (500-pc 7″ reel vs DLGT 13″)
— identical register map; no firmware difference.
*Critical:* the sensor has **no light source**. Skin reflectance requires an external
emitter (the ams reflection EVK uses a 45° LED + adapter + contact). The spectral channel
is therefore *illumination-defined*: record `illumination_state` and capture dark frames.

**Scientific grounding:** thenar NIRS StO₂ + vascular-occlusion testing shows sepsis/septic
coagulopathy alters microvascular oxygen extraction (e.g. InSpectra-model studies,
*Microvascular Research* 2026; hyperspectral skin-oxygenation imaging in septic patients,
*Annals of Intensive Care* 2019 — mottling is literally a chromophore pattern in skin
microcirculation). This is a legitimate anchor for an *exploratory* diffuse-reflectance
channel — at visible/NIR wavelengths the AS7341 covers — while the spec's prohibition on
claiming biomarkers remains binding.

### Temperature — MLX90632 / MLX90637 (replacing TO-can MLX90614)

**MLX90632** — 3×3×1 mm QFN SMD, I²C, factory-calibrated, 50° FOV; medical grade
±0.2 °C in the body-temperature range (object −20…100 °C); ambient sensor on-chip.
**MLX90637** — 3×3 mm SFN-5, I²C, 0.02 °C resolution, sleep <2.5 µA, 50° FOV,
ambient accuracy ±0.5 °C (0–60 °C), software-definable I²C address, post-calibration
option, automotive-qualified diagnostics.
Sources: Melexis product pages + datasheets.
*Implications:* both are non-contact radiometric sensors — they read skin-surface IR,
which is not core temperature; ambient-temperature compensation and contact state are
mandatory context. MLX90637's configurable address helps if a second IR sensor is added.
`IrTempSensor { object_c, ambient_c, refresh_hz }` abstraction; MLX90614 remains a
V0-driver only.

### Environmental — BME280 / BME680

BME280: temp/humidity/pressure, I²C `0x76/0x77` or SPI, forced-mode low power.
BME680 adds a heated metal-oxide **gas resistance** channel: non-selective VOC response,
heater costs mA-scale bursts and self-heats the board; published validation vs TD-GC-MS
shows >30 % device-to-device variability (reducible to ~5–7 % only with two-point
calibration); meaningful IAQ numbers require Bosch's proprietary BSEC library.
*Decision:* `EnvironmentalSensor { t, rh, p, gas_resistance? }`; gas stays optional
context for confound analysis, as the spec already requires. Default recommendation:
**BME280** for V1 (power, simplicity), BME680 supported by config.

### Motion — MPU6050 (V0) → replacement advised

MPU6050 is **NRND/obsolete**; TDK's recommended alternate is **ICM-42670-P**
(6-axis IMU, low power, I²C/SPI, on-chip features). LSM6DS3/BMI270 are alternatives.
Keep `MotionSensor { accel_g[3], gyro_dps[3] }` abstract; driver per part.

### Contact — FSR402

Analog force-sensitive resistor in a divider → ADC. Non-linear, hysteretic, drifts,
unit-to-unit spread wide. Suitable only as an **ordinal contact level**
(off / light / seated / excessive bands tuned per enclosure) that gates optical quality.
No SI units claimed.

### Compute — ESP32

Family choice open (S3 most capable for buffering/OTA; C6 cheaper). Firmware needs:
`esp_timer` µs monotonic stamps + SNTP discipline + offset/drift model; per-sensor
FreeRTOS tasks feeding a tagged ring buffer; flash/SD store-and-forward; chunked HTTPS
batch upload. Dual-core partitioning keeps sampling off the Wi-Fi core.
*Note:* Wi-Fi coexistence introduces ms-level jitter — timestamps must be taken in the
sensor task, never on transmit.

## B. Signal processing anchors

| Channel | Chain (with sources) |
|---|---|
| PPG | Detrend → bandpass ~0.5–4 Hz (HR) / 0.4–8 Hz (PRV) → systolic-peak detection → IBIs → HR, PRV metrics (reported as PRV, not HRV, since wrist PPG inter-beat intervals ≠ ECG RR). Motion handling via ACC-referenced adaptive filtering or spectral subtraction — surveyed in *Detection and Removal of Motion Artifacts in PPG Signals* (Springer MONET 2019). |
| PPG SQI | Rule/statistical indices — skewness, kurtosis, perfusion index (AC/DC), beat-template correlation, spectral purity — surveyed in *A Survey of PPG and iPPG Quality Assessment Methods* (Appl. Sci. 2022, doi:10.3390/app12199582) and Orphanidou's 2018 monograph. BUT-PPG supplies binary quality labels for training/validating a learnt SQI. |
| SpO₂ | Ratio-of-ratios `R = (AC_rms_red/DC_red)/(AC_rms_IR/DC_IR)`, then empirical `SpO₂ = a − bR` — the `110−25R` form is a population approximation for transmissive probes (TI SLAA655; Analog Devices SpO₂ guidelines). Path-length ratio varies between subjects (Sensors 2023, doi:10.3390/s23031434) → **per-device calibration is required before any %SpO₂ number is shown**; until then expose `R` and perfusion index as features. |
| Temperature | Object temp uses Melexis ambient-compensation math (datasheet + their GitHub driver); features = rolling mean/std, dT/dt, deviation from personal baseline, all quality-gated by contact state. |
| Spectral | Dark-frame subtraction (LED off) → emitter-intensity normalisation (CLEAR/reference channel or measured drive current) → per-channel normalised reflectance → pairwise ratios + temporal deltas → features. Geometry + ambient-leakage flags mandatory. |
| Motion→artifact | ACC magnitude/jerk + spectral energy inside the PPG band → artifact score joined to PPG SQI; prevents the "movement = illness" failure the spec calls out. |
| Contact | FSR level bands + PPG perfusion-index modulation → per-window `contact_quality` and downstream `sensor_confidence`. |

## C. Multimodal ML anchors

- **Clinical precedent:** continuous wearable HR/RR monitoring predicted deterioration in
  febrile ED patients (AUROC ≈ 0.86, ~5–9 h earlier than manual vitals — *J. Clin. Med.* /
  PMC9504566); Corsano CardioWatch programme for ward monitoring (*Critical Care* 2025
  editorial on early sepsis detection). NEWS/EWS scores are the clinical reference frame —
  useful as *context features*, not labels.
- **Fusion strategy:** windowed feature-level fusion first (concatenate per-modality
  features + per-modality SQI + missingness indicators); gated/masked fusion so a missing
  sensor degrades gracefully; deep encoders deferred until data justifies them (spec §21–22).
- **Personal baseline:** robust rolling statistics (median/MAD z-scores), isolation-forest
  or state-space residual anomaly scores computed **within subject** — no labels needed,
  matches spec Level 3.
- **Evaluation:** subject-disjoint splits for any generalisation claim; within-subject
  temporal splits for personal-baseline work; report calibration + abstention-by-SQI, not
  accuracy alone.

## D. Public datasets (pre-hardware bootstrap)

| Dataset | Contents | Match to VitalQ | Access |
|---|---|---|---|
| **PPG-DaLiA** (UCI/PhysioNet) | 15 subjects, wrist Empatica E4: BVP 64 Hz, EDA, skin temp 4 Hz, ACC 32 Hz + chest RespiBAN ECG truth; daily-life activities | **Best match** — wrist PPG + motion + skin temp with ECG ground truth | Public |
| **WESAD** | 15 subjects, E4 (BVP/EDA/TEMP/ACC) + labels (baseline/stress/amusement) | Personal-deviation + multimodal fusion exercises | Public |
| **BUT PPG** (PhysioNet) | 3,888 × 10 s PPG + ECG ref HR + ACC + **quality labels** + SpO₂/BP/glycaemia annotations | Train/validate SQI and HR pipeline | Public (PhysioNet) |
| **MIMIC-III-Ext-PPG / MIMIC-IV waveform** | 6.3 M × 30 s ICU PPG segments, rhythm annotations, ECG/ABP/RESP | Clinical-grade PPG; later sepsis-adjacent cohort work | Credentialed PhysioNet |
| **PulseDB** | Large PPG/ECG/ABP for cuffless BP | Optional BP-related research | Public |
| **Capnobase / BIDMC** | ICU PPG + RR + SpO₂ reference | Respiration-rate-from-PPG methods | Public |
| **WildPPG** | Multi-site real-world PPG incl. altitude | Artifact/realism validation | Public |

Caveat recorded: none share VitalQ's exact sensors/geometry — use for method development,
document the domain shift, never report cross-dataset claims as device performance.

## E. Quantum-simulation audit (spec §25)

Given parameters: 1600–1700 cm⁻¹ window, reference peak 1650 cm⁻¹, shift `20×agg`, width 8,
amplitude 1000, `r = 0.8`, SNR threshold 3, noise `√signal × e⁻ʳ`.

- **What is physically consistent:** for coherent light, photon shot noise std ∝ √N
  (variance ∝ N). Ideal squeezed-vacuum injection reduces the measured quadrature
  *variance* by e⁻²ʳ → std by e⁻ʳ — so `√s·e⁻ʳ` is the right *shape* if the term is a
  std-dev and the system is shot-noise-limited. `r = 0.8` → std ×0.45 (variance ×0.20,
  ≈ 7 dB). For scale: GEO600 runs ≈6–10 dB effective squeezing incl. losses; lab
  demonstrations ≈10–15 dB (Zander et al., *Quantum Sci. Technol.* 2022 — 10 dB in a
  Mach–Zehnder, squeeze factor β = e^{2r}).
- **What is missing (placeholders to flag):**
  1. **Detection loss** — loss mixes in vacuum noise: measured variance
     `η·e⁻²ʳ + (1−η)`; η = 0.8 already halves the ideal advantage.
  2. **Non-quantum noise floor** — ambient leakage, tissue/perfusion variation,
     electronic/ADC noise are independent additive terms; when they dominate, squeezing
     does nothing. The wearable regime is almost never shot-noise-limited → this is the
     honest headline.
  3. **Squeezing angle/quadrature choice** — amplitude vs phase squeezing;
     intensity measurements need amplitude squeezing.
  4. **Regime mismatch** — 1650 cm⁻¹ (amide-I Raman region) vs VitalQ's actual
     visible/NIR reflectance bands; keep the Raman sim as a separate, labelled toy model.
- **Design consequence:** the sim is a *model hierarchy* (`NoiseModel` interface:
  `ShotNoise → SqueezedShotNoise(r) → LossySqueezed(r, η) → AmbientDominated(...)`) with an
  experiment matrix that explicitly finds where the advantage vanishes. See
  `08-quantum-simulation.md`.

## F. Platform notes

- **Supabase:** TimescaleDB extension is **deprecated/unavailable on Postgres 17** projects
  (Supabase docs: migrate to native partitioning / `pg_partman`). Schema therefore uses
  declarative `PARTITION BY RANGE (time)` — portable and version-proof.
- **2018 MBP dev constraint:** no local Postgres required (Supabase-hosted); ML deps
  modularised (`pip install vitalq[ml]`); no large pretrained models; CPU-only PyTorch if
  ever needed.
