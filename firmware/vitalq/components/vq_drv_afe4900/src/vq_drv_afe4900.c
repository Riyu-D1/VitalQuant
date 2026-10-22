/* vq_drv_afe4900.c — AFE4900YZR (U6) PPG AFE driver, fix_v22 / hw_v3.
 *
 * Duty: main PPG — SFH7072 optical module, channels ppg.red (660 nm)
 * and ppg.ir (940 nm) at the 200 Hz profile rate. Engineering perfusion
 * proxy only; NO SpO2 / clinical claims anywhere.
 *
 * Board truth (pin_audit_r6/U6, pin_audit/U16, board_pins.h):
 *   - SPI mode 0, <=8 MHz. SEN=CS_AFE4900(IO18), SDOUT->R90->IO13.
 *     MOSI/SCK share the part's I2C_DAT/I2C_CLK balls.
 *   - OPEN QUESTION: I2C_SPI_SEL is tied to +3V3_ANA; pin audit records
 *     "1=SPI, 0=I2C" so the part should answer SPI. If a schematic re-spin
 *     shows the opposite polarity the part is really in I2C mode and all
 *     SPI traffic is a no-op -> set VQ_AFE4900_SPI_ENABLE to 0 and the
 *     whole driver degrades to a graceful "absent" probe.
 *   - ADC_RDY = IO21, active-high pulse; edge timestamped in ISR.
 *   - RESETZ = TCA6408 P0, R9 pulls low -> held in reset until driven.
 *   - CLK ball strapped to GND -> internal 128 kHz oscillator only.
 *   - TX_SUP = TX_5V (TPS61240 boost, TCA6408 P3) -> owned by led_sched.
 *   - SFH7072 cathodes: TX1=G1(530), TX2=G2(530), TX3=R(660), TX4=IR(940).
 *     PD1 on INP/INM (ppg.red), PD2 on INP2/INM2 (ppg.ir) per profile.
 *
 * LED POLICY: every microsecond of LED on-time is authorized through
 * vq_led_submit() on the VQ_LED_AFE4900 chain (~200 mA class). The AFE's
 * internal timing engine fires the actual pulses, but ONLY while a
 * scheduler reservation is active — see afe_led_* callbacks and the
 * grant watchdog in the drain task. We never touch TX5_EN ourselves.
 *
 * Uncertainties (all marked TODO(datasheet) inline): phase->TX binding,
 * LEDCNTRL field layout, FIFO ctrl/count regs, CONTROL0/2 bit decodes,
 * ID/version reg existence. See afe4900_regs.h for the map.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "vq_types.h"
#include "vq_sensor.h"
#include "board_pins.h"
#include "spi_bus.h"
#include "expander.h"
#include "led_sched.h"

#include "vq_drv_afe4900.h"
#include "afe4900_regs.h"

static const char *TAG = "afe4900";

/* =================== Build-time policy switches ====================== */

/* VQ_AFE4900_SPI_ENABLE — the I2C_SPI_SEL escape hatch described above.
 * 1 = part answers SPI (expected on fix_v22). 0 = probe is a no-op that
 * reports ESP_ERR_NOT_FOUND so the pipeline simply marks it absent.    */
#ifndef VQ_AFE4900_SPI_ENABLE
#define VQ_AFE4900_SPI_ENABLE        1
#endif

/* Phase->TX binding — the #1 open datasheet question.
 *   1 (default, rigid): fire window LEDk sinks TXk — the binding the
 *      public AFE4900EVM/CCS register dumps imply. On our board that
 *      puts red on the LED3 fire window (TX3) and IR on LED4 (TX4);
 *      their samples land in the shared ALED2_3 / LED1_4 windows
 *      (the "LED3-ish" style shared-window scheme from the task brief).
 *   0 (remap): a phase->TX map register exists — the task-literal plan:
 *      LED2 fire window -> TX3 = red (sample = dedicated LED2 window),
 *      LED3 fire window -> TX4 = IR (sample = shared ALED2_3 window).
 *      Writes AFE4900_REG_TX_SEL in init.
 * Both plans produce FIFO order {amb_red, red, amb_ir, ir} per frame.  */
#ifndef AFE4900_TXK_RIGID
#define AFE4900_TXK_RIGID            1
#endif

/* Use the on-chip 128-word FIFO (task contract). 0 falls back to direct
 * result-register reads per ADC_RDY edge — kept because the FIFO
 * ctrl/count registers are the least-confirmed part of the map.        */
#ifndef AFE4900_USE_FIFO
#define AFE4900_USE_FIFO             1
#endif

/* =================== Rate / timing constants ========================= */
/* Internal 128 kHz osc. PRF must be >=500 Hz inside the LED budget and
 * the profile output rate is 200 Hz: PRF=800 Hz (PRPCT=159), NUMAV=3
 * (4-fold decimation) -> exactly 200.0 Hz frames.                       */
#define AFE_OUT_RATE_HZ        200.0f
#define AFE_PRF_HZ             800u
#define AFE_NUMAV              3u
#define AFE_PRP_COUNT          ((AFE4900_OSC_HZ / AFE_PRF_HZ) - 1u) /* 159 */

/* Timing window widths in 128 kHz counts (1 count = 7.8125 us).
 * TODO(datasheet): confirm min conversion time / ADC-reset width vs
 * SBAS857; values here follow the public EVM table shape.              */
#define AFE_PHASES             4u
#define AFE_PHASE_PITCH        ((AFE_PRP_COUNT + 1u) / AFE_PHASES)  /* 40 */
#define AFE_SM_OFF             4u     /* sample start within slot        */
#define AFE_SM_W               9u     /* sample width ~70 us             */
#define AFE_FIRE_OFF           1u     /* fire leads sample ~23 us        */
#define AFE_RST_OFF            15u    /* ADC reset start                 */
#define AFE_RST_W              5u     /* ~39 us                          */
#define AFE_CONV_OFF           21u    /* conversion start                */
#define AFE_CONV_W             18u    /* ~141 us TODO(datasheet)         */

/* LED budget bookkeeping: 2 driven fire windows, ~94 us each per PRP
 * -> ~188 us LED-on per 1250 us period (~15%). Declared worst-case.    */
#define AFE_LED_ON_US_DECLARE  200u
#define AFE_PRP_US             (1000000u / AFE_PRF_HZ)              /* 1250 */
#define AFE_LED_CLASS_MA       200u   /* ~200 mA class per hw brief       */

/* LED current codes (8-bit; ILED_FS assumed 50 mA -> code ~= mA*255/50).
 * TODO(datasheet): confirm FS + field packing in afe4900_regs.h.        */
#define AFE_LED_CODE_RUN       0x1Au  /* ~5 mA default — conservative    */
#define AFE_LED_CODE_SELFTEST  0x06u  /* ~1.2 mA "minimum-current" pulse */

/* Self-test PD response floor (ADC counts delta over ambient).         */
#define AFE_ST_PD_MIN_DELTA    300

/* FIFO word indices inside one output frame (slot order).              */
#define AFE_FIDX_AMB_RED       0u
#define AFE_FIDX_RED           1u
#define AFE_FIDX_AMB_IR        2u
#define AFE_FIDX_IR            3u
#define AFE_FIFO_WORDS_FRAME   AFE_PHASES
#define AFE_FIFO_MAX_WORDS     64u    /* drain cap per edge              */

#define AFE_TASK_STACK         4096u
#define AFE_TASK_PRIO          8u
#define AFE_RESET_RELEASE_MS   5u     /* >1 ms before commands (DS §10)  */
#define AFE_HEALTH_ERR_MAX     16u
#define AFE_FRAME_US           (1000000u / (uint32_t)AFE_OUT_RATE_HZ)
#define AFE_GRANT_WAIT_US      200000u /* led grant wait at start       */

/* ======================= Types ======================================= */

typedef struct { uint8_t reg; uint32_t val; } afe_regval_t;

typedef struct {
    /* Timing slot i base = i * AFE_PHASE_PITCH. */
    uint8_t sm_st, sm_end;       /* sample window regs                  */
    uint8_t fire_st, fire_end;   /* LED fire window regs; 0,0 = dark    */
    uint8_t conv_st, conv_end;   /* ADC conversion window regs          */
    uint8_t rst_st,  rst_end;    /* ADC reset window regs               */
    const char *tag;             /* debug name                          */
} afe_phase_t;

typedef struct {
    void               *spi;          /* vq_spi handle (normal clock)    */
    SemaphoreHandle_t   rdy_sem;
    TaskHandle_t        task;
    vq_ts_anchor_t      anchor;
    vq_sensor_t        *sen;

    uint32_t            ctrl0_shadow; /* last CONTROL0 written           */
    uint32_t            ctrl1_shadow;
    bool                spi_rd_mode;  /* CONTROL0.SPI_READ currently set */

    bool                present;
    bool                inited;
    bool                streaming;
    bool                engine_on;
    bool                isr_installed;

    volatile bool       led_grant;    /* a led_sched slot is open        */
    volatile int64_t    led_gate_us;  /* grant expiry (watchdog)         */
    int64_t             last_resub_us;/* watchdog re-submit throttle     */
    volatile bool       st_armed;     /* selftest slot currently open    */
    volatile bool       st_slot_lost; /* slot closed while armed         */

    volatile int64_t    last_edge_us;
    int32_t             frame_seq;
    uint32_t            drain_err;
    uint32_t            fifo_partials;
} afe_priv_t;

/* ======================= Phase plan ==================================== */
/* Four slots per PRP; FIFO emits them in this order every output frame.
 * RIGID plan (default): red fire = LED3 window (TX3=R_C), ir fire = LED4
 *   window (TX4=IR_C); their samples ride the shared ALED2_3 / LED1_4
 *   windows; LED1 & LED2 fire windows stay dark.
 * REMAP plan (task-literal "LED2(red)+LED3-ish(IR)"): red fire = LED2
 *   window remapped to TX3 (sample = dedicated LED2 window); ir fire =
 *   LED3 window remapped to TX4 (sample = shared ALED2_3 "LED3-ish"
 *   window); LED1 & LED4 fire windows dark; AFE4900_REG_TX_SEL written
 *   in init. Flip AFE4900_TXK_RIGID to swap plans.                      */
#if AFE4900_TXK_RIGID
static const afe_phase_t s_phases[AFE_PHASES] = {
    /* slot0 — ambient for the red channel (ALED1 window, dark) */
    { AFE4900_REG_ALED1STC, AFE4900_REG_ALED1ENDC, 0, 0,
      AFE4900_REG_ALED1CONVST, AFE4900_REG_ALED1CONVEND,
      AFE4900_REG_ADCRSTST0, AFE4900_REG_ADCRSTEND0, "amb_red" },
    /* slot1 — RED sample (shared ALED2_3 window) + LED3 fire -> TX3 */
    { AFE4900_REG_ALED2STC, AFE4900_REG_ALED2ENDC,
      AFE4900_REG_LED3LEDSTC, AFE4900_REG_LED3LEDENDC,
      AFE4900_REG_ALED2CONVST, AFE4900_REG_ALED2CONVEND,
      AFE4900_REG_ADCRSTST1, AFE4900_REG_ADCRSTEND1, "red" },
    /* slot2 — ambient for the IR channel (LED2 window, dark) */
    { AFE4900_REG_LED2STC, AFE4900_REG_LED2ENDC, 0, 0,
      AFE4900_REG_LED2CONVST, AFE4900_REG_LED2CONVEND,
      AFE4900_REG_ADCRSTST2, AFE4900_REG_ADCRSTEND2, "amb_ir" },
    /* slot3 — IR sample (shared LED1_4 window) + LED4 fire -> TX4 */
    { AFE4900_REG_LED1STC, AFE4900_REG_LED1ENDC,
      AFE4900_REG_LED4LEDSTC, AFE4900_REG_LED4LEDENDC,
      AFE4900_REG_LED1CONVST, AFE4900_REG_LED1CONVEND,
      AFE4900_REG_ADCRSTST3, AFE4900_REG_ADCRSTEND3, "ir" },
};
/* VAL register per slot (same index order as s_phases) */
static const uint8_t s_slot_val[AFE_PHASES] = {
    AFE4900_REG_ALED1VAL, AFE4900_REG_ALED2VAL,
    AFE4900_REG_LED2VAL,  AFE4900_REG_LED1VAL };
#else /* remap plan */
static const afe_phase_t s_phases[AFE_PHASES] = {
    /* slot0 — ambient for red (LED1 window, dark) */
    { AFE4900_REG_LED1STC, AFE4900_REG_LED1ENDC, 0, 0,
      AFE4900_REG_LED1CONVST, AFE4900_REG_LED1CONVEND,
      AFE4900_REG_ADCRSTST0, AFE4900_REG_ADCRSTEND0, "amb_red" },
    /* slot1 — RED sample (LED2 window) + LED2 fire -> TX3 via TX_SEL */
    { AFE4900_REG_LED2STC, AFE4900_REG_LED2ENDC,
      AFE4900_REG_LED2LEDSTC, AFE4900_REG_LED2LEDENDC,
      AFE4900_REG_LED2CONVST, AFE4900_REG_LED2CONVEND,
      AFE4900_REG_ADCRSTST1, AFE4900_REG_ADCRSTEND1, "red" },
    /* slot2 — ambient for IR (ALED1 window, dark) */
    { AFE4900_REG_ALED1STC, AFE4900_REG_ALED1ENDC, 0, 0,
      AFE4900_REG_ALED1CONVST, AFE4900_REG_ALED1CONVEND,
      AFE4900_REG_ADCRSTST2, AFE4900_REG_ADCRSTEND2, "amb_ir" },
    /* slot3 — IR sample (shared ALED2_3 "LED3-ish" window) + LED3
     *        fire -> TX4 via TX_SEL */
    { AFE4900_REG_ALED2STC, AFE4900_REG_ALED2ENDC,
      AFE4900_REG_LED3LEDSTC, AFE4900_REG_LED3LEDENDC,
      AFE4900_REG_ALED2CONVST, AFE4900_REG_ALED2CONVEND,
      AFE4900_REG_ADCRSTST3, AFE4900_REG_ADCRSTEND3, "ir" },
};
static const uint8_t s_slot_val[AFE_PHASES] = {
    AFE4900_REG_LED1VAL, AFE4900_REG_LED2VAL,
    AFE4900_REG_ALED1VAL, AFE4900_REG_ALED2VAL };
/* TODO(datasheet): TX_SEL field encoding — placeholder; verify against
 * SBAS857 before enabling the remap plan.                              */
#define AFE4900_TXSEL_VALUE   0x000043u
#endif

/* Fire windows not driven by our plan — disabled with the family
 * "start > end" idiom (st=1, end=0 -> never fires), as seen in the EVM
 * dumps (LED1LEDSTC=1, LED1LEDENDC=0).                                */
#if AFE4900_TXK_RIGID
static const afe_regval_t s_fire_disable[] = {
    { AFE4900_REG_LED1LEDSTC, 1 }, { AFE4900_REG_LED1LEDENDC, 0 },
    { AFE4900_REG_LED2LEDSTC, 1 }, { AFE4900_REG_LED2LEDENDC, 0 },
};
#else
static const afe_regval_t s_fire_disable[] = {
    { AFE4900_REG_LED1LEDSTC, 1 }, { AFE4900_REG_LED1LEDENDC, 0 },
    { AFE4900_REG_LED4LEDSTC, 1 }, { AFE4900_REG_LED4LEDENDC, 0 },
};
#endif

#if AFE4900_USE_FIFO
#define AFE_INIT_C0  (AFE4900_C0_ULP_EN | AFE4900_C0_FIFO_EN)
#else
#define AFE_INIT_C0  (AFE4900_C0_ULP_EN)
#endif

/* Static init table — applied before the computed timing windows.      */
static const afe_regval_t s_init_table[] = {
    { AFE4900_REG_CONTROL0,   AFE_INIT_C0 },
    { AFE4900_REG_CONTROL2,   AFE4900_C2_INT_OSC_FS50 }, /* int 128k osc */
    { AFE4900_REG_CLKDIV1,    0x000000u },
    { AFE4900_REG_CLKDIV2,    0x000000u },
    { AFE4900_REG_TOGGLE,     0x000000u },
    { AFE4900_REG_TIAGAIN_2_3, 0x000000u },   /* PD2/3 TIA — TODO gain  */
    { AFE4900_REG_TIAGAIN,    AFE4900_TIA_RF_500K },     /* PD1 TIA      */
    { AFE4900_REG_TIA_AMB_GAIN, AFE4900_TIA_RF_500K },   /* ambient path */
    /* LED currents: TODO(datasheet) field packing — codes land at named
     * shifts; driven phases get AFE_LED_CODE_RUN, others 0.             */
    { AFE4900_REG_LEDCNTRL1,
        ((uint32_t)AFE_LED_CODE_RUN << AFE4900_LEDC1_LED2_SHIFT) |
        ((uint32_t)AFE_LED_CODE_RUN << AFE4900_LEDC1_LED3_SHIFT) },
    { AFE4900_REG_LEDCNTRL2,
        ((uint32_t)AFE_LED_CODE_RUN << AFE4900_LEDC2_LED4_SHIFT) },
    { AFE4900_REG_OFFDAC,     0x100000u },    /* EVM default; TODO(ds)   */
    { AFE4900_REG_I_OFFDAC,   0x000000u },
    { AFE4900_REG_CONTROL3,   AFE4900_C3_EVM_DEFAULT },
    { AFE4900_REG_THRDETLOW,  0x000000u },
    { AFE4900_REG_THRDETHIGH, 0x000000u },
    { AFE4900_REG_THRDET,     0x000000u },
#if !AFE4900_TXK_RIGID
    { AFE4900_REG_TX_SEL,     AFE4900_TXSEL_VALUE },     /* TODO(datasheet) */
#endif
#if AFE4900_USE_FIFO
    { AFE4900_REG_FIFO_CTRL,  0x000000u },    /* TODO(datasheet)         */
    { AFE4900_REG_FIFO_PHASE, 0x00000Fu },    /* TODO(datasheet) all 4   */
#endif
};

/* ======================= Static state ================================ */
static afe_priv_t   s_priv;
static vq_sensor_t  s_sensor;
static bool         s_registered;

/* forward decls */
static void     afe_drain(afe_priv_t *d);
static esp_err_t afe_wr(afe_priv_t *d, uint8_t reg, uint32_t val24);
static esp_err_t afe_rd(afe_priv_t *d, uint8_t reg, uint32_t *val24);

/* ======================= Low-level SPI =============================== */

/* Build/replace the SPI device handle at the requested clock. Pass
 * clock_hz=0 for the board default (VQ_SPI_AFE4900_HZ = 8 MHz).        */
static esp_err_t afe_spi_attach(afe_priv_t *d, uint32_t clock_hz)
{
    if (d->spi) { vq_spi_remove(d->spi); d->spi = NULL; }
    vq_spi_dev_cfg_t cfg = {
        .dev        = VQ_SPI_DEV_AFE4900,
        .cs_gpio    = VQ_CS_AFE4900_GPIO,
        .mode       = 0,
        .clock_hz   = clock_hz ? clock_hz : VQ_SPI_AFE4900_HZ,
        .cs_pre_us  = 1,
        .cs_post_us = 1,
    };
    d->spi = vq_spi_add(&cfg);
    d->spi_rd_mode = false;
    return d->spi ? ESP_OK : ESP_ERR_NO_MEM;
}

/* Single 32-bit frame. SPI_READ handling lives in afe_wr/afe_rd so the
 * bus mode always matches the operation.                               */
static esp_err_t afe_frame(afe_priv_t *d, uint8_t reg, uint32_t w24,
                           uint32_t *r24)
{
    uint8_t tx[4] = { reg,
                      (uint8_t)(w24 >> 16), (uint8_t)(w24 >> 8),
                      (uint8_t)(w24) };
    uint8_t rx[4] = {0};
    esp_err_t e = vq_spi_xfer(d->spi, tx, rx, sizeof(tx));
    if (e == ESP_OK && r24) {
        *r24 = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
    }
    return e;
}

static esp_err_t afe_set_read_mode(afe_priv_t *d, bool on)
{
    if (d->spi_rd_mode == on) { return ESP_OK; }
    uint32_t c0 = d->ctrl0_shadow;
    if (on) { c0 |=  AFE4900_C0_SPI_READ; }
    else    { c0 &= ~AFE4900_C0_SPI_READ; }
    esp_err_t e = afe_frame(d, AFE4900_REG_CONTROL0, c0, NULL);
    if (e == ESP_OK) { d->ctrl0_shadow = c0; d->spi_rd_mode = on; }
    return e;
}

static esp_err_t afe_wr(afe_priv_t *d, uint8_t reg, uint32_t val24)
{
    esp_err_t e = afe_set_read_mode(d, false);   /* writes need SPI_READ=0 */
    if (e != ESP_OK) { return e; }
    e = afe_frame(d, reg, val24 & 0xFFFFFFu, NULL);
    if (e == ESP_OK && reg == AFE4900_REG_CONTROL0) {
        d->ctrl0_shadow = val24 & 0xFFFFFFu;
    }
    return e;
}

static esp_err_t afe_rd(afe_priv_t *d, uint8_t reg, uint32_t *val24)
{
    esp_err_t e = afe_set_read_mode(d, true);    /* read-after-write mode */
    if (e != ESP_OK) { return e; }
    /* TODO(datasheet): if SBAS857 instead uses a dedicated read-command
     * frame, this is the single place to change. */
    return afe_frame(d, reg, 0, val24);
}

static esp_err_t afe_write_table(afe_priv_t *d, const afe_regval_t *t, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        esp_err_t e = afe_wr(d, t[i].reg, t[i].val);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "reg 0x%02x write failed (%s)", t[i].reg,
                     esp_err_to_name(e));
            return e;
        }
    }
    return ESP_OK;
}

static esp_err_t afe_soft_reset(afe_priv_t *d)
{
    esp_err_t e = afe_wr(d, AFE4900_REG_CONTROL0,
                         d->ctrl0_shadow | AFE4900_C0_SW_RESET);
    if (e != ESP_OK) { return e; }
    vTaskDelay(pdMS_TO_TICKS(2));
    d->ctrl0_shadow &= ~AFE4900_C0_SW_RESET;
    return afe_wr(d, AFE4900_REG_CONTROL0, d->ctrl0_shadow);
}

static esp_err_t afe_power_up(afe_priv_t *d)
{
    (void)d;
    esp_err_t e = vq_exp_set(VQ_EXP_AFE_RESETZ, true);   /* out of reset */
    if (e != ESP_OK) { return e; }
    vTaskDelay(pdMS_TO_TICKS(AFE_RESET_RELEASE_MS));
    return ESP_OK;
}

static esp_err_t afe_power_down(afe_priv_t *d)
{
    (void)d;
    return vq_exp_set(VQ_EXP_AFE_RESETZ, false);         /* reset+PWDN   */
}

/* ==================== Timing plan ===================================== */
/* Program PRP + the four slots from s_phases. Counts are 128 kHz ticks. */
static esp_err_t afe_program_timing(afe_priv_t *d)
{
    esp_err_t e = afe_wr(d, AFE4900_REG_PRPCOUNT, AFE_PRP_COUNT);
    if (e != ESP_OK) { return e; }

    for (size_t i = 0; i < AFE_PHASES; i++) {
        const afe_phase_t *p = &s_phases[i];
        uint32_t base = (uint32_t)i * AFE_PHASE_PITCH;
        uint32_t sm_st = base + AFE_SM_OFF;
        uint32_t sm_end = sm_st + AFE_SM_W;
        uint32_t rst_st = base + AFE_RST_OFF;
        uint32_t rst_end = rst_st + AFE_RST_W;
        uint32_t conv_st = base + AFE_CONV_OFF;
        uint32_t conv_end = conv_st + AFE_CONV_W;

        afe_regval_t w[6] = {
            { p->sm_st,   sm_st },   { p->sm_end,  sm_end },
            { p->rst_st,  rst_st },  { p->rst_end, rst_end },
            { p->conv_st, conv_st }, { p->conv_end, conv_end },
        };
        e = afe_write_table(d, w, 6);
        if (e != ESP_OK) { return e; }

        if (p->fire_st) {           /* dark slots have 0,0 regs */
            uint32_t f_st = base + AFE_FIRE_OFF;
            afe_regval_t f[2] = {
                { p->fire_st,  f_st }, { p->fire_end, sm_end },
            };
            e = afe_write_table(d, f, 2);
            if (e != ESP_OK) { return e; }
        }
    }
    /* Park all unused fire windows (start > end idiom).
     * TODO(datasheet): TG_PD1* PD-switch window left at reset values —
     * confirm whether a PD-toggle window is needed for our PD pair.    */
    return afe_write_table(d, s_fire_disable,
                           sizeof(s_fire_disable) / sizeof(s_fire_disable[0]));
}

/* CONTROL1 = NUMAV always; TIMEREN bit toggles the engine. */
static esp_err_t afe_timer_run(afe_priv_t *d, bool run)
{
    d->ctrl1_shadow = AFE4900_C1_NUMAV(AFE_NUMAV) |
                      (run ? AFE4900_C1_TIMEREN : 0u);
    esp_err_t e = afe_wr(d, AFE4900_REG_CONTROL1, d->ctrl1_shadow);
    if (e == ESP_OK) { d->engine_on = run; }
    return e;
}

/* ==================== led_sched integration =========================== */
/* The scheduler owns TX_5V and grants windows. cb_on/cb_off are invoked
 * from ITS context — keep them trivial: set flags + refresh the grant
 * deadline; no SPI (the callback context may be a timer ISR — SPI is
 * illegal there). The grant watchdog in the drain task suspends the AFE
 * timing engine if grants stop (preemption / kill_all / slot gap), so
 * LED on-time can never outlive scheduler authorization.               */
static void afe_led_on(void *ctx)
{
    afe_priv_t *d = ctx;
    d->led_grant = true;
    d->led_gate_us = esp_timer_get_time() + 3 * (int64_t)AFE_PRP_US;
    d->st_armed = true;
}

static void afe_led_off(void *ctx)
{
    afe_priv_t *d = ctx;
    if (d->st_armed) { d->st_slot_lost = true; }
    d->st_armed = false;
    d->led_grant = false;
}

/* Periodic reservation describing the whole pulse train to led_sched. */
static esp_err_t afe_led_reserve(afe_priv_t *d)
{
    vq_led_request_t rq = {
        .chain     = VQ_LED_AFE4900,
        .pulse_mA  = AFE_LED_CLASS_MA,
        .on_us     = AFE_LED_ON_US_DECLARE,
        .period_us = AFE_PRP_US,
        .priority  = 1,
    };
    return vq_led_submit(&rq, afe_led_on, afe_led_off, d, NULL);
}

/* Suspend/resume the timing engine as the scheduler grant comes/goes.
 * Called every drain-task loop so a suspended stream self-heals.       */
static void afe_grant_watchdog(afe_priv_t *d)
{
    int64_t now = esp_timer_get_time();
    bool granted = d->led_grant && now <= d->led_gate_us;
    if (!granted) {
        if (d->engine_on) {
            (void)afe_timer_run(d, false);
            ESP_LOGW(TAG, "led_sched grant lost — engine suspended");
        }
        /* Re-acquire, throttled — the scheduler may be busy. */
        if (d->streaming && now - d->last_resub_us > 100000) {
            d->last_resub_us = now;
            (void)afe_led_reserve(d);
        }
    } else if (d->streaming && !d->engine_on) {
        (void)afe_timer_run(d, true);
    }
}

/* ==================== ISR + drain task ================================ */

static void IRAM_ATTR afe_rdy_isr(void *arg)
{
    afe_priv_t *d = arg;
    vq_ts_anchor_edge(&d->anchor);           /* latch esp_timer µs      */
    d->last_edge_us = d->anchor.edge_us;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(d->rdy_sem, &hp);
    if (hp) { portYIELD_FROM_ISR(hp); }
}

/* Sign-extend a 24-bit ADC code to int32. */
static int32_t afe_s24(uint32_t v)
{
    return (v & AFE4900_VAL_SIGN_BIT) ? (int32_t)(v | 0xFF000000u)
                                    : (int32_t)v;
}

/* One frame = four slot words {amb_red, red, amb_ir, ir}; emit the two
 * ambient-subtracted channels stamped at acquisition.                  */
static void afe_emit(afe_priv_t *d, const uint32_t *w, int64_t t_us,
                     uint8_t flags)
{
    int32_t red = afe_s24(w[AFE_FIDX_RED]) - afe_s24(w[AFE_FIDX_AMB_RED]);
    int32_t ir  = afe_s24(w[AFE_FIDX_IR])  - afe_s24(w[AFE_FIDX_AMB_IR]);
    if (!d->sen || !d->sen->sink) { return; }
    vq_sample_t out[2] = {
        { .channel = VQ_CH_PPG_RED, .t_us = t_us,
          .value = (float)red, .flags = flags },
        { .channel = VQ_CH_PPG_IR,  .t_us = t_us,
          .value = (float)ir,  .flags = flags },
    };
    d->sen->sink(d->sen->sink_ctx, out, 2);
    d->sen->sample_count += 2;
    d->sen->last_t_us = t_us;
}

#if AFE4900_USE_FIFO
/* Read FIFO word count, then burst that many 32-bit frames in one CS.
 * TODO(datasheet): count register + field mask unconfirmed; if the FIFO
 * pop protocol differs (dedicated burst command), isolate the change
 * here.                                                               */
static esp_err_t afe_fifo_read(afe_priv_t *d, uint32_t *words, size_t *n_out)
{
    uint32_t cnt = 0;
    esp_err_t e = afe_rd(d, AFE4900_REG_FIFO_COUNT, &cnt);
    if (e != ESP_OK) { return e; }
    size_t n = (size_t)(cnt & AFE4900_FIFO_COUNT_MASK);
    if (n > AFE_FIFO_MAX_WORDS) { n = AFE_FIFO_MAX_WORDS; }
    *n_out = 0;
    if (n == 0) { return ESP_OK; }

    static uint8_t tx[AFE_FIFO_MAX_WORDS * 4];
    static uint8_t rx[AFE_FIFO_MAX_WORDS * 4];
    memset(tx, 0, n * 4);
    for (size_t i = 0; i < n; i++) { tx[i * 4] = AFE4900_REG_FIFO_DATA; }

    e = afe_set_read_mode(d, true);
    if (e != ESP_OK) { return e; }
    e = vq_spi_xfer(d->spi, tx, rx, n * 4);
    if (e != ESP_OK) { return e; }
    for (size_t i = 0; i < n; i++) {
        words[i] = ((uint32_t)rx[i * 4 + 1] << 16) |
                   ((uint32_t)rx[i * 4 + 2] << 8) | rx[i * 4 + 3];
    }
    *n_out = n;
    return ESP_OK;
}

/* Pop everything pending and discard it — used at stream start (and
 * after a partial-frame event) so the decode starts frame-aligned.     */
static void afe_fifo_flush(afe_priv_t *d)
{
    uint32_t words[AFE_FIFO_MAX_WORDS];
    size_t n = 0;
    (void)afe_fifo_read(d, words, &n);
}
#endif /* AFE4900_USE_FIFO */

static void afe_drain(afe_priv_t *d)
{
#if AFE4900_USE_FIFO
    uint32_t words[AFE_FIFO_MAX_WORDS];
    size_t n = 0;
    if (afe_fifo_read(d, words, &n) != ESP_OK) {
        d->drain_err++;
        if (d->sen) { d->sen->err_count++; }
        return;
    }
    size_t frames = n / AFE_FIFO_WORDS_FRAME;
    if (n % AFE_FIFO_WORDS_FRAME) {
        /* Partial frame: word-level phase alignment is unknown (the
         * FIFO gives no tag — TODO(datasheet) if tags exist). Discard
         * this batch and flush so the next read starts on a boundary. */
        d->fifo_partials++;
        afe_fifo_flush(d);
        return;
    }
    if (frames == 0) { return; }

    /* Oldest-first from the FIFO; the LAST frame is the edge frame. */
    int32_t last_seq = d->frame_seq + (int32_t)frames - 1;
    d->anchor.seq_anchor = last_seq;
    for (size_t f = 0; f < frames; f++) {
        int32_t seq = d->frame_seq + (int32_t)f;
        int64_t ts = vq_ts_anchor_stamp(&d->anchor, seq);
        uint8_t fl = (seq == last_seq) ? 0 : VQ_SAMPLE_F_ESTIMATED_TS;
        afe_emit(d, &words[f * AFE_FIFO_WORDS_FRAME], ts, fl);
    }
    d->frame_seq = last_seq + 1;
#else
    /* Direct result-register read per edge (no FIFO). */
    uint32_t w[AFE_PHASES] = {0};
    for (size_t i = 0; i < AFE_PHASES; i++) {
        if (afe_rd(d, s_slot_val[i], &w[i]) != ESP_OK) {
            d->drain_err++;
            if (d->sen) { d->sen->err_count++; }
            return;
        }
    }
    int32_t seq = d->frame_seq++;
    d->anchor.seq_anchor = seq;
    afe_emit(d, w, vq_ts_anchor_stamp(&d->anchor, seq), 0);
#endif
}

static void afe_task(void *arg)
{
    afe_priv_t *d = arg;
    for (;;) {
        bool edge = (xSemaphoreTake(d->rdy_sem, pdMS_TO_TICKS(500)) == pdTRUE);
        if (!d->streaming) { continue; }
        afe_grant_watchdog(d);
        if (edge) { afe_drain(d); }
    }
}

static esp_err_t afe_task_start(afe_priv_t *d)
{
    if (d->task) { return ESP_OK; }
    if (!d->rdy_sem) { d->rdy_sem = xSemaphoreCreateBinary(); }
    if (!d->rdy_sem) { return ESP_ERR_NO_MEM; }
    if (xTaskCreate(afe_task, "afe4900", AFE_TASK_STACK, d,
                    AFE_TASK_PRIO, &d->task) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t afe_isr_setup(afe_priv_t *d)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << VQ_AFE4900_RDY_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK) { return e; }
    e = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { return e; }
    if (!d->isr_installed) {
        e = gpio_isr_handler_add(VQ_AFE4900_RDY_GPIO, afe_rdy_isr, d);
        if (e != ESP_OK) { return e; }
        d->isr_installed = true;
    }
    return ESP_OK;
}

/* ==================== vq_sensor_ops ==================================== */

static esp_err_t afe_op_probe(vq_sensor_t *s)
{
#if !VQ_AFE4900_SPI_ENABLE
    (void)s;
    ESP_LOGW(TAG, "I2C_SPI_SEL polarity unresolved — probe is a no-op");
    return ESP_ERR_NOT_FOUND;
#else
    afe_priv_t *d = s->priv;
    /* present is only trusted while RESETZ is actually released. */
    if (d->present && vq_exp_get_out(VQ_EXP_AFE_RESETZ)) { return ESP_OK; }
    esp_err_t e = afe_spi_attach(d, 0);
    if (e != ESP_OK) { return e; }
    e = afe_power_up(d);
    if (e != ESP_OK) { return e; }

    d->ctrl0_shadow = AFE_INIT_C0;
    (void)afe_soft_reset(d);

    /* No public ID/version register on this family (TODO(datasheet)) —
     * try the candidate VER reg for the log, then do the authoritative
     * comms check: write/readback/restore on the harmless PRPCOUNT.     */
    uint32_t ver = 0;
    if (afe_rd(d, AFE4900_REG_VERSION, &ver) == ESP_OK) {
        ESP_LOGI(TAG, "ver-candidate reg = 0x%06lx", (unsigned long)ver);
    }
    uint32_t orig = 0, back = 0;
    bool comms = false;
    if (afe_rd(d, AFE4900_REG_PRPCOUNT, &orig) == ESP_OK &&
        afe_wr(d, AFE4900_REG_PRPCOUNT, 0x00009Fu) == ESP_OK &&
        afe_rd(d, AFE4900_REG_PRPCOUNT, &back) == ESP_OK &&
        back == 0x00009Fu) {
        (void)afe_wr(d, AFE4900_REG_PRPCOUNT, orig);
        comms = true;
    }
    if (!comms) {
        (void)afe_power_down(d);
        return ESP_ERR_NOT_FOUND;
    }
    d->present = true;
    return ESP_OK;
#endif
}

static esp_err_t afe_op_init(vq_sensor_t *s)
{
    afe_priv_t *d = s->priv;
    esp_err_t e = afe_power_up(d);      /* covers post-sleep/selftest    */
    if (e != ESP_OK) { return e; }
    e = afe_op_probe(s);
    if (e != ESP_OK) { return e; }

    e = afe_soft_reset(d);
    if (e != ESP_OK) { return e; }
    e = afe_write_table(d, s_init_table,
                        sizeof(s_init_table) / sizeof(s_init_table[0]));
    if (e != ESP_OK) { return e; }
    e = afe_program_timing(d);
    if (e != ESP_OK) { return e; }
    e = afe_timer_run(d, false);            /* NUMAV set, engine parked  */
    if (e != ESP_OK) { return e; }
    /* Family rule: reset the timing counter after programming windows.
     * TODO(datasheet): confirm the TM_COUNT_RST bit position.          */
    e = afe_wr(d, AFE4900_REG_CONTROL0,
               d->ctrl0_shadow | AFE4900_C0_TM_COUNT_RST);
    if (e != ESP_OK) { return e; }
    e = afe_wr(d, AFE4900_REG_CONTROL0, d->ctrl0_shadow);
    if (e != ESP_OK) { return e; }

    e = afe_isr_setup(d);
    if (e != ESP_OK) { return e; }
    e = afe_task_start(d);
    if (e != ESP_OK) { return e; }

    d->anchor.rate_hz = AFE_OUT_RATE_HZ;
    d->inited = true;
    return ESP_OK;
}

static esp_err_t afe_op_start(vq_sensor_t *s)
{
    afe_priv_t *d = s->priv;
    if (!d->inited) { return ESP_ERR_INVALID_STATE; }
    if (d->streaming) { return ESP_OK; }

    /* LED budget first — the engine only runs inside a granted window. */
    esp_err_t e = afe_led_reserve(d);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "led_sched denied AFE4900 slots (%s)",
                 esp_err_to_name(e));
        return e;
    }
    int64_t dl = esp_timer_get_time() + (int64_t)AFE_GRANT_WAIT_US;
    while (!d->led_grant && esp_timer_get_time() < dl) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!d->led_grant) {
        vq_led_cancel(VQ_LED_AFE4900);
        return ESP_ERR_TIMEOUT;
    }

    d->drain_err = 0;
    d->frame_seq = 0;
    d->anchor.seq_anchor = 0;
#if AFE4900_USE_FIFO
    afe_fifo_flush(d);               /* start frame-aligned            */
#endif
    e = gpio_set_intr_type(VQ_AFE4900_RDY_GPIO, GPIO_INTR_POSEDGE);
    if (e != ESP_OK) {
        vq_led_cancel(VQ_LED_AFE4900);
        d->led_grant = false;
        return e;
    }
    gpio_intr_enable(VQ_AFE4900_RDY_GPIO);

    e = afe_timer_run(d, true);
    if (e != ESP_OK) {
        gpio_intr_disable(VQ_AFE4900_RDY_GPIO);
        gpio_set_intr_type(VQ_AFE4900_RDY_GPIO, GPIO_INTR_DISABLE);
        vq_led_cancel(VQ_LED_AFE4900);
        d->led_grant = false;
        return e;
    }
    d->streaming = true;
    return ESP_OK;
}

static esp_err_t afe_op_stop(vq_sensor_t *s)
{
    afe_priv_t *d = s->priv;
    d->streaming = false;
    gpio_set_intr_type(VQ_AFE4900_RDY_GPIO, GPIO_INTR_DISABLE);
    gpio_intr_disable(VQ_AFE4900_RDY_GPIO);
    vq_led_cancel(VQ_LED_AFE4900);
    d->led_grant = false;
    if (d->present) { (void)afe_timer_run(d, false); }
    return ESP_OK;
}

static esp_err_t afe_op_sleep(vq_sensor_t *s)
{
    afe_priv_t *d = s->priv;
    (void)afe_op_stop(s);
    if (d->present) {
        /* TODO(datasheet): exact power-down register/field — RESETZ low
         * already gives the ~0 µA hardware power-down; this register
         * pass is belt-and-braces until the sleep map is confirmed.     */
        (void)afe_wr(d, AFE4900_REG_LEDCNTRL1, 0);
        (void)afe_wr(d, AFE4900_REG_LEDCNTRL2, 0);
        (void)afe_timer_run(d, false);
        (void)afe_wr(d, AFE4900_REG_POWERDN, 0x000001u);
        (void)afe_wr(d, AFE4900_REG_CONTROL0, AFE4900_C0_ULP_EN);
    }
    esp_err_t e = afe_power_down(d);        /* RESETZ low = reset+PWDN   */
    d->inited = false;                      /* config lost in reset      */
    return e;
}

static bool afe_op_healthy(vq_sensor_t *s)
{
    afe_priv_t *d = s->priv;
    if (!d->present) { return false; }
    if (!d->streaming) { return true; }
    if (d->drain_err > AFE_HEALTH_ERR_MAX) { return false; }
    int64_t since = esp_timer_get_time() - d->last_edge_us;
    if (d->last_edge_us != 0 && since > 3 * (int64_t)AFE_FRAME_US) {
        return false;                        /* edges stopped           */
    }
    return true;
}

/* ==================== Registration ===================================== */

static const vq_channel_id_t s_channels[] = { VQ_CH_PPG_RED, VQ_CH_PPG_IR };

static const vq_sensor_ops_t s_ops = {
    .name       = "afe4900",
    .probe      = afe_op_probe,
    .init       = afe_op_init,
    .start      = afe_op_start,
    .stop       = afe_op_stop,
    .sleep      = afe_op_sleep,
    .healthy    = afe_op_healthy,
    .channels   = s_channels,
    .n_channels = sizeof(s_channels) / sizeof(s_channels[0]),
    .rate_hz    = AFE_OUT_RATE_HZ,
    .required   = false,          /* absent tolerated per contract      */
};

esp_err_t vq_afe4900_register(void)
{
    if (s_registered) { return ESP_OK; }
    memset(&s_priv, 0, sizeof(s_priv));
    memset(&s_sensor, 0, sizeof(s_sensor));
    s_sensor.ops = &s_ops;
    s_sensor.priv = &s_priv;
    s_priv.sen = &s_sensor;
    esp_err_t e = vq_sensor_register(&s_sensor);
    if (e == ESP_OK) {
        s_registered = true;
        /* registry owns the live copy — sink/counters live there       */
        s_priv.sen = vq_sensor_find(s_sensor.ops->name);
        if (!s_priv.sen) s_priv.sen = &s_sensor;
    }
    return e;
}

/* ==================== Diagnostics ====================================== */

esp_err_t vq_afe4900_diag_read(uint16_t reg, uint32_t *val)
{
    if (!val) { return ESP_ERR_INVALID_ARG; }
    afe_priv_t *d = &s_priv;
    if (!d->present) { return ESP_ERR_INVALID_STATE; }
    return afe_rd(d, (uint8_t)(reg & 0xFFu), val);
}

/* ==================== Self-test ======================================== */

static void rpt(char *buf, size_t len, size_t *off, const char *fmt, ...)
{
    if (*off >= len) { return; }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, len - *off, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t room = len - *off;
        *off += ((size_t)n < room) ? (size_t)n : room;
    }
}

/* One minimal-current LED pulse inside a granted led_sched slot, then a
 * PD response check on the red channel. Runs synchronously from the
 * selftest caller while the scheduler holds the window open.           */
static esp_err_t afe_st_led_pulse(afe_priv_t *d, char *buf, size_t len,
                                  size_t *off, bool *pass)
{
    *pass = false;
    vq_led_request_t rq = {
        .chain = VQ_LED_AFE4900, .pulse_mA = 20 /* min-current pulse */,
        .on_us = 30000, .period_us = 0 /* one-shot */, .priority = 3,
    };
    d->st_armed = false;
    d->st_slot_lost = false;
    esp_err_t e = vq_led_submit(&rq, afe_led_on, afe_led_off, d, NULL);
    if (e != ESP_OK) {
        rpt(buf, len, off, "  led_sched submit FAIL (%s)\n",
            esp_err_to_name(e));
        return e;
    }
    /* Wait for the slot to open (cb_on sets st_armed). */
    int64_t deadline = esp_timer_get_time() + 2000000;
    while (!d->st_armed && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!d->st_armed) {
        rpt(buf, len, off, "  led pulse SKIP (no slot in 2 s)\n");
        return ESP_ERR_TIMEOUT;
    }

    /* Inside the window: min current on the driven codes, run the
     * engine ~2 output frames, read the red slot + its ambient.        */
    (void)afe_wr(d, AFE4900_REG_LEDCNTRL1,
        ((uint32_t)AFE_LED_CODE_SELFTEST << AFE4900_LEDC1_LED2_SHIFT) |
        ((uint32_t)AFE_LED_CODE_SELFTEST << AFE4900_LEDC1_LED3_SHIFT));
    (void)afe_wr(d, AFE4900_REG_LEDCNTRL2,
        ((uint32_t)AFE_LED_CODE_SELFTEST << AFE4900_LEDC2_LED4_SHIFT));
    e = afe_timer_run(d, true);
    if (e == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2 * (uint32_t)(1000 / AFE_OUT_RATE_HZ)));
        uint32_t lit = 0, amb = 0;
        e = afe_rd(d, s_slot_val[AFE_FIDX_RED], &lit);
        if (e == ESP_OK) { e = afe_rd(d, s_slot_val[AFE_FIDX_AMB_RED], &amb); }
        (void)afe_timer_run(d, false);
        if (e == ESP_OK) {
            int32_t delta = afe_s24(lit) - afe_s24(amb);
            *pass = (delta > AFE_ST_PD_MIN_DELTA) && !d->st_slot_lost;
            rpt(buf, len, off,
                "  led pulse: lit=%ld amb=%ld delta=%ld -> %s\n",
                (long)afe_s24(lit), (long)afe_s24(amb), (long)delta,
                *pass ? "PASS" : "FAIL");
        }
    }
    if (e != ESP_OK) {
        rpt(buf, len, off, "  led pulse FAIL (%s)\n", esp_err_to_name(e));
    } else if (!*pass) {
        /* delta can clear the floor while the LED slot itself was lost
         * mid-window (scheduler contention) — say which one it was.    */
        rpt(buf, len, off, "  led pulse FAIL (%s)\n",
            d->st_slot_lost ? "LED slot lost mid-window (led_sched grant)"
                            : "delta below floor");
    }
    /* Restore runtime LED codes, let the slot close. */
    (void)afe_wr(d, AFE4900_REG_LEDCNTRL1,
        ((uint32_t)AFE_LED_CODE_RUN << AFE4900_LEDC1_LED2_SHIFT) |
        ((uint32_t)AFE_LED_CODE_RUN << AFE4900_LEDC1_LED3_SHIFT));
    (void)afe_wr(d, AFE4900_REG_LEDCNTRL2,
        ((uint32_t)AFE_LED_CODE_RUN << AFE4900_LEDC2_LED4_SHIFT));
    vq_led_cancel(VQ_LED_AFE4900);
    d->st_armed = false;
    return *pass ? ESP_OK : (e != ESP_OK ? e : ESP_FAIL);
}

esp_err_t vq_afe4900_selftest(char *buf, size_t len)
{
    if (!buf || len == 0) { return ESP_ERR_INVALID_ARG; }
    afe_priv_t *d = &s_priv;
    size_t off = 0;
    buf[0] = '\0';

    rpt(buf, len, &off, "afe4900 selftest\n");

    if (d->streaming) {
        rpt(buf, len, &off, "  SKIP — streaming in progress\n");
        return ESP_ERR_INVALID_STATE;
    }
#if !VQ_AFE4900_SPI_ENABLE
    (void)d;
    rpt(buf, len, &off,
        "  SKIP — I2C_SPI_SEL polarity open; SPI disabled by build\n");
    return ESP_OK;
#else
    bool all_ok = true;
    bool was_inited = d->inited;
    esp_err_t e;

    /* --- stage 1: 1 MHz probe readback ------------------------------ */
    e = afe_spi_attach(d, VQ_SPI_PROBE_HZ);
    if (e == ESP_OK) { e = afe_power_up(d); }
    if (e == ESP_OK) {
        d->ctrl0_shadow = AFE_INIT_C0;
        e = afe_soft_reset(d);
    }
    bool s1 = false;
    if (e == ESP_OK) {
        uint32_t o = 0, b = 0;
        s1 = (afe_rd(d, AFE4900_REG_PRPCOUNT, &o) == ESP_OK &&
              afe_wr(d, AFE4900_REG_PRPCOUNT, 0x0000A5u) == ESP_OK &&
              afe_rd(d, AFE4900_REG_PRPCOUNT, &b) == ESP_OK &&
              b == 0x0000A5u &&
              afe_wr(d, AFE4900_REG_PRPCOUNT, o) == ESP_OK);
        rpt(buf, len, &off, "  spi@1MHz readback: %s (prp=0x%06lx)\n",
            s1 ? "PASS" : "FAIL", (unsigned long)b);
    } else {
        rpt(buf, len, &off, "  spi@1MHz FAIL (%s)\n", esp_err_to_name(e));
    }
    all_ok &= (e == ESP_OK) && s1;

    /* --- stage 2: 8 MHz readback ------------------------------------- */
    e = afe_spi_attach(d, VQ_SPI_AFE4900_HZ);
    bool s2 = false;
    if (e == ESP_OK) {
        uint32_t c0 = 0;
        s2 = (afe_rd(d, AFE4900_REG_CONTROL0, &c0) == ESP_OK &&
              c0 == d->ctrl0_shadow);
        rpt(buf, len, &off, "  spi@8MHz control0=0x%06lx: %s\n",
            (unsigned long)c0, s2 ? "PASS" : "FAIL");
    } else {
        rpt(buf, len, &off, "  spi@8MHz FAIL (%s)\n", esp_err_to_name(e));
    }
    all_ok &= (e == ESP_OK) && s2;

    /* --- stage 3: register write/readback verify ---------------------- */
    bool s3 = false;
    if (e == ESP_OK) {
        uint32_t o = 0, b = 0;
        s3 = (afe_rd(d, AFE4900_REG_LED2STC, &o) == ESP_OK &&
              afe_wr(d, AFE4900_REG_LED2STC, 0x000037u) == ESP_OK &&
              afe_rd(d, AFE4900_REG_LED2STC, &b) == ESP_OK &&
              b == 0x000037u &&
              afe_wr(d, AFE4900_REG_LED2STC, o) == ESP_OK);
        rpt(buf, len, &off, "  reg write/readback (led2stc): %s\n",
            s3 ? "PASS" : "FAIL");
    }
    all_ok &= s3;

    /* --- stage 4: LED pulse + PD response via led_sched --------------- */
    bool s4 = false;
    if (s1 || s2) { /* comms proven somewhere */
        if (afe_write_table(d, s_init_table,
                    sizeof(s_init_table) / sizeof(s_init_table[0])) == ESP_OK &&
            afe_program_timing(d) == ESP_OK) {
            (void)afe_st_led_pulse(d, buf, len, &off, &s4);
        } else {
            rpt(buf, len, &off, "  led pulse FAIL (timing load)\n");
        }
    } else {
        rpt(buf, len, &off, "  led pulse SKIP (no comms)\n");
    }
    all_ok &= s4;

    /* --- stage 5: park ------------------------------------------------- */
    (void)afe_timer_run(d, false);
    (void)afe_soft_reset(d);
    (void)afe_power_down(d);
    d->present = all_ok;              /* stay present only if sane       */
    d->inited = false;                /* config wiped by the reset       */
    if (was_inited) {
        rpt(buf, len, &off, "  note: was inited — pipeline must re-init\n");
    }
    rpt(buf, len, &off, "  overall: %s\n", all_ok ? "PASS" : "FAIL");
    return all_ok ? ESP_OK : ESP_FAIL;
#endif
}
