# ESP32 → API payload contract (firmware mirror)

Thin mirror of `docs/05-api-spec.md` for firmware authors — the device-side
shape only. Doc 05 stays authoritative; if they disagree, fix this file.

## What the device sends

Everything goes through **one endpoint** — batch ingest. No other device
endpoints are required for M2.

```jsonc
POST /v1/ingest/batch
X-Device-Key: <provisioned key, sent once at flash time over serial>

{
  "batch_id": "uuid",                    // idempotency key — safe to re-POST
  "session_id": "uuid",
  "firmware_version": "x.y.z",
  "profile_hash": "sha256:...",          // hash of the compiled-in hw_v*.yaml
  "clock": {
    "wall_time": "2026-10-22T10:50:00Z", // last SNTP-disciplined UTC
    "monotonic_us": 8123456789           // esp_timer µs at batch build
  },
  "scalars": [
    {"channel": "temp.object", "t_us": 8123400000, "v": 33.41}
  ],
  "windows": [
    {"channel": "ppg.ir", "rate_hz": 200,
     "t_start_us": 8123300000, "samples": [8123, 8119, ...]}
  ],
  "frames": [                            // AS7341 only
    {"t_us": ..., "read_group": 0, "gain_x": 64, "integ_ms": 27.8,
     "illumination": "emitter_on" | "dark_frame",
     "channels": {"f1": 123, ..., "clear": 901, "nir": 88, "flicker": 0}}
  ],
  "events": [
    {"t_us": ..., "kind": "ntp_sync", "detail": {"offset_ms": 12}}
  ]
}
```

## Rules the server enforces (build firmware to them)

- **Timestamps are acquisition-time** — `t_us`/`t_start_us` come from
  `esp_timer` at the DRDY/ADC_RDY edge, never at FIFO pop or transmit
  (see `firmware/esp32/core/sensor_driver.h` FIFO contract).
- `batch_id` reuse → `409` and safe re-drop — the RingBuffer replays
  buffered batches verbatim after an outage.
- Unknown `channel` → `400`; channels come from `vitalquant-core`'s registry
  (profile `outputs:` lists), so keep the profile in sync.
- Payload cap 2 MB — size batches by window count, not duration.
- Response `202` carries `accepted`, `batch_id`, `server_time` and the
  latest `clock_correction` — apply it to the next batch's clock state.
- `session.state` error → the session was closed server-side; start a
  new session before re-sending.

## Sessions

`POST /v1/sessions {device_id, profile_hash}` opens one; the returned id
goes in every batch until stopped. One session per wear episode is the
intended cadence.

## Transport

TLS only in deployment. `VITALQUANT_DEVICE_KEY` is provisioned once via
serial at flash time — **no secrets in source or profiles**.
