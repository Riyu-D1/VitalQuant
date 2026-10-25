/* vq_drv_env.c — env-group core: descriptor registry, the shared
 * cadence task, generic vq_sensor_ops glue, and the §9 self test.
 *
 * The cadence task is a deadline scheduler (see vq_drv_env.h for the
 * scheduling model). Each device alternates:
 *      trigger()  -- stamps t_acq_us = esp_timer READ START, arms fetch
 *      fetch()    -- runs conv_wait_ms later, emits under that stamp
 * start/stop/sleep are deferred to the task via dev->cmd so all I2C
 * traffic and all int64 scheduling fields stay single-owner. Standby /
 * sleep hw actions thus execute within ~ENV_TICK_MS of the call; their
 * result is logged (non-fatal by contract: never make optional hw fatal).
 */
#include "vq_env_priv.h"
#include "vq_drv_env.h"
#include <stdio.h>
#include <stdarg.h>
#include "freertos/task.h"

#define ENV_MAX_DEVS      8
#define ENV_TICK_MS       5       /* deadline scan granularity          */
#define ENV_FIRST_DUE_US  50000   /* first poll after start()           */
#define ENV_HEALTH_PERIODS 3      /* healthy(): ok within 3 periods     */
#define ENV_TASK_STACK    3072
#define ENV_TASK_PRIO     4       /* slow channels — below AFE tasks    */

portMUX_TYPE vq_env_mux = portMUX_INITIALIZER_UNLOCKED;

static vq_env_dev_t  *s_devs[ENV_MAX_DEVS];
static size_t         s_ndev;
static TaskHandle_t   s_task;
static bool           s_registered;
static vq_err_t       s_reg_err = ESP_OK;
static const char    *TAG = "vq_env";

/* ------------------------------------------------------------------ */
/* emit / bookkeeping                                                   */

/* The registry keeps ITS OWN copy of the descriptor — sink/sink_ctx and
 * the counters live there, the driver's template stays untouched.      */
static inline vq_sensor_t *env_reg(vq_env_dev_t *d)
{
    return d->s_reg ? d->s_reg : &d->s;   /* template pre-registration  */
}

void vq_env_emit(vq_env_dev_t *d, vq_channel_id_t ch, float value)
{
    vq_sensor_t *rs = env_reg(d);
    vq_sample_t smp = {
        .channel = ch,
        .t_us    = d->t_acq_us,        /* READ-START stamp — see contract */
        .value   = value,
        .flags   = 0,
    };
    if (rs->sink) {
        rs->sink(rs->sink_ctx, &smp, 1);
    }
    rs->sample_count++;
    rs->last_t_us = smp.t_us;
}

/* ------------------------------------------------------------------ */
/* cadence task                                                         */

static void env_apply_cmd(vq_env_dev_t *d, int64_t now)
{
    int cmd;
    portENTER_CRITICAL(&vq_env_mux);    /* read+clear vs ops writes      */
    cmd    = d->cmd;
    d->cmd = ENV_CMD_NONE;
    portEXIT_CRITICAL(&vq_env_mux);
    switch (cmd) {
    case ENV_CMD_START:
        portENTER_CRITICAL(&vq_env_mux);
        d->last_ok_us = now;            /* 3-period grace for first read */
        portEXIT_CRITICAL(&vq_env_mux);
        d->next_due_us = now + ENV_FIRST_DUE_US;
        d->state = ENV_ST_STREAM;
        ESP_LOGD(TAG, "%s streaming @ %.2f Hz", d->s.ops->name,
                 (double)d->s.ops->rate_hz);
        break;
    case ENV_CMD_STOP:
        d->state = ENV_ST_IDLE;
        if (d->drv->standby) {
            vq_err_t e = d->drv->standby(d);
            if (e != ESP_OK)
                ESP_LOGW(TAG, "%s standby err 0x%x", d->s.ops->name, e);
        }
        break;
    case ENV_CMD_SLEEP:
        d->state = ENV_ST_IDLE;
        if (d->drv->sleep) {
            vq_err_t e = d->drv->sleep(d);
            if (e != ESP_OK)
                ESP_LOGW(TAG, "%s sleep err 0x%x", d->s.ops->name, e);
        }
        break;
    default:
        break;
    }
}

static void env_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    TickType_t tick = pdMS_TO_TICKS(ENV_TICK_MS);
    if (tick == 0) {
        tick = 1;
    }
    for (;;) {
        int64_t now = esp_timer_get_time();
        for (size_t i = 0; i < s_ndev; i++) {
            vq_env_dev_t *d = s_devs[i];

            if (d->cmd != ENV_CMD_NONE) {
                env_apply_cmd(d, now);
            }

            if (d->state == ENV_ST_FETCH && now >= d->fetch_at_us) {
                vq_err_t e = d->drv->fetch(d);
                d->state = ENV_ST_STREAM;
                if (e == ESP_OK) {
                    portENTER_CRITICAL(&vq_env_mux);
                    d->last_ok_us = esp_timer_get_time();
                    portEXIT_CRITICAL(&vq_env_mux);
                } else {
                    env_reg(d)->err_count++;
                    ESP_LOGD(TAG, "%s fetch err 0x%x", d->s.ops->name, e);
                }
            }

            if (d->state == ENV_ST_STREAM && now >= d->next_due_us) {
                /* fresh stamp: `now` may lag after a slow fetch — the
                 * READ-START stamp must be the actual trigger instant.  */
                d->t_acq_us   = esp_timer_get_time();
                d->next_due_us = d->t_acq_us + d->period_us;
                if (d->drv->trigger(d) == ESP_OK) {
                    d->fetch_at_us =
                        d->t_acq_us + (int64_t)d->drv->conv_wait_ms * 1000;
                    d->state = ENV_ST_FETCH;
                } else {
                    env_reg(d)->err_count++;    /* retry next period    */
                }
            }
        }
        vTaskDelayUntil(&last, tick);
    }
}

static esp_err_t env_ensure_task(void)
{
    esp_err_t e = ESP_OK;
    portENTER_CRITICAL(&vq_env_mux);    /* single cadence task guarantee */
    if (!s_task
        && xTaskCreate(env_task, "env_cad", ENV_TASK_STACK, NULL,
                       ENV_TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        e = ESP_ERR_NO_MEM;
    }
    portEXIT_CRITICAL(&vq_env_mux);
    return e;
}

/* ------------------------------------------------------------------ */
/* generic ops — identical semantics for every env device               */

static inline vq_env_dev_t *env_dev(vq_sensor_t *s)
{
    return (vq_env_dev_t *)s->priv;
}

vq_err_t vq_env_op_probe(vq_sensor_t *s)
{
    return env_dev(s)->drv->probe(env_dev(s));
}

vq_err_t vq_env_op_init(vq_sensor_t *s)
{
    vq_env_dev_t *d = env_dev(s);
    d->state = ENV_ST_IDLE;
    d->cmd   = ENV_CMD_NONE;
    return d->drv->init(d);
}

vq_err_t vq_env_op_start(vq_sensor_t *s)
{
    /* Task applies the transition on its next tick (≤ ENV_TICK_MS);
     * keeps every scheduling field single-owner.                       */
    esp_err_t e = env_ensure_task();
    if (e != ESP_OK) {
        return e;
    }
    portENTER_CRITICAL(&vq_env_mux);
    env_dev(s)->cmd = ENV_CMD_START;
    portEXIT_CRITICAL(&vq_env_mux);
    return ESP_OK;
}

vq_err_t vq_env_op_stop(vq_sensor_t *s)
{
    /* Deferred standby: device reaches its low-power idle within one
     * tick. Any I2C error is logged by the task (non-fatal). If the
     * cadence task doesn't exist yet (never started), apply directly —
     * the bus is mutexed inside vq_i2c.                                */
    vq_env_dev_t *d = env_dev(s);
    if (s_task) {
        portENTER_CRITICAL(&vq_env_mux);
        d->cmd = ENV_CMD_STOP;
        portEXIT_CRITICAL(&vq_env_mux);
        return ESP_OK;
    }
    d->state = ENV_ST_IDLE;
    return d->drv->standby ? d->drv->standby(d) : ESP_OK;
}

vq_err_t vq_env_op_sleep(vq_sensor_t *s)
{
    /* Deepest per-device state, applied by the task (soft reset /
     * shutdown write per driver); synchronous when no task exists.     */
    vq_env_dev_t *d = env_dev(s);
    if (s_task) {
        portENTER_CRITICAL(&vq_env_mux);
        d->cmd = ENV_CMD_SLEEP;
        portEXIT_CRITICAL(&vq_env_mux);
        return ESP_OK;
    }
    d->state = ENV_ST_IDLE;
    return d->drv->sleep ? d->drv->sleep(d) : ESP_OK;
}

bool vq_env_op_healthy(vq_sensor_t *s)
{
    vq_env_dev_t *d = env_dev(s);
    if (d->state == ENV_ST_IDLE) {
        return true;                    /* not streaming → nothing owed */
    }
    int64_t last;
    portENTER_CRITICAL(&vq_env_mux);
    last = d->last_ok_us;
    portEXIT_CRITICAL(&vq_env_mux);
    if (last == 0) {
        return true;                    /* pre-start grace             */
    }
    return (esp_timer_get_time() - last)
           <= (int64_t)ENV_HEALTH_PERIODS * d->period_us;
}

/* ------------------------------------------------------------------ */
/* registry                                                             */

static vq_err_t env_add(vq_env_dev_t *d)
{
    if (s_ndev >= ENV_MAX_DEVS) {
        return ESP_ERR_NO_MEM;
    }
    vq_err_t e = vq_sensor_register(&d->s);
    if (e == ESP_OK) {
        d->s_reg = vq_sensor_find(d->s.ops->name);  /* live descriptor   */
        s_devs[s_ndev++] = d;
    }
    return e;
}

esp_err_t vq_env_register(void)
{
    if (s_registered) {
        return s_reg_err;
    }
    vq_err_t first = ESP_OK;

    size_t n_tmp = 0;
    vq_env_dev_t **tmps = vq_tmp117_devs(&n_tmp);
    for (size_t i = 0; i < n_tmp; i++) {
        vq_err_t e = env_add(tmps[i]);
        if (first == ESP_OK) {
            first = e;
        }
    }
    vq_err_t e = env_add(vq_bme280_dev());
    if (first == ESP_OK) {
        first = e;
    }
    e = env_add(vq_sht45_dev());
    if (first == ESP_OK) {
        first = e;
    }

    s_registered = true;
    s_reg_err    = first;
    ESP_LOGI(TAG, "registered %u env sensors%s", (unsigned)s_ndev,
             first == ESP_OK ? "" : " (registry errors)");
    return first;
}

/* ------------------------------------------------------------------ */
/* self test                                                            */

static void repf(char *r, size_t len, size_t *u, const char *fmt, ...)
{
    if (*u >= len) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(r + *u, len - *u, fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    *u = (*u + (size_t)n >= len) ? len : *u + (size_t)n;
}

esp_err_t vq_env_selftest(char *report, size_t len)
{
    if (!report || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    report[0] = '\0';
    (void)vq_env_register();            /* idempotent — devs must exist */

    size_t used = 0;
    int    fails = 0;
    repf(report, len, &used,
         "env-selftest: tmp117x3 + bme280 + sht45"
         " (research prototype — trend channels only)\n");

    for (size_t i = 0; i < s_ndev; i++) {
        vq_env_dev_t *d   = s_devs[i];
        const char   *nm  = d->s.ops->name;
        bool          req = d->s.ops->required;

        if (d->state != ENV_ST_IDLE) {
            repf(report, len, &used,
                 "%-12s busy/streaming — skipped (run pre-start)\n", nm);
            continue;
        }

        vq_err_t e = d->drv->probe(d);
        if (e == ESP_ERR_NOT_FOUND) {
            /* tolerated-absent path — this IS the J9-tail demo */
            repf(report, len, &used, "%-12s ABSENT — %s\n", nm,
                 req ? "REQUIRED, FAIL" : "optional, tolerated");
            if (req) {
                fails++;
            }
            continue;
        }
        if (e != ESP_OK) {
            repf(report, len, &used, "%-12s probe err 0x%x%s\n", nm, e,
                 req ? " — FAIL" : " — tolerated (optional)");
            if (req) {
                fails++;
            }
            continue;
        }

        char sub[160];
        sub[0] = '\0';
        e = d->drv->selftest(d, sub, sizeof(sub));
        if (e == ESP_OK) {
            repf(report, len, &used, "%-12s %s — OK\n", nm, sub);
        } else {
            repf(report, len, &used, "%-12s %s — err 0x%x%s\n", nm,
                 sub[0] ? sub : "(no detail)", e,
                 req ? " — FAIL" : " — tolerated (optional)");
            if (req) {
                fails++;
            }
        }
    }

    repf(report, len, &used, "env-selftest %s\n",
         fails ? "FAIL" : "PASS");
    return fails ? ESP_FAIL : ESP_OK;
}
