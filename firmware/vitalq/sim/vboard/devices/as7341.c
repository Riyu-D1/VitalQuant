/* as7341.c — AMS-OSRAM AS7341 11-channel spectral-sensor node
 * "as7341" @0x39 on the 1.8 V island (behind the PCA9306), fix_v22.
 *
 * Register model mirrors what components/vq_drv_island/src/as7341.c
 * uses (DS000504 §10 + AN000666 SMUX maps):
 *   0x00-0x13 SMUX RAM     RW  20 bytes; two 3-bit fields per byte,
 *                            field = ADC#+1 (0 = pixel disconnected)
 *   0x70      CONFIG       RW  bank-1 only: LED_SEL b3
 *   0x74      LED          RW  bank-1 only: LED_ACT b7 | LED_DRIVE[6:0]
 *   0x80      ENABLE       RW  PON b0, SP_EN b1, WEN b2 (driver mask
 *                            0x04 — driver's own comment says b3 but
 *                            AS7341_EN_WEN is 0x04; the model matches
 *                            THE DRIVER), SMUXEN b4, FDEN b6
 *   0x81      ATIME        RW  integration = (ATIME+1)(ASTEP+1)·2.78 µs
 *   0x83      WTIME        RW  wait = (WTIME+1)·2.78 ms when WEN
 *   0x90      AUXID        RO  = 0x02 (TODO(datasheet): plausible value)
 *   0x91      REVID        RO  = 0x01 (TODO(datasheet): plausible value)
 *   0x92      ID           RO  = 0x24 (driver + st_i2c_scan require it)
 *   0x93      STATUS       RO  SINT b7 | AINT b3 | FINT b2
 *   0x94      ASTATUS      RO  ASAT_STATUS b7
 *   0x95-0xA0 CH0..CH5     RO  six u16 little-endian ADC results
 *   0xA3      STATUS2      RO  AVALID b6 | ASAT_DIG b4 | ASAT_ANA b3
 *   0xA9      CFG0         RW  LOW_POWER b5 | REG_BANK b4 | WLONG b2
 *   0xAA      CFG1         RW  AGAIN[4:0] (0→0.5x, n→2^(n-1)x, ≤512x)
 *   0xAF      CFG6         RW  SMUX_CMD[4:3]
 *   0xCA/0xCB ASTEP_L/H    RW  integration step
 *   0xD6      AZ_CONFIG    RW  auto-zero every Nth integration (+15 ms)
 *   0xD8/0xDA FD_TIME_1/2  RW  flicker-detection timing
 *   0xDB      FD_STATUS    RO  flicker result (see TODO below)
 *
 * Frames: [ptr] or [ptr|data…] writes; repeated-start reads stream from
 * the pointer with per-byte auto-increment.  Writes to the bank-1
 * window (0x60-0x7F) take effect only while CFG0.REG_BANK=1 — the
 * driver always banks around its LED accesses; with REG_BANK=0 reads
 * there return 0x00 (documented simplification of the mirror page).
 *
 * Measurement engine: SMUXEN starts a ~200 µs SMUX commit that decodes
 * the 20-byte RAM into an ADC←diode routing (self-clearing bit the
 * driver polls).  SP_EN starts one integration: wait(WEN) + tint +
 * auto-zero time, then AVALID + AINT latch and the six ADC counts are
 * frozen from the scene AT completion:
 *
 *   counts = dark_floor + Σ_pixels(illumination·sensitivity·gain/64)
 *
 * where illumination = vbopt_illumination() (arb units; 0/100/1000)
 *              + LDR-LED contribution when LED_ACT&&LED_SEL
 *                (I[mA] = 4 + 2·LED_DRIVE, ≈25 arb-units/mA → ~2300 at
 *                the driver's 92 mA code 44 → dark ≪ lit for §9)
 * and NIR pixels additionally see IR_GATE (vboard_exp_pin(4), TCA6408
 * P4 — the Q1 low-side IR switch): +1800 arb-units onto every routed
 * NIR pixel, +15 % spill onto CLEAR pixels (unfiltered diode).
 * Counts ≥0xFFF0 clamp and raise ASAT flags — the driver's
 * AS7341_ADC_SAT quality path is exercised.
 *
 * TODO(datasheet): STATUS/ASTATUS/FD_STATUS bit layouts are simplified
 * to plausible flags (the driver only polls STATUS2.AVALID); FD engine
 * models conversion-valid only, no flicker-frequency stimulus.
 *
 * Concurrency: deliberately lock-free like the sibling models — the
 * firmware serialises I2C through its own bus mutex; tick-time engine
 * updates are idempotent.  Powered by VB_RAIL_1V8: rail down → NACK
 * (ESP_ERR_TIMEOUT) and full POR on restore.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "vboard.h"
#include "optical_feed.h"

/* ---- shared optical-scene store (optical_feed.h declares it extern) - */
vbopt_t g_vbopt = {
    .illum     = 100.0f,
    .mlx_obj_c = 33.0f,
    .mlx_amb_c = 30.0f,
};

#define A_ADDR          0x39u

/* register addresses */
#define A_REG_ENABLE    0x80u
#define A_REG_ATIME     0x81u
#define A_REG_WTIME     0x83u
#define A_REG_AUXID     0x90u
#define A_REG_REVID     0x91u
#define A_REG_ID        0x92u
#define A_REG_STATUS    0x93u
#define A_REG_ASTATUS   0x94u
#define A_REG_CH0_L     0x95u        /* …0xA0 = CH5_DATA_H            */
#define A_REG_CH5_H     0xA0u
#define A_REG_STATUS2   0xA3u
#define A_REG_CFG0      0xA9u
#define A_REG_CFG1      0xAAu
#define A_REG_CFG6      0xAFu
#define A_REG_ASTEP_L   0xCAu
#define A_REG_ASTEP_H   0xCBu
#define A_REG_AZ_CONFIG 0xD6u
#define A_REG_FD_TIME1  0xD8u
#define A_REG_FD_TIME2  0xDAu
#define A_REG_FD_STATUS 0xDBu
#define A_REG_CONFIG70  0x70u
#define A_REG_LED74     0x74u
#define A_SMUX_BASE     0x00u
#define A_SMUX_LEN      20u
#define A_BANK1_LO      0x60u
#define A_BANK1_HI      0x7Fu

/* ENABLE bits — the driver's masks (as7341.c: AS7341_EN_*), WEN = b3
 * per DS000504 ENABLE register table.                                 */
#define A_EN_PON        0x01u
#define A_EN_SP_EN      0x02u
#define A_EN_WEN        0x08u
#define A_EN_SMUXEN     0x10u
#define A_EN_FDEN       0x40u

/* CFG0/CONFIG/LED bits */
#define A_CFG0_WLONG    0x04u
#define A_CFG0_REG_BANK 0x10u
#define A_CFG_LED_SEL   0x08u
#define A_LED_ACT       0x80u
#define A_LED_DRIVE     0x7Fu

/* STATUS/STATUS2/ASTATUS/FD_STATUS bits */
#define A_ST_SINT       0x80u
#define A_ST_AINT       0x08u
#define A_ST_FINT       0x04u
#define A_ST2_AVALID    0x40u
#define A_ST2_ASAT_DIG  0x10u
#define A_ST2_ASAT_ANA  0x08u
#define A_AST_ASAT      0x80u
#define A_FD_VALID      0x40u        /* TODO(datasheet): simplified    */

#define A_ID_VALUE      0x24u        /* (id & 0xFC) == 0x24 required   */
#define A_AUXID_VALUE   0x02u
#define A_REVID_VALUE   0x01u

/* timing (virtual µs) */
#define A_SMUX_US       200
#define A_AZ_US         15000        /* auto-zero every cycle cost    */
#define A_FD_US         5000
#define A_TINT_STEP_NS  2780         /* 2.78 µs LSB                   */
#define A_WTIME_STEP_US 2780         /* (WTIME+1)·2.78 ms             */

/* optical scale */
#define A_DARK_BASE     6.0f         /* dark floor per ADC            */
#define A_LED_LUX_PER_MA 25.0f       /* LDR emitter → scene units     */
#define A_IR_GATE_LUX   1800.0f      /* IR_GATE (P4) boost, NIR pixels */
#define A_IR_CLR_SPILL  0.15f        /* …spill onto unfiltered CLEAR  */
#define A_ADC_SAT       0xFFF0       /* driver's saturation threshold */

#define IR_GATE_PIN     4            /* TCA6408 P4 — Q1 IR switch     */

/* photodiode ids for SMUX decode */
enum {
    D_NONE = 0,
    D_F1, D_F2, D_F3, D_F4, D_F5, D_F6, D_F7, D_F8,
    D_CLEAR, D_NIR, D_FLICK, D__N
};

/* counts per pixel per arb-illum unit at AGAIN=64x — a plausible
 * spectral shape peaking at the green F5 channel (driver feeds raw
 * counts; only relative channel response matters to the pipeline). */
static const float a_sens[D__N] = {
    [D_NONE] = 0.0f,
    [D_F1] = 1.0f, [D_F2] = 1.2f, [D_F3] = 1.5f, [D_F4] = 1.8f,
    [D_F5] = 2.0f, [D_F6] = 1.7f, [D_F7] = 1.3f, [D_F8] = 1.1f,
    [D_CLEAR] = 3.0f, [D_NIR] = 0.9f, [D_FLICK] = 2.2f,
};

/* SMUX RAM position → photodiode pair, {low-nibble field, high-nibble
 * field}.  Decoded from the driver's two AN000666 maps: every position
 * it ever routes names its diode in the as7341.c comments; positions
 * no bank uses stay D_NONE (their nibble pixel is irrelevant here).   */
static const uint8_t a_smux_diode[A_SMUX_LEN][2] = {
    [0x00] = { D_NONE,  D_F3    },   /* F3L on the high field        */
    [0x01] = { D_F1,    D_NONE  },   /* F1L                          */
    [0x02] = { D_NONE,  D_NONE  },   /* reserved                     */
    [0x03] = { D_NONE,  D_F8    },   /* hi: F8L  (bank B 0x40)       */
    [0x04] = { D_F6,    D_NONE  },   /* F6L                          */
    [0x05] = { D_F2,    D_F4    },   /* F2L low / F4L high           */
    [0x06] = { D_NONE,  D_F5    },   /* hi: F5L  (bank B 0x10)       */
    [0x07] = { D_F7,    D_NONE  },   /* F7L                          */
    [0x08] = { D_NONE,  D_CLEAR },   /* CLEAR(L)                     */
    [0x09] = { D_NONE,  D_F5    },   /* hi: F5R  (bank B 0x10)       */
    [0x0A] = { D_F7,    D_NONE  },   /* F7R                          */
    [0x0B] = { D_NONE,  D_NONE  },   /* reserved                     */
    [0x0C] = { D_NONE,  D_F2    },   /* F2R                          */
    [0x0D] = { D_F4,    D_NONE  },   /* F4R                          */
    [0x0E] = { D_F8,    D_F6    },   /* F8R low / F6R high           */
    [0x0F] = { D_NONE,  D_F3    },   /* F3R                          */
    [0x10] = { D_F1,    D_NONE  },   /* F1R                          */
    [0x11] = { D_NONE,  D_CLEAR },   /* CLEAR(R)                     */
    [0x12] = { D_NONE,  D_NONE  },   /* reserved                     */
    [0x13] = { D_NIR,   D_FLICK },   /* NIR low / FLICKER high       */
};

typedef struct {
    vboard_dev_t dev;
    uint8_t      ptr;

    /* register file */
    uint8_t      enable, atime, wtime;
    uint16_t     astep;
    uint8_t      cfg0, cfg1, cfg6, azcfg;
    uint8_t      config70, led74;        /* bank-1 page               */
    uint8_t      fd_time1;
    uint16_t     fd_time2;
    uint8_t      smux_ram[A_SMUX_LEN];

    /* decoded SMUX routing: per-ADC summed sensitivity + NIR/CLEAR
     * pixel counts (L and R pixels of a filter sum onto one ADC)      */
    float        adc_sens[6];
    uint8_t      adc_nir[6], adc_clr[6];

    /* async engine */
    bool         smux_pend;   int64_t smux_done_us;
    bool         integ_pend;  int64_t integ_ready_us;
    bool         avalid;      uint16_t ch[6];  uint8_t sat;
    bool         fd_pend;     int64_t fd_done_us;  uint8_t fd_status;

    bool         powered;
} as_t;

/* ---- decode / measurement ------------------------------------------ */

static void a_smux_decode(as_t *s)
{
    memset(s->adc_sens, 0, sizeof s->adc_sens);
    memset(s->adc_nir,  0, sizeof s->adc_nir);
    memset(s->adc_clr,  0, sizeof s->adc_clr);
    for (int i = 0; i < (int)A_SMUX_LEN; i++) {
        const uint8_t fld[2] = {
            (uint8_t)(s->smux_ram[i] & 0x07u),
            (uint8_t)((s->smux_ram[i] >> 4) & 0x07u),
        };
        for (int nib = 0; nib < 2; nib++) {
            uint8_t f = fld[nib];               /* 0 = grounded       */
            uint8_t d = a_smux_diode[i][nib];
            if (f == 0 || d == D_NONE || f > 6) {
                continue;
            }
            int adc = f - 1;                    /* field = ADC# + 1   */
            s->adc_sens[adc] += a_sens[d];
            if (d == D_NIR)   s->adc_nir[adc]++;
            if (d == D_CLEAR) s->adc_clr[adc]++;
        }
    }
}

/* AGAIN[4:0] → gain factor: 0 = 0.5x, n≥1 = 2^(n-1)x, clamp at 512x. */
static float a_again(uint8_t code)
{
    if (code == 0) {
        return 0.5f;
    }
    if (code > 10) {
        code = 10;
    }
    return (float)(1u << (code - 1));
}

/* Scene illumination the diode array sees: ambient feed + our own
 * LDR-driven emitter (only while LED_ACT && LED_SEL route it).       */
static float a_scene_illum(const as_t *s)
{
    float lux = g_vbopt.illum;
    if ((s->led74 & A_LED_ACT) && (s->config70 & A_CFG_LED_SEL)) {
        unsigned code = s->led74 & A_LED_DRIVE;
        lux += (float)(4u + 2u * code) * A_LED_LUX_PER_MA;
    }
    return lux;
}

/* Integration complete → freeze the six ADC counts from the scene. */
static void a_latch(as_t *s)
{
    const float lux  = a_scene_illum(s);
    const float g    = a_again((uint8_t)(s->cfg1 & 0x1Fu)) / 64.0f;
    const int   irg  = (vboard_exp_pin(IR_GATE_PIN) > 0);
    s->sat = 0;
    for (int i = 0; i < 6; i++) {
        float c = A_DARK_BASE + (float)i;
        c += lux * s->adc_sens[i] * g;
        if (irg) {
            c += A_IR_GATE_LUX * g *
                 ((float)s->adc_nir[i] * a_sens[D_NIR] +
                  (float)s->adc_clr[i] * a_sens[D_CLEAR] * A_IR_CLR_SPILL);
        }
        long v = lroundf(c);
        if (v >= 0xFFFF) {
            v = 0xFFFF;
            s->sat |= A_ST2_ASAT_ANA | A_ST2_ASAT_DIG;
        } else if (v >= A_ADC_SAT) {
            s->sat |= A_ST2_ASAT_DIG;
        }
        if (v < 0) {
            v = 0;
        }
        s->ch[i] = (uint16_t)v;
    }
    s->avalid = true;
}

/* Lazy async progression — called from tick() and every I2C path so
 * behaviour is correct with or without the tick thread.              */
static void a_poll(as_t *s, int64_t now)
{
    if (!vboard_rail(VB_RAIL_1V8)) {
        if (s->powered) {
            s->powered = false;
            s->smux_pend = s->integ_pend = s->fd_pend = s->avalid = false;
        }
        return;
    }
    if (!s->powered) {
        /* rail restore → POR: everything after `dev` returns to zero  */
        memset(&s->ptr, 0, sizeof(*s) - offsetof(as_t, ptr));
        s->powered = true;
    }
    if (s->smux_pend && now >= s->smux_done_us) {
        s->smux_pend = false;
        s->enable   &= (uint8_t)~A_EN_SMUXEN;    /* self-clearing bit  */
        a_smux_decode(s);
    }
    if (s->integ_pend && now >= s->integ_ready_us) {
        s->integ_pend = false;
        a_latch(s);
    }
    if (s->fd_pend && now >= s->fd_done_us) {
        s->fd_pend   = false;
        s->fd_status = A_FD_VALID;      /* steady scene, no freq flag */
    }
}

static int64_t a_tint_us(const as_t *s)
{
    /* tint = (ATIME+1)·(ASTEP+1)·2.78 µs; WLONG stretches ASTEP ×16.
     * int64: (255+1)·(65535+1)·2.78 µs·16 ≈ 745 s worst case — fits. */
    int64_t steps = (int64_t)(s->atime + 1u) * (int64_t)(s->astep + 1u);
    if (s->cfg0 & A_CFG0_WLONG) {
        steps *= 16;
    }
    int64_t t = steps * A_TINT_STEP_NS / 1000;
    if (s->azcfg) {
        t += A_AZ_US;                   /* auto-zero each integration */
    }
    return t;
}

/* ---- register access ------------------------------------------------ */

static void a_reg_write(as_t *s, uint8_t reg, uint8_t v)
{
    int64_t now = vboard_now_us();
    switch (reg) {
    case A_REG_ENABLE: {
        uint8_t old = s->enable;
        s->enable = v;
        if (!(v & A_EN_PON)) {                    /* standby: idle all  */
            s->integ_pend = s->smux_pend = s->fd_pend = false;
            s->avalid = false;
            break;
        }
        if ((v & A_EN_SMUXEN) && !(old & A_EN_SMUXEN) && !s->smux_pend) {
            s->smux_pend   = true;
            s->smux_done_us = now + A_SMUX_US;
        }
        if ((v & A_EN_SP_EN) && !(old & A_EN_SP_EN)) {
            int64_t wait_us = (v & A_EN_WEN)
                              ? (int64_t)(s->wtime + 1u) * A_WTIME_STEP_US
                              : 0;
            s->integ_pend    = true;
            s->integ_ready_us = now + wait_us + a_tint_us(s);
            s->avalid        = false;
        }
        if (!(v & A_EN_SP_EN)) {
            s->integ_pend = false;              /* SP_EN low → idle     */
            s->avalid     = false;
        }
        if ((v & A_EN_FDEN) && !(old & A_EN_FDEN)) {
            s->fd_pend    = true;
            s->fd_done_us = now + A_FD_US;
        }
        break;
    }
    case A_REG_ATIME:     s->atime    = v; break;
    case A_REG_WTIME:     s->wtime    = v; break;
    case A_REG_CFG0:      s->cfg0     = v; break;
    case A_REG_CFG1:      s->cfg1     = v; break;
    case A_REG_CFG6:      s->cfg6     = v; break;
    case A_REG_AZ_CONFIG: s->azcfg    = v; break;
    case A_REG_ASTEP_L:   s->astep    = (uint16_t)((s->astep & 0xFF00u) | v); break;
    case A_REG_ASTEP_H:   s->astep    = (uint16_t)((s->astep & 0x00FFu) | ((uint16_t)v << 8)); break;
    case A_REG_FD_TIME1:  s->fd_time1 = v; break;
    case A_REG_FD_TIME2:  s->fd_time2 = v; break;
    case A_REG_CONFIG70:
    case A_REG_LED74:
        if (s->cfg0 & A_CFG0_REG_BANK) {        /* bank-1 window gated */
            if (reg == A_REG_CONFIG70) {
                s->config70 = v;
            } else {
                s->led74 = v;
            }
        }
        break;
    default:
        if (reg < A_SMUX_LEN) {
            s->smux_ram[reg] = v;               /* SMUX pixel RAM       */
        }
        /* RO regs (ID/STATUS/data) and unmapped addresses drop silently */
        break;
    }
}

static uint8_t a_reg_read(as_t *s, uint8_t reg)
{
    switch (reg) {
    case A_REG_ENABLE:   return s->enable;
    case A_REG_ATIME:    return s->atime;
    case A_REG_WTIME:    return s->wtime;
    case A_REG_AUXID:    return A_AUXID_VALUE;
    case A_REG_REVID:    return A_REVID_VALUE;
    case A_REG_ID:       return A_ID_VALUE;
    case A_REG_STATUS:
        return (uint8_t)((s->avalid ? A_ST_AINT : 0) |
                         ((s->fd_status & A_FD_VALID) ? A_ST_FINT : 0));
    case A_REG_ASTATUS:
        return (uint8_t)(s->sat ? A_AST_ASAT : 0);
    case A_REG_STATUS2:
        return (uint8_t)((s->avalid ? A_ST2_AVALID : 0) | s->sat);
    case A_REG_CFG0:     return s->cfg0;
    case A_REG_CFG1:     return s->cfg1;
    case A_REG_CFG6:     return s->cfg6;
    case A_REG_ASTEP_L:  return (uint8_t)(s->astep & 0xFFu);
    case A_REG_ASTEP_H:  return (uint8_t)(s->astep >> 8);
    case A_REG_AZ_CONFIG:return s->azcfg;
    case A_REG_FD_TIME1: return s->fd_time1;
    case A_REG_FD_TIME2: return (uint8_t)(s->fd_time2 & 0xFFu);
    case A_REG_FD_STATUS:return s->fd_status;
    case A_REG_CONFIG70: return (s->cfg0 & A_CFG0_REG_BANK) ? s->config70 : 0;
    case A_REG_LED74:    return (s->cfg0 & A_CFG0_REG_BANK) ? s->led74    : 0;
    default:
        if (reg < A_SMUX_LEN) {
            return s->smux_ram[reg];
        }
        if (reg >= A_REG_CH0_L && reg <= A_REG_CH5_H) {
            int idx = reg - A_REG_CH0_L;
            return (uint8_t)((idx & 1) ? (s->ch[idx >> 1] >> 8)
                                      : (s->ch[idx >> 1] & 0xFF));
        }
        return 0x00;
    }
}

/* ---- I2C interface --------------------------------------------------- */

static esp_err_t a_i2c_write(vboard_dev_t *d, const uint8_t *data, size_t len)
{
    as_t *s = (as_t *)d->ctx;
    a_poll(s, vboard_now_us());
    if (!vboard_rail(VB_RAIL_1V8)) {
        return VB_ESP_ERR_TIMEOUT;              /* rail off → NACK      */
    }
    if (len == 0) {
        return VB_ESP_OK;                       /* bare probe           */
    }
    s->ptr = data[0];
    for (size_t i = 1; i < len; i++) {
        a_reg_write(s, s->ptr++, data[i]);
    }
    return VB_ESP_OK;
}

static esp_err_t a_i2c_read(vboard_dev_t *d, uint8_t *data, size_t len)
{
    as_t *s = (as_t *)d->ctx;
    a_poll(s, vboard_now_us());
    if (!vboard_rail(VB_RAIL_1V8)) {
        return VB_ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < len; i++) {
        uint8_t reg = s->ptr++;
        data[i] = a_reg_read(s, reg);
        if (reg == A_REG_CH5_H) {
            s->avalid = false;      /* data readout clears AVALID      */
        }
    }
    return VB_ESP_OK;
}

static void a_tick(vboard_dev_t *d, int64_t now_us)
{
    a_poll((as_t *)d->ctx, now_us);
}

static void a_reset(vboard_dev_t *d)
{
    as_t *s = (as_t *)d->ctx;
    if (vboard_rail(VB_RAIL_1V8)) {
        memset(&s->ptr, 0, sizeof(*s) - offsetof(as_t, ptr));
        s->powered = true;
    } else {
        s->powered = false;
    }
}

/* --------------------------------------------------------------- */

static as_t s_as = {
    .dev = {
        .name      = "as7341",
        .i2c_addr  = A_ADDR,
        .i2c_write = a_i2c_write,
        .i2c_read  = a_i2c_read,
        .spi_cs    = -1,
        .tick      = a_tick,
        .reset     = a_reset,
    },
};

void vbdev_as7341_register(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    s_as.dev.ctx = &s_as;
    vboard_register(&s_as.dev);
}

/* Self-register at load so the default population exists even before
 * vboard_init() wires anything explicitly (idempotent via `done`).   */
__attribute__((constructor))
static void a_ctor(void)
{
    vbdev_as7341_register();
}
