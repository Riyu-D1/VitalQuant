# VitalQ

### Next-generation biological sensing

VitalQ is an early-stage biotech research project exploring high-sensitivity
biological sensing using photonics, computational methods, and emerging
quantum-inspired techniques. The goal is to investigate whether better
measurement plus better computation can surface subtle biological changes
earlier than conventional approaches.

> **Status:** Early-stage research & prototype development.
> **Not a medical device or diagnostic system** — model outputs are
> experimental anomaly scores only.

## Repository layout

This is the VitalQuant monorepo — all VitalQ work in one place:

```
hardware/pcb/        fix_v23 board (8-layer, ESP32-S3-WROOM-1-N16R8);
                     fab zip in hardware/pcb/fab/
hardware/case/       V10 enclosure design (passes CAD checks; nothing
                     printed, no physical fit validation)
                     — layout detail in hardware/README.md
firmware/esp32/      sensor driver contract + hw_v0/hw_v1/hw_v2 profiles;
                     hw_v3 (WROOM) and firmware v2 are in open PR #16,
                     pending bench bring-up
web/                 VitalQ website (Vite + React 19 + Tailwind + three.js)
software/            Python platform + services:
                     src/vitalq/    core: ingest, processing, ml, quantum, synth, datasets
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

## Hardware status

- **Board:** fix_v23, 8 layers, 55.65 × 81.05 mm outline, ESP32-S3-WROOM-1-N16R8.
  Design complete; KiCad **10.0.4** measured results: DRC 0 errors /
  0 unconnected / 0 shorts / 0 schematic-parity issues, ERC 0 errors
  (`hardware/pcb/reports/DRC_STATEMENT.md`, `VERIFY_REPORT.md`). Not yet
  fabricated — fab/bench review still required before ordering.
- **Case:** V10 enclosure in `hardware/case/` — CAD checks pass
  (`case/docs/CHECKS.md`); **no parts printed and no physical fit test done**.

## Status and open items

- 900 mA worst-case LED pulse current needs firmware interleaving — the 5 V
  boost can sustain only ~400 mA continuous.
- USB can exceed 500 mA while charging on current-limited hosts.
- No physical prototype exists yet; RF/antenna performance and case fit are
  untested on hardware.
- J12 (FPC), U24 (RTC) and U25 (microphone) are DNP for the current build
  (absent from the JLC assembly files).
- D12 (730 nm emitter) is a consigned part; U12 (MLX90632) uses LCSC C5349491
  with consignment fallback.
- Not a medical device; no safety, sterility or regulatory compliance claimed.

## Platform quickstart (local, no hardware)

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
