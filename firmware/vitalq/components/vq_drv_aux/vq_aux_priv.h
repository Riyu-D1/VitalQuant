/* vq_aux_priv.h — internals shared by the aux drivers and the component
 * core (vq_drv_aux.c). NOT installed: component-private contract.
 *
 * Unlike vq_drv_env there is NO shared cadence task here: the RTC is a
 * 0-channel clock resource and the gauge mixes a 5 s poll with an
 * ISR-driven alert path, so each chip file owns its own ops + state.
 */
#pragma once
#include "vq_sensor.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "esp_log.h"

/* Registered sensor objects (defined in the per-chip files). */
extern vq_sensor_t vq_aux_rtc_sensor;    /* "rv3028"   — 0 channels      */
extern vq_sensor_t vq_aux_gauge_sensor;  /* "max17048" — batt_v/batt_soc */

/* Chip-level selftest fragments: append a one-line report into buf
 * (NUL-terminated, truncated-safe) and return chip pass/fail.
 * ESP_ERR_NOT_FOUND = device absent (allowed for the optional RTC). */
esp_err_t vq_aux_rtc_selftest(char *buf, size_t len);
esp_err_t vq_aux_gauge_selftest(char *buf, size_t len);

/* Report helper shared with the aggregator (offset-capped vsnprintf). */
int       vq_aux_repf(char *r, size_t len, int off, const char *fmt, ...);
