/* optical_feed.h — shared optical scene stimulus for the 1.8 V-island
 * device models (sim/vboard/devices/{as7341,mlx90632}.c) and the
 * scenario tests that drive them.
 *
 * One global store, g_vbopt, holds the "true" optical scene the virtual
 * board is exposed to.  Scenario tests write it through the setters;
 * the device models convert it into sensor-native raw values at each
 * conversion/measurement, so the firmware's real driver formulas land
 * back on the injected stimulus.
 *
 * Channel mapping:
 *   illum     → AS7341 "as7341" scene illumination, ARBITRARY units
 *               (0 = dark scene, 100 = default ambient, 1000 = lit —
 *               the §9 optical selftest needs dark < light).  The
 *               AS7341's own LDR-driven LED and the IR_GATE expander
 *               line (TCA6408 P4, read via vboard_exp_pin(4)) add their
 *               contributions ON TOP of this scene level — the LED is a
 *               device-local emitter, not part of the scene.
 *   mlx_obj_c → MLX90632 "mlx90632" object temperature, °C — the model
 *               synthesises RAM raw values that the driver's DSP5
 *               equations turn back into ~this temperature.
 *   mlx_amb_c → MLX90632 sensor ambient (die) temperature, °C — feeds
 *               temp.ambient through calc_ambient().
 *
 * Defaults (set at definition): illum 100.0, mlx_obj_c 33.0 (skin),
 * mlx_amb_c 30.0.
 */
#ifndef OPTICAL_FEED_H
#define OPTICAL_FEED_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float illum;       /* scene illumination, arb. units (0..~1000)    */
    float mlx_obj_c;   /* MLX90632 object (skin) temperature, °C       */
    float mlx_amb_c;   /* MLX90632 ambient (die) temperature, °C       */
} vbopt_t;

/* Defined in as7341.c (single TU — this header is include-only). */
extern vbopt_t g_vbopt;

/* --------------------------------------------------------------- *
 *  Scenario-facing setters/getters.  Safe to call at any time; the
 *  change is visible from the next conversion each model performs.
 * --------------------------------------------------------------- */
static inline float vbopt_illumination(void)     { return g_vbopt.illum;    }
static inline void  vbopt_set_lux(float lux)     { g_vbopt.illum = lux;     }
static inline void  vbmlx_set_object_c(float c)  { g_vbopt.mlx_obj_c = c;   }
static inline void  vbmlx_set_ambient_c(float c) { g_vbopt.mlx_amb_c = c;   }
static inline float vbmlx_object_c(void)         { return g_vbopt.mlx_obj_c;}
static inline float vbmlx_ambient_c(void)        { return g_vbopt.mlx_amb_c;}

/* --------------------------------------------------------------- *
 *  Device-model registration.  Idempotent; also self-invoked by a
 *  constructor in each device file so the default population exists
 *  without vboard_init() having to know these symbols (vboard_init.c
 *  resolves them weakly as vbdev_as7341_register/vbdev_mlx90632_register).
 * --------------------------------------------------------------- */
void vbdev_as7341_register(void);    /* "as7341"   @0x39, VB_RAIL_1V8 */
void vbdev_mlx90632_register(void);  /* "mlx90632" @0x3A, VB_RAIL_1V8 */

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

#endif /* OPTICAL_FEED_H */
