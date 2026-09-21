# 06 — Data-Processing Pipeline (Step 6)

Runs in `vitalq-processing` (batch worker). Every stage is a pure-ish function over
raw records → clean/quality/features rows, versioned by `pipeline_version` so
reprocessing never mutates history.

## 0. Canonical stage graph

```
raw.signal_windows / raw.measurements_scalar / raw.spectral_frames
   │
   ├─ S1 VALIDATE      ranges, rate consistency, duplicates, unit sanity
   ├─ S2 CLOCK-FIX     device_time_us → UTC via per-batch ClockModel; flag jumps
   ├─ S3 SYNC          resample to per-session analysis grid (union clock)
   ├─ S4 FILTER        per-modality chains (below)
   ├─ S5 ARTIFACT      motion/contact/ambient artifact scoring
   ├─ S6 QUALITY       per-window per-channel SQI ∈ [0,1]  → quality.channel_quality
   ├─ S7 FEATURES      windowed feature vectors            → features.windows
   └─ S8 FUSE/MODEL    multimodal assembly                 → ml.predictions
```

S5/S6 are not optional gates at the end — they are inputs the model consumes
(the spec's core anti-"movement = illness" requirement).

## 1. Synchronisation model (audit G3/C2)

- Analysis grid per session: PPG channels share their native grid; scalar/frame channels
  are carried **as-is with timestamps** into windowed aggregation (no fake upsampling).
- AS7341 read groups keep `read_group` + per-group timestamps; spectral "frames" join
  groups within a tolerance window (~50 ms) during processing — honesty about the mux.
- All joins happen on corrected UTC; unclocked data is flagged `clock.unreliable` and
  excluded from fusion, not silently re-timed.

## 2. Per-modality chains

### PPG (`ppg.red`, `ppg.ir`, future `ppg.*` per MAX86141 channel)
```
detrend (polyfit or HP) → bandpass 0.5–4 Hz (Butterworth 4, zero-phase off-line;
forward-backward causal variant for live) → systolic peaks (adaptive threshold +
refractory 250 ms) → IBI series →
  features: hr_bpm, ibi stats, PRV (rmssd/sdnn — labelled PRV), perfusion_index = AC/DC,
            pulse_amp, rise_time
quality: skew_sqi, kurt_sqi, template_corr, spectral_purity, clipped_frac
```
Sources: SQI survey (Appl. Sci. 2022), motion-artifact survey (Springer 2019);
peak/IBI conventions follow Elgendi/BUT-PPG practice.

### SpO₂-related (specced as feature, not measurement)
`r_ratio = (ac_rms_red/dc_red)/(ac_rms_ir/dc_ir)` per window →
`spo2_est_uncalibrated = 110 − 25·r` kept *named as uncalibrated* until a per-device
calibration exists (research §B). Perfusion index and R are always stored alongside.

### Spectral (AS7341)
```
dark-frame subtraction (nearest illumination='dark_frame' sample)
→ emitter/reference normalisation (divide by 'clear' or fitted emitter curve)
→ per-channel normalised reflectance f1..f8, nir
→ ratios (f5/f3, f8/f5, nir/clear …) + Δvs session baseline
→ features: channel vector, ratio set, slope stats, flicker flag
ambient_leakage flag when CLEAR-with-emitter-off ≫ 0
```
Research caveat enforced in code: features are exploratory; no biochem naming.

### Temperature (`temp.object`, `temp.ambient`, env temp)
```
validity range check → rolling mean/std (60 s, 5 min) → dT/dt → deviation vs subject
baseline (features.baselines) → features: temp_c, temp_slope, temp_var, baseline_dev
contact-gated: low contact_quality ⇒ temp features carry low weight downstream
```

### Motion (`motion.accel_*`, `motion.gyro_*`)
```
magnitude + jerk + band energy in PPG band (0.5–4 Hz) → motion_score →
used by S5 to modulate ppg SQI and by S7 as feature; also free-running activity class
(rest/moving) — deliberately simple until data justifies a learnt classifier
```

### Contact (`contact.level` from FSR)
```
quantised bands {off, light, seated, excessive} (per-enclosure thresholds in profile)
→ contact_quality ∈ [0,1] (seated=1, off/excessive→0)
→ fused with motion_score → quality.contact_state.sensor_confidence
```

### Environment (`env.*`)
Context channels only: used for confound features (ambient temp drift on skin temp,
humidity on spectral leakage) and for sensor-fault forensics. Never model inputs by
default — included via feature flag `use_env_features`.

## 3. Quality scoring — composition

`quality.channel_quality.sqi` per channel; `quality.contact_state.sensor_confidence` =
`f(contact_quality, 1 − motion_score)` feeds the fusion layer so a model sees
*confidence-weighted* features instead of garbage. Abstention: windows with
`sensor_confidence < τ` (default 0.4, config) produce features but are excluded from
model training and flagged on the dashboard.

## 4. Provenance & determinism

- Every stage reads from the previous layer's tables and writes with
  `pipeline_version = vitalq_processing.__version__` + `source_window_ids`.
- Reprocessing = insert rows under a new version → side-by-side comparisons are free.
- Worker is deterministic given inputs (seeded RNG in quality/feature code); a
  `pipeline run` is itself recorded (`meta.pipeline_runs` — optional polish, not in DDL yet).

## 5. Failure robustness (spec §28)

The corruption battery is a first-class test suite, not an afterthought:
`tests/corruption/` injects {missing windows, rate drift, NTP jumps, ADC clipping,
buffer-wrap duplicates, garbage frames, FSR disconnect} into synthetic ingest and asserts
that S1/S2/S6 detect and flag them. Pipeline must *fail loudly and mark data*, never
emit silently plausible features.

## 6. Performance notes for the laptop constraint

Windows are processed vectorised (NumPy/SciPy) in session batches; no per-sample Python
loops; feature extraction scales by `window_seconds` not by session length; heavy
reprocessing is chunked and resumable via `(session_id, window_start)` checkpointing.
