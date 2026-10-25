/* vq_drv_ads1292.h — ADS1292R two-channel biopotential AFE driver.
 *
 * Registers the vq_sensor_t "ads1292r" producing channels ecg.ii and
 * resp.waveform at 500 sps (required device on fix_v22).
 *
 * fix_v22 wiring (board_pins.h): SPI2 host, mode 1 (CPOL0/CPHA1),
 * CS = IO10, ~DRDY = IO2 (active-low, external R99 100k pull-up only),
 * PWDN/~RESET = TCA6408 P1 (default low = powered down), START pin tied
 * to GND so conversions run only via the START opcode, CLKSEL high so the
 * internal 512 kHz oscillator is used.
 *
 * Research prototype — engineering data only, no clinical claims.
 */
#pragma once
#include "vq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the "ads1292r" sensor into the vq registry. Idempotent. */
esp_err_t vq_ads1292_register(void);

/* Bring-up self-test: ID at probe clock + operating clock, internal
 * test-signal capture (non-noise-floor check), lead-off status read.
 * Fills a human-readable report into buf. Refuses while streaming. */
esp_err_t vq_ads1292_selftest(char *buf, size_t len);

/* Read the ID register (0x00) via the 1 MHz command handle. Powers the
 * device up/down itself if needed. Refuses while streaming. */
esp_err_t vq_ads1292_read_id(uint8_t *id);

#ifdef __cplusplus
}
#endif
