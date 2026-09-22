# 01 — Spec Audit (Step 1)

Audit of the VitalQ agent-handoff specification. Each item is tagged
**GAP** (missing requirement), **CONFLICT** (internal contradiction / tension),
**ASSUMPTION** (technically questionable, needs evidence), **DEP** (external dependency),
**UNKNOWN** (cannot be resolved without research or hardware decisions).

## 1. Missing requirements (GAP)

| # | Item | Why it matters | Proposed resolution |
|---|------|----------------|---------------------|
| G1 | **Subject model** | §23/§31 require per-person baselines and subject-level splits, but no `subject` entity exists. Device ≠ wearer. | Add `subjects` (pseudonymised), `subject_consents`, `session.subject_id`. See `04-database-schema.md`. |
| G2 | **Per-modality sampling rates** | The spec never states target rates. DSP design is impossible without them. | Set defaults in config: PPG 200 Hz, motion 52 Hz, spectral ~2 Hz, temp 1 Hz, env 0.2–1 Hz, contact 20 Hz. Tune on hardware. |
| G3 | **Clock model / sync accuracy target** | "Accurate timestamps" is qualitative. PPG-vs-motion fusion needs ms-level agreement; ESP32 RTC drifts (~20–150 ppm uncalibrated). | Explicit clock discipline: NTP sync + monotonic `esp_timer` + device→server offset estimation. See `05-api-spec.md` §clock-sync and `06-processing-pipeline.md`. |
| G4 | **Transport protocol + packet format** | "Wi-Fi" is named, but not HTTP vs MQTT, JSON vs binary, batch size, compression. | `/v1/ingest/batch` over HTTPS with CBOR/JSON envelope and per-sample time offsets. See `05-api-spec.md`. |
| G5 | **Store-and-forward / offline buffer spec** | "Recover from temporary network failures" has no sizing or format. | Ring buffer in flash (or SD on V0), chunked replay with `batch_id` idempotency keys. |
| G6 | **OTA / firmware update path** | `firmware_version` implies updates ship; no mechanism specified. | ESP-IDF/Arduino OTA partition scheme; even manual serial OTA is acceptable for prototype — must be a decision. |
| G7 | **Body site + optical geometry** | Wrist vs finger vs chest changes PPG amplitude ~10×, reflectance SpO₂ feasibility, and AS7341 illumination needs. | Open question to hardware; software treats `body_site` and `optical_geometry` as session/device metadata. |
| G8 | **Power budget** | Drives sampling duty cycle, Wi-Fi burst strategy, BME680 heater use (mA-scale draw). | Config field `power_profile`; nothing in software assumes mains power. |
| G9 | **Label/ground-truth strategy** | §20 Level-4 risk modelling presumes labels that will never exist unless deliberately collected (clinical events, illness diary, reference thermometer readings). | Add `labels`/`events` table with provenance (`self_report`, `reference_device`, `clinical`). Only then is supervised modelling honest. |
| G10 | **Retention & storage budget** | 3-channel PPG @200 Hz float32 ≈ 17 MB/hour ≈ 400 MB/day per device — Supabase free tier is 500 MB database / 1 GB storage. | Waveform windows as compressed arrays (see schema), retention policy per `data_class`, cold export to storage objects. |
| G11 | **Threat model** | §35 lists security tools but no adversary model (device spoofing, replay, leaked API key, malicious payload). | §13 API + §35 mapped onto an explicit STRIDE-lite list in `05-api-spec.md` §security. |
| G12 | **Dashboard latency/liveness target** | "Live data" is undefined (true streaming vs 5 s poll). | Define two modes: live poll (2–5 s) and review (historical). Websockets optional, not required. |
| G13 | **Ethics/consent for human data** | Physiological data is GDPR Art. 9 special-category. Even self-experimentation needs records if others ever wear it. | `subject_consents` table + `data_class` separation + anonymisation option in schema. |
| G14 | **Environment/light-sealing requirement for spectral channel** | AS7341 has no emitter — reflectance-mode requires a controlled light source and mechanical light sealing, else ambient leakage dominates. | Flagged to hardware; software stores `illumination_state` + dark frames for subtraction. |

## 2. Contradictions and tensions (CONFLICT)

| # | Items in tension | Resolution |
|---|------------------|------------|
| C1 | "Preserve raw data" vs row-per-measurement schema | Row-per-sample is impractical for 200 Hz waveforms (both storage and write-rate). **Store high-rate signals as compressed per-window arrays** in `signal_windows`; scalar channels stay row-per-sample. Both remain "raw". |
| C2 | AS7341 "synchronised measurements" vs hardware reality | AS7341 has 6 parallel ADC channels + a mux for the rest → channels land in ≥2 sequential read groups. **Do not pretend simultaneity**: record `read_group` and per-group timestamps; synchronisation happens in the pipeline. |
| C3 | I²C bus sharing | On one bus: MAX30102 `0x57`, AS7341 `0x39`, MLX90632 `0x3B`, MPU6050 `0x68`, BME280 `0x76/0x77`. No clash today, but MLX90632's fixed address blocks a second IR thermometer; MLX90637's software-definable address is more flexible. MAX86141 is SPI → separate bus. **Bus plan is a config item, not an assumption.** |
| C4 | SpO₂ claims vs hardware | MAX30102 red/IR reflectance on the wrist with no per-device calibration does not produce SpO₂ — the standard `110 − 25·R` is a population approximation for transmissive finger probes. **Store `R` (ratio-of-ratios) as the honest measurement; "SpO₂ estimate" is a derived, uncalibrated feature flagged accordingly.** |
| C5 | MPU6050 inclusion | MPU6050 is NRND/obsolete (TDK recommends ICM-42670-P). Including an obsolete part in a custom PCB design contradicts the "designed to last" intent. Keep `motion` abstract; recommend ICM-42670-P-class part to hardware. |
| C6 | FSR402 as measurement *and* quality channel | FSR402 is an uncalibrated, drifting, hysteretic analog divider output — fine as an **ordinal contact indicator**; it must never be treated as a pressure in units. Schema uses unitless `contact_level` + quality contribution. |
| C7 | "Quantum-enhanced" framing vs prototype physics | No quantum hardware exists or is planned; and wearable PPG is rarely shot-noise-limited (perfusion/motion/ambient dominate). The simulation must include the loss/competing-noise terms that make the advantage vanish — which is the scientifically valuable result. See `08-quantum-simulation.md`. |
| C8 | BME680 gas channel | Gas resistance is a non-selective metal-oxide VOC proxy with >30 % device-to-device spread without calibration (IEEE TIM 2024 validation vs GC-MS). Treat as optional context/confound only; never a physiological signal. Doc already says don't depend on it — schema makes it `nullable` and optional. |

## 3. Questionable assumptions needing evidence (ASSUMPTION)

| # | Assumption | Assessment |
|---|-----------|------------|
| A1 | AS7341 skin reflectance carries physiological signal | Plausible as *exploratory* channel: NIRS/StO₂ and skin-mottling literature show microcirculatory oxygenation is optically measurable in sepsis cohorts (InSpectra StO₂ + VOT studies; hyperspectral imaging studies of mottling). But AS7341 is an ambient/color sensor — needs controlled emitter + sealing + contact geometry. Frame as "diffuse reflectance, exploratory", never as biochemistry. |
| A2 | Wrist reflectance SpO₂ | Commercially solved only with heavy calibration + motion gating. Prototype honesty: compute R and PI, report `spo2_estimate_uncalibrated`. |
| A3 | Synthetic data realism | Synthetic PPG must be validated against real-device statistics (PPG-DaLiA/WESAD amplitude/noise distributions), else the pipeline learns the simulator. Add a validation step comparing synthetic vs real feature distributions. |
| A4 | "Multimodal beats unimodal" | This is a *hypothesis*, explicitly tested by the ablation study in `07-ml-experiment-plan.md` — the design already treats it as falsifiable. Good. |
| A5 | Raman toy model (1650 cm⁻¹, shift 20·agg, noise √s·e⁻ʳ) | The noise form is physically consistent *only if* it is std-dev and shot-noise-limited (√N shot noise; ideal squeezing cuts quadrature variance by e⁻²ʳ → std by e⁻ʳ). Missing: detection loss (η), ambient/electronic noise floor, and the fact that 1650 cm⁻¹ amide-I Raman is a different regime than wearable reflectance. Keep as clearly-labelled illustrative sim. See `08-quantum-simulation.md`. |

## 4. External dependencies (DEP)

| # | Dependency | Risk |
|---|-----------|------|
| D1 | Hardware finalisation (sensor picks, body site, optical geometry, LED emitters for reflectance) | Blocks firmware pin/bus map and spectral processing. Mitigation: config-driven registry. |
| D2 | Supabase project + Postgres version | **TimescaleDB extension is deprecated on Supabase for Postgres 17** — do not build on hypertables; use declarative range partitioning (+`pg_partman` if wanted). Mitigation already in schema doc. |
| D3 | PhysioNet credentialed access (MIMIC waveform subsets) | Needed for clinical-grade PPG with labels; takes days to obtain. PPG-DaLiA / WESAD / BUT-PPG are unrestricted — start there. |
| D4 | Wi-Fi credentials / local network | Device-side config + secrets provisioning (never in firmware repo). |

## 5. Open questions returned to the project owner (UNKNOWN)

1. Body site and mechanical design (wrist strap? fingertip clip? chest?) — single biggest DSP determinant.
2. Is an illumination source part of the AS7341 spectral channel, or ambient-only?
3. Expected deployment context: bench rig, self-worn, or multi-subject study? (Determines consent/ethics load.)
4. Battery target: hours continuous, or duty-cycled days?
5. ESP32 variant (S3 vs C6) — affects available ADCs/PSRAM for buffering.
6. Is a phone-gateway (BLE→phone→cloud) acceptable, or must it be Wi-Fi-direct-to-cloud?

## 6. What the spec gets right (kept)

- Sensor-agnostic, config-driven architecture — endorsed, implemented via `hardware_config` registry.
- RAW→CLEANED→FEATURES→MODEL separation — enforced structurally in schema (separate tables, raw is append-only).
- "Classical baselines before deep learning", subject-level splits, honesty clauses — adopted as the evaluation protocol in `07-ml-experiment-plan.md`.
- Quality-as-first-class-signal — implemented as per-window SQI records joined into fusion.
- Synthetic data labelling — `data_class` column is `NOT NULL` on every measurement table; mixing without that flag is a schema violation.
