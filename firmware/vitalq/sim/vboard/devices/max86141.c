/* max86141.c — vboard device model: MAX86141ENP+ satellite PPG AFE
 * (U22, J10 tail domain, fix_v22).
 *
 * Bus/power:
 *   SPI2, CS = IO38, mode 0.  Powered from the +1V8 rail.  IO38 doubles
 *   as the active-low OE of the U26 SN74AXC2T245 MISO level shifter —
 *   SDO only drives while CS is low.  That gating is INHERENT to CS
 *   dispatch (the shim only calls spi_xfer while CS is asserted), so no
 *   extra work is needed here — noted per hw_v3.yaml D1.
 *   INT = IO39, open-drain active-low, R113 10k pull-up to +3V3.
 *
 * SPI frame format (driver vq_drv_max86141.c / max86141_regs.h):
 *   one CS assertion per transaction: [reg][0x00=write | 0xFF=read][data..]
 *   single reg access = 3 bytes.  FIFO burst = [0x08][0xFF] then N*3
 *   bytes under the same CS; each word is 24 bits =
 *   {tag[4:0] in byte0[7:3], 19-bit ADC counts in bits 18:0}.
 *
 * Emulated behaviour:
 *   - PART_ID (0xFF) reads 0x25 (MX_PART_ID_MAX86141).
 *   - MODE_CONFIG: bit0 RESET = soft reset (self-clearing, full POR);
 *     bit1 SHDN stops the sampling engine and resets the FIFO pointers
 *     (wr/rd/count -> 0), matching the driver's assumption.
 *   - Sampling engine: while out of SHDN on a live 1V8 rail, produces
 *     one exposure word per non-zero LEDC slot (LED_SEQ_1..3 order,
 *     [3:0] first) at the PPG_SR rate (199.805 sps default = 5005 us;
 *     SMP_AVE divides the word rate).  LED-phase counts =
 *     ambient feed + ppg feed scaled by LEDx_PA x range; the LED
 *     contribution collapses to ambient when the TX_5V rail is off
 *     (LED_AN dead — the driver's "only ambient words" caveat).
 *   - FIFO: 128 words, wr/rd shadow pointers, FIFO_DATA_COUNT,
 *     OVF_COUNTER (read-clears), FLUSH_FIFO strobe, FIFO_RO rollover,
 *     A_FULL (empty-space watermark at 128-FIFO_A_FULL) honouring
 *     A_FULL_TYPE (edge vs level).  DATA_RDY sets on every push.
 *   - INT is level-held while (STAT & EN) != 0; INT_STAT_1/2 are
 *     read-to-clear; FIFO_STAT_CLR makes FIFO_DATA reads clear too.
 *     Line transitions go through vboard_irq_pulse(39, level).
 *   - Rail loss => SPI dead (reads 0xFF, MISO released); on restore the
 *     part repowers to POR with INT_STAT_1.PWR_RDY set ("VDD dipped
 *     UVLO"), which the driver logs.
 *
 * NOT silicon-accurate — register-plausible + protocol-faithful per
 * vboard.h.
 */
#include "vboard.h"
#include "bio2_feed.h"

#include <string.h>
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* pins                                                                */
#define MX_PIN_CS       38          /* also U26 MISO-shifter OE (inherent) */
#define MX_PIN_INT      39          /* open-drain, R113 pull-up         */

/* ------------------------------------------------------------------ */
/* SPI framing                                                         */
#define MX_WR_EN        0x00
#define MX_RD_EN        0xff

/* register addresses                                                  */
#define R_INT_STAT_1    0x00
#define R_INT_STAT_2    0x01
#define R_INT_EN_1      0x02
#define R_INT_EN_2      0x03
#define R_FIFO_WR_PTR   0x04
#define R_FIFO_RD_PTR   0x05
#define R_OVF_COUNTER   0x06
#define R_FIFO_COUNT    0x07
#define R_FIFO_DATA     0x08
#define R_FIFO_CFG_1    0x09
#define R_FIFO_CFG_2    0x0A
#define R_MODE_CFG      0x0D
#define R_PPG_SYNC      0x10
#define R_PPG_CFG_1     0x11
#define R_PPG_CFG_2     0x12
#define R_LED_SEQ_1     0x20
#define R_LED_SEQ_2     0x21
#define R_LED_SEQ_3     0x22
#define R_LED1_PA       0x23          /* ..0x28 LED6_PA, 0x29 PILOT_PA   */
#define R_LED_PILOT_PA  0x29
#define R_LED_RANGE_1   0x2A
#define R_LED_RANGE_2   0x2B
#define R_PART_ID       0xFF

/* field bits                                                          */
#define MX_INT1_PWR_RDY   0x01
#define MX_INT1_VDD_OOR   0x02
#define MX_INT1_DATA_RDY  0x40
#define MX_INT1_A_FULL    0x80
#define MX_MODE_RESET     0x01
#define MX_MODE_SHDN      0x02
#define MX_CFG2_FIFO_RO   0x02
#define MX_CFG2_A_FULL_T  0x04        /* 1 = new-condition edge          */
#define MX_CFG2_STAT_CLR  0x08
#define MX_CFG2_FLUSH     0x10
#define MX_PARTID         0x25        /* MAX86141                        */
#define MX_DEPTH          128
#define MX_VAL_MASK       0x7ffffu    /* 19-bit ADC                      */

/* PPG_SR (PPG_CFG_2[7:3]) frame period in us @32768 Hz osc.
 * 0=24.995 1=50.027 2=84.021 3=99.902 4=199.805 5=399.610 sps; idx >5
 * clamps to the fastest listed rate (driver never uses them).          */
static const int32_t s_sr_us[6] = { 40016, 19989, 11902, 10010, 5005, 2502 };

/* LED full-scale range table (LED_RANGE 2-bit field) mA                */
static const int s_led_rge_ma[4] = { 31, 62, 93, 124 };

/* LEDC exposure codes                                                 */
#define LEDC_NONE      0x0
#define LEDC_LED1      0x1
#define LEDC_LED2      0x2
#define LEDC_LED3      0x3
#define LEDC_PILOT1    0x8
#define LEDC_AMBIENT   0x9

/* ------------------------------------------------------------------ */
/* feeds                                                               */
static uint32_t s_feed_ppg = 200000u;   /* LED-phase PD counts (19-bit)  */
static uint32_t s_feed_amb = 50000u;    /* direct-ambient counts         */

uint32_t vbb2_ppg_sat(void) { return s_feed_ppg; }
uint32_t vbb2_ppg_amb(void) { return s_feed_amb; }
void vbb2_ppg_sat_set(uint32_t c) { s_feed_ppg = c & MX_VAL_MASK; }
void vbb2_ppg_set    (uint32_t c) { vbb2_ppg_sat_set(c); }
void vbb2_ppg_amb_set(uint32_t c) { s_feed_amb = c & MX_VAL_MASK; }

/* ------------------------------------------------------------------ */
typedef struct {
    pthread_mutex_t lock;
    bool     powered;
    uint8_t  r[256];

    /* FIFO — explicit count: 7-bit wr/rd alone can't tell empty/full */
    uint32_t fifo[MX_DEPTH];
    uint8_t  wr, rd, fcnt;
    uint8_t  ovf;

    /* interrupt */
    uint8_t  stat1, stat2;
    bool     a_full_latched;       /* TYPE=1 new-condition memory        */
    int      int_lvl;

    /* sampling engine */
    int64_t  next_frame_us;
    uint32_t word_seq;             /* deterministic wobble source        */

    /* SPI shift state — persists across the firmware's split TX/RX legs
     * while CS38 stays low (vq_spi_write_read does cmd leg + data leg
     * as separate spi_xfer calls under one held CS; a real chip just
     * sees a continuous bit stream).                                   */
    uint8_t  sh_addr;              /* byte 0: register address          */
    uint8_t  sh_mode;              /* byte 1: 0x00 write / 0xFF read    */
    int      sh_pos;               /* byte index since CS assert        */
    bool     sh_fifo_rd;           /* a FIFO word popped under this CS  */
    uint32_t sh_last_word;         /* last FIFO pop — mid-word replay   */
} mx_t;

static mx_t s_mx;
static void mx_por_ctx(mx_t *s);

/* ------------------------------------------------------------------ */
static int fifo_cnt(const mx_t *s) { return s->fcnt; }

static int a_full_thresh(const mx_t *s)
{
    return MX_DEPTH - (s->r[R_FIFO_CFG_1] & 0x7f);
}

/* open-drain INT: low while any enabled status bit stands              */
static void eval_int(mx_t *s)
{
    /* level-mode A_FULL re-derives from the standing condition         */
    if (!(s->r[R_FIFO_CFG_2] & MX_CFG2_A_FULL_T) &&
        fifo_cnt(s) >= a_full_thresh(s))
        s->stat1 |= MX_INT1_A_FULL;

    int lvl = ((s->stat1 & s->r[R_INT_EN_1]) |
               (s->stat2 & s->r[R_INT_EN_2])) ? 0 : 1;
    if (lvl == s->int_lvl)
        return;
    s->int_lvl = lvl;
    vboard_irq_pulse(MX_PIN_INT, lvl);
}

static void fifo_push(mx_t *s, uint32_t w)
{
    if (fifo_cnt(s) >= MX_DEPTH) {
        if (s->r[R_FIFO_CFG_2] & MX_CFG2_FIFO_RO) {
            s->fifo[s->wr] = w;                 /* overwrite oldest     */
            s->wr = (uint8_t)((s->wr + 1) & 0x7f);
            s->rd = s->wr;                      /* oldest dropped       */
        } else {
            s->ovf = (uint8_t)(s->ovf + 1);     /* drop incoming        */
        }
    } else {
        s->fifo[s->wr] = w;
        s->wr = (uint8_t)((s->wr + 1) & 0x7f);
        s->fcnt++;
    }
    s->stat1 |= MX_INT1_DATA_RDY;
    if (!s->a_full_latched && fifo_cnt(s) >= a_full_thresh(s)) {
        s->a_full_latched = true;
        s->stat1 |= MX_INT1_A_FULL;
    }
    if (fifo_cnt(s) < a_full_thresh(s))
        s->a_full_latched = false;
    eval_int(s);
}

static uint32_t fifo_pop(mx_t *s)
{
    if (fifo_cnt(s) == 0)
        return 0;
    uint32_t w = s->fifo[s->rd];
    s->rd = (uint8_t)((s->rd + 1) & 0x7f);
    s->fcnt--;
    if (fifo_cnt(s) < a_full_thresh(s))
        s->a_full_latched = false;
    return w;
}

static void fifo_flush(mx_t *s)
{
    s->wr = s->rd = s->fcnt = s->ovf = 0;
    s->a_full_latched = false;
    if (!(s->r[R_FIFO_CFG_2] & MX_CFG2_A_FULL_T))
        s->stat1 &= (uint8_t)~MX_INT1_A_FULL;   /* level mode re-evals  */
    eval_int(s);
}

/* ------------------------------------------------------------------ */
/* exposure word synthesis                                             */

/* deterministic +-0.4% dither so consecutive words aren't identical    */
static uint32_t wobble(mx_t *s)
{
    s->word_seq = s->word_seq * 1103515245u + 12345u;
    return ((s->word_seq >> 16) & 0x3ff);
}

/* counts track LED current: PA/255 x range (mA) normalised to the     */
/* 124 mA full-scale, x the per-wavelength coupling factor pct         */
static uint32_t led_contrib(mx_t *s, int led, unsigned pct)
{
    uint8_t pa = s->r[R_LED1_PA + led - 1];
    /* LED1-4 ranges in RANGE_1[7:0] pairs; LED5-6 in RANGE_2[3:0]       */
    int     rg = (led <= 4)
               ? (s->r[R_LED_RANGE_1] >> (2 * (led - 1))) & 0x3
               : (s->r[R_LED_RANGE_2] >> (2 * (led - 5))) & 0x3;
    if (!pa)
        return 0;
    uint32_t ua_x10 = (uint32_t)pa * (uint32_t)s_led_rge_ma[rg] * 10u / 255u;
    return (uint32_t)(((uint64_t)s_feed_ppg * ua_x10 * pct) / (1240u * 100u));
}

/* FIFO word: {tag[4:0] = exposure-slot index, data[18:0] = counts}.
 * `slot` is the LEDC1..6 position (PPG1 slots 1..6), `code` the LEDC
 * function programmed into it — real silicon tags by slot, not code. */
static uint32_t make_word(mx_t *s, uint8_t slot, uint8_t code)
{
    uint32_t amb  = s_feed_amb;
    uint32_t val  = amb;
    bool     lit  = vboard_rail(VB_RAIL_TX5);    /* LED_AN = TX_5V       */

    if (lit) {
        switch (code) {
        case LEDC_LED1:
            val += led_contrib(s, 1, 100);                break;
        case LEDC_LED2:
            val += led_contrib(s, 2, 90);                 break;
        case LEDC_LED3:
            val += led_contrib(s, 3, 80);                 break;
        case 0x4:   /* LED1+LED2 */
            val += led_contrib(s, 1, 100) + led_contrib(s, 2, 90);   break;
        case 0x5:   /* LED1+LED3 */
            val += led_contrib(s, 1, 100) + led_contrib(s, 3, 80);   break;
        case 0x6:   /* LED2+LED3 */
            val += led_contrib(s, 2, 90) + led_contrib(s, 3, 80);    break;
        case 0x7:   /* all three */
            val += led_contrib(s, 1, 100) + led_contrib(s, 2, 90) +
                   led_contrib(s, 3, 80);                 break;
        case LEDC_PILOT1: {
            uint8_t pa = s->r[R_LED_PILOT_PA];
            uint32_t ua10 = (uint32_t)pa * 1240u / 255u;
            val += (uint32_t)(((uint64_t)s_feed_ppg * ua10 * 50) /
                             (1240u * 100u));
            break;
        }
        case 0xA: case 0xB: case 0xC:
        case 0xD: case 0xE: case 0xF:
            val += led_contrib(s, code - 0xA + 4, 70);
            break;
        default:
            break;                              /* ambient / none       */
        }
    }

    val += (int32_t)(wobble(s) - 512);          /* +-512 counts dither   */
    if ((int32_t)val < 0)
        val = 0;
    if (val > MX_VAL_MASK)
        val = MX_VAL_MASK;
    return ((uint32_t)slot << 19) | (val & MX_VAL_MASK);
}

/* one frame = one exposure word per non-zero slot, LEDC1..6 order;
 * tag = slot position (LEDC1→1, LEDC2→2, … LEDC6→6), not the LEDC code */
static void emit_frame(mx_t *s)
{
    static const uint8_t seq_reg[3] = { R_LED_SEQ_1, R_LED_SEQ_2, R_LED_SEQ_3 };
    for (int r_ = 0; r_ < 3; r_++) {
        uint8_t v = s->r[seq_reg[r_]];
        uint8_t lo = v & 0x0f, hi = v >> 4;
        if (lo)
            fifo_push(s, make_word(s, (uint8_t)(2 * r_ + 1), lo));
        if (hi)
            fifo_push(s, make_word(s, (uint8_t)(2 * r_ + 2), hi));
    }
}

static int32_t frame_period_us(const mx_t *s)
{
    unsigned idx = (s->r[R_PPG_CFG_2] >> 3) & 0x1f;
    unsigned ave = s->r[R_PPG_CFG_2] & 0x7;            /* SMP_AVE 1<<ave  */
    if (idx > 5)
        idx = 5;
    return s_sr_us[idx] << ave;
}

/* ------------------------------------------------------------------ */
/* register access                                                     */
static uint8_t reg_read(mx_t *s, uint8_t a)
{
    uint8_t v;
    switch (a) {
    case R_INT_STAT_1: v = s->stat1; s->stat1 = 0; eval_int(s); return v;
    case R_INT_STAT_2: v = s->stat2; s->stat2 = 0; eval_int(s); return v;
    case R_FIFO_WR_PTR: return s->wr;
    case R_FIFO_RD_PTR: return s->rd;
    case R_OVF_COUNTER: v = s->ovf; s->ovf = 0;      return v;
    case R_FIFO_COUNT:  return (uint8_t)fifo_cnt(s);
    case R_PART_ID:     return MX_PARTID;
    default:            return s->r[a];
    }
}

static void reg_write(mx_t *s, uint8_t a, uint8_t v, int64_t now)
{
    switch (a) {
    case R_MODE_CFG:
        if (v & MX_MODE_RESET) {                    /* self-clearing      */
            mx_por_ctx(s);
            return;
        }
        {
            bool was_shdn = (s->r[R_MODE_CFG] & MX_MODE_SHDN) != 0;
            bool now_shdn = (v & MX_MODE_SHDN) != 0;
            s->r[R_MODE_CFG] = v;
            if (now_shdn && !was_shdn) {
                s->wr = s->rd = s->fcnt = 0;        /* stop + ptr reset   */
                s->a_full_latched = false;
            } else if (!now_shdn && was_shdn) {
                s->next_frame_us = now + frame_period_us(s);
            }
        }
        break;
    case R_FIFO_CFG_2:
        if (v & MX_CFG2_FLUSH)
            fifo_flush(s);                          /* strobe, self-clear */
        s->r[a] = (uint8_t)(v & ~MX_CFG2_FLUSH);
        break;
    case R_FIFO_WR_PTR: s->wr = v & 0x7f;           break;
    case R_FIFO_RD_PTR: s->rd = v & 0x7f;           break;
    case R_FIFO_CFG_1:
        s->r[a] = v;
        eval_int(s);
        break;
    case R_INT_EN_1: case R_INT_EN_2:
        s->r[a] = v;
        eval_int(s);
        break;
    default:
        s->r[a] = v;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* power-on reset                                                      */
static void mx_por_ctx(mx_t *s)
{
    memset(s->r, 0, sizeof s->r);
    s->wr = s->rd = s->fcnt = s->ovf = 0;
    s->stat1 = s->stat2 = 0;
    s->a_full_latched = false;
    s->next_frame_us  = 0;
    s->sh_pos = 0; s->sh_mode = s->sh_addr = 0;
    s->sh_fifo_rd = false; s->sh_last_word = 0;
    s->r[R_MODE_CFG]  = MX_MODE_SHDN;   /* powers up in shutdown         */
    s->stat1 |= MX_INT1_PWR_RDY;        /* "VDD dipped UVLO" evidence    */
    eval_int(s);
}

/* rail tracking — power loss kills the SPI block; restore => fresh POR */
static void power_sync(mx_t *s)
{
    bool rail = vboard_rail(VB_RAIL_1V8);
    if (rail && !s->powered)
        mx_por_ctx(s);                  /* repower: POR + PWR_RDY flag    */
    s->powered = rail;
}

/* ------------------------------------------------------------------ */
/* vboard_dev hooks                                                    */

/* One MOSI byte in, one MISO byte out.  State lives across calls so the
 * driver's split cmd/data legs (CS held low the whole time) behave like
 * the continuous bit stream a real die sees.                            */
static uint8_t mx_byte(mx_t *s, uint8_t mosi, int64_t now)
{
    int pos = s->sh_pos++;
    if (pos == 0) {
        s->sh_addr = mosi;
        return 0xFF;
    }
    if (pos == 1) {
        s->sh_mode = mosi;
        return 0xFF;
    }
    size_t idx = (size_t)(pos - 2);
    if (s->sh_mode == MX_WR_EN) {
        reg_write(s, (uint8_t)(s->sh_addr + idx), mosi, now);
    } else if (s->sh_mode == MX_RD_EN) {
        if (s->sh_addr == R_FIFO_DATA) {
            /* FIFO lane: every 3rd byte pops one 24-bit word           */
            uint32_t w;
            if (idx % 3 == 0) {
                s->sh_last_word = fifo_pop(s);
                s->sh_fifo_rd   = true;
            }
            w = s->sh_last_word;   /* bytes 2/3 replay the same word    */
            return (uint8_t)(w >> (8 * (2 - (idx % 3))));
        }
        return reg_read(s, (uint8_t)(s->sh_addr + idx));
    }
    return 0xFF;
}

/* CS38 also OE's the U26 MISO shifter.  Assert: new frame.  Deassert:
 * frame done — apply end-of-frame semantics (STAT_CLR post-FIFO-read). */
static void cs_cb(int gpio, int lvl, void *ctx)
{
    (void)gpio;
    mx_t *s = ctx;
    pthread_mutex_lock(&s->lock);
    if (lvl == 0) {
        s->sh_pos     = 0;
        s->sh_mode    = 0;
        s->sh_addr    = 0;
        s->sh_fifo_rd = false;
    } else {
        if (s->sh_fifo_rd && (s->r[R_FIFO_CFG_2] & MX_CFG2_STAT_CLR)) {
            s->stat1 = s->stat2 = 0;
            eval_int(s);
        }
        s->sh_pos = 0;
    }
    pthread_mutex_unlock(&s->lock);
}

static esp_err_t mx_spi(vboard_dev_t *d, const uint8_t *tx,
                        uint8_t *rx, size_t len)
{
    mx_t   *s   = d->ctx;
    int64_t now = vboard_now_us();

    pthread_mutex_lock(&s->lock);
    power_sync(s);

    /* rail down: SPI block dead — MISO released (0xFF).                */
    if (!s->powered) {
        if (rx)
            memset(rx, 0xFF, len);
        pthread_mutex_unlock(&s->lock);
        return VB_ESP_OK;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t m = mx_byte(s, tx != NULL ? tx[i] : 0xFF, now);
        if (rx)
            rx[i] = m;
    }
    pthread_mutex_unlock(&s->lock);
    return VB_ESP_OK;
}

static void mx_tick(vboard_dev_t *d, int64_t now_us)
{
    mx_t *s = d->ctx;
    pthread_mutex_lock(&s->lock);
    power_sync(s);
    if (s->powered && !(s->r[R_MODE_CFG] & MX_MODE_SHDN)) {
        int32_t per = frame_period_us(s);
        if (s->next_frame_us == 0 || s->next_frame_us < now_us - 50000)
            s->next_frame_us = now_us + per;        /* resync after idle */
        while (s->next_frame_us <= now_us) {
            emit_frame(s);
            s->next_frame_us += per;
        }
    }
    pthread_mutex_unlock(&s->lock);
}

static void mx_reset(vboard_dev_t *d)
{
    mx_t *s = d->ctx;
    pthread_mutex_lock(&s->lock);
    mx_por_ctx(s);
    s->powered = vboard_rail(VB_RAIL_1V8);
    pthread_mutex_unlock(&s->lock);
}

static void mx_rail_cb(vboard_rail_t r, bool on, void *ctx)
{
    (void)r; (void)on;
    mx_t *s = ctx;
    pthread_mutex_lock(&s->lock);
    power_sync(s);
    pthread_mutex_unlock(&s->lock);
}

static vboard_dev_t s_dev = {
    .name      = "max86141",
    .i2c_addr  = 0,
    .i2c_write = NULL,
    .i2c_read  = NULL,
    .spi_cs    = MX_PIN_CS,
    .spi_xfer  = mx_spi,
    .tick      = mx_tick,
    .reset     = mx_reset,
    .ctx       = &s_mx,
};

/* ------------------------------------------------------------------ */
void vbdev_max86141_register(void)
{
    pthread_mutex_init(&s_mx.lock, NULL);
    s_mx.int_lvl = 1;
    s_mx.powered = vboard_rail(VB_RAIL_1V8);
    mx_por_ctx(&s_mx);
    vboard_gpio_set(MX_PIN_INT, 1);     /* released via R113 pull-up    */
    vboard_register(&s_dev);
    vboard_on_rail(VB_RAIL_1V8, mx_rail_cb, &s_mx);
    vboard_on_gpio_out(MX_PIN_CS, cs_cb, &s_mx);
}
