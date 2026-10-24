/* vq_tmp117.c — TMP117 temperature nodes: U11 skin (0x48), U20 dome
 * (0x49), optional J9 tail (0x4A).
 *
 * Cadence: one-shot conversion per poll at 1 Hz. Continuous conversion
 * would add self-heating for no benefit on a slow trend channel; one-shot
 * at 1 Hz keeps the die active ~15.5 ms/s (~1.5 % duty). The cadence task
 * stamps t_acq at the config write (conversion start); fetch runs 20 ms
 * later and emits temp_skin / temp_dome / temp_distal °C.
 *
 * temp.dome (U20, foam insulation dome) and temp.distal (J9 flex tail)
 * are TREND channels only — the ZHF-style pair tracks core-temperature
 * trend; no calibrated core-temperature or clinical claim (docs/10 §1).
 *
 * U11 ALERT → IO5 via R65 pull-up: kept in alert function (T/nA=0
 * alert mode, POL=0 active-low, DR/Alert=0) but never consumed — IO5 is
 * configured as a plain input, no ISR. Alert thresholds stay at factory
 * limits (+96 °C / −256 °C) so the line simply never asserts; it exists
 * for future threshold use. U20 ALERT is NC on the board.
 *
 * Registers are 16-bit, MSB first. Device ID reg 0x0F = 0x0117.
 * Temp reg 0x00 LSB = 0.0078125 °C.
 */
#include "vq_env_priv.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include <stdio.h>
#include "freertos/task.h"

#define T_REG_TEMP      0x00u
#define T_REG_CONFIG    0x01u
#define T_REG_ID        0x0Fu
#define T_DEV_ID        0x0117u
#define T_LSB_C         0.0078125f
#define T_OS_WAIT_MS    20u     /* one-shot 15.5 ms max + margin        */

/* Config reg 0x01: MOD[11:10] CONV[9:7] AVG[6:5] T/nA[4] POL[3]
 *                  DR/Alert[2] Soft_Reset[1]
 * BASE: shutdown (MOD=01), 1 s cycle (CONV=100, unused in one-shot),
 *       no averaging (AVG=00 → 15.5 ms OS), alert mode (T/nA=0),
 *       alert active-low (POL=0), ALERT pin = alert flags (DR/Alert=0).
 *       = 0x0600.  Writing it also parks the device (used by
 *       stop()/sleep() — shutdown is the deepest I2C-reachable state). */
#define T_CFG_BASE      0x0600u
/* Same + MOD=11 → one-shot conversion; auto-returns to shutdown.      */
#define T_CFG_OS        0x0E00u

typedef struct {
    vq_env_dev_t dev;                   /* MUST be first               */
    uint8_t      addr;
    int          alert_gpio;            /* -1 = none                    */
} tmp117_t;

static inline tmp117_t *ctx(vq_env_dev_t *d) { return (tmp117_t *)d; }

static vq_err_t t_rd16(uint8_t addr, uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    vq_err_t e = vq_i2c_read_reg(addr, reg, b, sizeof(b));
    if (e == ESP_OK) {
        *v = ((uint16_t)b[0] << 8) | b[1];
    }
    return e;
}

static vq_err_t t_wr16(uint8_t addr, uint8_t reg, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };
    return vq_i2c_write_reg(addr, reg, b, sizeof(b));
}

/* ------------------------------------------------------------------ */

static vq_err_t t_probe(vq_env_dev_t *d)
{
    tmp117_t *t = ctx(d);
    if (vq_i2c_probe(t->addr) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;       /* absent — required flag decides */
    }
    uint16_t id = 0;
    vq_err_t e = t_rd16(t->addr, T_REG_ID, &id);
    if (e != ESP_OK) {
        return e;                       /* ACK'd but bus died — real err  */
    }
    if (id != T_DEV_ID) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static vq_err_t t_init(vq_env_dev_t *d)
{
    tmp117_t *t = ctx(d);
    uint16_t cfg;
    vq_err_t e = t_rd16(t->addr, T_REG_CONFIG, &cfg); /* read clears flags */
    if (e != ESP_OK) {
        return e;
    }
    e = t_wr16(t->addr, T_REG_CONFIG, T_CFG_BASE);
    if (e != ESP_OK) {
        return e;
    }
    if (t->alert_gpio >= 0) {
        /* IO5: floating input — R65 is the pull-up; alert-only config,
         * no ISR ever (vq_drv_env.h).                                  */
        gpio_config_t g = {
            .pin_bit_mask = 1ULL << t->alert_gpio,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        esp_err_t ge = gpio_config(&g);
        if (ge != ESP_OK) {
            return ge;
        }
    }
    return ESP_OK;
}

static vq_err_t t_trigger(vq_env_dev_t *d)
{
    return t_wr16(ctx(d)->addr, T_REG_CONFIG, T_CFG_OS);
}

static vq_err_t t_fetch(vq_env_dev_t *d)
{
    uint16_t raw = 0;
    vq_err_t e = t_rd16(ctx(d)->addr, T_REG_TEMP, &raw);
    if (e != ESP_OK) {
        return e;
    }
    vq_env_emit(d, d->s.ops->channels[0],
                (float)(int16_t)raw * T_LSB_C);
    return ESP_OK;
}

static vq_err_t t_standby(vq_env_dev_t *d)
{
    /* MOD=01 shutdown — TMP117's lowest-power I2C state; alert-only
     * config retained (T/nA=0, POL=0, DR/Alert=0).                     */
    return t_wr16(ctx(d)->addr, T_REG_CONFIG, T_CFG_BASE);
}

static vq_err_t t_sleep(vq_env_dev_t *d)
{
    /* Deepest available state short of depowering: same shutdown write.
     * The tail device may be unplugged — caller logs, never fatal.     */
    return t_wr16(ctx(d)->addr, T_REG_CONFIG, T_CFG_BASE);
}

static vq_err_t t_selftest(vq_env_dev_t *d, char *buf, size_t len)
{
    tmp117_t *t = ctx(d);
    uint16_t  id = 0, raw = 0;
    vq_err_t  e = t_rd16(t->addr, T_REG_ID, &id);
    if (e != ESP_OK) {
        snprintf(buf, len, "id read err 0x%x", e);
        return e;
    }
    if (id != T_DEV_ID) {
        snprintf(buf, len, "id=0x%04X != 0x0117", id);
        return ESP_ERR_INVALID_RESPONSE;
    }
    e = t_trigger(d);                    /* one conversion (not streamed) */
    if (e == ESP_OK) {
        vq_env_delay_ms(T_OS_WAIT_MS);
        e = t_rd16(t->addr, T_REG_TEMP, &raw);
    }
    (void)t_standby(d);                  /* always leave in shutdown      */
    if (e != ESP_OK) {
        snprintf(buf, len, "id=0x%04X conv err 0x%x", id, e);
        return e;
    }
    snprintf(buf, len, "id=0x%04X one-shot=%.3fC", id,
             (double)((int16_t)raw * T_LSB_C));
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

static const vq_env_drv_t tmp117_drv = {
    .probe        = t_probe,
    .init         = t_init,
    .trigger      = t_trigger,
    .fetch        = t_fetch,
    .standby      = t_standby,
    .sleep        = t_sleep,
    .selftest     = t_selftest,
    .conv_wait_ms = T_OS_WAIT_MS,
};

#define TMP117_DESC(_var, _name, _ch, _req, _addr, _gpio)              \
    static const vq_channel_id_t _var##_chs[] = { _ch };               \
    static const vq_sensor_ops_t _var##_ops = {                        \
        .name       = _name,                                           \
        .probe      = vq_env_op_probe,                                 \
        .init       = vq_env_op_init,                                  \
        .start      = vq_env_op_start,                                 \
        .stop       = vq_env_op_stop,                                  \
        .sleep      = vq_env_op_sleep,                                 \
        .healthy    = vq_env_op_healthy,                               \
        .channels   = _var##_chs,                                      \
        .n_channels = 1,                                               \
        .rate_hz    = 1.0f,                                            \
        .required   = _req,                                            \
    };                                                                 \
    static tmp117_t _var = {                                           \
        .dev = {                                                       \
            .s         = { .ops = &_var##_ops, .priv = &_var.dev },    \
            .drv       = &tmp117_drv,                                  \
            .period_us = 1000000,                                      \
        },                                                             \
        .addr       = _addr,                                           \
        .alert_gpio = _gpio,                                           \
    }

TMP117_DESC(t_skin, "tmp117.skin", VQ_CH_TEMP_SKIN,  true,
            VQ_ADDR_TMP117_SKIN, VQ_TMP117_ALERT_GPIO);
TMP117_DESC(t_dome, "tmp117.dome", VQ_CH_TEMP_DOME,  true,
            VQ_ADDR_TMP117_DOME, -1);
TMP117_DESC(t_tail, "tmp117.tail", VQ_CH_TEMP_DISTAL, false,
            VQ_ADDR_TMP117_TAIL, -1);

vq_env_dev_t **vq_tmp117_devs(size_t *n)
{
    static vq_env_dev_t *devs[] = {
        &t_skin.dev, &t_dome.dev, &t_tail.dev,
    };
    *n = sizeof(devs) / sizeof(devs[0]);
    return devs;
}
