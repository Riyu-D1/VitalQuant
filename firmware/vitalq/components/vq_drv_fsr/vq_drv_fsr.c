/* vq_drv_fsr.c — FSR402 contact sensor on IO1 / ADC1_CH0.
 *
 * Front end: +3V3 --[FSR]--+-- FSR_ADC (IO1)
 *                          |
 *                        [R8 10k]
 *                          |
 *                         GND
 * adc_oneshot @ 12-bit / ADC_ATTEN_DB_12 (~3.1 V full scale); optional
 * adc_cali converts counts->mV for selftest/console text only — the
 * emitted channel value stays an ORDINAL level, NO SI UNITS.
 *
 * Lifecycle (vq_sensor.h contract): probe() claims ADC1 + verifies the
 * IO1<->ADC1_CH0 mapping; init() configures the channel + timer;
 * start() arms a 20 Hz esp_timer whose callback reads the ADC, stamps
 * esp_timer µs AT the read (the read is the acquisition — no data-ready
 * edge exists), maps counts -> ordinal band with hysteresis, and pushes
 * one vq_sample_t to s_sensor.sink. stop() halts the timer but keeps
 * the ADC claimed; sleep() releases ADC1 + cali and parks the LED low
 * (sleep walk only reaches sensor ops; gpio holds are power_mgr's).
 *
 * Band edges are engineering thresholds in raw counts — per-enclosure
 * tuning belongs in the hardware profile, not here. Missing FSR ==
 * level OFF by design (passive divider, nothing to ACK).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "board_pins.h"     /* VQ_FSR_ADC_GPIO, VQ_STATUS_LED_GPIO */
#include "vq_types.h"
#include "vq_sensor.h"
#include "vq_drv_fsr.h"
#include "vq_drv_fsr_priv.h"

static const char *TAG = "vq_drv_fsr";

/* -------------------- module-private constants ----------------------- */
#define VQ_FSR_ADC_UNIT        ADC_UNIT_1
#define VQ_FSR_ADC_CHAN        ADC_CHANNEL_0    /* IO1 == ADC1_CH0 (S3)  */
#define VQ_FSR_ADC_ATTEN       ADC_ATTEN_DB_12
#define VQ_FSR_ADC_BITWIDTH    ADC_BITWIDTH_12
#define VQ_FSR_RATE_HZ         20.0f
#define VQ_FSR_PERIOD_US       (1000000ULL / 20ULL)

/* Ordinal band edges, raw 12-bit counts (divider rises with pressure).
 * FSR>R8 ratio: rest ~MOhm -> ~0; ~100k light -> ~370; ~10k seated ->
 * ~2050; ~1k press -> ~3720. Edges sit mid-way between plateaus. */
#define VQ_FSR_CNT_OFF_MAX       150
#define VQ_FSR_CNT_LIGHT_MAX    1000
#define VQ_FSR_CNT_SEATED_MAX   2800
#define VQ_FSR_CNT_HYST           60   /* crossing hysteresis, counts   */

#define VQ_FSR_ERR_HEALTH_MAX      5   /* consecutive read errors       */
#define VQ_FSR_PRESS_WINDOW_MS  2500   /* selftest "press now" window   */

/* -------------------- driver state ---------------------------------- */
typedef struct {
    adc_oneshot_unit_handle_t adc;
    adc_cali_handle_t         cali;
    esp_timer_handle_t        tmr;
    SemaphoreHandle_t         lock;        /* hw ensure/teardown mutex */
    bool                      hw_ready;
    bool                      registered;
    volatile uint32_t         consec_err;
    int                       last_level;  /* -1 = unknown (hysteresis) */
    int                       last_raw;
} vq_fsr_state_t;

static vq_fsr_state_t s_fsr;
static vq_sensor_t   *s_reg;         /* registry copy — sink/counters  */
static vq_sensor_t    s_sensor;            /* .ops set at register()    */

static void fsr_timer_cb(void *arg);       /* defined below            */

/* ------------------------------------------------------------------ */
static void fsr_lock(void)
{
    if (!s_fsr.lock) {
        /* Boot-time single creation is the norm; this only guards an
         * exotic first call from two tasks. xSemaphoreCreateMutex uses
         * internal-heap malloc which is legal under a port critical. */
        static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&mux);
        if (!s_fsr.lock) s_fsr.lock = xSemaphoreCreateMutex();
        portEXIT_CRITICAL(&mux);
    }
    if (s_fsr.lock) xSemaphoreTake(s_fsr.lock, portMAX_DELAY);
}

static void fsr_unlock(void)
{
    if (s_fsr.lock) xSemaphoreGive(s_fsr.lock);
}

/* counts -> ordinal band, no hysteresis (single-shot use). */
static int fsr_band(int raw)
{
    if (raw < VQ_FSR_CNT_OFF_MAX)    return VQ_FSR_LEVEL_OFF;
    if (raw < VQ_FSR_CNT_LIGHT_MAX)  return VQ_FSR_LEVEL_LIGHT;
    if (raw < VQ_FSR_CNT_SEATED_MAX) return VQ_FSR_LEVEL_SEATED;
    return VQ_FSR_LEVEL_EXCESSIVE;
}

/* Band edges of `level`'s top boundary (for hysteresis). */
static int fsr_band_top_edge(int level)
{
    switch (level) {
    case VQ_FSR_LEVEL_OFF:    return VQ_FSR_CNT_OFF_MAX;
    case VQ_FSR_LEVEL_LIGHT:  return VQ_FSR_CNT_LIGHT_MAX;
    default:                  return VQ_FSR_CNT_SEATED_MAX; /* seated+ */
    }
}

/* Ordinal with hysteresis: level only rises once raw clears the old
 * band's top edge + HYST, and only falls once it dips under it - HYST. */
static int fsr_level_hyst(int raw, int prev)
{
    int band = fsr_band(raw);
    if (prev < 0 || band == prev) return band;
    int edge = fsr_band_top_edge(prev);       /* edge we're crossing  */
    if (band > prev) return (raw >= edge + VQ_FSR_CNT_HYST) ? band : prev;
    /* falling: the relevant edge is the top of the band BELOW prev */
    edge = fsr_band_top_edge(band);
    return (raw < edge - VQ_FSR_CNT_HYST) ? band : prev;
}

/* -------------------- hardware bring-up ------------------------------ */

/* Caller holds s_fsr.lock. Idempotent. */
static esp_err_t fsr_hw_init_locked(void)
{
    if (s_fsr.hw_ready) return ESP_OK;

    adc_oneshot_unit_init_cfg_t ucfg = {
        .unit_id  = VQ_FSR_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&ucfg, &s_fsr.adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t ccfg = {
        .atten    = VQ_FSR_ADC_ATTEN,
        .bitwidth = VQ_FSR_ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_fsr.adc, VQ_FSR_ADC_CHAN, &ccfg);
    if (err != ESP_OK) goto fail_unit;

    /* Optional mV calibration — enriches selftest/console text only;
     * absent eFuse cal data is not an error. */
    s_fsr.cali = NULL;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    {
        adc_cali_curve_fitting_config_t calcfg = {
            .unit_id  = VQ_FSR_ADC_UNIT,
            .chan     = VQ_FSR_ADC_CHAN,
            .atten    = VQ_FSR_ADC_ATTEN,
            .bitwidth = VQ_FSR_ADC_BITWIDTH,
        };
        if (adc_cali_create_scheme_curve_fitting(&calcfg, &s_fsr.cali) != ESP_OK)
            s_fsr.cali = NULL;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    {
        /* NB: no .default_vref — that field exists only on the
         * CONFIG_IDF_TARGET_ESP32 variant of this struct. */
        adc_cali_line_fitting_config_t calcfg = {
            .unit_id  = VQ_FSR_ADC_UNIT,
            .atten    = VQ_FSR_ADC_ATTEN,
            .bitwidth = VQ_FSR_ADC_BITWIDTH,
        };
        if (adc_cali_create_scheme_line_fitting(&calcfg, &s_fsr.cali) != ESP_OK)
            s_fsr.cali = NULL;
    }
#endif
    if (!s_fsr.cali)
        ESP_LOGW(TAG, "adc_cali unavailable — selftest reports counts only");

    if (!s_fsr.tmr) {
        const esp_timer_create_args_t tcfg = {
            .callback        = &fsr_timer_cb,   /* fwd-decl'd below    */
            .name            = "fsr20hz",
            .dispatch_method = ESP_TIMER_TASK,  /* task ctx, not ISR   */
            .skip_unhandled_events = true,
        };
        err = esp_timer_create(&tcfg, &s_fsr.tmr);
        if (err != ESP_OK) goto fail_cali;
    }

    s_fsr.hw_ready  = true;
    s_fsr.consec_err = 0;
    s_fsr.last_level = -1;
    return ESP_OK;

fail_cali:
    if (s_fsr.cali) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(s_fsr.cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(s_fsr.cali);
#endif
        s_fsr.cali = NULL;
    }
fail_unit:
    adc_oneshot_del_unit(s_fsr.adc);
    s_fsr.adc = NULL;
    return err;
}

static esp_err_t fsr_hw_init(void)
{
    fsr_lock();
    esp_err_t err = fsr_hw_init_locked();
    fsr_unlock();
    return err;
}

/* Full teardown; caller holds s_fsr.lock (mutex is non-recursive, so
 * probe/start share this with the outer fsr_hw_deinit wrapper). Note
 * esp_timer_stop() only DISARMS the timer — it does NOT wait for a
 * callback already dispatched in the esp_timer task. Safety comes from
 * the lock instead: the cb snapshots s_fsr.adc under s_fsr.lock, so a
 * late cb either runs its read BEFORE we free the unit, or waits on
 * the lock and then sees adc == NULL. */
static void fsr_hw_deinit_locked(void)
{
    if (s_fsr.tmr) esp_timer_stop(s_fsr.tmr);
    if (s_fsr.adc) {
        adc_oneshot_del_unit(s_fsr.adc);
        s_fsr.adc = NULL;
    }
    if (s_fsr.cali) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(s_fsr.cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(s_fsr.cali);
#endif
        s_fsr.cali = NULL;
    }
    s_fsr.hw_ready   = false;
    s_fsr.last_level = -1;
}

static void fsr_hw_deinit(void)
{
    fsr_lock();
    fsr_hw_deinit_locked();
    fsr_unlock();
}

/* 20 Hz sampling — esp_timer TASK context. Takes the hw lock around the
 * handle snapshot + read: teardown (sleep) can then never free the unit
 * mid-read — a cb dispatched just before esp_timer_stop() waits on the
 * lock, sees adc == NULL, and returns. adc_oneshot_read is ~50 us, fine
 * on a 50 ms period. Lock is released before the sink push (sink may be
 * slow; the read serialization is all teardown needs). */
static void fsr_timer_cb(void *arg)
{
    (void)arg;
    int raw = 0;
    int64_t t_us;
    esp_err_t err;

    fsr_lock();
    if (!s_fsr.adc) { fsr_unlock(); return; }
    t_us = esp_timer_get_time();           /* stamp AT acquisition    */
    err = adc_oneshot_read(s_fsr.adc, VQ_FSR_ADC_CHAN, &raw);
    fsr_unlock();

    vq_sensor_t *rs = s_reg ? s_reg : &s_sensor;   /* registry copy    */

    if (err != ESP_OK) {
        s_fsr.consec_err++;
        rs->err_count++;
        return;
    }
    s_fsr.consec_err = 0;
    s_fsr.last_raw   = raw;
    s_fsr.last_level = fsr_level_hyst(raw, s_fsr.last_level);

    vq_sample_t smp = {
        .channel = VQ_CH_CONTACT,
        .t_us    = t_us,
        .value   = (float)s_fsr.last_level,  /* ordinal, no SI units */
        .flags   = 0,
    };
    rs->sample_count++;
    rs->last_t_us = t_us;
    if (rs->sink) rs->sink(rs->sink_ctx, &smp, 1);
}

/* -------------------- vq_sensor_ops ---------------------------------- */

static vq_err_t fsr_probe(vq_sensor_t *s)
{
    (void)s;
    /* No bus ACK exists for a passive divider — probeable resource is
     * the MCU ADC itself plus the silicon's IO1<->ADC1_CH0 mapping.
     * Cheap: one unit alloc + one ROM table lookup, no sampling. */
    fsr_lock();
    esp_err_t err = fsr_hw_init_locked();
    if (err == ESP_OK) {
        int io = -1;   /* signature is (unit, chan, int *io_num)       */
        if (adc_oneshot_channel_to_io(VQ_FSR_ADC_UNIT, VQ_FSR_ADC_CHAN, &io) != ESP_OK
            || io != (int)VQ_FSR_ADC_GPIO) {
            ESP_LOGE(TAG, "ADC1_CH0 maps to IO%d, expected IO%d",
                     io, (int)VQ_FSR_ADC_GPIO);
            err = ESP_ERR_NOT_FOUND;
        }
    }
    if (err != ESP_OK) fsr_hw_deinit_locked();  /* leave nothing claimed */
    fsr_unlock();
    return err;
}

static vq_err_t fsr_init(vq_sensor_t *s)
{
    (void)s;
    esp_err_t err = fsr_hw_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "fsr: IO%d ADC1_CH0 12-bit/DB_12, 20 Hz, cali %s",
                 (int)VQ_FSR_ADC_GPIO, s_fsr.cali ? "on" : "off");
    }
    return err;
}

static vq_err_t fsr_start(vq_sensor_t *s)
{
    fsr_lock();
    esp_err_t err = fsr_hw_init_locked();
    if (err == ESP_OK) {
        s_fsr.last_level = -1;
        s_fsr.consec_err = 0;
        /* start_periodic on a RUNNING timer returns INVALID_STATE —
         * stop first (its INVALID_STATE on a stopped timer is benign)
         * so start() is a legal re-arm, not just first-arm. */
        esp_timer_stop(s_fsr.tmr);
        err = esp_timer_start_periodic(s_fsr.tmr, VQ_FSR_PERIOD_US);
    }
    fsr_unlock();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "fsr streaming @ 20 Hz -> sink %s",
                 s->sink ? "attached" : "(null, counting only)");
    }
    return err;
}

static vq_err_t fsr_stop(vq_sensor_t *s)
{
    (void)s;
    /* ADC stays claimed for fast restart + vq_fsr_raw()/selftest;
     * only the streaming timer stops. */
    if (s_fsr.tmr) esp_timer_stop(s_fsr.tmr);
    return ESP_OK;
}

static vq_err_t fsr_sleep(vq_sensor_t *s)
{
    (void)s;
    fsr_hw_deinit();          /* ADC unit + cali released, timer stopped */
    vq_status_led_park();     /* LED off + driven low; holds are pwr_mgr */
    return ESP_OK;
}

static bool fsr_healthy(vq_sensor_t *s)
{
    (void)s;
    return s_fsr.hw_ready && s_fsr.consec_err < VQ_FSR_ERR_HEALTH_MAX;
}

/* Channel doc — VQ_CH_CONTACT "contact": ORDINAL level 0..3
 * (off/light/seated/excessive), NO SI UNITS — not force, not pressure.
 * Bands are engineering thresholds for contact-quality gating only. */
static const vq_channel_id_t s_fsr_channels[] = { VQ_CH_CONTACT };

static const vq_sensor_ops_t s_fsr_ops = {
    .name       = "fsr",
    .probe      = fsr_probe,
    .init       = fsr_init,
    .start      = fsr_start,
    .stop       = fsr_stop,
    .sleep      = fsr_sleep,
    .healthy    = fsr_healthy,
    .channels   = s_fsr_channels,
    .n_channels = sizeof(s_fsr_channels) / sizeof(s_fsr_channels[0]),
    .rate_hz    = VQ_FSR_RATE_HZ,
    .required   = false,      /* absent FSR is indistinguishable from OFF */
};

/* -------------------- public API ------------------------------------- */

esp_err_t vq_fsr_register(void)
{
    if (s_fsr.registered) return ESP_OK;
    s_sensor.ops = &s_fsr_ops;
    esp_err_t err = vq_sensor_register(&s_sensor);
    if (err == ESP_OK) {
        s_reg = vq_sensor_find(s_sensor.ops->name);
        s_fsr.registered = true;
        ESP_LOGI(TAG, "registered sensor 'fsr' (contact, 20 Hz)");
    }
    return err;
}

esp_err_t vq_fsr_raw(int *counts)
{
    if (!counts) return ESP_ERR_INVALID_ARG;
    esp_err_t err = fsr_hw_init();
    if (err != ESP_OK) return err;
    fsr_lock();
    err = s_fsr.adc ? adc_oneshot_read(s_fsr.adc, VQ_FSR_ADC_CHAN, counts)
                    : ESP_ERR_INVALID_STATE;
    fsr_unlock();
    return err;
}

esp_err_t vq_fsr_selftest(char *report, size_t len)
{
    if (!report || len == 0) return ESP_ERR_INVALID_ARG;
    report[0] = '\0';
    esp_err_t err = fsr_hw_init();
    if (err != ESP_OK) {
        snprintf(report, len, "fsr_adc: FAIL adc init (%s)",
                 esp_err_to_name(err));
        return err;
    }

    int rest = -1, pressed = -1, mv_r = -1, mv_p = -1;
    err = vq_fsr_raw(&rest);
    if (err != ESP_OK) {
        snprintf(report, len, "fsr_adc: FAIL rest read (%s)",
                 esp_err_to_name(err));
        return err;
    }
    if (s_fsr.cali) adc_cali_raw_to_voltage(s_fsr.cali, rest, &mv_r);

    /* Interactive leg: LED on = "press and hold the FSR pad now". */
    ESP_LOGI(TAG, "fsr_adc: rest=%d — press and hold the FSR pad...",
             rest);
    vq_status_led_init();
    vq_status_led(true);
    vTaskDelay(pdMS_TO_TICKS(VQ_FSR_PRESS_WINDOW_MS));
    esp_err_t err2 = vq_fsr_raw(&pressed);
    vq_status_led(false);
    if (err2 != ESP_OK) {
        snprintf(report, len, "fsr_adc: rest=%d, FAIL pressed read (%s)",
                 rest, esp_err_to_name(err2));
        return err2;
    }
    if (s_fsr.cali) adc_cali_raw_to_voltage(s_fsr.cali, pressed, &mv_p);

    int delta = pressed - rest;
    int n = snprintf(report, len,
                     "fsr_adc: rest=%d pressed=%d delta=%d counts",
                     rest, pressed, delta);
    if (n > 0 && (size_t)n < len && mv_r >= 0 && mv_p >= 0) {
        n += snprintf(report + n, len - (size_t)n, " (~%d/~%d mV)",
                      mv_r, mv_p);
    }
    if (n > 0 && (size_t)n < len) {
        snprintf(report + n, len - (size_t)n,
                 " bands %d->%d%s (ordinal, no SI units)",
                 fsr_band(rest), fsr_band(pressed),
                 delta > 200 ? " RESPONDING" : " no-change?was-pad-pressed");
    }
    return ESP_OK;
}
