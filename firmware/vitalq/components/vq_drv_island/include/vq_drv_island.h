/* vq_drv_island.h — drivers for the 1.8 V I2C island behind the PCA9306.
 *
 * fix_v22 roster (board_pins.h):
 *   AS7341   0x39  11-channel spectral sensor (F1-F8, CLEAR, NIR, FLICKER).
 *                  6 ADCs + SMUX → one "frame" = TWO sequential 6-ch banks,
 *                  never simultaneous (hw profile parallel_groups=2).
 *                  On-chip LDR pin sinks an external LED (~91 mA class) —
 *                  every LED-on capture is scheduled through led_sched
 *                  (VQ_LED_AS7341) so the 400 mA budget keeps non-overlap.
 *                  INT/GPIO are NC → data-ready is polled, never an ISR.
 *   MLX90632 0x3A  non-contact IR temperature (ADDR strap = GND → 0x3A;
 *                  hw_v1's 0x3B is wrong). Full DSP5 EEPROM/RAM equation
 *                  set → temp.object + temp.ambient.
 *
 * Non-medical research prototype: spectral counts and IR temperature feed
 * a derived index only — no oximetry, no clinical claims.
 */
#pragma once
#include "vq_types.h"

/* Registers both island sensors ("as7341", "mlx90632") into the vq_sensor
 * registry. Pure registration — no bus traffic; probe() decides presence. */
esp_err_t vq_island_register(void);

/* Bring-up selftest (brief §9 style): chip IDs, one capture per device,
 * and the AS7341 dark-vs-illuminated CLEAR delta proving the LDR-driven
 * LED actually lights the scene. Fills a text report; returns ESP_OK on
 * pass, an error code otherwise (report still written). */
esp_err_t vq_island_selftest(char *report, size_t len);

/* One AS7341 spectral frame for selftest + feature code.
 *
 * Signature matches the pre-existing weak declaration used by the
 * optical selftest (vq_selftest/st_priv.h): counts[11] + illum_on.
 *
 *   illum_on = false → force our LDR LED off, integrate, return RAW
 *                      dark-frame counts.
 *   illum_on = true  → integrate immediately, return RAW counts. The
 *                      driver does NOT toggle the LDR LED and does NOT
 *                      request a scheduler slot — call this only inside
 *                      an already-active illumination window (IR_GATE /
 *                      NIR730 scheduled pulse) or under ambient light.
 *
 * ch11[0..10] order: F1,F2,F3,F4,F5,F6,F7,F8,CLEAR,NIR,FLICKER.
 * FLICKER is the dedicated flicker photodiode routed to ADC5 of bank B
 * (raw counts — the FD *frequency* engine is not used).
 * Counts are RAW (no dark subtraction) — callers compute deltas.
 *
 * Serialised against the 2 Hz streaming task; ~120 ms of bus work.
 * Returns ESP_ERR_NOT_FOUND when the device is absent (same contract
 * as probe()), ESP_ERR_TIMEOUT if the device or mutex stalls. */
esp_err_t vq_as7341_capture(uint16_t *ch11, bool illum_on);
