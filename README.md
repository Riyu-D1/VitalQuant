# VitalQuant

### Next-generation biological sensing

VitalQuant is an early-stage biotech research project exploring high-sensitivity
biological sensing using photonics, computational methods, and emerging
quantum-inspired techniques. The goal is to investigate whether better
measurement plus better computation can surface subtle biological changes
earlier than conventional approaches.

> **Status:** Early-stage research & prototype development.
> **Not a medical device or diagnostic system** — model outputs are
> experimental anomaly scores only.

## Repository layout

This is the VitalQuant monorepo — all VitalQuant work in one place:

```
src/vitalquant/          core Python platform: ingest, processing, ml, quantum, synth, datasets
apps/api|worker|dashboard   service entrypoints (FastAPI, pipeline worker, Streamlit)
firmware/esp32/      sensor driver contract + hw_v0/hw_v1 profiles
supabase/migrations/ Postgres DDL (native partitioning; no TimescaleDB)
config/              hardware profile examples
docs/                design rationale: spec audit → research → architecture → schema
                     → API → pipeline → ML plan → quantum-sim design → roadmap
tests/               unit and ingestion test suite
web/                 VitalQuant website (Vite + React 19 + Tailwind + three.js)
web/react-bits-repo/ vendored react-bits component library the site draws from
simulations/         standalone research simulations (quantum-enhanced detection)
archive/             legacy single-file site (pre-React)
JOURNAL.md           hardware/sensor research log
docker-compose.yml   local Postgres 17
.github/workflows/   CI
```

## Platform quickstart (local, no hardware)

```bash
pip install -e ".[ingest,processing,ml,dashboard,dev]"
docker compose up -d db                      # local Postgres 17
export DATABASE_URL=postgresql://vitalquant:vitalquant@localhost:5432/vitalquant
vitalquant-migrate                               # apply supabase/migrations
vitalquant-device --label bench-01 --hardware-revision hw_v0   # prints device key once
export VITALQUANT_DEVICE_KEY=vq_...

uvicorn vitalquant.ingest.app:app --port 8000 &  # API
vitalquant-synth --profile config/hardware.example.yaml --duration 120 \
    --api http://localhost:8000              # synthetic device → API → DB
vitalquant-worker --all                          # → quality + features
streamlit run apps/dashboard/app.py          # dashboard
```

## Website

```bash
cd web
npm ci
npm run dev      # Vite dev server
npm run build    # production build → web/dist
```

## License

Source-available for **Hack Club review only** — not open source.
See [LICENSE](LICENSE).
