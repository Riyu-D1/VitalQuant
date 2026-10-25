/* vq_drv_env.h — environmental I2C sensor group for fix_v22 / hw_v3.
 *
 * Owns the five slow environmental nodes on the single 100 kHz I2C bus
 * (i2c_bus.h — all transactions mutexed there):
 *
 *   name          addr   descriptor channels           notes
 *   tmp117.skin   0x48   temp.skin     U11, ALERT->IO5 via R65 (alert-only)
 *   tmp117.dome   0x49   temp.dome     U20, ALERT NC — TREND ONLY
 *   tmp117.tail   0x4A   temp.distal   J9 tail, OPTIONAL — probe tolerated
 *   bme280        0x76   env.temp/env.rh/env.press       forced mode 0.5 Hz
 *   sht45         0x44   skin.rh/skin.temp               high-prec ~1 Hz
 *
 * RESEARCH PROTOTYPE — temp.dome and temp.distal are trend channels
 * (docs/10 §1: the foam-dome pair tracks core-temperature *trend*; no
 * calibrated core-temperature or clinical claim is made anywhere).
 *
 * Scheduling model: vq_sensor_ops has no poll/read hook — cadence sensors
 * push samples through their sink — so this component runs ONE shared
 * cadence task (the "0.2-2 Hz I2C cadence group" of pipeline.h). Each
 * poll stamps esp_timer at READ START (the conversion trigger); the
 * result is fetched a fixed conversion-latency later under that same
 * stamp. These are non-FIFO single-shot devices: acquisition ≈ read, so
 * a read-start stamp satisfies the vq_types.h timestamp contract. The
 * stamp leads the conversion-complete instant by a constant, per-device
 * latency (≤20 ms) — documented divergence, immaterial at 0.5-1 Hz.
 *
 * TMP117 self-heating guard: never continuous-convert faster than 1 Hz
 * burst period — this driver uses one-shot conversions at 1 Hz cadence
 * (~1.5 % duty). U11's ALERT keeps alert-function config (open-drain,
 * active-low) — firmware never consumes it; IO5 stays an input, no ISR.
 */
#pragma once
#include "vq_types.h"

/* Register all five sensor descriptors with the pipeline registry
 * (vq_sensor_register). Idempotent; call once before pipeline start.
 * Absent optional hardware is handled per-sensor via probe(), not here. */
esp_err_t vq_env_register(void);

/* §9-style self test: per-device ID/serial + one conversion each, and a
 * demonstration of the tolerated-absent path for the optional J9 tail.
 * Fills `report` (always NUL-terminated) and returns ESP_OK when every
 * REQUIRED device passes; ESP_ERR_FAIL otherwise. Devices that are
 * already streaming are skipped (never disturbs a running pipeline). */
esp_err_t vq_env_selftest(char *report, size_t len);
