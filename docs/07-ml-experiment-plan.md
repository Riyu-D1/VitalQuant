# 07 — AI/ML Experimentation Plan (Step 7)

Staged so the level of method always matches the level of available data and labels
(spec §20–22, §31). Hierarchy from spec kept verbatim in spirit:

- **L1 quality** — SQI per channel (mostly engineered, partly learnt on BUT-PPG labels)
- **L2 physiology features** — deterministic extraction (pipeline doc)
- **L3 anomaly** — personal-baseline deviation, *no labels required*
- **L4 risk modelling** — only when honest labels exist (audit G9); blocked otherwise

## Phase A — public-dataset baselines (pre-hardware)

| Study | Data | Question |
|---|---|---|
| A1 SQI | BUT-PPG (binary quality labels) | Can a small feature-based classifier (skew/kurt/PI/template/spectral-purity → logistic/GBM) match published SQI quality? Metric: AUPRC vs prevalence |
| A2 HR from wrist PPG | PPG-DaLiA (ECG truth) | Peak-based HR + ACC-reference artifact rejection; metric: MAE vs ECG HR under activity |
| A3 deviation detection | WESAD (baseline vs stress) | Within-subject robust-z deviation detects state change without subject-specific training — closest honest proxy for "personal baseline" anomaly detection |
| A4 (optional) SpO₂ regression | Capnobase/BIDMC | R-ratio→SpO₂ calibration transfer across devices — quantifies why VitalQ cannot claim SpO₂ uncalibrated |

Deliverable per study: `experiments/A*/` = config + data manifest + metrics + figures +
`ml.experiments` rows. Any method that fails on public data is not ported to VitalQ.

## Phase B — synthetic↔real alignment check (audit A3)

Compare synthetic generator output vs PPG-DaLiA feature distributions (KS tests on
IBI stats, spectral shape, PI). Goal: synthetic data is fit for *pipeline testing and
corruption injection*, and its limits are measured and documented — never silently used
as a stand-in for physiology.

## Phase C — personal-baseline anomaly detection (first real VitalQ data)

- `features.baselines`: rolling median/MAD per channel per subject (14-day window default).
- Anomaly score = robust multivariate deviation (MCD or isolation forest on feature
  windows) **within subject**; output `kind='anomaly_score'` only.
- Success criterion: stable false-alarm rate during labelled `self_report` normal periods;
  sensitivity demonstrated against injected synthetic anomalies + device-off events —
  reported as engineering performance, not clinical.

## Phase D — the ablation study (spec §30: *the* scientific question)

Runs once ≥2 modalities stream real data:

```
cells = {PPG} × {PPG+T} × {PPG+T+S} × {PPG+T+S+M} × {PPG+T+S+M+C} × {all}
models = {logreg, random_forest, lightgbm}  (+ temporal model only if data supports)
split  = LOSO (leave-one-subject-out) for generalisation claims;
         within-subject temporal split for personalisation questions
```

Report, per cell: AUROC/AUPRC where labels exist, otherwise anomaly-detection stability;
calibration curves; CI via bootstrap over subjects. **The honest output is a table where
adding modalities might not help** — that result is publishable-quality engineering.

## Phase E — temporal/deep models (gated)

Only when: ≥ tens of subjects OR sustained multi-week per-subject data, *and* Phase D
baselines are published. Candidates: 1-D CNN/GRU per-modality encoders → masked fusion
(spec §22's diagram), tiny parameter budget (<1 M params — laptop-trainable or one cloud
job). Premature scale is an explicit anti-goal.

## Leakage controls (spec §31)

- Subject-disjoint CV via `GroupKFold(subject_id)`; session-level temporal gap (≥1 day
  between a subject's train/test windows) to kill autocorrelation leakage.
- Pipeline-fit statistics (normalisation, baselines) computed on train folds only.
- `data_class` filter enforced: synthetic/simulated rows excluded from real-data evals
  unless the experiment's declared purpose is sim→real transfer (flagged in `model_spec`).

## Experiment registry

`ml.experiments`/`ml.models` (DDL in doc 04) + per-run directory
`experiments/<id>/{config.yaml, metrics.json, figures/}` — spec §32 fields all covered
(dataset_version, hw/fw/feature/model versions, seeds, commit). Optional W&B/MLflow only
if iteration volume justifies; tables suffice to start.
