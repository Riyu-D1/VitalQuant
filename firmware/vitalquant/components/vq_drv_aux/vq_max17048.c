/* vq_max17048.c — Maxim MAX17048G+ fuel gauge (U17) @ VQ_ADDR_MAX17048.
 *
 * fix_v22 hardware context (hardware/pcb/pin_audit_r6/U17_MAX17048.md):
 *   - VDD is wired to VBAT → ALWAYS-ON whenever a cell is attached
 *     (no battery → no ACK → probe simply reports absent).
 *   - ALRT (open-drain) → GAUGE_ALRT IO6 via R74 10k to +3V3; this line
 *     is the deep-sleep EXT1 wake source AND the battery-cutoff trip —
 *     see power_mgr.h. The IC must therefore never be I2C-slept:
 *     sleep() is a documented no-op here.
 *   - QSTRT is grounded → software quick-start (MODE bit14) is the only
 *     path, kept for console/debug use ("use sparingly" per datasheet:
 *     it re-seeds SOC from instantaneous VCELL and can jump the result).
 *   - The cell is assumed UNPROTECTED (power_mgr.h): the firmware
 *     cut-off runs off this gauge, so a wedged/absent gauge is a real
 *     fault — descriptor is required=true.
 *
 * All registers are 16-bit, MSB first; the datasheet requires full-word
 * writes (8-bit writes take no effect). Reference: Maxim DS 19-4209
 * (MAX17048/MAX17049) Rev 7.
 */
#include "vq_aux_priv.h"
#include "vq_drv_aux.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "vq_max17048";

/* ---- register map (16-bit, MSB first) -------------------------------- */
#define GA_REG_VCELL    0x02u   /* 78.125 µV/LSB                        */
#define GA_REG_SOC      0x04u   /* 1 %/256 per LSB                      */
#define GA_REG_MODE     0x06u   /* bit14 QuickStart, bit13 EnSleep      */
#define GA_REG_VERSION  0x08u   /* probe: (v & 0xFFF0) == 0x0010        */
#define GA_REG_CONFIG   0x0Cu   /* RCOMP | SLEEP ALSC ALRT ATHD[4:0]    */
#define GA_REG_VALRT    0x14u   /* MSB=MIN byte, LSB=MAX byte, 20 mV    */
#define GA_REG_CRATE    0x16u   /* signed, 0.208 %/h per LSB            */
#define GA_REG_STATUS   0x1Au   /* VQ_GAUGE_ST_* flags, low byte        */
#define GA_REG_CMD      0xFEu   /* 0x5400 = POR (NACKs on purpose)      */

#define GA_MODE_QUICKSTART  0x4000u
#define GA_MODE_ENSLEEP     0x2000u

#define GA_CFG_SLEEP    0x0080u /* low byte bit7 — requires EnSleep     */
#define GA_CFG_ALSC     0x0040u /* SOC-change alert enable (left off)   */
#define GA_CFG_ALRT     0x0020u /* master alert flag — write 0 to clear */
#define GA_CFG_ATHD     0x001Fu /* alert% = (32 - ATHD)                 */

#define GA_STATUS_FLAGS 0x003Fu /* RI|VH|VL|VR|HD|SC — low byte, w0c    */

#define GA_VERSION_ID   0x0010u /* (ver & 0xFFF0) == this               */
#define GA_VERSION_MASK 0xFFF0u

/* Alert thresholds — from board_pins.h rails + task spec:
 *   VALRT.MIN ≈ VQ_BATT_LOW_MV (3.2 V → stop loads / deep sleep)
 *   VALRT.MAX ≈ 4.3 V (abs-max guard above the 4.2 V charger target)
 *   ATHD      = 32 - VQ_SOC_LOW_PCT → SOC-low alert at ~5 %
 * TODO(datasheet): VALRT 20 mV/LSB and the MIN/MAX byte order are per
 * DS 19-4209 Rev 7 (byte@0x14=MIN, byte@0x15=MAX; POR default 0x00FF is
 * "no window") — verify codes on silicon during §9 bring-up.          */
#define GA_VALRT_LSB_MV   20u
#define GA_VALRT_MAX_MV   4300u
#define GA_VALRT_MIN_MV   VQ_BATT_LOW_MV
#define GA_ATHD_FOR(pct)  ((uint16_t)(32u - (pct)))

#define GA_PERIOD_US    5000000LL  /* 0.2 Hz cadence                    */
#define GA_HEALTH_PERIODS 3
#define GA_TASK_STACK   3072
#define GA_TASK_PRIO    4

/* ---- private state --------------------------------------------------- */

typedef struct {
    bool          present;
    uint16_t      version;
    /* cadence/alert task — single owner of all int64 schedule fields  */
    TaskHandle_t  task;
    volatile int  streaming;        /* emit samples each period        */
    volatile int  stream_reset;     /* ops → task: re-anchor next_due  */
    volatile int  run;              /* task alive flag                 */
    int64_t       next_due_us;      /* written ONLY by the task        */
    uint32_t      last_ok_ms;       /* task writes; healthy() reads    */
    /* alert path                                                    */
    volatile uint32_t alert_edges;  /* ISR increments                  */
    volatile int      alert_pending;/* ISR → task                      */
    void (*alert_cb)(void *);
    void *alert_cb_ctx;
    bool          isr_installed;
} gauge_priv_t;

static gauge_priv_t s_gauge;

/* ---- bus helpers ------------------------------------------------------ */

static esp_err_t ga_rd16(uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    esp_err_t e = vq_i2c_read_reg(VQ_ADDR_MAX17048, reg, b, sizeof(b));
    if (e == ESP_OK) {
        *v = ((uint16_t)b[0] << 8) | b[1];
    }
    return e;
}

static esp_err_t ga_wr16(uint8_t reg, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };
    return vq_i2c_write_reg(VQ_ADDR_MAX17048, reg, b, sizeof(b));
}

/* ---- public data-plane ------------------------------------------------ */

esp_err_t vq_gauge_read(float *vcell_v, float *soc_pct)
{
    uint16_t vraw = 0, sraw = 0;
    if (vcell_v != NULL) {
        esp_err_t e = ga_rd16(GA_REG_VCELL, &vraw);
        if (e != ESP_OK) {
            return e;
        }
        *vcell_v = (float)vraw * 78.125e-6f;    /* 78.125 µV/LSB */
    }
    if (soc_pct != NULL) {
        esp_err_t e = ga_rd16(GA_REG_SOC, &sraw);
        if (e != ESP_OK) {
            return e;
        }
        *soc_pct = (float)sraw / 256.0f;        /* 1/256 % per LSB */
    }
    s_gauge.present = true;                     /* reads work → present */
    return ESP_OK;
}

esp_err_t vq_gauge_crate(float *pct_per_hr)
{
    if (pct_per_hr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t raw = 0;
    esp_err_t e = ga_rd16(GA_REG_CRATE, &raw);
    if (e != ESP_OK) {
        return e;
    }
    *pct_per_hr = (float)(int16_t)raw * 0.208f; /* signed %/hr */
    return ESP_OK;
}

/* ---- alert path -------------------------------------------------------- */

esp_err_t vq_gauge_alert_status(uint8_t *flags)
{
    if (flags == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t st = 0;
    esp_err_t e = ga_rd16(GA_REG_STATUS, &st);
    if (e == ESP_OK) {
        *flags = (uint8_t)(st & GA_STATUS_FLAGS);
    }
    return e;
}

esp_err_t vq_gauge_clear_alert(void)
{
    /* 1) clear the latched causes — STATUS bits are write-0-to-clear;
     *    don't-care bits are written back as read (datasheet-safe).    */
    uint16_t st = 0;
    esp_err_t e = ga_rd16(GA_REG_STATUS, &st);
    if (e != ESP_OK) {
        return e;
    }
    if (st & GA_STATUS_FLAGS) {
        e = ga_wr16(GA_REG_STATUS, (uint16_t)(st & ~GA_STATUS_FLAGS));
        if (e != ESP_OK) {
            return e;
        }
    }
    /* 2) clear the master flag → ALRT (open-drain) releases high and
     *    the next threshold crossing can edge again.                   */
    uint16_t cfg = 0;
    e = ga_rd16(GA_REG_CONFIG, &cfg);
    if (e != ESP_OK) {
        return e;
    }
    if (cfg & GA_CFG_ALRT) {
        e = ga_wr16(GA_REG_CONFIG, (uint16_t)(cfg & ~GA_CFG_ALRT));
    }
    return e;
}

esp_err_t vq_gauge_config_alerts(void)
{
    /* VALRT: MIN in MSB (byte@0x14), MAX in LSB (byte@0x15), 20 mV/LSB */
    uint16_t vmin = GA_VALRT_MIN_MV / GA_VALRT_LSB_MV;   /* 160 → 3.20 V */
    uint16_t vmax = GA_VALRT_MAX_MV / GA_VALRT_LSB_MV;   /* 215 → 4.30 V */
    uint16_t valrt = (uint16_t)((vmin << 8) | vmax);
    esp_err_t e = ga_wr16(GA_REG_VALRT, valrt);
    if (e != ESP_OK) {
        return e;
    }
    uint16_t rb = 0;
    if ((e = ga_rd16(GA_REG_VALRT, &rb)) != ESP_OK) {
        return e;
    }
    if (rb != valrt) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* CONFIG: set ATHD → SOC-low alert at VQ_SOC_LOW_PCT %, and clear a
     * stale master ALRT while we're here. RCOMP / SLEEP / ALSC kept.   */
    uint16_t cfg = 0;
    if ((e = ga_rd16(GA_REG_CONFIG, &cfg)) != ESP_OK) {
        return e;
    }
    uint16_t want = (uint16_t)((cfg & ~(GA_CFG_ATHD | GA_CFG_ALRT)) |
                               GA_ATHD_FOR(VQ_SOC_LOW_PCT));
    if (want != cfg) {
        if ((e = ga_wr16(GA_REG_CONFIG, want)) != ESP_OK) {
            return e;
        }
        if ((e = ga_rd16(GA_REG_CONFIG, &rb)) != ESP_OK) {
            return e;
        }
        if ((rb & GA_CFG_ATHD) != GA_ATHD_FOR(VQ_SOC_LOW_PCT)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    /* flush any pending cause bits so the line starts de-asserted      */
    return vq_gauge_clear_alert();
}

esp_err_t vq_gauge_quickstart(void)
{
    /* MODE is effectively write-only for commands; QuickStart bit14.
     * Re-seeds OCV/SOC from instantaneous VCELL — datasheet "use with
     * caution": never auto-issued by this driver, console/debug only.  */
    return ga_wr16(GA_REG_MODE, GA_MODE_QUICKSTART);
}

bool     vq_gauge_present(void)      { return s_gauge.present; }
uint32_t vq_gauge_alert_edges(void)  { return s_gauge.alert_edges; }

/* ---- ISR + cadence task ------------------------------------------------ */

/* ALRT is level-held low until cleared → NEGEDGE fires once per alert.
 * We latch the edge + edge count and wake the task; all clearing/I2C
 * runs in task context (the open-drain line needs an explicit clear).  */
static void IRAM_ATTR gauge_alrt_isr(void *arg)
{
    gauge_priv_t *p = (gauge_priv_t *)arg;
    p->alert_pending = 1;
    p->alert_edges++;
    TaskHandle_t t = p->task;
    if (t != NULL) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(t, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

static void gauge_emit(vq_sensor_t *s, int64_t t_us, float vcell, float soc)
{
    vq_sample_t smp[2] = {
        { .channel = VQ_CH_BATT_V,   .t_us = t_us, .value = vcell, .flags = 0 },
        { .channel = VQ_CH_BATT_SOC, .t_us = t_us, .value = soc,   .flags = 0 },
    };
    if (s->sink != NULL) {
        s->sink(s->sink_ctx, smp, 2);
    }
    s->sample_count += 2;
    s->last_t_us = t_us;
}

static void gauge_poll_once(vq_sensor_t *s)
{
    /* The task may outlive the template arg it was spawned with (armed
     * pre-pipeline): resolve the registry-owned copy per poll — sink and
     * counters live there after vq_sensor_register's by-value copy.   */
    vq_sensor_t *rs = vq_sensor_find(s->ops->name);
    if (!rs) rs = s;
    float v = 0.0f, soc = 0.0f;
    int64_t t = esp_timer_get_time();       /* read-start stamp (contract) */
    esp_err_t e = vq_gauge_read(&v, &soc);
    if (e != ESP_OK) {
        rs->err_count++;
        ESP_LOGD(TAG, "gauge poll err %s", esp_err_to_name(e));
        return;
    }
    gauge_emit(rs, t, v, soc);
    s_gauge.last_ok_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

/* One low-prio task owns both jobs: the fixed 0.2 Hz cadence (when
 * streaming) and ISR→task alert dispatch (whenever armed).            */
static void gauge_task(void *arg)
{
    vq_sensor_t  *s = (vq_sensor_t *)arg;
    gauge_priv_t *p = s->priv;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (p->streaming) {
            int64_t d = p->next_due_us - esp_timer_get_time();
            wait = (d <= 0) ? 0 : pdMS_TO_TICKS((uint32_t)((d + 999) / 1000));
        }
        (void)ulTaskNotifyTake(pdTRUE, wait);
        if (!p->run) {
            break;
        }
        if (p->alert_pending) {
            p->alert_pending = 0;
            if (p->alert_cb != NULL) {
                p->alert_cb(p->alert_cb_ctx);   /* task context, per spec */
            }
        }
        if (p->stream_reset) {
            p->stream_reset = 0;
            p->next_due_us  = esp_timer_get_time();    /* sample now     */
        }
        if (p->streaming) {
            int64_t now = esp_timer_get_time();
            if (now >= p->next_due_us) {
                p->next_due_us = now + GA_PERIOD_US;   /* re-anchor: no drift, no burst */
                gauge_poll_once(s);
            }
        }
    }
    p->task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t gauge_ensure_task(vq_sensor_t *s)
{
    gauge_priv_t *p = s->priv;
    if (p->task != NULL) {
        return ESP_OK;
    }
    p->run = 1;
    if (xTaskCreate(gauge_task, "vq_gauge", GA_TASK_STACK, s,
                    GA_TASK_PRIO, &p->task) != pdPASS) {
        p->run  = 0;
        p->task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void gauge_release_task(gauge_priv_t *p)
{
    p->run = 0;
    if (p->task != NULL) {
        xTaskNotifyGive(p->task);
        for (int i = 0; i < 100 && p->task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(2));   /* bounded join — ~200 ms   */
        }
    }
}

esp_err_t vq_gauge_on_alert(void (*cb)(void *), void *ctx)
{
    gauge_priv_t *p = &s_gauge;
    p->alert_cb     = cb;
    p->alert_cb_ctx = ctx;

    if (cb == NULL) {                       /* detach                    */
        if (p->isr_installed) {
            /* remove the ISR FIRST — after this no gauge_alrt_isr can
             * be queued against a task we are about to release.        */
            gpio_isr_handler_remove(VQ_GAUGE_ALRT_GPIO);
            gpio_intr_disable(VQ_GAUGE_ALRT_GPIO);
            p->isr_installed = false;
        }
        if (!p->streaming) {
            gauge_release_task(p);          /* alert path no longer owed */
        }
        return ESP_OK;
    }

    /* IO6: plain input, NO internal pulls — R74 10k to +3V3 holds the
     * open-drain line high; the gauge asserts it low on alert.         */
    const gpio_config_t g = {
        .pin_bit_mask = 1ULL << VQ_GAUGE_ALRT_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    esp_err_t e = gpio_config(&g);
    if (e != ESP_OK) {
        return e;
    }
    e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {   /* already installed */
        return e;
    }
    e = gpio_isr_handler_add(VQ_GAUGE_ALRT_GPIO, gauge_alrt_isr, p);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return e;
    }
    p->isr_installed = true;

    /* the cb runs in the gauge task — create it on demand so the alert
     * path exists even when streaming was never start()ed (power_mgr
     * registers its alert handler long before pipeline_start).        */
    e = gauge_ensure_task(&vq_aux_gauge_sensor);
    if (e != ESP_OK) {
        return e;
    }

    /* A line already held low can never re-edge → seed one dispatch so
     * a pre-asserted alert (e.g. armed before a wake) is not lost.     */
    if (gpio_get_level(VQ_GAUGE_ALRT_GPIO) == 0) {
        p->alert_pending = 1;
    }
    xTaskNotifyGive(p->task);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* vq_sensor_ops                                                          */

static esp_err_t gauge_probe(vq_sensor_t *s)
{
    gauge_priv_t *p = s->priv;
    if (vq_i2c_probe(VQ_ADDR_MAX17048) != ESP_OK) {
        p->present = false;
        return ESP_ERR_NOT_FOUND;       /* no battery / part absent     */
    }
    uint16_t ver = 0;
    esp_err_t e = ga_rd16(GA_REG_VERSION, &ver);
    if (e != ESP_OK) {
        return e;                       /* ACK'd but bus died — real err */
    }
    if ((ver & GA_VERSION_MASK) != GA_VERSION_ID) {
        ESP_LOGW(TAG, "version 0x%04X ≠ 0x001x — not a MAX17048?", ver);
        return ESP_ERR_INVALID_RESPONSE;
    }
    p->version = ver;
    p->present = true;
    return ESP_OK;
}

static esp_err_t gauge_init(vq_sensor_t *s)
{
    gauge_priv_t *p = s->priv;
    esp_err_t e = gauge_probe(s);
    if (e != ESP_OK) {
        return e;
    }

    /* make sure the IC can't be in sleep/idle armed states: MODE is a
     * command register — writing 0 clears QuickStart/EnSleep.          */
    if ((e = ga_wr16(GA_REG_MODE, 0x0000)) != ESP_OK) {
        return e;
    }
    uint16_t cfg = 0;
    if ((e = ga_rd16(GA_REG_CONFIG, &cfg)) != ESP_OK) {
        return e;
    }
    if (cfg & GA_CFG_SLEEP) {
        if ((e = ga_wr16(GA_REG_CONFIG,
                         (uint16_t)(cfg & ~GA_CFG_SLEEP))) != ESP_OK) {
            return e;
        }
    }

    /* alert block: VALRT 3.2 V/4.3 V + ATHD ~5 %, stale flags flushed  */
    if ((e = vq_gauge_config_alerts()) != ESP_OK) {
        return e;
    }
    ESP_LOGI(TAG, "max17048 up: ver=0x%04X valrt=%u/%umV athd=%u%%",
             (unsigned)p->version, (unsigned)GA_VALRT_MIN_MV,
             (unsigned)GA_VALRT_MAX_MV, (unsigned)VQ_SOC_LOW_PCT);
    return ESP_OK;
}

static esp_err_t gauge_start(vq_sensor_t *s)
{
    gauge_priv_t *p = s->priv;
    if (!p->present) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t e = gauge_ensure_task(s);
    if (e != ESP_OK) {
        return e;
    }
    p->stream_reset = 1;                     /* task re-anchors next_due */
    p->streaming    = 1;                     /* (int64 stays task-owned) */
    xTaskNotifyGive(p->task);
    return ESP_OK;
}

static esp_err_t gauge_stop(vq_sensor_t *s)
{
    gauge_priv_t *p = s->priv;
    p->streaming = 0;
    if (p->alert_cb == NULL) {
        gauge_release_task(p);              /* nothing left to serve    */
    } else if (p->task != NULL) {
        xTaskNotifyGive(p->task);           /* idle-wait, keep ISR path */
    }
    return ESP_OK;
}

static esp_err_t gauge_sleep(vq_sensor_t *s)
{
    /* Documented NO-OP per task spec: the gauge is VBAT-powered and is
     * the deep-sleep wake source (EXT1 on GAUGE_ALRT/IO6) — parking it
     * in CONFIG.SLEEP would kill the low-battery trip that is supposed
     * to wake us, and it must keep tracking SOC while we sleep. The
     * IC's own hibernate already idles it; alert config (VALRT/ATHD)
     * stays latched. Streaming teardown before sleep is stop()'s job
     * (pipeline runs it ahead of the §4.4 checklist).                  */
    (void)s;
    return ESP_OK;
}

static bool gauge_healthy(vq_sensor_t *s)
{
    gauge_priv_t *p = s->priv;
    if (!p->present) {
        return false;
    }
    if (!p->streaming) {
        return true;                    /* idle — nothing owed          */
    }
    uint32_t last = p->last_ok_ms;      /* word-sized → atomic enough   */
    if (last == 0) {
        return true;                    /* pre-first-sample grace       */
    }
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return (now_ms - last) <=
           (uint32_t)(GA_HEALTH_PERIODS * (GA_PERIOD_US / 1000));
}

static const vq_channel_id_t s_gauge_chs[] = {
    VQ_CH_BATT_V, VQ_CH_BATT_SOC,
};

static const vq_sensor_ops_t gauge_ops = {
    .name       = "max17048",
    .probe      = gauge_probe,
    .init       = gauge_init,
    .start      = gauge_start,
    .stop       = gauge_stop,
    .sleep      = gauge_sleep,
    .healthy    = gauge_healthy,
    .channels   = s_gauge_chs,
    .n_channels = 2,
    .rate_hz    = 0.2f,
    .required   = true,      /* only battery monitor; unprotected cell  */
};

vq_sensor_t vq_aux_gauge_sensor = {
    .ops  = &gauge_ops,
    .priv = &s_gauge,
};

/* ------------------------------------------------------------------ */
/* self-test fragment                                                   */

esp_err_t vq_aux_gauge_selftest(char *buf, size_t len)
{
    int off = 0;
    if (vq_i2c_probe(VQ_ADDR_MAX17048) != ESP_OK) {
        snprintf(buf, len, "no ACK @0x36 (battery absent?)");
        return ESP_ERR_NOT_FOUND;
    }
    uint16_t ver = 0, vraw = 0, sraw = 0, craw = 0;
    esp_err_t e = ga_rd16(GA_REG_VERSION, &ver);
    if (e == ESP_OK) e = ga_rd16(GA_REG_VCELL, &vraw);
    if (e == ESP_OK) e = ga_rd16(GA_REG_SOC, &sraw);
    if (e == ESP_OK) e = ga_rd16(GA_REG_CRATE, &craw);
    if (e != ESP_OK) {
        snprintf(buf, len, "ver=0x%04X reg read err 0x%x", ver, e);
        return e;
    }
    off = vq_aux_repf(buf, len, off,
                      "ver=0x%04X vcell=%.3fV soc=%.1f%% crate=%.2f%%/h"
                      " (vcell vs DMM@J2)", ver,
                      (double)((float)vraw * 78.125e-6f),
                      (double)((float)sraw / 256.0f),
                      (double)((float)(int16_t)craw * 0.208f));
    if ((ver & GA_VERSION_MASK) != GA_VERSION_ID) {
        vq_aux_repf(buf, len, off, " BAD-ID");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* alert threshold write/readback — idempotent, live-verified       */
    e = vq_gauge_config_alerts();
    if (e != ESP_OK) {
        vq_aux_repf(buf, len, off, " alerts err 0x%x", e);
        return e;
    }
    uint16_t rb = 0;
    e = ga_rd16(GA_REG_VALRT, &rb);
    if (e != ESP_OK) {
        return e;
    }
    off = vq_aux_repf(buf, len, off, " valrt=0x%04X(%.2f/%.2fV)",
                      rb, (double)((rb >> 8) * 20e-3f),
                      (double)((rb & 0xFF) * 20e-3f));
    e = ga_rd16(GA_REG_CONFIG, &rb);
    if (e != ESP_OK) {
        return e;
    }
    off = vq_aux_repf(buf, len, off, " athd=%u(%u%%) alrt=%d pin=%d"
                      " edges=%lu",
                      (unsigned)(rb & GA_CFG_ATHD),
                      (unsigned)(32 - (rb & GA_CFG_ATHD)),
                      (rb & GA_CFG_ALRT) ? 1 : 0,
                      gpio_get_level(VQ_GAUGE_ALRT_GPIO),
                      (unsigned long)s_gauge.alert_edges);
    return ESP_OK;
}
