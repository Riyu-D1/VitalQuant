/* vq_island_priv.h — component-internal declarations shared between the
 * island driver sources. NOT installed; do not include outside vq_drv_island.
 */
#pragma once
#include "vq_types.h"
#include "vq_sensor.h"

/* Sensor objects (static storage; ops tables in each driver source).   */
vq_sensor_t *vq_island_as7341_sensor(void);
vq_sensor_t *vq_island_mlx90632_sensor(void);

/* Create each driver's private mutex early (register() calls these so
 * later lazy paths never race on creation). No bus traffic.            */
void as7341_early_init(void);
void mlx90632_early_init(void);

/* ---- AS7341 internals used by the island selftest -------------------- */

/* Chip-id readback: ID (0x92), AUXID (0x90), REVID (0x91). No configure. */
esp_err_t as7341_read_ids(uint8_t *id, uint8_t *auxid, uint8_t *revid);

/* Raw dark + LED-illuminated frames in ADC order (bank A then bank B):
 *   frame[0..5]  = bank A ADC0..5 → F1,F2,F3,F4,CLEAR,NIR
 *   frame[6..11] = bank B ADC0..5 → F5,F6,F7,F8,CLEAR,FLICKER
 * t_us[0..3] = acquisition stamps {darkA, darkB, litA, litB}.
 * Applies device config lazily; serialised against the streaming task.
 * Returns error when the LED scheduler grants no slot (lit[] invalid). */
esp_err_t as7341_capture_frames(uint16_t dark[12], uint16_t lit[12],
                                int64_t t_us[4]);

/* ---- MLX90632 internals used by the island selftest ------------------ */

/* EEPROM identity: product code (0x2409) + EE_VERSION (0x240B). */
esp_err_t mlx90632_read_ids(uint16_t *product_code, uint16_t *ee_version);

/* One fresh measurement → object/ambient °C (full DSP5 equation set).
 * Applies config lazily; may block ~1 s at the default 2 Hz refresh. */
esp_err_t mlx90632_measure(float *tobj_c, float *tamb_c);
