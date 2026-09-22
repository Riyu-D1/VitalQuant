# 09 — Staged Implementation Roadmap (Step 9)

Maps spec §40–44 milestones onto concrete engineering steps with entry/exit criteria.
Sequence is strict — each stage's exit criteria gate the next.

## M0 — Foundations (this package → repo scaffold)

- Create `vitalq` repo with the layout in doc 03; commit this docs package.
- `vitalq-core`: channel registry, units, `data_class`, hardware-profile schema +
  `config/hardware.example.yaml` loader, ClockModel.
- Supabase project provisioned; migrations from doc 04 applied; RLS on.
- CI: ruff + pytest + contract tests skeleton.
- **Exit:** migrations apply clean on a fresh Supabase project; `vitalq-core` imports and
  validates the example profile.

## M1 — Vertical slice, synthetic end-to-end (spec §40)

- `vitalq-ingest`: sessions + batch ingest + events; auth via device keys.
- `vitalq-processing` v0.1: S1–S3 + PPG chain + scalar chains + features for
  {ppg, temp, contact, motion} synthetic inputs.
- Synthetic generator (`vitalq-core.synth` or separate pkg): PPG-ish waveform, temp trend,
  motion bursts, contact dropouts, packet-loss/timestamp-drift injection —
  all `data_class='synthetic'`.
- Dashboard v0.1: session list → waveform viewer → SQI overlay → feature table.
- **Exit:** synthetic device → API → DB → processed features visible on dashboard;
  corruption tests (pipeline §5) all detected and flagged.

## M2 — First real sensors (spec §41)

- ESP32 V0 firmware: ClockService, SensorRegistry, drivers for **MAX30102 + BME280**
  (deliberately two — bus + rate diversity without full complexity), ring buffer,
  batch uploader, idempotent replay.
- API hardening: rate limits, replay protection, profile-hash check.
- **Exit:** ≥1 h continuous real capture, <5 ms cross-channel clock agreement verified
  against a known event (e.g. tapping), zero unflagged data loss across a forced
  10-min Wi-Fi outage (store-and-forward replay).

## M3 — Multimodal bring-up (spec §42)

- Add drivers as hardware lands: AS7341(+emitter), MLX90632/7, IMU (recommend
  ICM-42670-P over obsolete MPU6050 — audit C5), FSR402.
- Full S1–S8 pipeline incl. contact/motion gating, spectral chain with dark frames.
- Dashboard v0.2: spectral frame heatmap, quality timeline, baseline deviation view.
- **Exit:** all-channel session recorded; SQI correctly degrades during scripted
  motion/contact-loss tests; env confounds logged.

## M4 — Experimental AI layer (spec §43)

- Phase C personal-baseline anomaly detection on accumulated real data.
- Phase D ablation once ≥2 real modalities — results table regardless of outcome.
- **Exit:** `ml.experiments` rows + report; honest statement of whether multimodal
  fusion beats the strongest unimodal baseline, with CIs.

## M5 — Quantum simulation environment (spec §44)

- `vitalq-quantum` per doc 08; experiment matrix run; advantage-boundary figure.
- **Exit:** classical (`r=0`) reproduces shot-noise limit; squeezing advantage
  quantified *and* shown to vanish under realistic noise floors; all outputs labelled
  simulated.

## Standing workstreams (not milestones)

- Public-dataset Phase A studies can start immediately in parallel with M0–M1
  (no hardware needed) and de-risk the DSP choices.
- Docs-as-code: every pipeline/model version bump updates the relevant doc section.
- Hardware feedback loop: software findings (bus conflicts, power, optical geometry
  needs) are pushed back to the PCB iteration as written notes — the software is
  designed so late sensor swaps cost a driver + profile edit, not an architecture change.

## Explicitly deferred (guardrails against spec-creep)

MQTT, OTA fleet management, multi-user study admin, mobile app, real streaming alerts,
any clinical framing, deep-learning scale-up, GPU infrastructure.
Each requires a mini-design review + data justification before entering the roadmap.
