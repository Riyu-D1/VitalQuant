/* fsr_src.c — vboard device model for the FSR402 contact sensor + R8
 * divider.  Off-bus (passive analog front end): the registered
 * vboard_dev exists so reset() restores power-on state and the model
 * shows up in the board population; the data path is
 * vbdev_fsr_raw_counts(), consumed by esp_shim's adc_sim.c for
 * ADC1_CH0 / IO1.
 *
 * Resistance model: polymer-thick-film FSRs drop roughly log-linearly
 * with force, so level01k (0..1000) interpolates R between REST_R (pad
 * open) and FULL_PRESS_R:
 *   counts = 4095 * R8 / (R8 + R_fsr)          (12-bit, ~3.3 V ref)
 * giving rest ~10 counts and full press ~4076 — matching the driver's
 * documented plateaus (~0 / ~370 light / ~2050 seated / ~3720 press).
 * A ±3 LSB dither emulates conversion noise.
 */
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "vboard.h"
#include "fsr_src.h"

#define FSR_R8_OHM        10000.0f    /* divider pull-down              */
#define FSR_REST_R_OHM    4.0e6f      /* pad open — FSR ~MOhm           */
#define FSR_PRESS_R_OHM   47.0f       /* hardest press — node ~3.28 V   */
#define FSR_ADC_FULLSCALE 4095        /* 12-bit counts                  */
#define FSR_NOISE_LSB     3           /* conversion noise half-span     */

typedef struct {
    int      level01k;   /* 0..1000                                    */
    uint32_t rng;
} fsr_src_t;

static fsr_src_t    s_fsr;
static vboard_dev_t s_fsr_dev;
static bool         s_fsr_registered;

static uint32_t fsr_rng_next(fsr_src_t *f)
{
    uint32_t x = f->rng;               /* xorshift32                    */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    f->rng = x;
    return x;
}

/* FSR resistance for the current pressure level — log-linear between
 * the rest and full-press endpoints (conductance ~ exponential in the
 * applied force for this class of sensor). */
static float fsr_resistance_ohm(int level01k)
{
    if (level01k <= 0)    return FSR_REST_R_OHM;
    if (level01k >= 1000) return FSR_PRESS_R_OHM;
    const float lo = log10f(FSR_REST_R_OHM);
    const float hi = log10f(FSR_PRESS_R_OHM);
    const float t  = (float)level01k / 1000.0f;
    return powf(10.0f, lo + t * (hi - lo));
}

void vbdev_fsr_set_pressure(int level01k)
{
    if (level01k < 0)    level01k = 0;
    if (level01k > 1000) level01k = 1000;
    s_fsr.level01k = level01k;
}

int vbdev_fsr_raw_counts(void)
{
    /* Divider top is the +3V3 rail — rail down means no excitation. */
    if (!vboard_rail(VB_RAIL_3V3)) {
        return 0;
    }
    float r      = fsr_resistance_ohm(s_fsr.level01k);
    float counts = (float)FSR_ADC_FULLSCALE * FSR_R8_OHM / (FSR_R8_OHM + r);
    int    noise = (int)(fsr_rng_next(&s_fsr) % (2u * FSR_NOISE_LSB + 1u))
                   - FSR_NOISE_LSB;
    int    raw   = (int)lrintf(counts) + noise;
    if (raw < 0)                  raw = 0;
    if (raw > FSR_ADC_FULLSCALE)  raw = FSR_ADC_FULLSCALE;
    return raw;
}

static void fsr_src_reset(vboard_dev_t *d)
{
    (void)d;
    s_fsr.level01k = 0;
    s_fsr.rng      = 0xF5A402u;         /* deterministic reseed          */
}

void vbdev_fsr_register(void)
{
    if (s_fsr_registered) {
        return;
    }
    s_fsr_dev.name      = "fsr402";
    s_fsr_dev.i2c_addr  = 0;            /* not on I2C                    */
    s_fsr_dev.i2c_write = NULL;
    s_fsr_dev.i2c_read  = NULL;
    s_fsr_dev.spi_cs    = -1;           /* not on SPI                    */
    s_fsr_dev.spi_xfer  = NULL;
    s_fsr_dev.tick      = NULL;         /* sampled on demand only        */
    s_fsr_dev.reset     = fsr_src_reset;
    s_fsr_dev.ctx       = &s_fsr;
    vboard_register(&s_fsr_dev);
    fsr_src_reset(&s_fsr_dev);
    s_fsr_registered = true;
}

/* Auto-populate the virtual board when this TU is linked (see
 * mic_src.c — idempotent either way). */
__attribute__((constructor)) static void fsr_src_autoreg(void)
{
    vbdev_fsr_register();
}
