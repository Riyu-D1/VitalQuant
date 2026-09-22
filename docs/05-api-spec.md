# 05 — API Specification (Step 5)

FastAPI service (`vitalq-ingest`), REST + JSON, versioned under `/v1`.
Two auth contexts (spec §13/§35):

| Caller | Auth | Rights |
|---|---|---|
| ESP32 device | `X-Device-Key: <key>` → matched against `meta.devices.api_key_hash` (sha256), revocable | `POST /v1/sessions`, `POST /v1/ingest/batch`, `POST /v1/events` for its own `device_id` only |
| Dashboard/researcher | Supabase JWT (user or service role) | Read endpoints; RLS scopes to permitted subjects |

## Endpoint surface

Deliberately minimal for M1 (spec: "do not blindly implement all endpoints"):

```
POST  /v1/sessions                      open a session {device, subject?, hw_rev, fw_ver, data_class, body_site}
POST  /v1/sessions/{id}/end             close
POST  /v1/ingest/batch                  THE device write path (see envelope below)
POST  /v1/events                        device events (ntp_sync, sensor_fault, buffer_flush)
GET   /v1/sessions/{id}                 session + latest status
GET   /v1/sessions?device_id=&from=&to=
GET   /v1/signals/{session_id}/{channel}?from=&to=&decimate=   waveform windows (array chunks)
GET   /v1/scalars/{session_id}/{channel}?from=&to=
GET   /v1/quality/{session_id}          per-channel SQI timeline
GET   /v1/features/{session_id}?set=
GET   /v1/predictions/{session_id}
GET   /v1/health   GET /v1/version      unauthenticated
```

## Batch ingest envelope — the contract that matters

Device batches buffered samples; timestamps stay device-side (`device_time_us`) plus the
fitted clock model, so **the backend never guesses acquisition time** (spec §10).

```jsonc
POST /v1/ingest/batch
X-Device-Key: ****
{
  "batch_id": "3d0b…",                    // uuid, idempotency key (raw.ingest_batches PK)
  "session_id": "9f2e…",
  "firmware_version": "0.3.1",
  "profile_hash": "sha256:…",             // hardware profile drift detection
  "clock": {                              // device clock state at batch build time
    "wall_time": "2026-09-21T10:50:00Z",  // last SNTP-disciplined UTC
    "monotonic_us": 8123456789
  },
  "scalars": [                            // temp, env, contact, spectral-summary channels
    {"channel": "temp.object", "t_us": 8123400000, "v": 33.41},
    {"channel": "env.humidity", "t_us": 8123400100, "v": 41.2}
  ],
  "windows": [                            // waveform channels
    {"channel": "ppg.ir", "rate_hz": 200,
     "t_start_us": 8123300000, "samples": [8123, 8119, …]},   // int array → real[]
    {"channel": "motion.accel_x", "rate_hz": 52, "t_start_us": …, "samples": […]}
  ],
  "frames": [                             // AS7341 spectral frames
    {"t_us": 8123400500, "read_group": 0, "gain_x": 64, "integ_ms": 27.8,
     "illumination": "emitter_on",
     "channels": {"f1": 123, "f2": 234, "…": "…", "clear": 901, "nir": 88, "flicker": 0}}
  ],
  "events": [{"t_us": …, "kind": "ntp_sync", "detail": {"offset_ms": 12}}]
}
```

Server behaviour:
- `400` schema violation (Pydantic v2 models generated from `vitalq-core` channel registry —
  unknown `channel` rejected), `401/403` auth, `409` reused `batch_id` (idempotent replay —
  safe retransmission), `413` payload cap (default 2 MB), `429` rate limit.
- Per-window `t_start_us` + `rate_hz` + count are validated for consistency; samples out of
  the session's time envelope are flagged not dropped (raw stays raw).
- On success: `202 {accepted: N_rows, batch_id}` — async ACK semantics; the processing
  worker picks up windows in order.
- Response includes `server_time` + latest fitted `clock_correction` so the device can
  improve its clock model on the next batch.

## Clock sync sub-protocol (audit G3)

1. Device SNTP-syncs at session start and periodically → `wall_time` + `monotonic_us` anchor.
2. Each batch carries that pair; the server records `received_at`.
3. `vitalq-core`'s `ClockModel` fits `(offset_us, drift_ppm)` per session from the batch
   series (bounded |offset| jump → logs `clock_jump` event).
4. Processing converts `t_us → UTC` with the *batch-local* correction, storing both raw and
   corrected values. Target cross-channel agreement: <5 ms (PPG↔motion fusion needs it).

## Error model

```jsonc
{ "error": { "code": "schema.channel_unknown", "message": "…", "fields": ["windows[2].channel"], "request_id": "…" } }
```
Codes: `auth.invalid_key`, `auth.revoked`, `batch.replay`, `schema.*`, `clock.*`,
`payload.too_large`, `rate.exceeded`, `session.state` (writing to a closed session).

## Versioning & docs

- `/v1` path prefix; breaking changes → `/v2` alongside.
- FastAPI emits OpenAPI at `/docs` — the spec here stays authoritative for the *device*
  contract (a thin `docs/esp32-payload.md` mirrors it for firmware authors).

## Security (spec §35 + audit G11, STRIDE-lite)

| Threat | Mitigation |
|---|---|
| Device spoofing | Per-device random 256-bit keys, hashed at rest, revocable; key rotation endpoint later |
| Replay | `batch_id` uniqueness + session-time envelope check |
| Tampering | TLS only; HMAC signing of payload optional post-M1 |
| Leaked key | Scoped role (insert-only on `raw.*`), `revoked` flag, event log |
| Dashboard access | Supabase RLS: subject-scoped reads |
| Secrets | Device key provisioned once via serial at flash time; no secrets in repo/firmware source |
