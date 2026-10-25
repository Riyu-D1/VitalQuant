/* tmp117.c — TMP117 high-precision temperature nodes, fix_v22.
 *
 *   "tmp117_48" @0x48  U11, skin contact — ALERT open-drain → IO5 (R65
 *                      pull-up; POL=0 active-low default, alert function)
 *   "tmp117_49" @0x49  U20, under the insulation dome — ALERT is NC
 *
 * The J9 tail device @0x4A is intentionally NOT modelled (the driver
 * marks it optional; absence is a supported scenario).
 *
 * Register model mirrors what components/vq_drv_env/vq_tmp117.c uses:
 *   0x00 Temp    RO  int16, 1/128 °C/LSB (0.0078125)
 *   0x01 Config  RW  MOD[11:10] CONV[9:7] AVG[6:5] T/nA[4] POL[3]
 *                    DR/Alert[2] Soft_Reset[1]  — a read clears the
 *                    alert/data-ready flags (driver relies on this)
 *   0x02 THIGH   RW  int16, same scaling — POR +96 °C
 *   0x03 TLOW    RW  int16, same scaling — POR −256 °C
 *   0x04 EEPROM_UL   held RW, no EEPROM behaviour modelled
 *   0x0F DeviceID RO = 0x0117
 *
 * Frames: [ptr] or [ptr|MSB|LSB] writes; repeated-start reads stream the
 * pointed 16-bit register MSB first (the real pointer does not
 * auto-increment — long reads re-serve the same register).
 *
 * Conversion engine: MOD=11 (one-shot) converts once in AVG-dependent
 * time then returns MOD to 01 (shutdown); MOD=00/10 run continuously
 * at the CONV period; MOD=01 parks the die.  Temp is latched from
 * env_feed at conversion-complete time (skin_c for 0x48, amb_c else).
 *
 * ALERT pin (0x48 only): T/nA=0 → alert function, asserts when temp is
 * outside THIGH/TLOW (cleared on config read); T/nA=1 → thermostat
 * mode with THIGH/TLOW hysteresis.  DR/Alert=1 repurposes the pin as
 * data-ready.  POL selects asserted level; deasserted releases the
 * open-drain line to the R65 pull-up (driven 1).  Rail loss floats the
 * pin → pull-up → 1.
 *
 * TODO(datasheet): exact alert-flag clear semantics in alert mode and
 * DR/Alert pin release timing are simplified (level + cfg-read clear).
 *
 * Powered by VB_RAIL_3V3: rail down → NACK every transfer
 * (ESP_ERR_TIMEOUT) and full POR state on restore.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>

#include "vboard.h"
#include "env_feed.h"

/* ---- shared env stimulus store (env_feed.h declares it extern) ---- */
vbenv_t g_vbenv = {
    .skin_c   = 32.0f,
    .amb_c    = 23.5f,
    .rh_pct   = 45.0f,
    .press_pa = 101325.0f,
};

#define T_ADDR_SKIN     0x48u
#define T_ADDR_DOME     0x49u
#define T_ALERT_GPIO    5           /* IO5 — TMP117_ALERT, hw_v3 map    */

enum {                                  /* register map               */
    T_REG_TEMP  = 0x00,
    T_REG_CFG   = 0x01,
    T_REG_THIGH = 0x02,
    T_REG_TLOW  = 0x03,
    T_REG_EEP   = 0x04,
    T_REG_ID    = 0x0F,
};
#define T_DEV_ID        0x0117u
#define T_LSB_C         (1.0f / 128.0f)

/* Config bit fields — same layout the firmware driver documents. */
#define T_CFG_MOD(c)    (((c) >> 10) & 0x3u)
#define T_CFG_CONV(c)   (((c) >>  7) & 0x7u)
#define T_CFG_AVG(c)    (((c) >>  5) & 0x3u)
#define T_CFG_TNA(c)    (((c) >>  4) & 0x1u)
#define T_CFG_POL(c)    (((c) >>  3) & 0x1u)
#define T_CFG_DRAL(c)   (((c) >>  2) & 0x1u)
#define T_CFG_SRST      0x0002u
#define T_CFG_MOD_MASK  0x0C00u

/* POR state (driver comment: factory limits +96 °C / −256 °C). */
#define T_POR_CFG       0x0220u     /* MOD=00, CONV=100 (1 s), AVG=01   */
#define T_POR_THIGH     0x3000u     /* +96 °C                           */
#define T_POR_TLOW      0x8000u     /* −256 °C                          */

/* Conversion timing: AVG → single-shot length; CONV → cycle period.   */
static const int64_t t_conv_us[4] = { 15500, 125000, 250000, 500000 };
static const int64_t t_cyc_us[8]  = {
    15500, 125000, 250000, 500000, 1000000, 4000000, 8000000, 16000000
};

typedef struct {
    vboard_dev_t dev;               /* registered model — ctx = this   */
    uint8_t      addr;
    int          alert_gpio;        /* -1 = ALERT not connected        */

    uint16_t     cfg, thigh, tlow, eep;
    int16_t      temp_raw;          /* last latched conversion         */
    uint8_t      ptr;               /* register pointer                */

    bool         conv_pend;
    int64_t      conv_ready_us;
    int64_t      next_conv_us;      /* continuous-mode next start      */

    bool         alert;             /* alert/therm output state        */
    bool         drdy;              /* data-ready (DR/Alert=1 mode)    */
    bool         powered;
} tmp117_t;

/* --------------------------------------------------------------- */

static float t_feed_c(const tmp117_t *t)
{
    return (t->addr == T_ADDR_SKIN) ? g_vbenv.skin_c : g_vbenv.amb_c;
}

static void t_pin_update(tmp117_t *t)
{
    if (t->alert_gpio < 0) {
        return;
    }
    bool asserted = T_CFG_DRAL(t->cfg) ? t->drdy : t->alert;
    int  pol      = (int)T_CFG_POL(t->cfg);
    /* open-drain: asserted drives the POL level, released floats to 1 */
    vboard_gpio_set(t->alert_gpio, asserted ? pol : 1 - pol);
}

static void t_alert_eval(tmp117_t *t)
{
    bool over = (t->temp_raw > (int16_t)t->thigh)
             || (t->temp_raw < (int16_t)t->tlow);
    if (T_CFG_TNA(t->cfg)) {
        /* thermostat mode: assert above THIGH, clear below TLOW      */
        if (t->temp_raw > (int16_t)t->thigh) {
            t->alert = true;
        } else if (t->temp_raw < (int16_t)t->tlow) {
            t->alert = false;
        }
    } else {
        /* alert (comparator) mode: pin follows out-of-range while the
         * flags latch until the config register is read — modelled as
         * level output + cfg-read clear (TODO(datasheet) nuance).     */
        t->alert = over;
    }
    t_pin_update(t);
}

static void t_por(tmp117_t *t)
{
    t->cfg         = T_POR_CFG;
    t->thigh       = T_POR_THIGH;
    t->tlow        = T_POR_TLOW;
    t->eep         = 0;
    t->ptr         = 0;
    t->temp_raw    = (int16_t)lroundf(t_feed_c(t) / T_LSB_C);
    t->conv_pend   = false;
    t->next_conv_us = 0;
    t->alert       = false;
    t->drdy        = false;
    t_pin_update(t);
}

/* Conversion-complete: latch the env temp, set DRDY, refresh alert. */
static void t_conv_done(tmp117_t *t)
{
    t->temp_raw = (int16_t)lroundf(t_feed_c(t) / T_LSB_C);
    t->drdy     = true;
    if (T_CFG_MOD(t->cfg) == 3) {
        /* one-shot auto-returns to shutdown (MOD 11 → 01)            */
        t->cfg = (uint16_t)((t->cfg & ~T_CFG_MOD_MASK) | 0x0400u);
    }
    t_alert_eval(t);
}

/* Async state machine — called from tick() and lazily from I2C paths
 * so behaviour is correct with or without the tick thread running.  */
static void t_poll(tmp117_t *t, int64_t now)
{
    if (!vboard_rail(VB_RAIL_3V3)) {
        t->powered   = false;
        t->conv_pend = false;
        if (t->alert_gpio >= 0) {
            vboard_gpio_set(t->alert_gpio, 1);   /* floats → R65 high */
        }
        return;
    }
    if (!t->powered) {
        t->powered = true;
        t_por(t);
    }
    if (t->conv_pend && now >= t->conv_ready_us) {
        t->conv_pend = false;
        t_conv_done(t);
    }
    unsigned mod = T_CFG_MOD(t->cfg);
    if ((mod == 0 || mod == 2) && !t->conv_pend
        && now >= t->next_conv_us) {
        t->conv_pend    = true;
        t->conv_ready_us = now + t_conv_us[T_CFG_AVG(t->cfg)];
        t->next_conv_us  = now + t_cyc_us[T_CFG_CONV(t->cfg)];
    }
}

static void t_reg_write(tmp117_t *t, uint8_t reg, uint16_t v)
{
    switch (reg) {
    case T_REG_CFG:
        if (v & T_CFG_SRST) {
            t_por(t);                    /* soft reset → POR state     */
            return;
        }
        t->cfg = v;
        switch (T_CFG_MOD(v)) {
        case 3:                          /* one-shot                  */
            t->conv_pend     = true;
            t->conv_ready_us = vboard_now_us()
                               + t_conv_us[T_CFG_AVG(v)];
            break;
        case 0:
        case 2:                          /* continuous                */
            t->conv_pend    = false;
            t->next_conv_us = vboard_now_us();   /* due immediately    */
            break;
        default:                         /* shutdown                  */
            t->conv_pend = false;
            break;
        }
        /* the comparator only re-evaluates at conversion-complete —
         * a write just re-derives the pin from the latched flags
         * (POL/DR-Alert/T-nA may have changed).                       */
        t_pin_update(t);
        break;
    case T_REG_THIGH:
        t->thigh = v;
        t_pin_update(t);
        break;
    case T_REG_TLOW:
        t->tlow = v;
        t_pin_update(t);
        break;
    case T_REG_EEP:
        t->eep = v;                      /* stored; no EEPROM modelled */
        break;
    default:
        break;                           /* RO / unmapped — ACK, drop  */
    }
}

static uint16_t t_reg_read(const tmp117_t *t, uint8_t reg)
{
    switch (reg) {
    case T_REG_TEMP:  return (uint16_t)t->temp_raw;
    case T_REG_CFG:   return t->cfg;
    case T_REG_THIGH: return t->thigh;
    case T_REG_TLOW:  return t->tlow;
    case T_REG_EEP:   return t->eep;
    case T_REG_ID:    return T_DEV_ID;
    default:          return 0;
    }
}

/* --------------------------------------------------------------- */

static esp_err_t t_i2c_write(vboard_dev_t *d, const uint8_t *data, size_t len)
{
    tmp117_t *t = (tmp117_t *)d->ctx;
    t_poll(t, vboard_now_us());
    if (!vboard_rail(VB_RAIL_3V3)) {
        return VB_ESP_ERR_TIMEOUT;            /* rail off → NACK        */
    }
    if (len == 0) {
        return VB_ESP_OK;                     /* bare probe             */
    }
    if (len != 1 && len != 3) {
        return VB_ESP_ERR_TIMEOUT;            /* frames are ptr|ptr+16b */
    }
    t->ptr = data[0];
    if (len == 3) {
        t_reg_write(t, t->ptr, (uint16_t)(((uint16_t)data[1] << 8) | data[2]));
    }
    return VB_ESP_OK;
}

static esp_err_t t_i2c_read(vboard_dev_t *d, uint8_t *data, size_t len)
{
    tmp117_t *t = (tmp117_t *)d->ctx;
    t_poll(t, vboard_now_us());
    if (!vboard_rail(VB_RAIL_3V3)) {
        return VB_ESP_ERR_TIMEOUT;
    }
    if (len == 0) {
        return VB_ESP_OK;
    }
    uint16_t v = t_reg_read(t, t->ptr);
    for (size_t i = 0; i < len; i++) {        /* no ptr auto-increment  */
        data[i] = (i & 1u) ? (uint8_t)(v & 0xFF) : (uint8_t)(v >> 8);
    }
    if (t->ptr == T_REG_CFG) {                /* "read clears flags"    */
        t->drdy = false;
        if (!T_CFG_TNA(t->cfg)) {
            t->alert = false;                 /* therm mode is level    */
        }
        t_pin_update(t);
    }
    return VB_ESP_OK;
}

static void t_tick(vboard_dev_t *d, int64_t now_us)
{
    t_poll((tmp117_t *)d->ctx, now_us);
}

static void t_reset(vboard_dev_t *d)
{
    tmp117_t *t = (tmp117_t *)d->ctx;
    if (vboard_rail(VB_RAIL_3V3)) {
        t->powered = true;
        t_por(t);
    } else {
        t->powered = false;
    }
}

/* --------------------------------------------------------------- */

static tmp117_t s_skin = {
    .dev = {
        .name      = "tmp117_48",
        .i2c_addr  = T_ADDR_SKIN,
        .i2c_write = t_i2c_write,
        .i2c_read  = t_i2c_read,
        .spi_cs    = -1,
        .tick      = t_tick,
        .reset     = t_reset,
    },
    .addr       = T_ADDR_SKIN,
    .alert_gpio = T_ALERT_GPIO,         /* IO5 — only U11 is wired      */
};

static tmp117_t s_dome = {
    .dev = {
        .name      = "tmp117_49",
        .i2c_addr  = T_ADDR_DOME,
        .i2c_write = t_i2c_write,
        .i2c_read  = t_i2c_read,
        .spi_cs    = -1,
        .tick      = t_tick,
        .reset     = t_reset,
    },
    .addr       = T_ADDR_DOME,
    .alert_gpio = -1,                   /* U20 ALERT is NC              */
};

void vbdev_tmp117_register(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    s_skin.dev.ctx = &s_skin;
    s_dome.dev.ctx = &s_dome;
    vboard_register(&s_skin.dev);
    vboard_register(&s_dome.dev);
}

/* Self-register at load so the default population exists even before
 * vboard_init() wires anything explicitly (idempotent via `done`).   */
__attribute__((constructor))
static void t_ctor(void)
{
    vbdev_tmp117_register();
}
