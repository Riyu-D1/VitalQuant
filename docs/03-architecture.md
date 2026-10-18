# 03 — Software Architecture (Step 3)

Decision record + component design. Each major decision follows
*decision → reason → evidence → trade-offs*.

## 0. Top-level decisions

| Decision | Choice | Reason | Evidence | Trade-offs |
|---|---|---|---|---|
| Repo shape | **Monorepo**, Python packages + `firmware/` + `dashboard/` | One versioning unit for schema↔pipeline↔API; experiment provenance needs commit-level traceability (spec §32) | Single `git` history simplifies `code_commit` provenance field | Heavier clone for firmware-only work — mitigated by sparse checkout |
| Backend | **FastAPI** (as specced) on Python 3.12, Pydantic v2 | Async batch ingestion, auto OpenAPI docs, type-safe validation | Specced; team familiarity (sentalens backend pattern) | — |
| Storage | **Supabase Postgres** with *native* range partitioning; no TimescaleDB | Specced; TimescaleDB deprecated on PG17 | Supabase docs (see research §F) | pg_partman adds an extension dependency; acceptable |
| High-rate waveforms | **Array-per-window rows** (`real[]`/bytea), not row-per-sample | Row-per-sample at 200 Hz×3 ch is write-amplified ~600× and unqueryable | Audit C1; storage math (§G10) | Slightly more complex queries; accessors hide it |
| Processing | **Offline/batch worker** (Python, APScheduler or cron-run) + optional live path | Research prototype: correctness/reproducibility before latency; batch enables reprocessing with new `pipeline_version` | Spec wants provenance + reprocessing | Live dashboard poll reads latest windows — near-real-time without a streaming stack |
| ML | scikit-learn first; PyTorch optional extra | Specced; small datasets → classical baselines | §21, §41–43 milestones | Keeps the 16 GB laptop viable |
| Dashboard | **Next.js + Tailwind + lightweight charting (uPlot/ECharts)**, Supabase auth | Team's established stack (yavuno/sentalens patterns); server components read Postgres directly | Existing repos | Could start with Streamlit for M1 speed — listed as alternative in roadmap |
| Firmware | ESP-IDF (or Arduino-ESP32 for V0 speed), C++ sensor drivers behind a registry | Specced ESP32; drivers already abstracted per research §A | Datasheets | ESP-IDF curve steeper; Arduino first is pragmatic |
| Config | **YAML hardware profiles** validated by Pydantic; `hardware_revision` pinned per device | Spec §34 literal example | Doc | — |
| Transport | HTTPS POST batched JSON (CBOR later if needed) | Simplest robust path over Wi-Fi; protobuf premature | — | MQTT reconsidered if bidirectional control needed |

## 1. Component view

```
┌──────────────────────────┐        ┌──────────────────────────────┐
│  ESP32 firmware          │        │  vitalq-api (FastAPI)        │
│  ┌────────────────────┐  │ HTTPS  │  /v1/ingest/batch            │
│  │ SensorRegistry     │  │───────▶│  /v1/sessions ...            │
│  │  drivers/ (per IC) │  │ batch  │  auth: device key / JWT      │
│  │ ClockService (SNTP │  │        │  idempotent ingest           │
│  │  + esp_timer)      │  │        └──────────┬───────────────────┘
│  │ RingBuffer→flash/SD│  │                   │ write
│  │ BatchUploader      │  │                   ▼
│  └────────────────────┘  │        ┌──────────────────────────────┐
└──────────────────────────┘        │  Supabase Postgres           │
                                     │  raw.* (append-only)         │
┌──────────────────────────┐        │  clean.*, features.*, ml.*   │
│  vitalq-worker (Python)  │───────▶│  config.* , meta.*           │
│  sync → SQI → features   │        └──────────┬───────────────────┘
│  → baselines → anomaly   │                   │ read
└──────────┬───────────────┘        ┌──────────┴───────────────────┐
           │                        │  dashboard (Next.js)         │
           ▼                        │  waveforms, SQI, trends,     │
┌──────────────────────────┐        │  experimental model output   │
│  experiments/ notebooks  │        └──────────────────────────────┘
│  + ML harness (offline)  │
│  + quantum_sim           │
└──────────────────────────┘
```

## 2. Repository layout

*M1 note:* the package split below consolidated into one `src/vitalq` distribution with
submodules (`core`, `synth`, `ingest`, `processing`, `ml`) — same separation, single
install; extras (`[ingest]`, `[processing]`, `[ml]`, `[dashboard]`) keep dependencies
modular. Splitting into separately versioned packages is deferred until a real second
consumer exists.

Adapted from spec §9 — same separation, concretised:

```
vitalq/
├── firmware/esp32/            # ESP-IDF/Arduino project
│   ├── drivers/               # max30102.cpp, max86141.cpp, as7341.cpp, mlx.cpp, bme.cpp, imu.cpp, fsr.cpp
│   ├── core/                  # clock_service, ring_buffer, batch_uploader, sensor_registry
│   └── profiles/              # hw_v0.yaml, hw_v1.yaml → compiled-in default + runtime override
├── packages/
│   ├── vitalq-core/           # shared schemas (Pydantic), units, clock math, config loader
│   ├── vitalq-ingest/         # FastAPI app: auth, validation, DB writes
│   ├── vitalq-processing/     # sync, SQI, per-modality chains, features (worker lib)
│   ├── vitalq-ml/             # baselines, evaluation harness, experiment registry client
│   └── vitalq-quantum/        # noise-model hierarchy + experiment runner
├── apps/
│   ├── api/                   # thin entrypoint wiring vitalq-ingest
│   ├── worker/                # processing daemon entrypoint
│   └── dashboard/             # Next.js app
├── supabase/
│   ├── migrations/            # DDL from doc 04, applied via supabase CLI
│   └── seed/                  # hardware revisions, demo subject
├── experiments/               # reproducible experiment dirs (config + results + figures)
├── data/                      # local synthetic/dataset cache (gitignored)
├── tests/                     # unit + pipeline + API contract tests
└── docs/                      # this package
```

`vitalq-core` is deliberately dependency-light (pydantic, numpy) — firmware-adjacent
constants (channel names, unit enums) and the hardware-config schema live there so every
layer validates against the same definitions.

## 3. Key cross-cutting designs

### 3.1 Hardware profile resolution

`hardware_revision` (e.g. `hw_v1`) resolves to a YAML profile (`config/hardware.example.yaml`
attached) listing every sensor: `{type, model, bus, address, channels, rate_hz, enabled}`.
Firmware validates its compiled drivers against the profile at boot and reports
`profile_hash` in every batch → the backend can detect config drift.

### 3.2 Data layering (enforced)

```
raw.measurements_scalar      raw.signal_windows        raw.spectral_frames
        │ append-only; worker reads in session order
        ▼
clean.* (same shapes + outlier/artifact flags)  → quality.signal_windows
        ▼
features.windows  (keyed by session+window_id+pipeline_version)
        ▼
ml.predictions / ml.experiments
```

Raw tables accept INSERT only (no UPDATE/DELETE at DB level for the ingest role) —
correctness enforced structurally, not by convention.

### 3.3 Clock discipline

Device stamps samples with µs monotonic counter + wall-clock at batch creation;
server records receipt. `vitalq-core` fits offset/drift per batch (`clock_offset_us`,
`clock_drift_ppm`) stored on the batch row — pipelines reconstruct corrected UTC per sample
without the backend "guessing" (spec §10 satisfied explicitly). NTP resync events are logged
as `device_events`.

### 3.4 Provenance

Every derived row carries `pipeline_version` (semver of `vitalq-processing`) and
`source_ids` (array of raw PKs or window ids) — the §46 traceability chain
`sensor → raw → preprocessing → feature → model → output` is then a SQL join.

### 3.5 Deployment topology

- Supabase hosted Postgres + Auth + (optional) Storage for cold export.
- `vitalq-api` on any small host (Fly.io/Railway/VPS); the user already runs this pattern
  (sentalens uses FastAPI + Caddy).
- `vitalq-worker` co-located or on the laptop — it is a batch process, not a daemon that
  must be up for ingestion to work.
- Dashboard on Vercel/self-host.
- Everything except Supabase runs fine offline → ESP32→API on LAN works without internet
  (important for bench testing).

## 4. What is deliberately NOT built yet

- No MQTT/streaming broker, no Kubernetes, no feature-store service, no MLflow server,
  no GPU anything — each has a named insertion point in the roadmap if it earns its way in.
- No FHIR/clinical-interop layer — explicitly out of scope for a research prototype.
- No mobile app — the dashboard is the interface.
