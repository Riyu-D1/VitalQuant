# VitalQ

Research platform for a multimodal wearable sensing prototype (ESP32 → FastAPI →
Postgres/Supabase → signal processing → features → experimental ML → dashboard).

**VitalQ is a research prototype — not a medical device. It does not diagnose
sepsis or any condition; model outputs are experimental anomaly scores.**

Design rationale lives in `docs/` (Steps 1–9 of the project handoff):
audit → research → architecture → schema → API → pipeline → ML plan →
quantum-sim design → roadmap.

## Quickstart (local, no hardware)

```bash
pip install -e ".[ingest,processing,ml,dashboard,dev]"
docker compose up -d db                      # local Postgres 17
export DATABASE_URL=postgresql://vitalq:vitalq@localhost:5432/vitalq
vitalq-migrate                               # apply supabase/migrations
vitalq-device --label bench-01 --hardware-revision hw_v0   # prints device key once
export VITALQ_DEVICE_KEY=vq_...

uvicorn vitalq.ingest.app:app --port 8000 &  # API
vitalq-synth --profile config/hardware.example.yaml --duration 120 \
    --api http://localhost:8000              # synthetic device → API → DB
vitalq-worker --all                          # → quality + features
streamlit run apps/dashboard/app.py          # dashboard v0.1
```

## Layout

```
src/vitalq/core/        channel registry, hardware profiles, clock model, wire types
src/vitalq/synth/       deterministic synthetic device (data_class='synthetic')
src/vitalq/ingest/      FastAPI app, auth, asyncpg layer, migrations runner, device CLI
src/vitalq/processing/  S4–S7 chains: PPG, scalars, SQI, feature windows, worker
src/vitalq/ml/          personal-baseline anomaly detection + experiment registry
apps/api|worker|dashboard   entrypoints
firmware/esp32/         driver contract + hw_v0/hw_v1 profiles (M2)
supabase/migrations/    Postgres DDL (native partitioning; no TimescaleDB)
config/                 example hardware profile
experiments/            per-run artifacts (config + metrics + figures)
tests/                  unit + API-contract tests (Postgres-backed)
```

## Invariants

- RAW rows are append-only; every derived row carries `pipeline_version` +
  `source_window_ids` (sensor → raw → clean → feature → model → output is a SQL join).
- `data_class ∈ {real, synthetic, simulated}` is non-nullable — simulated/synthetic
  data can never mix silently with real measurements.
- Sensor presence/models/rates come from a hardware profile YAML, keyed by
  `hardware_revision` — never hard-coded.
