/* sht45.c — SHT45 humidity/temperature node "sht45" @0x44 (U23, skin
 * face microclimate).
 *
 * Command protocol (no register map) as exercised by
 * components/vq_drv_env/vq_sht45.c:
 *   0xFD  measure, high precision   → ~9 ms, then T word + RH word
 *   0xF6  measure, medium precision → ~4.5 ms
 *   0xE0  measure, low precision    → ~1.7 ms
 *   0x89  read serial number        → 2 words + CRCs (this is the
 *                                     driver's probe/ID check)
 *   0x94  soft reset                → known idle, nothing staged
 *   heater+measure commands (datasheet set, not used by the driver):
 *   0x36  200 mW, 1 s      0x32  200 mW, 0.1 s
 *   0x30  110 mW, 1 s      0x2F  110 mW, 0.1 s
 *   0x24   20 mW, 1 s      0x1E   20 mW, 0.1 s
 *
 * Every 16-bit data word is followed by CRC-8 (Sensirion: poly 0x31,
 * init 0xFF, no reflection, xorout 0x00) — REAL CRCs so the driver's
 * validation passes.  A read while a conversion is pending NACKs
 * (ESP_ERR_TIMEOUT), matching the driver's s_read_words() retry loop;
 * a read with nothing staged NACKs too.
 *
 *   T  = −45 + 175 · S_T  / 65535  (°C)   ← env amb_c
 *   RH = −6  + 125 · S_RH / 65535  (%)    ← env rh_pct
 *
 * Serial is a model-fixed plausible 32-bit value (SHT4x has no ID reg;
 * the serial read IS the identity check).
 *
 * Powered by VB_RAIL_3V3: rail down → NACK; restore → POR (no staged
 * data until a command arrives).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>

#include "vboard.h"
#include "env_feed.h"

#define S_ADDR          0x44u

/* command set */
#define S_CMD_MEAS_HI   0xFDu
#define S_CMD_MEAS_MED  0xF6u
#define S_CMD_MEAS_LO   0xE0u
#define S_CMD_SERIAL    0x89u
#define S_CMD_RESET     0x94u
#define S_CMD_H200_1S   0x36u
#define S_CMD_H200_01S  0x32u
#define S_CMD_H110_1S   0x30u
#define S_CMD_H110_01S  0x2Fu
#define S_CMD_H20_1S    0x24u
#define S_CMD_H20_01S   0x1Eu

#define S_SN            0x51E4A2B7u   /* model-fixed serial            */

typedef struct {
    vboard_dev_t dev;
    uint8_t      resp[6];
    uint8_t      resp_len;
    uint8_t      resp_pos;
    bool         resp_pend;
    bool         resp_is_meas;      /* rebuild from env at serve time */
    int64_t      ready_us;
    bool         powered;
} sht45_t;

/* Sensirion CRC-8 — poly 0x31, init 0xFF (same code the driver runs). */
static uint8_t s_crc(uint8_t b0, uint8_t b1)
{
    uint8_t crc = 0xFF;
    uint8_t b[2] = { b0, b1 };
    for (int i = 0; i < 2; i++) {
        crc ^= b[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31)
                               : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static uint16_t s_clamp_u16(int32_t v)
{
    return (v < 0) ? 0 : (v > 65535) ? 65535 : (uint16_t)v;
}

static void s_stage(sht45_t *s, uint16_t w0, uint16_t w1)
{
    s->resp[0] = (uint8_t)(w0 >> 8);
    s->resp[1] = (uint8_t)(w0 & 0xFF);
    s->resp[2] = s_crc(s->resp[0], s->resp[1]);
    s->resp[3] = (uint8_t)(w1 >> 8);
    s->resp[4] = (uint8_t)(w1 & 0xFF);
    s->resp[5] = s_crc(s->resp[3], s->resp[4]);
    s->resp_len  = 6;
    s->resp_pos  = 0;
    s->resp_pend = true;
    s->resp_is_meas = false;        /* bytes materialised now         */
}

/* Arm a measurement: the raw words are materialised lazily when the
 * read is served, so the env feed is sampled at conversion-complete
 * time (like the real die), not at command time.                     */
static void s_arm_meas(sht45_t *s)
{
    s->resp_pend    = true;
    s->resp_is_meas = true;
    s->resp_len     = 6;
    s->resp_pos     = 0;
}

static void s_stage_meas(sht45_t *s)
{
    /* invert the driver's formulas onto the env feed                */
    uint16_t st = s_clamp_u16(lroundf((g_vbenv.amb_c + 45.0f)
                                      * 65535.0f / 175.0f));
    uint16_t sh = s_clamp_u16(lroundf((g_vbenv.rh_pct + 6.0f)
                                      * 65535.0f / 125.0f));
    s_stage(s, st, sh);
}

static void s_por(sht45_t *s)
{
    s->resp_pend    = false;
    s->resp_is_meas = false;
    s->resp_len     = 0;
    s->resp_pos     = 0;
    s->ready_us     = 0;
}

static void s_poll(sht45_t *s)
{
    if (!vboard_rail(VB_RAIL_3V3)) {
        s->powered = false;
        s_por(s);                       /* unpowered → nothing staged */
        return;
    }
    if (!s->powered) {
        s->powered = true;
        s_por(s);
    }
}

/* --------------------------------------------------------------- */

static esp_err_t s_i2c_write(vboard_dev_t *d, const uint8_t *data, size_t len)
{
    sht45_t *s = (sht45_t *)d->ctx;
    s_poll(s);
    if (!vboard_rail(VB_RAIL_3V3)) {
        return VB_ESP_ERR_TIMEOUT;
    }
    if (len == 0) {
        return VB_ESP_OK;                       /* bare probe          */
    }
    if (len != 1) {
        return VB_ESP_ERR_TIMEOUT;              /* commands are 1 byte */
    }
    int64_t now = vboard_now_us();
    switch (data[0]) {
    case S_CMD_MEAS_HI:
        s_arm_meas(s);
        s->ready_us = now + 9000;               /* ≤10 ms high prec.   */
        break;
    case S_CMD_MEAS_MED:
        s_arm_meas(s);
        s->ready_us = now + 4500;
        break;
    case S_CMD_MEAS_LO:
        s_arm_meas(s);
        s->ready_us = now + 1700;
        break;
    case S_CMD_SERIAL:
        s_stage(s, (uint16_t)(S_SN >> 16), (uint16_t)(S_SN & 0xFFFF));
        s->ready_us = now;                      /* ROM read is instant */
        break;
    case S_CMD_RESET:
        s_por(s);
        s->ready_us = now + 1000;               /* ≤1 ms soft reset    */
        break;
    case S_CMD_H200_1S:
    case S_CMD_H110_1S:
    case S_CMD_H20_1S:
        s_arm_meas(s);
        s->ready_us = now + 1100000;            /* ~1 s + meas         */
        break;
    case S_CMD_H200_01S:
    case S_CMD_H110_01S:
    case S_CMD_H20_01S:
        s_arm_meas(s);
        s->ready_us = now + 110000;             /* ~0.1 s + meas       */
        break;
    default:
        return VB_ESP_ERR_TIMEOUT;              /* unknown command     */
    }
    return VB_ESP_OK;
}

static esp_err_t s_i2c_read(vboard_dev_t *d, uint8_t *data, size_t len)
{
    sht45_t *s = (sht45_t *)d->ctx;
    s_poll(s);
    if (!vboard_rail(VB_RAIL_3V3)) {
        return VB_ESP_ERR_TIMEOUT;
    }
    if (len == 0) {
        return VB_ESP_OK;
    }
    /* still converting / nothing staged → read-header NACK           */
    if (!s->resp_pend || vboard_now_us() < s->ready_us) {
        return VB_ESP_ERR_TIMEOUT;
    }
    if (s->resp_is_meas) {
        s_stage_meas(s);               /* sample env at conv-complete  */
    }
    size_t rem = (size_t)(s->resp_len - s->resp_pos);
    size_t n   = (len < rem) ? len : rem;
    for (size_t i = 0; i < len; i++) {
        data[i] = (i < n) ? s->resp[s->resp_pos + i] : 0xFF;
    }
    s->resp_pos += (uint8_t)n;
    if (s->resp_pos >= s->resp_len) {
        s->resp_pend = false;                   /* consumed — NACK next */
    }
    return VB_ESP_OK;
}

static void s_tick(vboard_dev_t *d, int64_t now_us)
{
    (void)now_us;
    s_poll((sht45_t *)d->ctx);
}

static void s_reset(vboard_dev_t *d)
{
    sht45_t *s = (sht45_t *)d->ctx;
    if (vboard_rail(VB_RAIL_3V3)) {
        s->powered = true;
        s_por(s);
    } else {
        s->powered = false;
    }
}

/* --------------------------------------------------------------- */

static sht45_t s_dev = {
    .dev = {
        .name      = "sht45",
        .i2c_addr  = S_ADDR,
        .i2c_write = s_i2c_write,
        .i2c_read  = s_i2c_read,
        .spi_cs    = -1,
        .tick      = s_tick,
        .reset     = s_reset,
    },
};

void vbdev_sht45_register(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    s_dev.dev.ctx = &s_dev;
    vboard_register(&s_dev.dev);
}

__attribute__((constructor))
static void s_ctor(void)
{
    vbdev_sht45_register();
}
