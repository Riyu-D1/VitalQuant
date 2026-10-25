/* bme280.c — BME280 pressure/humidity/temperature node "bme280" @0x76
 * (U13, CSB→3V3 I2C, SDO→GND).
 *
 * Register model mirrors what components/vq_drv_env/vq_bme280.c uses:
 *   0x88..0xA1  calibration block A (26 B, LE pairs; dig_H1 @0xA1)
 *   0xD0        chip_id = 0x60 (RO)
 *   0xE0        reset — write 0xB6 → POR (NVM calib retained)
 *   0xE1..0xE7  calibration block B (dig_H2..H6, packed nibbles)
 *   0xF2        ctrl_hum  — effective only after a ctrl_meas write
 *   0xF3        status    bit3 measuring, bit0 im_update (NVM copy)
 *   0xF4        ctrl_meas osrs_t[7:5] osrs_p[4:2] mode[1:0]
 *   0xF5        config    t_sb[7:5] filter[4:2]
 *   0xF7..0xFE  data burst: P[19:0] T[19:0] H[15:0], MSB first
 *
 * Conversion engine: mode 01/10 (forced) runs one conversion then the
 * mode bits fall back to 00 (sleep); mode 11 (normal) repeats at the
 * config t_sb period.  Duration follows the datasheet worst case
 * t_meas = 1.25 + 2.3·osrs_t + (2.3·osrs_p + 0.575)
 *            + (2.3·osrs_h + 0.575)  [ms, skipped terms drop].
 * While busy, status.measuring reads 1.
 *
 * Calibration is the Bosch datasheet appendix-A example set — real,
 * plausible constants.  Raw ADC values are produced by numerically
 * inverting the same integer compensation the driver runs (bisection;
 * the math below is a mirror of it), so the firmware's compensation
 * lands back on the env_feed stimulus (amb_c / rh_pct / press_pa).
 * A skipped channel reports the 0x80000/0x8000 sentinel.
 *
 * Powered by VB_RAIL_3V3: rail down → NACK (ESP_ERR_TIMEOUT); restore
 * → POR.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>

#include "vboard.h"
#include "env_feed.h"

#define B_ADDR          0x76u

enum {                                  /* register map               */
    B_REG_CAL_A   = 0x88,               /* 0x88..0xA1                 */
    B_REG_ID      = 0xD0,
    B_REG_RESET   = 0xE0,
    B_REG_CAL_B   = 0xE1,               /* 0xE1..0xE7                 */
    B_REG_CTRL_HUM  = 0xF2,
    B_REG_STATUS    = 0xF3,
    B_REG_CTRL_MEAS = 0xF4,
    B_REG_CONFIG    = 0xF5,
    B_REG_DATA    = 0xF7,               /* 0xF7..0xFE                 */
};
#define B_DEV_ID        0x60u
#define B_RESET_WORD    0xB6u
#define B_ADC_SKIP      0x80000
#define B_ADC_SKIP_H    0x8000

/* Calibration block — Bosch datasheet (BST-BME280-DS002) appendix-A
 * example device.  dig_T1..dig_P9 as LE u16/s16 pairs, 0xA0 reserved,
 * dig_H1 @0xA1. */
static const uint8_t b_cal_a[26] = {
    0x70, 0x6B,             /* dig_T1 = 27504                          */
    0x43, 0x67,             /* dig_T2 = 26435                          */
    0x18, 0xFC,             /* dig_T3 = −1000                          */
    0x7D, 0x8E,             /* dig_P1 = 36477                          */
    0x47, 0xD6,             /* dig_P2 = −10681                         */
    0xD0, 0x0B,             /* dig_P3 = 3024                           */
    0x27, 0x0B,             /* dig_P4 = 2855                           */
    0x8C, 0x00,             /* dig_P5 = 140                            */
    0xF9, 0xFF,             /* dig_P6 = −7                             */
    0x8C, 0x3C,             /* dig_P7 = 15500                          */
    0xF8, 0xC6,             /* dig_P8 = −14600                         */
    0x70, 0x17,             /* dig_P9 = 6000                           */
    0x00,                   /* 0xA0 reserved                           */
    0x4B,                   /* dig_H1 = 75                             */
};
/* dig_H2 LE, H3, then the packed 12-bit pairs for H4/H5, then H6.    */
static const uint8_t b_cal_b[7] = {
    0x71, 0x01,             /* dig_H2 = 369                            */
    0x00,                   /* dig_H3 = 0                              */
    0x13, 0x8A,             /* dig_H4 = (0x13<<4)|0x0A = 314           */
    0x03,                   /* dig_H5 = (0x03<<4)|0x08 = 56            */
    0x3C,                   /* dig_H6 = 60                             */
};

/* decoded calibration (same unpacking the driver performs) */
typedef struct {
    uint16_t t1;
    int16_t  t2, t3;
    uint16_t p1;
    int16_t  p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t  h1, h3;
    int16_t  h2, h4, h5;
    int8_t   h6;
} bme_cal_t;

typedef struct {
    vboard_dev_t dev;
    bme_cal_t    cal;

    uint8_t      ptr;               /* register pointer               */
    uint8_t      ctrl_hum;          /* as written (pending)           */
    uint8_t      ctrl_hum_eff;      /* latched by ctrl_meas write     */
    uint8_t      ctrl_meas;
    uint8_t      config;
    uint8_t      data[8];           /* P,T,H raw burst                */

    bool         busy;
    int64_t      ready_us;
    int64_t      next_conv_us;      /* normal-mode next start         */
    int64_t      im_update_us;      /* NVM-copy window after reset    */
    bool         powered;
} bme280_t;

/* --------------------------------------------------------------- *
 *  Datasheet appendix-A integer compensation — mirrors the driver's
 *  b_comp_t/p/h.  t_fine chains T→P→H exactly as on the firmware side.
 * --------------------------------------------------------------- */
static uint16_t u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static int16_t s16le(const uint8_t *p)
{
    return (int16_t)u16le(p);
}

static void b_cal_decode(bme_cal_t *c)
{
    const uint8_t *a = b_cal_a;
    const uint8_t *h = b_cal_b;
    c->t1 = u16le(&a[0]);
    c->t2 = s16le(&a[2]);
    c->t3 = s16le(&a[4]);
    c->p1 = u16le(&a[6]);
    c->p2 = s16le(&a[8]);
    c->p3 = s16le(&a[10]);
    c->p4 = s16le(&a[12]);
    c->p5 = s16le(&a[14]);
    c->p6 = s16le(&a[16]);
    c->p7 = s16le(&a[18]);
    c->p8 = s16le(&a[20]);
    c->p9 = s16le(&a[22]);
    c->h1 = a[25];
    c->h2 = s16le(&h[0]);
    c->h3 = h[2];
    c->h4 = (int16_t)((h[3] << 4) | (h[4] & 0x0F));
    c->h5 = (int16_t)((h[5] << 4) | (h[4] >> 4));
    c->h6 = (int8_t)h[6];
}

static int32_t b_comp_t(const bme_cal_t *c, int32_t adc_T, int32_t *t_fine)
{
    int32_t v1 = ((((adc_T >> 3) - ((int32_t)c->t1 << 1)))
                  * ((int32_t)c->t2)) >> 11;
    int32_t v2 = (((((adc_T >> 4) - ((int32_t)c->t1))
                    * ((adc_T >> 4) - ((int32_t)c->t1))) >> 12)
                  * ((int32_t)c->t3)) >> 14;
    *t_fine = v1 + v2;
    return (*t_fine * 5 + 128) >> 8;            /* 0.01 °C            */
}

static uint32_t b_comp_p(const bme_cal_t *c, int32_t t_fine, int32_t adc_P)
{
    int64_t v1 = ((int64_t)t_fine) - 128000;
    int64_t v2 = v1 * v1 * (int64_t)c->p6;
    v2 = v2 + ((v1 * (int64_t)c->p5) << 17);
    v2 = v2 + (((int64_t)c->p4) << 35);
    v1 = ((v1 * v1 * (int64_t)c->p3) >> 8)
         + ((v1 * (int64_t)c->p2) << 12);
    v1 = (((((int64_t)1) << 47) + v1)) * ((int64_t)c->p1) >> 33;
    if (v1 == 0) {
        return 0;
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = (((int64_t)c->p9) * (p >> 13) * (p >> 13)) >> 25;
    v2 = (((int64_t)c->p8) * p) >> 19;
    p = ((p + v1 + v2) >> 8) + (((int64_t)c->p7) << 4);
    return (uint32_t)p;                          /* Q24.8 Pa          */
}

static uint32_t b_comp_h(const bme_cal_t *c, int32_t t_fine, int32_t adc_H)
{
    int32_t v = t_fine - ((int32_t)76800);
    v = (((((adc_H << 14) - (((int32_t)c->h4) << 20)
            - (((int32_t)c->h5) * v)) + ((int32_t)16384)) >> 15)
         * (((((((v * ((int32_t)c->h6)) >> 10)
               * (((v * ((int32_t)c->h3)) >> 11) + ((int32_t)32768)))
              >> 10) + ((int32_t)2097152))
            * ((int32_t)c->h2) + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7)
              * ((int32_t)c->h1)) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return (uint32_t)(v >> 12);                  /* Q22.10 %RH        */
}

/* --------------------------------------------------------------- *
 *  Inverse mapping env → raw ADC: bisection over the forward comp
 *  (monotone in each channel: T,H increasing; P decreasing in adc_P).
 * --------------------------------------------------------------- */
static int32_t b_solve_t(const bme_cal_t *c, int32_t want01, int32_t *tf_out)
{
    int32_t lo = 0, hi = 0xFFFFF, best = 0x80000, tf = 0;
    int64_t best_err = INT64_MAX;
    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        int32_t t01 = b_comp_t(c, mid, &tf);
        int64_t err = (int64_t)t01 - want01;
        if (err < 0) {
            if (-err < best_err) { best_err = -err; best = mid; }
            lo = mid + 1;
        } else if (err > 0) {
            if (err < best_err) { best_err = err; best = mid; }
            hi = mid - 1;
        } else {
            best = mid;
            break;
        }
    }
    (void)b_comp_t(c, best, &tf);
    *tf_out = tf;
    return best;
}

static int32_t b_solve_p(const bme_cal_t *c, int32_t tf, uint32_t want_q8)
{
    int32_t lo = 0, hi = 0xFFFFF, best = 0x80000;
    int64_t best_err = INT64_MAX;
    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        int64_t err = (int64_t)b_comp_p(c, tf, mid) - want_q8;
        if (err < 0) {                          /* comp ↓ in adc_P    */
            if (-err < best_err) { best_err = -err; best = mid; }
            hi = mid - 1;
        } else if (err > 0) {
            if (err < best_err) { best_err = err; best = mid; }
            lo = mid + 1;
        } else {
            best = mid;
            break;
        }
    }
    return best;
}

static int32_t b_solve_h(const bme_cal_t *c, int32_t tf, uint32_t want_q10)
{
    int32_t lo = 0, hi = 0xFFFF, best = 0x8000;
    int64_t best_err = INT64_MAX;
    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        int64_t err = (int64_t)b_comp_h(c, tf, mid) - want_q10;
        if (err < 0) {
            if (-err < best_err) { best_err = -err; best = mid; }
            lo = mid + 1;
        } else if (err > 0) {
            if (err < best_err) { best_err = err; best = mid; }
            hi = mid - 1;
        } else {
            best = mid;
            break;
        }
    }
    return best;
}

/* --------------------------------------------------------------- */

static unsigned b_osrs_n(uint8_t o)             /* osrs code → multiplier */
{
    return o ? (1u << (o - 1)) : 0u;
}

static int64_t b_conv_us(const bme280_t *b)
{
    unsigned nt = b_osrs_n((uint8_t)((b->ctrl_meas >> 5) & 0x7));
    unsigned np = b_osrs_n((uint8_t)((b->ctrl_meas >> 2) & 0x7));
    unsigned nh = b_osrs_n((uint8_t)(b->ctrl_hum_eff & 0x7));
    int64_t us = 1250;
    if (nt) us += 2300 * (int64_t)nt;
    if (np) us += 2300 * (int64_t)np + 575;
    if (nh) us += 2300 * (int64_t)nh + 575;
    return us;                                   /* x1/x1/x1 → 9300 µs */
}

static int64_t b_tsb_us(uint8_t config)
{
    static const int64_t t[8] = {
        500, 62500, 125000, 250000, 500000, 1000000, 10000, 20000
    };
    return t[(config >> 5) & 0x7];
}

static void b_finish_conv(bme280_t *b)
{
    uint8_t osrs_t = (uint8_t)((b->ctrl_meas >> 5) & 0x7);
    uint8_t osrs_p = (uint8_t)((b->ctrl_meas >> 2) & 0x7);
    uint8_t osrs_h = (uint8_t)(b->ctrl_hum_eff & 0x7);

    int32_t tf    = 0;
    int32_t adc_T = osrs_t ? b_solve_t(&b->cal,
                                       lroundf(g_vbenv.amb_c * 100.0f), &tf)
                           : B_ADC_SKIP;
    if (!osrs_t) {
        (void)b_comp_t(&b->cal, 0x80000, &tf);   /* any tf for P/H — same
                                                  * chain the driver
                                                  * would see           */
    }
    int32_t adc_P = osrs_p ? b_solve_p(&b->cal, tf,
                                       (uint32_t)lroundf(g_vbenv.press_pa
                                                         * 256.0f))
                           : B_ADC_SKIP;
    int32_t adc_H = osrs_h ? b_solve_h(&b->cal, tf,
                                       (uint32_t)lroundf(g_vbenv.rh_pct
                                                         * 1024.0f))
                           : B_ADC_SKIP_H;

    b->data[0] = (uint8_t)((uint32_t)adc_P >> 12);
    b->data[1] = (uint8_t)((uint32_t)adc_P >> 4);
    b->data[2] = (uint8_t)((uint32_t)adc_P << 4);
    b->data[3] = (uint8_t)((uint32_t)adc_T >> 12);
    b->data[4] = (uint8_t)((uint32_t)adc_T >> 4);
    b->data[5] = (uint8_t)((uint32_t)adc_T << 4);
    b->data[6] = (uint8_t)((uint32_t)adc_H >> 8);
    b->data[7] = (uint8_t)((uint32_t)adc_H);

    unsigned mode = b->ctrl_meas & 0x3;
    if (mode == 1 || mode == 2) {                /* forced → sleep bits */
        b->ctrl_meas &= (uint8_t)~0x03u;
    } else if (mode == 3) {                      /* normal → reschedule */
        b->next_conv_us = b->ready_us + b_tsb_us(b->config);
    }
}

static void b_por(bme280_t *b)
{
    b->ptr          = 0;
    b->ctrl_hum     = 0;
    b->ctrl_hum_eff = 0;
    b->ctrl_meas    = 0;
    b->config       = 0;
    b->busy         = false;
    b->next_conv_us = 0;
    b->im_update_us = vboard_now_us() + 2000;    /* ~2 ms NVM copy      */
    /* pre-conversion data reads back as the "skipped" sentinel        */
    b->data[0] = 0x80; b->data[1] = 0x00; b->data[2] = 0x00;
    b->data[3] = 0x80; b->data[4] = 0x00; b->data[5] = 0x00;
    b->data[6] = 0x80; b->data[7] = 0x00;
}

static void b_poll(bme280_t *b, int64_t now)
{
    if (!vboard_rail(VB_RAIL_3V3)) {
        b->powered = false;
        b->busy    = false;
        return;
    }
    if (!b->powered) {
        b->powered = true;
        b_por(b);
    }
    if (b->busy && now >= b->ready_us) {
        b->busy = false;
        b_finish_conv(b);
    }
    if ((b->ctrl_meas & 0x3) == 3 && !b->busy && now >= b->next_conv_us) {
        b->busy     = true;
        b->ready_us = now + b_conv_us(b);
    }
}

/* --------------------------------------------------------------- */

static uint8_t b_reg_read(bme280_t *b, uint8_t reg, int64_t now)
{
    if (reg >= B_REG_CAL_A && reg <= 0xA1) {
        return b_cal_a[reg - B_REG_CAL_A];
    }
    if (reg >= B_REG_CAL_B && reg <= 0xE7) {
        return b_cal_b[reg - B_REG_CAL_B];
    }
    if (reg >= B_REG_DATA && reg <= 0xFE) {
        return b->data[reg - B_REG_DATA];
    }
    switch (reg) {
    case B_REG_ID:       return B_DEV_ID;
    case B_REG_CTRL_HUM: return b->ctrl_hum;
    case B_REG_STATUS:
        /* bit3 measuring (poll already completes due conversions),
         * bit0 im_update during the post-reset NVM copy window        */
        return (uint8_t)((b->busy ? 0x08 : 0)
                         | ((now < b->im_update_us) ? 0x01 : 0));
    case B_REG_CTRL_MEAS: return b->ctrl_meas;
    case B_REG_CONFIG:    return b->config;
    default:             return 0;
    }
}

static void b_reg_write(bme280_t *b, uint8_t reg, uint8_t v, int64_t now)
{
    switch (reg) {
    case B_REG_RESET:
        if (v == B_RESET_WORD) {
            b_por(b);
        }
        break;
    case B_REG_CTRL_HUM:
        b->ctrl_hum = (uint8_t)(v & 0x07);       /* latched via F4 write */
        break;
    case B_REG_CTRL_MEAS:
        b->ctrl_meas    = v;
        b->ctrl_hum_eff = b->ctrl_hum;           /* datasheet ordering   */
        switch (v & 0x3) {
        case 1:
        case 2:                                  /* forced conversion    */
            b->busy     = true;
            b->ready_us = now + b_conv_us(b);
            break;
        case 3:                                  /* normal mode          */
            b->next_conv_us = now;
            break;
        default:                                 /* sleep                */
            b->busy = false;
            break;
        }
        break;
    case B_REG_CONFIG:
        b->config = v;
        break;
    default:
        break;                                   /* RO/unmapped — drop   */
    }
}

static esp_err_t b_i2c_write(vboard_dev_t *d, const uint8_t *data, size_t len)
{
    bme280_t *b = (bme280_t *)d->ctx;
    int64_t now = vboard_now_us();
    b_poll(b, now);
    if (!vboard_rail(VB_RAIL_3V3)) {
        return VB_ESP_ERR_TIMEOUT;
    }
    if (len == 0) {
        return VB_ESP_OK;
    }
    b->ptr = data[0];
    for (size_t i = 1; i < len; i++) {           /* writes auto-increment */
        b_reg_write(b, b->ptr, data[i], now);
        b->ptr++;
    }
    return VB_ESP_OK;
}

static esp_err_t b_i2c_read(vboard_dev_t *d, uint8_t *data, size_t len)
{
    bme280_t *b = (bme280_t *)d->ctx;
    int64_t now = vboard_now_us();
    b_poll(b, now);
    if (!vboard_rail(VB_RAIL_3V3)) {
        return VB_ESP_ERR_TIMEOUT;
    }
    if (len == 0) {
        return VB_ESP_OK;
    }
    for (size_t i = 0; i < len; i++) {           /* reads auto-increment  */
        data[i] = b_reg_read(b, b->ptr, now);
        b->ptr++;
    }
    return VB_ESP_OK;
}

static void b_tick(vboard_dev_t *d, int64_t now_us)
{
    b_poll((bme280_t *)d->ctx, now_us);
}

static void b_reset(vboard_dev_t *d)
{
    bme280_t *b = (bme280_t *)d->ctx;
    if (vboard_rail(VB_RAIL_3V3)) {
        b->powered = true;
        b_por(b);
    } else {
        b->powered = false;
    }
}

/* --------------------------------------------------------------- */

static bme280_t s_dev = {
    .dev = {
        .name      = "bme280",
        .i2c_addr  = B_ADDR,
        .i2c_write = b_i2c_write,
        .i2c_read  = b_i2c_read,
        .spi_cs    = -1,
        .tick      = b_tick,
        .reset     = b_reset,
    },
};

void vbdev_bme280_register(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    b_cal_decode(&s_dev.cal);
    s_dev.dev.ctx = &s_dev;
    vboard_register(&s_dev.dev);
}

__attribute__((constructor))
static void b_ctor(void)
{
    vbdev_bme280_register();
}
