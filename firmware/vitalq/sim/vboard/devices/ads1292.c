/* ads1292.c — ADS1292R two-channel biopotential AFE device model.
 *
 * The firmware driver (components/vq_drv_ads1292/vq_drv_ads1292.c) is the
 * protocol spec; this model is register-plausible, not silicon-accurate.
 *
 * Wire truth (board_pins.h / driver):
 *   CS = IO10 (VQ_CS_ADS1292_GPIO), SPI mode 1 — host-side mode does not
 *   affect xfer semantics here; the shim delivers raw byte streams.
 *   ~DRDY = IO2, active-low (NEGEDGE), R99 100k pull-up -> idle HIGH.
 *   PWDN/~RESET = TCA6408 P1 (default low = powered down) — the model
 *   answers ONLY while P1 is high AND VB_RAIL_3V3_ANA is up; otherwise
 *   MISO floats high (0xFF) and writes are ignored.
 *   START pin tied to GND -> conversions run only via the START opcode.
 *
 * Command set (single-byte opcodes, decoded on DIN while CS low):
 *   0x02 WAKEUP  0x04 STANDBY  0x06 RESET   0x08 START
 *   0x0A STOP    0x10 RDATAC   0x11 SDATAC  0x12 RDATA
 *   0x20|a RREG  0x40|a WREG   (2nd byte = reg count - 1)
 * Command frames may span multiple spi_xfer calls inside one CS window
 * (vq_spi_write_read holds CS across write+read); the parser aborts on
 * CS rising edge, like the real part.
 *
 * RDATAC frame = 9 B: 24-bit status {4'b1100, LOFF_STAT[4:0], GPIO[1:0],
 * 13'b0} then 2 x 24-bit two's-complement channel words, MSB first.
 *
 * DRDY engine: while powered + STARTed + not standby, one active-low
 * pulse per sample period (500 sps at CONFIG1.DR=010 reset value; the
 * DR field is honored 125..8000 sps).  The low level is held ~400 us so
 * both the NEGEDGE ISR path and the selftest's gpio poll see it.
 */
#include "vboard.h"
#include "bioz_feed.h"

#include <math.h>
#include <string.h>

/* ------------------------------ Pins / wiring -------------------------- */
#define ADS_CS_GPIO      10      /* CS_ADS1292                             */
#define ADS_DRDY_GPIO    2       /* ADS1292_DRDY, active-low               */
#define ADS_EXP_PWDN     1       /* TCA6408 P1 — high = powered            */

/* ------------------------------ SPI opcodes ---------------------------- */
#define ADS_OP_WAKEUP    0x02
#define ADS_OP_STANDBY   0x04
#define ADS_OP_RESET     0x06
#define ADS_OP_START     0x08
#define ADS_OP_STOP      0x0A
#define ADS_OP_RDATAC    0x10
#define ADS_OP_SDATAC    0x11
#define ADS_OP_RDATA     0x12
#define ADS_OP_RREG      0x20    /* | addr[4:0] */
#define ADS_OP_WREG      0x40    /* | addr[4:0] */

/* ------------------------------ Registers ------------------------------ */
#define ADS_REG_ID        0x00
#define ADS_REG_CONFIG1   0x01
#define ADS_REG_CONFIG2   0x02
#define ADS_REG_LOFF      0x03
#define ADS_REG_CH1SET    0x04
#define ADS_REG_CH2SET    0x05
#define ADS_REG_RLD_SENS  0x06
#define ADS_REG_LOFF_SENS 0x07
#define ADS_REG_LOFF_STAT 0x08
#define ADS_REG_RESP1     0x09
#define ADS_REG_RESP2     0x0A
#define ADS_REG_GPIO      0x0B
#define ADS_NREGS         12

/* CHnSET MUX field values that change what the model emits. */
#define ADS_MUX_NORMAL    0x00
#define ADS_MUX_SHORT     0x01
#define ADS_MUX_TEST      0x05

/* Frame = 24-bit status + 2 x 24-bit channels. */
#define ADS_FRAME_LEN     9
#define ADS_DRDY_LOW_US   400    /* poll-visible low window per sample    */

/* LSB weight = VREF/(GAIN*(2^23-1)); internal ref 2.42 V, ch1 gain 6,
 * ch2 (resp demod slot) nominal gain 1 — mirrors the driver's scaling. */
#define ADS_LSB_UV_CH1    (2.42e6 / (6.0 * 8388607.0))   /* ~0.0481 uV    */
#define ADS_LSB_UV_CH2    (2.42e6 / 8388607.0)           /* ~0.2885 uV    */
#define ADS_TEST_AMP_UV   1000.0  /* internal ~1 mV test signal           */

/* ------------------------------ Model state ---------------------------- */
enum {
    ST_CMD = 0,   /* expecting an opcode byte                          */
    ST_RREG_N,    /* expecting the (count-1) byte of an RREG           */
    ST_RREG_D,    /* shifting out `n_regs` register bytes              */
    ST_WREG_N,    /* expecting the (count-1) byte of a WREG            */
    ST_WREG_D,    /* latching `n_regs` register bytes                  */
    ST_RDATA,     /* shifting out one 9-byte RDATA frame               */
};

typedef struct {
    uint8_t  regs[ADS_NREGS];
    bool     powered;          /* sampled rail + PWDN                    */
    bool     converting;       /* START seen                             */
    bool     standby;          /* STANDBY seen                           */
    bool     rdatac;           /* read-data-continuous mode              */
    /* command-stream FSM (reset on CS rising) */
    uint8_t  st;
    uint8_t  reg_addr;
    uint8_t  n_regs;
    /* data readout */
    uint8_t  frame[ADS_FRAME_LEN];
    uint8_t  fb;
    /* DRDY engine */
    int64_t  next_drdy_us;
    int64_t  drdy_release_us;
    bool     drdy_low;
    uint32_t seq;
} ads_ctx_t;

/* ------------------------------ bioz feed ------------------------------ */
/* Implementation of the feed declared in bioz_feed.h — lives here so all
 * device models share one virtual subject.                             */
typedef struct {
    float hr_bpm;      /* heart rate, beats/min                          */
    float rr_brpm;     /* respiration, breaths/min                       */
    float ecg_uv;      /* lead-II R-peak amplitude                       */
    float resp_uv;     /* resp demod amplitude                           */
    float ppg_scale;   /* perfusion scale 0..1                           */
    bool  leadoff;     /* electrodes lifted -> noise floor + LOFF_STAT   */
} vbbio_state_t;

static vbbio_state_t s_bio = {
    VBBIO_DEF_HEART_BPM, VBBIO_DEF_RESP_BRPM, VBBIO_DEF_ECG_AMP_UV,
    VBBIO_DEF_RESP_AMP_UV, VBBIO_DEF_PPG_SCALE, false
};

/* Deterministic per-(t,lane) noise in [0,1) — no shared RNG state, so the
 * tick thread and SPI context can race harmlessly.                     */
static float vbb_noise(int64_t t_us, int lane)
{
    uint64_t h = (uint64_t)t_us * 0x9E3779B97F4A7C15ull
               + (uint64_t)lane * 0xBF58476D1CE4E5B9ull;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27; h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return (float)(h & 0xFFFFFFu) / 16777216.0f;
}

static float vbb_gauss(float x, float c, float w)
{
    float d = x - c;
    return expf(-(d * d) / (2.0f * w * w));
}

float vbbio_ecg(int64_t t_us)
{
    float nz = (vbb_noise(t_us, 1) - 0.5f) * 4.0f;      /* +/-2 uV floor */
    if (s_bio.leadoff) return nz;                     /* no contact    */
    float per = 60.0e6f / s_bio.hr_bpm;
    float ph  = (float)fmod((double)t_us / per, 1.0);
    /* P-QRS-T composite; R peak ~1.0 of amplitude. */
    float ecg = vbb_gauss(ph, 0.160f, 0.030f) * 0.16f   /* P  */
              - vbb_gauss(ph, 0.246f, 0.008f) * 0.10f   /* Q  */
              + vbb_gauss(ph, 0.262f, 0.010f) * 1.00f   /* R  */
              - vbb_gauss(ph, 0.278f, 0.012f) * 0.25f   /* S  */
              + vbb_gauss(ph, 0.460f, 0.055f) * 0.30f;  /* T  */
    return ecg * s_bio.ecg_uv + nz;
}

float vbbio_resp(int64_t t_us)
{
    if (s_bio.leadoff) return (vbb_noise(t_us, 2) - 0.5f) * 4.0f;
    float per = 60.0e6f / s_bio.rr_brpm;
    float s   = sinf(6.2831853f * (float)t_us / per);
    /* ~5% cardiac bleed-through, as the demod picks up both. */
    float c   = 0.05f * vbbio_ecg(t_us) / (s_bio.ecg_uv + 1e-9f) * s_bio.resp_uv;
    return s * s_bio.resp_uv + c
         + (vbb_noise(t_us, 2) - 0.5f) * 3.0f;
}

float vbbio_ppg(int64_t t_us)
{
    float per = 60.0e6f / s_bio.hr_bpm;
    float ph  = (float)fmod((double)t_us / per, 1.0);
    /* systolic peak + dicrotic-wave bump, ~0..1. */
    float v = vbb_gauss(ph, 0.14f, 0.045f)
            + 0.45f * vbb_gauss(ph, 0.38f, 0.090f);
    v += (vbb_noise(t_us, 3) - 0.5f) * 0.02f;           /* +/-1% noise  */
    return v * s_bio.ppg_scale;
}

void vbbio_set_heart_rate(float bpm)   { if (bpm   > 0.0f) s_bio.hr_bpm   = bpm; }
void vbbio_set_resp_rate (float brpm)  { if (brpm  > 0.0f) s_bio.rr_brpm  = brpm; }
void vbbio_set_ecg_amp_uv(float uv)    { s_bio.ecg_uv   = uv; }
void vbbio_set_resp_amp_uv(float uv)   { s_bio.resp_uv  = uv; }
void vbbio_set_ppg_scale (float s)     { s_bio.ppg_scale = s; }
void vbbio_set_leadoff   (bool off)    { s_bio.leadoff  = off; }
bool vbbio_get_leadoff   (void)        { return s_bio.leadoff; }
void vbbio_reset(void)
{
    s_bio.hr_bpm    = VBBIO_DEF_HEART_BPM;
    s_bio.rr_brpm   = VBBIO_DEF_RESP_BRPM;
    s_bio.ecg_uv    = VBBIO_DEF_ECG_AMP_UV;
    s_bio.resp_uv   = VBBIO_DEF_RESP_AMP_UV;
    s_bio.ppg_scale = VBBIO_DEF_PPG_SCALE;
    s_bio.leadoff   = false;
}

/* ------------------------------ Helpers -------------------------------- */
static ads_ctx_t s_ads;

static void ads_drdy_release(ads_ctx_t *c)
{
    if (c->drdy_low) {
        vboard_gpio_set(ADS_DRDY_GPIO, 1);
        c->drdy_low = false;
    }
}

/* Power-on reset state — also the RESET-opcode target. */
static void ads_por(ads_ctx_t *c)
{
    /* SBAS502 reset values; ID rev = 0x73 (driver accepts 0x7x). */
    static const uint8_t por[ADS_NREGS] = {
        [ADS_REG_ID]        = 0x73,
        [ADS_REG_CONFIG1]   = 0x02,   /* continuous, DR=010 -> 500 sps  */
        [ADS_REG_CONFIG2]   = 0xE0,
        [ADS_REG_LOFF]      = 0x10,
        [ADS_REG_CH1SET]    = 0x00,
        [ADS_REG_CH2SET]    = 0x00,
        [ADS_REG_RLD_SENS]  = 0x00,
        [ADS_REG_LOFF_SENS] = 0x00,
        [ADS_REG_LOFF_STAT] = 0x00,
        [ADS_REG_RESP1]     = 0x02,
        [ADS_REG_RESP2]     = 0x01,
        [ADS_REG_GPIO]      = 0x0C,
    };
    memcpy(c->regs, por, sizeof(por));
    c->converting = false;
    c->standby    = false;
    c->rdatac     = false;
    c->st         = ST_CMD;
    c->fb         = 0;
    c->seq        = 0;
    c->drdy_low   = false;
    vboard_gpio_set(ADS_DRDY_GPIO, 1);   /* R99 pull-up: idle high */
}

/* Powered iff the analog rail is up AND the expander PWDN pin is high.
 * Checked on every bus touch and every tick; a low->high edge re-PORs
 * the part (power loss drops all registers). -1 (expander model absent)
 * is treated as NOT powered — the part must not answer before P1 is
 * driven, that is the behavioural contract the driver relies on.      */
static void ads_power_sync(ads_ctx_t *c)
{
    bool p = vboard_rail(VB_RAIL_3V3_ANA) && (vboard_exp_pin(ADS_EXP_PWDN) == 1);
    if (p && !c->powered) ads_por(c);
    c->powered = p;
}

/* Sample period from CONFIG1.DR[2:0] (512 kHz internal osc, low-power
 * column of the rate table); 111 reserved -> treat as 500 sps.        */
static int64_t ads_period_us(const ads_ctx_t *c)
{
    static const uint16_t sps[8] = { 125, 250, 500, 1000, 2000, 4000, 8000, 500 };
    return 1000000 / (int64_t)sps[c->regs[ADS_REG_CONFIG1] & 0x07];
}

/* Dynamic LOFF_STAT: the scenario's leadoff flag lifts the CH1 electrode
 * pair (bits[1:0] = IN1N/IN1P off — the pair the driver senses on).    */
static uint8_t ads_loff_stat(const ads_ctx_t *c)
{
    (void)c;
    return s_bio.leadoff ? 0x03 : 0x00;
}

static uint8_t ads_reg_read(const ads_ctx_t *c, uint8_t addr)
{
    if (addr == ADS_REG_LOFF_STAT) return ads_loff_stat(c);
    return c->regs[addr % ADS_NREGS];
}

static void ads_reg_write(ads_ctx_t *c, uint8_t addr, uint8_t val)
{
    /* ID and LOFF_STAT are read-only on the real part. */
    if (addr == ADS_REG_ID || addr == ADS_REG_LOFF_STAT) return;
    c->regs[addr % ADS_NREGS] = val;
}

/* One 24-bit channel word in counts, per CHnSET mux selection. */
static int32_t ads_ch_counts(uint8_t chset, int ch, int64_t t_us)
{
    const double lsb_uv = (ch == 1) ? ADS_LSB_UV_CH1 : ADS_LSB_UV_CH2;
    double uv;
    switch (chset & 0x0F) {
    case ADS_MUX_TEST:
        /* Internal test signal: ~1 mV square, ~10 Hz. */
        uv = ((t_us / 50000) & 1) ? ADS_TEST_AMP_UV : -ADS_TEST_AMP_UV;
        break;
    case ADS_MUX_SHORT:
        uv = 0.0;                       /* input shorted — offset only   */
        break;
    case ADS_MUX_NORMAL:
    default:
        /* ch2 PD|short is how the driver routes the resp demodulator
         * onto this slot (SBAS502 respiration section) — PD does not
         * blank the channel here. */
        uv = (ch == 1) ? (double)vbbio_ecg(t_us) : (double)vbbio_resp(t_us);
        break;
    }
    double counts = uv / lsb_uv + (vbb_noise(t_us, 10 + ch) - 0.5) * 6.0;
    if (counts >  8388607.0) counts =  8388607.0;
    if (counts < -8388608.0) counts = -8388608.0;
    return (int32_t)counts;
}

/* Snapshot the current conversion into the 9-byte frame buffer:
 * {0xC<<20 | LOFF_STAT<<15 | GPIO[1:0]<<13}, ch1, ch2 — MSB first.     */
static void ads_snapshot(ads_ctx_t *c)
{
    int64_t  t    = vboard_now_us();
    uint32_t stat = (0xCu << 20) | ((uint32_t)ads_loff_stat(c) << 15);
    int32_t  v1   = ads_ch_counts(c->regs[ADS_REG_CH1SET], 1, t);
    int32_t  v2   = ads_ch_counts(c->regs[ADS_REG_CH2SET], 2, t);
    uint8_t *f    = c->frame;
    f[0] = (uint8_t)(stat >> 16); f[1] = (uint8_t)(stat >> 8); f[2] = (uint8_t)stat;
    f[3] = (uint8_t)(v1 >> 16);   f[4] = (uint8_t)(v1 >> 8);   f[5] = (uint8_t)v1;
    f[6] = (uint8_t)(v2 >> 16);   f[7] = (uint8_t)(v2 >> 8);   f[8] = (uint8_t)v2;
    c->fb = 0;
    c->seq++;
}

/* ------------------------------ SPI engine ----------------------------- */
/* One DIN byte -> one DOUT byte, with the command FSM. */
static uint8_t ads_step(ads_ctx_t *c, uint8_t in)
{
    if (c->rdatac) {
        /* RDATAC: data shifts out every clock while the single-byte
         * SYSTEM opcodes keep decoding on DIN — the driver sends
         * RDATAC then START and expects conversion to begin; only the
         * register commands (RREG/WREG/RDATA) are locked out until
         * SDATAC.                                                       */
        uint8_t out = c->frame[c->fb++];
        if (c->fb >= ADS_FRAME_LEN)
            ads_snapshot(c);        /* next 9-byte window -> new sample */
        switch (in) {
        case ADS_OP_SDATAC:  c->rdatac = false;                                break;
        case ADS_OP_STOP:    c->converting = false;                            break;
        case ADS_OP_START:
            c->converting   = true;
            c->next_drdy_us = vboard_now_us() + ads_period_us(c);
            break;
        case ADS_OP_WAKEUP:  c->standby = false;                               break;
        case ADS_OP_STANDBY: c->standby = true; c->converting = false;         break;
        case ADS_OP_RESET:   ads_por(c);                                       break;
        default:                                                               break;
        }
        return out;
    }

    switch (c->st) {
    case ST_RREG_N:
        c->n_regs = (uint8_t)((in & 0x1Fu) + 1u);
        c->st = ST_RREG_D;
        return 0xFF;
    case ST_RREG_D: {
        uint8_t out = ads_reg_read(c, c->reg_addr);
        c->reg_addr = (uint8_t)((c->reg_addr + 1u) % ADS_NREGS);
        if (--c->n_regs == 0 || c->n_regs > 32) c->st = ST_CMD;
        return out;
    }
    case ST_WREG_N:
        c->n_regs = (uint8_t)((in & 0x1Fu) + 1u);
        c->st = ST_WREG_D;
        return 0xFF;
    case ST_WREG_D:
        ads_reg_write(c, c->reg_addr, in);
        c->reg_addr = (uint8_t)((c->reg_addr + 1u) % ADS_NREGS);
        if (--c->n_regs == 0 || c->n_regs > 32) c->st = ST_CMD;
        return 0xFF;
    case ST_RDATA: {
        uint8_t out = c->frame[c->fb++];
        if (c->fb >= ADS_FRAME_LEN) { c->fb = 0; c->st = ST_CMD; }
        return out;
    }
    default:
        break;
    }

    /* ST_CMD — opcode decode. */
    if ((in & 0xE0u) == 0x20u) {          /* RREG */
        c->reg_addr = in & 0x1Fu;
        c->st = ST_RREG_N;
        return 0xFF;
    }
    if ((in & 0xE0u) == 0x40u) {          /* WREG */
        c->reg_addr = in & 0x1Fu;
        c->st = ST_WREG_N;
        return 0xFF;
    }
    switch (in) {
    case ADS_OP_WAKEUP:  c->standby = false;                          break;
    case ADS_OP_STANDBY: c->standby = true; c->converting = false;    break;
    case ADS_OP_START:
        c->converting   = true;
        c->next_drdy_us = vboard_now_us() + ads_period_us(c);
        break;
    case ADS_OP_STOP:    c->converting = false;                       break;
    case ADS_OP_RESET:   ads_por(c);                                  break;
    case ADS_OP_RDATAC:  c->rdatac = true; ads_snapshot(c);           break;
    case ADS_OP_SDATAC:                                               break;
    case ADS_OP_RDATA:   ads_snapshot(c); c->st = ST_RDATA;           break;
    default:               /* 0x00 NOP and friends */                 break;
    }
    return 0xFF;
}

static esp_err_t ads_spi_xfer(vboard_dev_t *d, const uint8_t *tx,
                              uint8_t *rx, size_t len)
{
    ads_ctx_t *c = (ads_ctx_t *)d->ctx;
    ads_power_sync(c);
    for (size_t i = 0; i < len; i++) {
        uint8_t in  = tx ? tx[i] : 0;
        uint8_t out = 0xFF;
        if (c->powered) out = ads_step(c, in);   /* off -> MISO floats hi */
        if (rx) rx[i] = out;
    }
    return VB_ESP_OK;
}

/* CS edges (firmware output on IO10): rising = transaction over — abort
 * any half-finished command like the real decode. Falling while RDATAC
 * arms a fresh 9-byte snapshot for this CS window.                    */
static void ads_cs_cb(int gpio, int level, void *ctx)
{
    ads_ctx_t *c = (ads_ctx_t *)ctx;
    (void)gpio;
    if (level) {
        c->st = ST_CMD;
        c->fb = 0;
    } else if (c->rdatac) {
        ads_snapshot(c);
    }
}

/* ------------------------------ tick / reset --------------------------- */
static void ads_tick(vboard_dev_t *d, int64_t now_us)
{
    ads_ctx_t *c = (ads_ctx_t *)d->ctx;
    ads_power_sync(c);

    if (c->drdy_low && now_us >= c->drdy_release_us)
        ads_drdy_release(c);
    if (!c->powered || !c->converting || c->standby)
        return;

    const int64_t per = ads_period_us(c);
    int n = 0;
    while (now_us >= c->next_drdy_us && n < 4) {
        vboard_gpio_set(ADS_DRDY_GPIO, 0);       /* ~DRDY low            */
        c->drdy_low        = true;
        c->drdy_release_us = c->next_drdy_us + ADS_DRDY_LOW_US;
        vboard_irq_pulse(ADS_DRDY_GPIO, 0);      /* NEGEDGE to firmware  */
        c->next_drdy_us += per;
        n++;
    }
    if (n == 4) c->next_drdy_us = now_us + per;   /* shed sim backlog    */
}

static void ads_dev_reset(vboard_dev_t *d)
{
    ads_ctx_t *c = (ads_ctx_t *)d->ctx;
    ads_por(c);
    c->powered = false;    /* PWDN re-read on next touch; expander resets */
}

/* ------------------------------ registration --------------------------- */
static vboard_dev_t s_ads_dev = {
    .name      = "ads1292r",
    .i2c_addr  = 0,
    .spi_cs    = ADS_CS_GPIO,
    .spi_xfer  = ads_spi_xfer,
    .tick      = ads_tick,
    .reset     = ads_dev_reset,
    .ctx       = &s_ads,
};

/* Registration entry point — vboard_init() weak-calls this when wiring
 * the default device population; vboard_register() dedups by name so
 * re-registration is safe.                                            */
void vbdev_ads1292_register(void)
{
    static bool done;
    if (done) return;
    done = true;
    vboard_gpio_set(ADS_DRDY_GPIO, 1);   /* R99 pull-up: idle high */
    vboard_on_gpio_out(ADS_CS_GPIO, ads_cs_cb, &s_ads);
    vboard_register(&s_ads_dev);
}

/* Self-register at load so the default population exists even before
 * vboard_init() wires anything explicitly (idempotent via `done`).    */
#if defined(__GNUC__)
__attribute__((constructor))
static void ads_ctor(void)
{
    vbdev_ads1292_register();
}
#endif
