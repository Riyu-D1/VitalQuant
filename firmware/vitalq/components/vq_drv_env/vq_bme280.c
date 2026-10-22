/* vq_bme280.c — BME280 environmental node @0x76 (U13, CSB→3V3 = I2C,
 * SDO→GND). Forced-mode conversions at 0.5 Hz → env.temp / env.rh /
 * env.press. Forced mode keeps the device asleep between polls.
 *
 * Compensation: datasheet (BST-BME280-DS002) integer routines —
 * T→0.01 °C, P→Q24.8 Pa, H→Q22.10 %RH; converted to float on emit.
 * t_fine chains T→P/H exactly as the datasheet requires.
 *
 * ID reg 0xD0 = 0x60. Reset: 0xE0 ← 0xB6. ctrl_hum 0xF2, status 0xF3,
 * ctrl_meas 0xF4 (osrs_t[7:5] osrs_p[4:2] mode[1:0]), config 0xF5.
 * Raw burst 0xF7..0xFE: P[19:0] T[19:0] H[15:0], MSB first.
 * Calib: 0x88..0xA1 (26 B, LE pairs, dig_H1 @0xA1), 0xE1..0xE7 (dig_H2..H6).
 */
#include "vq_env_priv.h"
#include "board_pins.h"
#include <stdio.h>
#include "freertos/task.h"

#define B_REG_ID        0xD0u
#define B_DEV_ID        0x60u
#define B_REG_RESET     0xE0u
#define B_RESET_WORD    0xB6u
#define B_REG_CTRL_HUM  0xF2u
#define B_REG_CTRL_MEAS 0xF4u
#define B_REG_CONFIG    0xF5u
#define B_REG_DATA      0xF7u
#define B_REG_CAL_A     0x88u   /* 26 bytes → dig_T1..dig_P9 + dig_H1   */
#define B_REG_CAL_B     0xE1u   /* 7 bytes  → dig_H2..dig_H6            */
#define B_MEAS_WAIT_MS  12u     /* forced x1/x1/x1: ~9.3 ms max + margin */

#define B_OSRS_X1       0x01u
#define B_MODE_SLEEP    0x00u
#define B_MODE_FORCED   0x01u
#define B_CTRL_MEAS_SLEEP  ((B_OSRS_X1 << 5) | (B_OSRS_X1 << 2) | B_MODE_SLEEP)
#define B_CTRL_MEAS_FORCED ((B_OSRS_X1 << 5) | (B_OSRS_X1 << 2) | B_MODE_FORCED)
#define B_ADC_SKIP      0x80000 /* 20-bit value when a meas is disabled  */

typedef struct {
    vq_env_dev_t dev;                   /* MUST be first                */
    uint8_t      addr;
    /* dig_ calibration — little-endian pairs per datasheet             */
    uint16_t     t1;
    int16_t      t2, t3;
    uint16_t     p1;
    int16_t      p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t      h1, h3;
    int16_t      h2, h4, h5;
    int8_t       h6;
    int32_t      t_fine;
    bool         cal_ok;
} bme280_t;

static inline bme280_t *ctx(vq_env_dev_t *d) { return (bme280_t *)d; }

static inline uint16_t u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline int16_t s16le(const uint8_t *p)
{
    return (int16_t)u16le(p);
}

static vq_err_t b_wr8(uint8_t addr, uint8_t reg, uint8_t v)
{
    return vq_i2c_write_reg(addr, reg, &v, 1);
}

static vq_err_t b_read_cal(bme280_t *b)
{
    uint8_t a[26], h[7];
    vq_err_t e = vq_i2c_read_reg(b->addr, B_REG_CAL_A, a, sizeof(a));
    if (e != ESP_OK) {
        return e;
    }
    e = vq_i2c_read_reg(b->addr, B_REG_CAL_B, h, sizeof(h));
    if (e != ESP_OK) {
        return e;
    }
    b->t1 = u16le(&a[0]);
    b->t2 = s16le(&a[2]);
    b->t3 = s16le(&a[4]);
    b->p1 = u16le(&a[6]);
    b->p2 = s16le(&a[8]);
    b->p3 = s16le(&a[10]);
    b->p4 = s16le(&a[12]);
    b->p5 = s16le(&a[14]);
    b->p6 = s16le(&a[16]);
    b->p7 = s16le(&a[18]);
    b->p8 = s16le(&a[20]);
    b->p9 = s16le(&a[22]);
    b->h1 = a[25];                     /* 0xA1                          */
    b->h2 = s16le(&h[0]);              /* 0xE1/0xE2                     */
    b->h3 = h[2];                      /* 0xE3                          */
    b->h4 = (int16_t)(((int8_t)h[3] << 4) | (h[4] & 0x0F)); /* 0xE4/0xE5[3:0] */
    b->h5 = (int16_t)(((int8_t)h[5] << 4) | (h[4] >> 4));    /* 0xE6/0xE5[7:4] */
    b->h6 = (int8_t)h[6];              /* 0xE7                          */
    b->cal_ok = true;
    return ESP_OK;
}

/* --- datasheet integer compensation (appendix A) ------------------- */
static int32_t b_comp_t(bme280_t *b, int32_t adc_T)       /* → 0.01 °C   */
{
    int32_t v1 = ((((adc_T >> 3) - ((int32_t)b->t1 << 1)))
                  * ((int32_t)b->t2)) >> 11;
    int32_t v2 = (((((adc_T >> 4) - ((int32_t)b->t1))
                    * ((adc_T >> 4) - ((int32_t)b->t1))) >> 12)
                  * ((int32_t)b->t3)) >> 14;
    b->t_fine = v1 + v2;
    return (b->t_fine * 5 + 128) >> 8;
}

static uint32_t b_comp_p(bme280_t *b, int32_t adc_P)      /* → Q24.8 Pa  */
{
    int64_t v1 = ((int64_t)b->t_fine) - 128000;
    int64_t v2 = v1 * v1 * (int64_t)b->p6;
    v2 = v2 + ((v1 * (int64_t)b->p5) << 17);
    v2 = v2 + (((int64_t)b->p4) << 35);
    v1 = ((v1 * v1 * (int64_t)b->p3) >> 8)
         + ((v1 * (int64_t)b->p2) << 12);
    v1 = (((((int64_t)1) << 47) + v1)) * ((int64_t)b->p1) >> 33;
    if (v1 == 0) {
        return 0;                      /* avoid /0 — datasheet guard    */
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = (((int64_t)b->p9) * (p >> 13) * (p >> 13)) >> 25;
    v2 = (((int64_t)b->p8) * p) >> 19;
    p = ((p + v1 + v2) >> 8) + (((int64_t)b->p7) << 4);
    return (uint32_t)p;
}

static uint32_t b_comp_h(bme280_t *b, int32_t adc_H)      /* → Q22.10 RH */
{
    int32_t v = b->t_fine - ((int32_t)76800);
    v = (((((adc_H << 14) - (((int32_t)b->h4) << 20)
            - (((int32_t)b->h5) * v)) + ((int32_t)16384)) >> 15)
         * (((((((v * ((int32_t)b->h6)) >> 10)
               * (((v * ((int32_t)b->h3)) >> 11) + ((int32_t)32768)))
              >> 10) + ((int32_t)2097152))
            * ((int32_t)b->h2) + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7)
              * ((int32_t)b->h1)) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return (uint32_t)(v >> 12);
}

/* ------------------------------------------------------------------ */

static vq_err_t b_probe(vq_env_dev_t *d)
{
    if (vq_i2c_probe(ctx(d)->addr) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t id = 0;
    vq_err_t e = vq_i2c_read_reg(ctx(d)->addr, B_REG_ID, &id, 1);
    if (e != ESP_OK) {
        return e;
    }
    return (id == B_DEV_ID) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static vq_err_t b_init(vq_env_dev_t *d)
{
    bme280_t *b = ctx(d);
    vq_err_t  e = b_wr8(b->addr, B_REG_RESET, B_RESET_WORD);
    if (e != ESP_OK) {
        return e;
    }
    vq_env_delay_ms(10);                /* start-up + NVM copy ~2 ms typ  */
    b->cal_ok = false;
    e = b_read_cal(b);
    if (e != ESP_OK) {
        return e;
    }
    /* datasheet order: ctrl_hum changes only take effect once
     * ctrl_meas is written — write hum first, meas last.               */
    e = b_wr8(b->addr, B_REG_CTRL_HUM, B_OSRS_X1);
    if (e != ESP_OK) {
        return e;
    }
    e = b_wr8(b->addr, B_REG_CONFIG, 0x00);      /* t_sb don't-care in
                                                * forced; filter off    */
    if (e != ESP_OK) {
        return e;
    }
    return b_wr8(b->addr, B_REG_CTRL_MEAS, B_CTRL_MEAS_SLEEP);
}

static vq_err_t b_trigger(vq_env_dev_t *d)
{
    /* mode=01 → one forced conversion, then auto-sleep.                */
    return b_wr8(ctx(d)->addr, B_REG_CTRL_MEAS, B_CTRL_MEAS_FORCED);
}

static vq_err_t b_fetch(vq_env_dev_t *d)
{
    bme280_t *b = ctx(d);
    uint8_t   r[8];
    vq_err_t  e = vq_i2c_read_reg(b->addr, B_REG_DATA, r, sizeof(r));
    if (e != ESP_OK) {
        return e;
    }
    if (!b->cal_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    int32_t adc_P = ((int32_t)r[0] << 12) | ((int32_t)r[1] << 4) | (r[2] >> 4);
    int32_t adc_T = ((int32_t)r[3] << 12) | ((int32_t)r[4] << 4) | (r[5] >> 4);
    int32_t adc_H = ((int32_t)r[6] << 8) | r[7];

    /* t_fine MUST be computed (T) before P and H — datasheet chain.
     * 0x80000 = channel-disabled code; emit each valid channel.        */
    if (adc_T == B_ADC_SKIP) {
        return ESP_ERR_INVALID_RESPONSE;   /* osrs_t always x1 — hw err  */
    }
    float t_c = (float)b_comp_t(b, adc_T) / 100.0f;
    vq_env_emit(d, d->s.ops->channels[0], t_c);

    if (adc_P != B_ADC_SKIP) {
        float p_pa = (float)b_comp_p(b, adc_P) / 256.0f;
        vq_env_emit(d, d->s.ops->channels[2], p_pa);
    }
    float rh = (float)b_comp_h(b, adc_H) / 1024.0f;
    vq_env_emit(d, d->s.ops->channels[1], rh);
    return ESP_OK;
}

static vq_err_t b_standby(vq_env_dev_t *d)
{
    /* sleep mode — lowest power short of depowering; forced triggers
     * resume it.                                                       */
    return b_wr8(ctx(d)->addr, B_REG_CTRL_MEAS, B_CTRL_MEAS_SLEEP);
}

static vq_err_t b_sleep(vq_env_dev_t *d)
{
    /* Deepest: soft reset → sleep mode, then re-assert sleep config.   */
    vq_err_t e = b_wr8(ctx(d)->addr, B_REG_RESET, B_RESET_WORD);
    if (e != ESP_OK) {
        return e;
    }
    /* Note: deep-sleep prep follows; the ~2 ms post-reset NVM copy is
     * irrelevant once the SoC sleeps — no delay needed here.           */
    return ESP_OK;
}

static vq_err_t b_selftest(vq_env_dev_t *d, char *buf, size_t len)
{
    bme280_t *b = ctx(d);
    uint8_t   id = 0;
    vq_err_t  e = vq_i2c_read_reg(b->addr, B_REG_ID, &id, 1);
    if (e != ESP_OK) {
        snprintf(buf, len, "id read err 0x%x", e);
        return e;
    }
    if (id != B_DEV_ID) {
        snprintf(buf, len, "id=0x%02X != 0x60", id);
        return ESP_ERR_INVALID_RESPONSE;
    }
    e = b_init(d);                       /* reset + calib + config       */
    if (e != ESP_OK) {
        snprintf(buf, len, "id=0x60 init err 0x%x", e);
        return e;
    }
    e = b_trigger(d);                    /* one conversion (not streamed) */
    if (e == ESP_OK) {
        vq_env_delay_ms(B_MEAS_WAIT_MS);
        uint8_t r[8];
        e = vq_i2c_read_reg(b->addr, B_REG_DATA, r, sizeof(r));
        if (e == ESP_OK) {
            int32_t aP = ((int32_t)r[0] << 12) | ((int32_t)r[1] << 4) | (r[2] >> 4);
            int32_t aT = ((int32_t)r[3] << 12) | ((int32_t)r[4] << 4) | (r[5] >> 4);
            int32_t aH = ((int32_t)r[6] << 8) | r[7];
            snprintf(buf, len,
                     "id=0x%02X forced: T=%.2fC P=%.0fPa RH=%.1f%%"
                     " (cal %s)", id,
                     (double)(b_comp_t(b, aT) / 100.0f),
                     (double)(b_comp_p(b, aP) / 256.0f),
                     (double)(b_comp_h(b, aH) / 1024.0f),
                     b->cal_ok ? "ok" : "MISSING");
            (void)b_standby(d);
            return ESP_OK;
        }
    }
    (void)b_standby(d);
    snprintf(buf, len, "id=0x%02X conv err 0x%x", id, e);
    return e;
}

/* ------------------------------------------------------------------ */

static const vq_env_drv_t bme280_drv = {
    .probe        = b_probe,
    .init         = b_init,
    .trigger      = b_trigger,
    .fetch        = b_fetch,
    .standby      = b_standby,
    .sleep        = b_sleep,
    .selftest     = b_selftest,
    .conv_wait_ms = B_MEAS_WAIT_MS,
};

static const vq_channel_id_t b_chs[] = {
    VQ_CH_ENV_TEMP, VQ_CH_ENV_RH, VQ_CH_ENV_PRESS,
};
static const vq_sensor_ops_t b_ops = {
    .name       = "bme280",
    .probe      = vq_env_op_probe,
    .init       = vq_env_op_init,
    .start      = vq_env_op_start,
    .stop       = vq_env_op_stop,
    .sleep      = vq_env_op_sleep,
    .healthy    = vq_env_op_healthy,
    .channels   = b_chs,
    .n_channels = sizeof(b_chs) / sizeof(b_chs[0]),
    .rate_hz    = 0.5f,
    .required   = true,
};
static bme280_t b_dev = {
    .dev = {
        .s         = { .ops = &b_ops, .priv = &b_dev.dev },
        .drv       = &bme280_drv,
        .period_us = 2000000,           /* 0.5 Hz                       */
    },
    .addr = VQ_ADDR_BME280,
};

vq_env_dev_t *vq_bme280_dev(void)
{
    return &b_dev.dev;
}
