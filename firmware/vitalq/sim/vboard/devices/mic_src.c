/* mic_src.c — vboard device model for the IM69D130 PDM MEMS mic (U25).
 *
 * Sits off-bus: no I2C address, no SPI CS — the part hangs on the I2S0
 * PDM RX line (CLK=IO40 out, DAT=IO41 in, SELECT=GND -> left slot).
 * The registered vboard_dev exists so reset() restores power-on state
 * and the model shows up in the board population; the actual data path
 * is vbdev_mic_next_sample(), consumed by esp_shim's i2s_sim.c.
 *
 * Data model (register-plausible, not silicon-accurate):
 *   pcm[n] = DC_OFFSET + tone(hz, amp) + dither(±4 LSB)
 * - a real IM69D130 leaves a DC offset in the PCM (no HW high-pass on
 *   S3 PDM RX — the firmware's selftest prints it as `dc`), so the model
 *   carries a fixed +850 LSB offset.
 * - the ±4 LSB uniform dither is the sigma-delta self-noise that keeps
 *   peak-peak > 2 — the driver's flat-stream absence check.
 * - sleeping/powered-down -> constant 0 (flat stream == absent/dead).
 */
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "vboard.h"
#include "mic_src.h"

/* PCM rate the firmware configures (VQ_MIC_PCM_RATE_HZ).  The model
 * advances the tone phase per next_sample() call against this rate. */
#define MIC_MODEL_RATE_HZ   16000.0f
#define MIC_DC_OFFSET       850       /* IM69D130 DC in PCM, LSB        */
#define MIC_DITHER_LSB      4         /* self-noise half-span, LSB      */
#define MIC_2PI             6.283185307179586f

typedef struct {
    float    tone_hz;
    float    tone_amp;     /* peak LSB                                 */
    uint32_t phase_q16;    /* tone phase, 16.16 fixed-point of a cycle */
    uint32_t phase_step;   /* q16 increment per PCM sample             */
    bool     sleeping;
    uint32_t rng;
} mic_src_t;

static mic_src_t   s_mic;
static vboard_dev_t s_mic_dev;
static bool         s_mic_registered;

static uint32_t mic_rng_next(mic_src_t *m)
{
    /* xorshift32 — deterministic cheap dither */
    uint32_t x = m->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->rng = x;
    return x;
}

static void mic_recompute_step(mic_src_t *m)
{
    /* phase_step = hz * 2^16 / rate, in q16 of a cycle per sample */
    double st = (double)m->tone_hz * 65536.0 / (double)MIC_MODEL_RATE_HZ;
    if (st < 0.0) st = 0.0;
    m->phase_step = (uint32_t)(st + 0.5);
}

void vbdev_mic_set_tone(float hz, float amp)
{
    if (hz < 0.0f)  hz = 0.0f;
    if (amp < 0.0f) amp = 0.0f;
    if (amp > 32767.0f) amp = 32767.0f;
    s_mic.tone_hz  = hz;
    s_mic.tone_amp = amp;
    mic_recompute_step(&s_mic);
}

void vbdev_mic_set_sleep(bool sleeping)
{
    s_mic.sleeping = sleeping;
}

int16_t vbdev_mic_next_sample(void)
{
    if (s_mic.sleeping) {
        return 0;                       /* powered-down: flat stream   */
    }
    int32_t dither = (int32_t)(mic_rng_next(&s_mic) % (2u * MIC_DITHER_LSB + 1u))
                     - MIC_DITHER_LSB;
    float tone = 0.0f;
    if (s_mic.tone_amp > 0.0f && s_mic.phase_step != 0) {
        tone = s_mic.tone_amp * sinf(MIC_2PI * (float)s_mic.phase_q16 / 65536.0f);
    }
    s_mic.phase_q16 += s_mic.phase_step;    /* wraps naturally in q16   */

    int32_t v = MIC_DC_OFFSET + dither + (int32_t)lrintf(tone);
    if (v > INT16_MAX)  v = INT16_MAX;
    if (v < INT16_MIN)  v = INT16_MIN;
    return (int16_t)v;
}

static void mic_src_reset(vboard_dev_t *d)
{
    (void)d;
    s_mic.tone_hz    = 0.0f;
    s_mic.tone_amp   = 0.0f;
    s_mic.phase_q16  = 0;
    s_mic.phase_step = 0;
    s_mic.sleeping   = false;
    s_mic.rng        = 0x0169D130u;      /* deterministic reseed        */
}

void vbdev_mic_register(void)
{
    if (s_mic_registered) {
        return;
    }
    s_mic_dev.name      = "im69d130";
    s_mic_dev.i2c_addr  = 0;             /* not on I2C                  */
    s_mic_dev.i2c_write = NULL;
    s_mic_dev.i2c_read  = NULL;
    s_mic_dev.spi_cs    = -1;            /* not on SPI                  */
    s_mic_dev.spi_xfer  = NULL;
    s_mic_dev.tick      = NULL;          /* sampled on demand only      */
    s_mic_dev.reset     = mic_src_reset;
    s_mic_dev.ctx       = &s_mic;
    vboard_register(&s_mic_dev);
    mic_src_reset(&s_mic_dev);
    s_mic_registered = true;
}

/* Auto-populate the virtual board when this TU is linked.  Explicit
 * vbdev_mic_register() calls from a board-init are equally legal —
 * the function is idempotent. */
__attribute__((constructor)) static void mic_src_autoreg(void)
{
    vbdev_mic_register();
}
