# AGENTS.md

Guidance for agentic work in this monorepo. Research prototype throughout —
engineering signals only, no clinical claims; model outputs are experimental
anomaly scores, never diagnostic.

## Firmware v2 (`firmware/vitalq/`)

ESP-IDF app for the fix_v22 board, ESP32-S3-WROOM-1-N16R8 (16 MB QIO flash,
8 MB octal PSRAM), target `esp32s3`. See `firmware/vitalq/README.md` for the
full build/flash/selftest doc.

### Verified commands

```sh
. ~/esp/esp-idf/export.sh        # ESP-IDF v5.4.2 lives at ~/esp/esp-idf
cd firmware/vitalq && idf.py build        # clean-tree verify
python3 tools/export_model.py info <blob> # VQMB blob check (stdlib only)
python3 tools/export_model.py stub --out /tmp/stub.bin && \
python3 tools/export_model.py info /tmp/stub.bin   # tool smoke test
```

`idf.py build` was the last verified green check on this tree. Console-side
verification is the on-device `selftest all` suite over the USB-Serial/JTAG
REPL.

### The vq-contract pattern (how to add a component)

- `components/vq/` is interface-only: every cross-component contract header
  lives in `components/vq/include/`. These headers are **frozen contracts** —
  implementations `REQUIRES vq` and must not modify them. If a signature
  needs to change, that is a deliberate cross-component decision, not a
  drive-by edit.
- Each implementation component `vq_<name>/` has its own `CMakeLists.txt`
  with `REQUIRES vq ...`, keeps module-private constants in its own private
  headers (`*_priv.h` or `include/vq_<name>_ext.h` for extension points), and
  implements exactly the contract API.
- Driver components implement `vq_sensor_ops_t` (`vq_sensor.h`) and register
  a descriptor via `vq_sensor_register()`; probe failures must be
  non-fatal (`ESP_ERR_NOT_FOUND` → pipeline marks absent and continues).
- Sample timestamps are esp_timer µs taken **at acquisition** (DRDY/INT edge
  latched in ISR), never at FIFO pop or transmit — the FIFO timestamp
  contract in `vq_types.h`.
- Console/test surface goes through `vq_selftest` + `vq_console`; handlers
  are thin wrappers over contract APIs and must survive absent services.

### Hardware hard rules (summary — see README for the full list)

- Forbidden GPIO: 26–32, 35/36/37 (flash/octal-PSRAM), 45 (VDD_SPI strap,
  read-only), 46–48 (NC); IO0/IO3/IO46 strap input-only; GPIO19/20 are USB
  D-/D+, never GPIO.
- I2C0 stays at 100 kHz (2.2k pulls, ~503 ns rise). One SPI CS at a time,
  CS idle high; per-device clock caps in `board_pins.h`.
- LED chains interlocked — one per slot, ≥300 mA class ≤1% duty, TX_5V
  only inside scheduler windows.
- Battery: cell assumed unprotected — ≤3.2 V/5% SOC deep-sleep cutoff,
  ≤3.0 V emergency, no TX_5V below 3.3 V.
- Never burn eFuses; never reclaim GPIO19/20 (console is the recovery
  path).

## Other trees

- `firmware/esp32/` — earlier skeleton + hardware profiles (`hw_v3.yaml`
  is the fix_v22 GPIO/sensor truth table).
- `hardware/`, `docs/`, `software/`, `web/` — see top-level `README.md`
  for the layout map.
