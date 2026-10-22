/* vq_drv_mic.c — IM69D130 PDM mic (U25) on ESP32-S3 I2S0 PDM-RX.
 * ESP-IDF 5.4 driver/i2s_pdm.h API only (NOT the legacy i2s driver).
 *
 * Wiring (board_pins.h): CLK = IO40 out, DAT = IO41 in, SELECT = GND
 * (SELECT low → the mic is the RIGHT-slot device → I2S_PDM_SLOT_RIGHT).
 *
 * Clock / decimation chain (verified against esp_driver_i2s i2s_pdm.c):
 *   PDM CLK on IO40 = I2S BCLK
 *       = fs_pcm * I2S_LL_PDM_BCK_FACTOR(64)                (S3, fixed)
 *         * (dn_sample_mode == I2S_PDM_DSR_16S ? 2 : 1)
 *       = 16000 * 64 * 2 = 2.048 MHz        with DSR_16S
 *   → CAUTION: 2.048 MHz falls between the IM69D130's characterized
 *     operating-mode bands (400–950 k, 1.05–1.9 M, 2.1–2.65 M,
 *     2.9–3.3 MHz — datasheet Table 4). The PDM interface accepts
 *     0.4–3.3 MHz so it almost certainly still streams, but datasheet
 *     acoustic specs aren't guaranteed in the 1.9–2.1 MHz gap.
 *     Guaranteed-spec alternative: 24 kHz ×128 → 3.072 MHz (the
 *     datasheet headline test clock) — pending bench confirmation.
 *   HW PDM→PCM conversion: selectable sinc decimation ÷16
 *   (rx_pdm_sinc_dsr_16_en = I2S_PDM_DSR_16S) in series with the fixed
 *   ÷8 PDM-RX back-end → net decimation 128 → 1-bit PDM @2.048 MHz
 *   becomes 16-bit PCM @16 kHz, mono, right slot.
 *   bclk_div = 8 → MCLK 16.384 MHz internal (not pinned out).
 *   NOTE: ESP32-S3 has NO HW high-pass in the PDM RX path
 *   (SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER is P4-only), so the mic's DC
 *   offset passes through to the PCM — selftest reports it as `dc`.
 *
 * Lifecycle: this mic never streams. probe()/selftest/capture arm the
 * channel, enable the clock for a bounded window, read DMA into the
 * caller's buffer, then disable again — between captures the clock is
 * off and the mic sits in power-down. sleep() additionally parks IO40
 * low as a plain GPIO. On-demand only, per "optional chest acoustics".
 *
 * Presence detection: no ACK exists. A live IM69D130 always dithers the
 * LSBs (sigma-delta self-noise ≫ digital silence); a flat PCM stream
 * (peak-peak ≈ 0 over ≥100 ms) means unfitted mic, dead part, or DATA
 * pinned — reported as ESP_ERR_NOT_FOUND, which is a soft condition.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"        /* SIG_GPIO_OUT_IDX */
#include "driver/gpio.h"
#include "driver/i2s_pdm.h"

#include "board_pins.h"              /* VQ_MIC_CLK_GPIO / VQ_MIC_DAT_GPIO / VQ_MIC_PDM_CLK_HZ */
#include "vq_sensor.h"
#include "vq_types.h"
#include "vq_drv_mic.h"

static const char *TAG = "vq_mic";

/* ----------------------------- tunables ---------------------------- */
#define MIC_PROBE_MS          150    /* probe capture window            */
#define MIC_SELFTEST_MS       1000   /* selftest capture window         */
#define MIC_SETTLE_MS         50     /* IM69D130 power-on settle — the
                                      * first ~50 ms of data discarded  */
#define MIC_READ_CHUNK_B      1024   /* per i2s_channel_read            */
#define MIC_READ_TIMEOUT_MS   250    /* per-read block ceiling          */
#define MIC_DEADLINE_SLOP_MS  800    /* overall deadline margin         */
#define MIC_PP_FLAT_LSB       2      /* peak-peak ≤ this ⇒ flat stream  */
#define MIC_MAX_ERRS          8      /* healthy() error ceiling         */
#define MIC_SCRATCH_SAMPLES   (VQ_MIC_PCM_RATE_HZ)   /* 1 s, in PSRAM   */

/* --------------------------- driver state -------------------------- */
typedef struct {
    i2s_chan_handle_t rx;            /* NULL until first use            */
    bool        chan_ready;          /* init_pdm_rx_mode done (READY)   */
    bool        running;             /* channel enabled (clock out)     */
    bool        clk_parked;          /* IO40 re-routed + driven low     */
    bool        armed;               /* start() ran: on-demand mode     */
    int16_t    *scratch;             /* PSRAM scratch, MIC_SCRATCH_SAMPLES */
    uint32_t    captures;            /* lifetime capture count          */
    float       last_rms;            /* RMS of last capture, LSB        */
    int64_t     last_t_us;           /* esp_timer of last capture start */
} mic_state_t;

static mic_state_t      s_st;
static SemaphoreHandle_t s_mu;       /* serialises capture paths        */
static vq_sensor_t      *s_sensor_p; /* back-pointer for sink emission  */

/* --------------------------- helpers ------------------------------- */

static esp_err_t mic_lock(void)
{
    if (!s_mu) {
        s_mu = xSemaphoreCreateMutex();
        if (!s_mu) return ESP_ERR_NO_MEM;
    }
    return xSemaphoreTake(s_mu, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_FAIL;
}

static void mic_unlock(void)
{
    if (s_mu) xSemaphoreGive(s_mu);
}

/* Lazy scratch alloc — lives in PSRAM ("buffer in PSRAM"), ~32 KB,
 * allocated once on first probe/selftest and kept for the life of the
 * driver (it's the working buffer for every probe/selftest call).     */
static esp_err_t mic_ensure_scratch(void)
{
    if (s_st.scratch) return ESP_OK;
    s_st.scratch = heap_caps_malloc(MIC_SCRATCH_SAMPLES * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM);
    if (!s_st.scratch) {
        ESP_LOGE(TAG, "no PSRAM for mic scratch (%u B)",
                 (unsigned)(MIC_SCRATCH_SAMPLES * sizeof(int16_t)));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* GPIO config shared by init and un-park. */
static i2s_pdm_rx_gpio_config_t mic_gpio_cfg(void)
{
    i2s_pdm_rx_gpio_config_t g = {
        .clk = VQ_MIC_CLK_GPIO,
        .din = VQ_MIC_DAT_GPIO,
        .invert_flags = { .clk_inv = false },
    };
    return g;
}

/* Ensure channel exists, is PDM-RX initialised (READY, not enabled),
 * and IO40 is owned by the I2S peripheral again if sleep() parked it. */
static esp_err_t mic_ensure_channel(void)
{
    if (!s_st.rx) {
        i2s_chan_config_t ccfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,
                                                          I2S_ROLE_MASTER);
        /* PDM RX is only supported on I2S_NUM_0 (driver enforces).     */
        esp_err_t ret = i2s_new_channel(&ccfg, NULL, &s_st.rx);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    if (!s_st.chan_ready) {
        /* Hardware strap: U25's SELECT pin is tied to GND.  Per the
         * IM69D130 datasheet (Fig. 9 timing / Fig. 10 stereo circuit)
         * and Infineon's L/R convention, SELECT = GND is the RIGHT
         * channel device — it drives DATA during the clock-low
         * half-cycle (HiZ while the clock is high).  With this
         * driver's DSR_16S decimation the right-slot mask is what
         * samples that phase; the default-macro mono slot
         * (I2S_PDM_SLOT_LEFT) would instead sample the high-Z
         * half-cycle, i.e. off-strap.  Fab audit F-03
         * (reports/findings_sensb.md); bench-verify on hardware.     */
        i2s_pdm_rx_slot_config_t mic_slot =
            I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                           I2S_SLOT_MODE_MONO);
        mic_slot.slot_mask = I2S_PDM_SLOT_RIGHT;   /* SELECT=GND → right */
        i2s_pdm_rx_config_t pdm = {
            .clk_cfg = {
                .sample_rate_hz  = VQ_MIC_PCM_RATE_HZ,     /* 16 kHz PCM */
                .clk_src         = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple   = I2S_MCLK_MULTIPLE_256,
                .dn_sample_mode  = I2S_PDM_DSR_16S,  /* ÷16 sinc stage:
                                                      * PDM CLK = fs*128
                                                      * = 2.048 MHz      */
                .bclk_div        = 8,
            },
            /* MONO 16-bit. On S3 the macro carries no HP-filter fields
             * — PDM RX has no high-pass on this chip.                 */
            .slot_cfg = mic_slot,
            .gpio_cfg = mic_gpio_cfg(),
        };
        esp_err_t ret = i2s_channel_init_pdm_rx_mode(s_st.rx, &pdm);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "init_pdm_rx_mode: %s", esp_err_to_name(ret));
            return ret;
        }
        s_st.chan_ready = true;
        s_st.clk_parked = false;
    } else if (s_st.clk_parked) {
        /* sleep() re-routed IO40 to the GPIO output driver; hand it
         * back to the I2S WS/CLK out signal (READY state required).     */
        i2s_pdm_rx_gpio_config_t g = mic_gpio_cfg();
        esp_err_t ret = i2s_channel_reconfig_pdm_rx_gpio(s_st.rx, &g);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "reconfig gpio: %s", esp_err_to_name(ret));
            return ret;
        }
        s_st.clk_parked = false;
    }
    return ESP_OK;
}

/* Bounded DMA capture into `pcm` (mono int16 @16 kHz). Assumes the lock
 * is held and the channel is READY with IO40 un-parked. Leaves the
 * channel DISABLED on return. Returns ESP_OK if ≥1 sample landed.      */
static esp_err_t mic_capture_raw(int16_t *pcm, size_t want, size_t *got)
{
    *got = 0;
    esp_err_t ret = i2s_channel_enable(s_st.rx);   /* clock starts → mic wakes */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "enable: %s", esp_err_to_name(ret));
        return ret;
    }
    s_st.running = true;

    /* IM69D130 needs its start-up time after CLK appears (datasheet
     * ~ms-order; 50 ms is generous): wait, then flush the stale DMA
     * backlog with non-blocking reads before keeping data.            */
    vTaskDelay(pdMS_TO_TICKS(MIC_SETTLE_MS));
    uint8_t d[256];
    size_t br = 0;
    while (i2s_channel_read(s_st.rx, d, sizeof d, &br, 0) == ESP_OK && br) {
        /* discard settle-window data */
    }

    const size_t want_b = want * sizeof(int16_t);
    size_t got_b = 0;
    /* Overall deadline: expected audio time + slop, so a dead channel
     * can never wedge the caller. */
    int64_t deadline = esp_timer_get_time() +
        ((int64_t)(want * 1000 / VQ_MIC_PCM_RATE_HZ) + MIC_DEADLINE_SLOP_MS) * 1000;
    ret = ESP_OK;
    while (got_b < want_b) {
        size_t chunk = want_b - got_b;
        if (chunk > MIC_READ_CHUNK_B) chunk = MIC_READ_CHUNK_B;
        int64_t remain_us = deadline - esp_timer_get_time();
        if (remain_us <= 0) { ret = ESP_ERR_TIMEOUT; break; }
        uint32_t tmo = (uint32_t)(remain_us / 1000);
        if (tmo > MIC_READ_TIMEOUT_MS) tmo = MIC_READ_TIMEOUT_MS;
        esp_err_t r = i2s_channel_read(s_st.rx, (uint8_t *)pcm + got_b,
                                       chunk, &br, tmo);
        if (r != ESP_OK) { ret = r; break; }
        got_b += br;
    }

    esp_err_t dret = i2s_channel_disable(s_st.rx); /* clock stops → mic sleeps */
    if (dret == ESP_OK) s_st.running = false;
    else ESP_LOGW(TAG, "disable: %s", esp_err_to_name(dret));

    *got = got_b / sizeof(int16_t);
    return (*got > 0) ? ESP_OK : (ret != ESP_OK ? ret : ESP_FAIL);
}

/* Peak-peak spread + "stream is alive" test. Flat ⇒ absent/dead.     */
static int32_t mic_peak_to_peak(const int16_t *p, size_t n)
{
    if (!p || !n) return 0;
    int16_t mn = INT16_MAX, mx = INT16_MIN;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < mn) mn = p[i];
        if (p[i] > mx) mx = p[i];
    }
    return (int32_t)mx - (int32_t)mn;
}

static double mic_mean(const int16_t *p, size_t n)
{
    if (!p || !n) return 0.0;
    double acc = 0.0;
    for (size_t i = 0; i < n; i++) acc += p[i];
    return acc / (double)n;
}

/* Emit one block-summary sample to the pipeline sink (if registered):
 * VQ_CH_MIC value = capture RMS in LSB — engineering units, no SI
 * claim. t_us = capture start (ESTIMATED_TS: block-level stamp).     */
static void mic_emit_block_stat(int64_t t0_us, float rms)
{
    if (!s_sensor_p || !s_sensor_p->sink) return;
    vq_sample_t smp = {
        .channel = VQ_CH_MIC,
        .t_us    = t0_us,
        .value   = rms,
        .flags   = VQ_SAMPLE_F_ESTIMATED_TS,
    };
    s_sensor_p->sink(s_sensor_p->sink_ctx, &smp, 1);
}

/* ========================== public API ============================= */

float vq_mic_rms(const int16_t *pcm, size_t n)
{
    if (!pcm || !n) return 0.0f;
    double acc = 0.0;                  /* double: ≤ 1e13 for 1 s of FS  */
    for (size_t i = 0; i < n; i++) {
        double v = (double)pcm[i];
        acc += v * v;
    }
    return sqrtf((float)(acc / (double)n));
}

esp_err_t vq_mic_capture(float seconds, int16_t *pcm, size_t max, size_t *n)
{
    if (!pcm || max == 0 || !(seconds > 0.0f)) return ESP_ERR_INVALID_ARG;
    size_t want = (size_t)(seconds * (float)VQ_MIC_PCM_RATE_HZ + 0.5f);
    if (want == 0) want = 1;
    if (want > max) want = max;

    esp_err_t ret = mic_lock();
    if (ret != ESP_OK) return ret;

    ret = mic_ensure_channel();
    if (ret == ESP_OK) {
        int64_t t0 = esp_timer_get_time();
        size_t got = 0;
        ret = mic_capture_raw(pcm, want, &got);
        s_st.last_t_us = t0;
        s_st.last_rms  = vq_mic_rms(pcm, got);
        if (ret == ESP_OK) {
            s_st.captures++;
            mic_emit_block_stat(t0, s_st.last_rms);
        } else if (s_sensor_p) {
            s_sensor_p->err_count++;
        }
        if (n) *n = got;
    } else if (n) {
        *n = 0;
    }

    mic_unlock();
    return ret;
}

esp_err_t vq_mic_sleep(void)
{
    esp_err_t ret = mic_lock();
    if (ret != ESP_OK) return ret;

    if (s_st.rx && s_st.running) {
        /* Stops the I2S clock output → IM69D130 drops to power-down.   */
        esp_err_t r = i2s_channel_disable(s_st.rx);
        if (r == ESP_OK) s_st.running = false;
        else ESP_LOGW(TAG, "sleep disable: %s", esp_err_to_name(r));
    }
    /* Re-route IO40 from whatever owns it (I2S CLK out, or nothing yet)
     * to the plain GPIO output register and hold it low — deterministic
     * low clock regardless of the I2S idle level, safe even if the
     * channel was never created. IO41 (DATA) is left alone: it is
     * already an input and sources nothing.                           */
    esp_rom_gpio_connect_out_signal(VQ_MIC_CLK_GPIO, SIG_GPIO_OUT_IDX, 0, 0);
    gpio_set_level(VQ_MIC_CLK_GPIO, 0);
    gpio_set_direction(VQ_MIC_CLK_GPIO, GPIO_MODE_OUTPUT);
    s_st.clk_parked = true;            /* un-parked on next ensure/init */
    s_st.armed = false;

    mic_unlock();
    return ESP_OK;
}

esp_err_t vq_mic_selftest(char *report, size_t len)
{
    if (!report || !len) return ESP_ERR_INVALID_ARG;
    report[0] = '\0';

    esp_err_t ret = mic_ensure_scratch();
    if (ret != ESP_OK) {
        snprintf(report, len, "mic_pdm: FAIL no PSRAM scratch");
        return ret;
    }

    size_t n = 0;
    ret = vq_mic_capture((float)MIC_SELFTEST_MS / 1000.0f,
                         s_st.scratch, MIC_SCRATCH_SAMPLES, &n);
    if (ret != ESP_OK || n == 0) {
        snprintf(report, len,
                 "mic_pdm: FAIL capture ret=%s n=%u — mic absent or I2S fault (soft condition)",
                 esp_err_to_name(ret), (unsigned)n);
        return (ret != ESP_OK) ? ret : ESP_FAIL;
    }

    float    rms  = vq_mic_rms(s_st.scratch, n);
    int32_t  pp   = mic_peak_to_peak(s_st.scratch, n);
    double   dc   = mic_mean(s_st.scratch, n);
    float    dbfs = (rms > 0.0f) ? 20.0f * log10f(rms / 32768.0f) : -120.0f;
    bool     alive = (pp > MIC_PP_FLAT_LSB);

    /* Non-silence heuristic: pp > a few LSB proves a live sigma-delta
     * stream. A silent room may legitimately read LOW RMS — that alone
     * is NOT a failure; only a perfectly flat stream or dead capture is.
     * Tap the mic port to verify acoustic coupling if in doubt.        */
    snprintf(report, len,
             "mic_pdm: %s n=%u rms=%.1f LSB (%.1f dBFS) pp=%ld dc=%.1f | "
             "clk=2.048MHz pcm=16k mono/R | silent room may legitimately "
             "read low — flat stream (pp~0) = absent; tap mic to verify",
             alive ? "PASS" : "WARN-LOW",
             (unsigned)n, rms, dbfs, (long)pp, dc);
    if (!alive) return ESP_ERR_NOT_FOUND;   /* soft: pipeline tolerates */
    return ESP_OK;
}

/* ======================= vq_sensor_ops glue ========================= */

static vq_err_t mic_op_probe(vq_sensor_t *s)
{
    /* "Probe" = capture-and-check-non-silence on demand. ~150 ms.      */
    esp_err_t ret = mic_ensure_scratch();
    if (ret != ESP_OK) return ret;

    size_t n = 0;
    ret = vq_mic_capture((float)MIC_PROBE_MS / 1000.0f,
                         s_st.scratch, MIC_SCRATCH_SAMPLES, &n);
    if (ret != ESP_OK || n == 0) {
        if (s) s->err_count++;
        return ESP_ERR_NOT_FOUND;      /* soft: tolerated absence       */
    }
    if (mic_peak_to_peak(s_st.scratch, n) <= MIC_PP_FLAT_LSB) {
        ESP_LOGW(TAG, "probe: flat stream — mic likely absent");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static vq_err_t mic_op_init(vq_sensor_t *s)
{
    (void)s;
    esp_err_t ret = mic_lock();
    if (ret != ESP_OK) return ret;
    ret = mic_ensure_channel();        /* leave READY, not streaming     */
    mic_unlock();
    return ret;
}

static vq_err_t mic_op_start(vq_sensor_t *s)
{
    (void)s;
    /* On-demand capture mode ONLY — the mic never streams. Arming means
     * the channel exists and is READY; captures run via vq_mic_capture. */
    esp_err_t ret = mic_lock();
    if (ret != ESP_OK) return ret;
    ret = mic_ensure_channel();
    if (ret == ESP_OK) s_st.armed = true;
    mic_unlock();
    return ret;
}

static vq_err_t mic_op_stop(vq_sensor_t *s)
{
    (void)s;
    esp_err_t ret = mic_lock();
    if (ret != ESP_OK) return ret;
    if (s_st.rx && s_st.running) {
        i2s_channel_disable(s_st.rx);  /* clock stops → mic sleeps       */
        s_st.running = false;
    }
    s_st.armed = false;
    mic_unlock();
    return ESP_OK;
}

static vq_err_t mic_op_sleep(vq_sensor_t *s)
{
    (void)s;
    return vq_mic_sleep();
}

static bool mic_op_healthy(vq_sensor_t *s)
{
    return s_st.chan_ready && (!s || s->err_count < MIC_MAX_ERRS);
}

static const vq_channel_id_t s_mic_channels[] = { VQ_CH_MIC };

static const vq_sensor_ops_t s_mic_ops = {
    .name       = "im69d130",
    .probe      = mic_op_probe,
    .init       = mic_op_init,
    .start      = mic_op_start,      /* arm on-demand mode — never streams */
    .stop       = mic_op_stop,
    .sleep      = mic_op_sleep,
    .healthy    = mic_op_healthy,
    .channels   = s_mic_channels,
    .n_channels = sizeof(s_mic_channels) / sizeof(s_mic_channels[0]),
    .rate_hz    = (float)VQ_MIC_PCM_RATE_HZ,   /* 16 kHz PCM (block ch) */
    .required   = false,             /* absent is tolerated             */
};

static vq_sensor_t s_mic_sensor = { .ops = &s_mic_ops };

esp_err_t vq_mic_register(void)
{
    if (!s_mu) {
        s_mu = xSemaphoreCreateMutex();
        if (!s_mu) return ESP_ERR_NO_MEM;
    }
    s_mic_sensor.priv = &s_st;
    s_sensor_p = &s_mic_sensor;
    esp_err_t err = vq_sensor_register(&s_mic_sensor);
    /* The registry owns the live copy — sink/counters live there.      */
    s_sensor_p = vq_sensor_find(s_mic_sensor.ops->name);
    if (!s_sensor_p) s_sensor_p = &s_mic_sensor;
    return err;
}
