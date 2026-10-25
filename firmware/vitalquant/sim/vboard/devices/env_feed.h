/* env_feed.h — shared environment stimulus for the env-sensor device
 * models (sim/vboard/devices/{tmp117,sht45,bme280}.c) and the scenario
 * tests that drive them.
 *
 * One global store, g_vbenv, holds the "true" environment the virtual
 * board is exposed to.  Scenario tests write it through the setters;
 * the device models convert it into sensor-native raw values each
 * conversion, so the firmware's real compensation formulas land back
 * on (within quantisation) the injected stimulus.
 *
 * Channel mapping (hw_v3.yaml / vq_tmp117.c):
 *   skin_c   → TMP117 "tmp117_48" (U11, skin contact; also ALERT→IO5)
 *   amb_c    → TMP117 "tmp117_49" (U20, insulation dome) + SHT45 T
 *              + BME280 T
 *   rh_pct   → SHT45 RH + BME280 RH
 *   press_pa → BME280 pressure
 *
 * Defaults (set at device registration): skin 32.0 °C, amb 23.5 °C,
 * RH 45.0 %, pressure 101325.0 Pa.
 */
#ifndef ENV_FEED_H
#define ENV_FEED_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float skin_c;      /* U11 skin/contact temperature, °C          */
    float amb_c;       /* dome / ambient temperature, °C            */
    float rh_pct;      /* relative humidity, % (0-100)              */
    float press_pa;    /* barometric pressure, Pa                   */
} vbenv_t;

/* Defined in tmp117.c (single TU — this header is include-only). */
extern vbenv_t g_vbenv;

/* --------------------------------------------------------------- *
 *  Scenario-facing setters/getters.  Safe to call at any time; the
 *  change is visible from the next conversion each model performs.
 * --------------------------------------------------------------- */
static inline void  vbenv_set_skin_c(float c)   { g_vbenv.skin_c   = c; }
static inline void  vbenv_set_amb_c(float c)    { g_vbenv.amb_c    = c; }
static inline void  vbenv_set_rh_pct(float r)   { g_vbenv.rh_pct   = r; }
static inline void  vbenv_set_press_pa(float p) { g_vbenv.press_pa = p; }

static inline float vbenv_skin_c(void)    { return g_vbenv.skin_c;   }
static inline float vbenv_amb_c(void)     { return g_vbenv.amb_c;    }
static inline float vbenv_rh_pct(void)    { return g_vbenv.rh_pct;   }
static inline float vbenv_press_pa(void)  { return g_vbenv.press_pa; }

/* --------------------------------------------------------------- *
 *  Device-model registration.  Idempotent; also self-invoked by a
 *  constructor in each device file so the default population exists
 *  without vboard_init() having to know these symbols.
 * --------------------------------------------------------------- */
void vbdev_tmp117_register(void);   /* "tmp117_48" @0x48 + "tmp117_49" @0x49 */
void vbdev_sht45_register(void);    /* "sht45" @0x44                        */
void vbdev_bme280_register(void);   /* "bme280" @0x76                       */

/* --------------------------------------------------------------- *
 *  esp_err values used by the models.  vboard.h already defines the
 *  VB_ESP_* aliases when it owns the esp_err_t typedef; if a shim
 *  esp_err.h beat it to the guard, fall back here so the models still
 *  compile.  Values match the real ESP-IDF codes (NACK → TIMEOUT).
 * --------------------------------------------------------------- */
#ifndef VB_ESP_OK
#define VB_ESP_OK            0
#define VB_ESP_ERR_TIMEOUT   0x107
#define VB_ESP_FAIL          (-1)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ENV_FEED_H */
