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
hardware/pcb/        fix_v25 board (ESP32-S3-WROOM-1-N16R8) — fix_v24 plus
                     12 probe test points (TP29-TP40, top side, DNP); DRC
                     0 errors / 0 unconnected / 0 shorts / 0 parity, ERC 0,
                     KiCad 10.0.4; fab zip in hardware/pcb/fab/ —
                     see hardware/README.md
hardware/case/       V10 enclosure design (passes CAD checks; nothing
                     printed, no physical fit validation)
                     — layout detail in hardware/README.md
firmware/esp32/      sensor driver contract + hw_v0/hw_v1/hw_v2 profiles
firmware/vitalquant/ firmware v2 for ESP32-S3 (sensors, model, cloud, power)
                     + POSIX host board simulator
web/                 VitalQuant website (Vite + React 19 + Tailwind + three.js)
software/            Python platform + services:
                     src/vitalquant/    core: ingest, processing, ml, quantum, synth, datasets
                     apps/          service entrypoints (FastAPI, pipeline worker, Streamlit)
                     tests/         unit and ingestion test suite
                     supabase/      Postgres DDL migrations (native partitioning)
                     config/        hardware profile examples
                     simulations/   standalone research simulations
                     pyproject.toml, docker-compose.yml, .env.example
docs/                design rationale: spec audit → research → architecture → schema
                     → API → pipeline → ML plan → quantum-sim design → roadmap;
                     docs/FUNDER_SUMMARY.md — one-page status for reviewers
JOURNAL.md           hardware/sensor research log
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
