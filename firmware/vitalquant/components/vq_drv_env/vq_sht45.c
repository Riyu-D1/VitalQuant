/* vq_sht45.c — SHT45 skin-face humidity/temperature node @0x44 (U23,
 * on the bottom skin face: sweat-rate context / microclimate — context
 * channel, not a clinical output).
 *
 * Protocol: single-byte commands (no register map), each 16-bit data
 * word followed by CRC-8 (poly 0x31, init 0xFF, no reflection/xor-out).
 *   0x89  read serial number      → 2 words (sn[31:0]) + CRCs
 *   0xFD  measure, high precision → ≤10 ms, then T word + RH word
 *   0x94  soft reset              → ≤1 ms
 * No ID register — probe = serial-number command + CRC check.
 * T  = −45 + 175 · S_T  / 65535   (°C)
 * RH = −6  + 125 · S_RH / 65535   (%RH, clamped 0-100)
 *
 * Cadence ~1 Hz from the pipeline: trigger sends 0xFD under the
 * READ-START stamp; fetch 12 ms later reads + CRC-checks both words and
 * emits skin.temp then skin.rh. Single-shot device — idle between
 * commands is already µA-class; sleep() = soft reset to a known state.
 */
#include "vq_env_priv.h"
#include "board_pins.h"
#include <stdio.h>
#include "freertos/task.h"

#define S_CMD_SERIAL    0x89u
#define S_CMD_MEAS_HIGH 0xFDu
#define S_CMD_RESET     0x94u
#define S_MEAS_WAIT_MS  12u     /* high-precision ≤10 ms + margin       */
#define S_RESET_WAIT_MS 10u
#define S_READ_TRIES    3       /* NACK while busy → short retry        */

typedef struct {
    vq_env_dev_t dev;                   /* MUST be first                */
    uint8_t      addr;
    uint32_t     serial;                /* cached at init/selftest      */
} sht45_t;

static inline sht45_t *ctx(vq_env_dev_t *d) { return (sht45_t *)d; }

/* Sensirion CRC-8: poly 0x31, init 0xFF, no reflection, xorout 0x00.  */
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

static vq_err_t s_read_words(sht45_t *s, uint8_t *buf6)
{
    vq_err_t e = ESP_FAIL;
    for (int i = 0; i < S_READ_TRIES; i++) {
        e = vq_i2c_read(s->addr, buf6, 6);
        if (e == ESP_OK) {
            return ESP_OK;
        }
        vq_env_delay_ms(2);             /* still converting → retry      */
    }
    return e;
}

static vq_err_t s_serial(sht45_t *s, uint32_t *sn)
{
    uint8_t cmd = S_CMD_SERIAL, b[6];
    /* command and read are separate bus transactions — a concurrent
     * pipeline measurement can interleave and re-stage the chip's
     * response. Retry the whole pair rather than trusting one shot.   */
    vq_err_t e = ESP_FAIL;
    for (int i = 0; i < 3; i++) {
        e = vq_i2c_write(s->addr, &cmd, 1);
        if (e != ESP_OK) {
            continue;
        }
        vq_env_delay_ms(2);             /* serial read-out latency      */
        e = s_read_words(s, b);
        if (e == ESP_OK) {
            break;
        }
        vq_env_delay_ms(3);
    }
    if (e != ESP_OK) {
        return e;
    }
    if (s_crc(b[0], b[1]) != b[2] || s_crc(b[3], b[4]) != b[5]) {
        return ESP_ERR_INVALID_CRC;
    }
    *sn = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
        | ((uint32_t)b[3] << 8) | b[4];
    return ESP_OK;
}

static vq_err_t s_measure(sht45_t *s, float *t_c, float *rh)
{
    uint8_t cmd = S_CMD_MEAS_HIGH, b[6];
    vq_err_t e = vq_i2c_write(s->addr, &cmd, 1);
    if (e != ESP_OK) {
        return e;
    }
    vq_env_delay_ms(S_MEAS_WAIT_MS);
    e = s_read_words(s, b);
    if (e != ESP_OK) {
        return e;
    }
    if (s_crc(b[0], b[1]) != b[2] || s_crc(b[3], b[4]) != b[5]) {
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t st = ((uint16_t)b[0] << 8) | b[1];
    uint16_t sh = ((uint16_t)b[3] << 8) | b[4];
    *t_c = -45.0f + 175.0f * (float)st / 65535.0f;
    *rh  = -6.0f + 125.0f * (float)sh / 65535.0f;
    if (*rh < 0.0f) {
        *rh = 0.0f;
    } else if (*rh > 100.0f) {
        *rh = 100.0f;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

static vq_err_t s_probe(vq_env_dev_t *d)
{
    if (vq_i2c_probe(ctx(d)->addr) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    uint32_t sn = 0;
    vq_err_t e = s_serial(ctx(d), &sn);     /* serial read = the ID check */
    if (e == ESP_ERR_INVALID_CRC) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return e;
}

static vq_err_t s_init(vq_env_dev_t *d)
{
    sht45_t *s = ctx(d);
    uint8_t  cmd = S_CMD_RESET;
    vq_err_t e = vq_i2c_write(s->addr, &cmd, 1);
    if (e != ESP_OK) {
        return e;
    }
    vq_env_delay_ms(S_RESET_WAIT_MS);
    /* confirm post-reset comms + cache serial for the log             */
    e = s_serial(s, &s->serial);
    if (e == ESP_OK) {
        ESP_LOGI("vq_env", "sht45 sn=0x%08lX", (unsigned long)s->serial);
    }
    return e;
}

static vq_err_t s_trigger(vq_env_dev_t *d)
{
    uint8_t cmd = S_CMD_MEAS_HIGH;
    return vq_i2c_write(ctx(d)->addr, &cmd, 1);
}

static vq_err_t s_fetch(vq_env_dev_t *d)
{
    sht45_t *s = ctx(d);
    uint8_t  b[6];
    /* the 0xFD was already sent by trigger(); just collect the words   */
    vq_err_t e = s_read_words(s, b);
    if (e != ESP_OK) {
        return e;
    }
    if (s_crc(b[0], b[1]) != b[2] || s_crc(b[3], b[4]) != b[5]) {
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t st = ((uint16_t)b[0] << 8) | b[1];
    uint16_t sh = ((uint16_t)b[3] << 8) | b[4];
    float t_c = -45.0f + 175.0f * (float)st / 65535.0f;
    float rh  = -6.0f + 125.0f * (float)sh / 65535.0f;
    if (rh < 0.0f) {
        rh = 0.0f;
    } else if (rh > 100.0f) {
        rh = 100.0f;
    }
    /* channels[] order: {skin.rh, skin.temp}                            */
    vq_env_emit(d, d->s.ops->channels[1], t_c);
    vq_env_emit(d, d->s.ops->channels[0], rh);
    return ESP_OK;
}

static vq_err_t s_standby(vq_env_dev_t *d)
{
    /* Single-shot device: it is already µA-idle between commands —
     * nothing to write.                                                */
    (void)d;
    return ESP_OK;
}

static vq_err_t s_sleep(vq_env_dev_t *d)
{
    /* Deepest: soft reset → known idle state for pre-deep-sleep.       */
    uint8_t cmd = S_CMD_RESET;
    return vq_i2c_write(ctx(d)->addr, &cmd, 1);
}

static vq_err_t s_selftest(vq_env_dev_t *d, char *buf, size_t len)
{
    sht45_t  *s = ctx(d);
    uint32_t  sn = 0;
    float     t_c = 0.0f, rh = 0.0f;
    vq_err_t  e = s_serial(s, &sn);
    if (e != ESP_OK) {
        snprintf(buf, len, "serial err 0x%x", e);
        return e;
    }
    e = s_measure(s, &t_c, &rh);           /* one conversion, unsent    */
    (void)s_sleep(d);                      /* leave in a known state     */
    if (e != ESP_OK) {
        snprintf(buf, len, "sn=0x%08lX meas err 0x%x",
                 (unsigned long)sn, e);
        return e;
    }
    s->serial = sn;
    snprintf(buf, len, "sn=0x%08lX meas: T=%.2fC RH=%.1f%%",
             (unsigned long)sn, (double)t_c, (double)rh);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

static const vq_env_drv_t sht45_drv = {
    .probe        = s_probe,
    .init         = s_init,
    .trigger      = s_trigger,
    .fetch        = s_fetch,
    .standby      = s_standby,
    .sleep        = s_sleep,
    .selftest     = s_selftest,
    .conv_wait_ms = S_MEAS_WAIT_MS,
};

static const vq_channel_id_t s_chs[] = {
    VQ_CH_SKIN_RH, VQ_CH_SKIN_TEMP,
};
static const vq_sensor_ops_t s_ops = {
    .name       = "sht45",
    .probe      = vq_env_op_probe,
    .init       = vq_env_op_init,
    .start      = vq_env_op_start,
    .stop       = vq_env_op_stop,
    .sleep      = vq_env_op_sleep,
    .healthy    = vq_env_op_healthy,
    .channels   = s_chs,
    .n_channels = sizeof(s_chs) / sizeof(s_chs[0]),
    .rate_hz    = 1.0f,
    .required   = true,
};
static sht45_t s_dev = {
    .dev = {
        .s         = { .ops = &s_ops, .priv = &s_dev.dev },
        .drv       = &sht45_drv,
        .period_us = 1000000,
    },
    .addr = VQ_ADDR_SHT45,
};

vq_env_dev_t *vq_sht45_dev(void)
{
    return &s_dev.dev;
}
