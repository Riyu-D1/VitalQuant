/* vq_drv_ad5940.c — AD5940 impedance AFE driver (U7, fix_v22).
 *
 * One physical AD5940 on the shared SPI2 bus.  Two logical vq_sensor_t
 * descriptors share this file's single device state and SPI handle:
 *
 *   "ad5940.eda"   — VQ_CH_EDA + VQ_CH_BIOZ   (J6 EDA site / J7 BioZ site)
 *   "ad5940.sweat" — VQ_CH_SWEAT_LACTATE      (J11 sweat electrode site)
 *                    RESEARCH-GRADE ONLY — engineering electrode current,
 *                    never a clinical lactate concentration.
 *
 * Architecture (single-shot, host-triggered):
 *   - AD5940 sequencer SRAM holds four pre-programmed sequences:
 *       SEQ0 = RCAL leg + EDA leg     (4 DFT words)
 *       SEQ1 = RCAL leg + BioZ leg    (4 DFT words)
 *       SEQ2 = sweat amperometric leg (4 SINC2 words, LP loop)
 *       SEQ3 = RCAL-only leg          (2 DFT words, calibration probe)
 *     + a tiny SRAM region used by the IRQ-loopback self-test.
 *   - esp_timer tick → task → wake chip → SEQEN+TRIGSEQ → sequencer runs
 *     switches/AFECON itself → FIFO-threshold IRQ asserts GP0 → IO14 ISR
 *     latches the edge timestamp + wakes task → task drains FIFO over SPI
 *     → hibernate until next tick.
 *   - Reported |Z| = RCAL_nom × |DFT_rcal| / |DFT_z| − 51 kΩ series-R
 *     (see note near SERIES_R_OHM about the kelvin caveat).
 *
 * Uncertainties are marked TODO(datasheet) / TODO(hw) inline; the big
 * ones: GPIO2 has no plain-GPIO mux (driven via SYNC function), the LP
 * switch-map for the J11 sweat pins (AFE3/AFE4/AIN6) is unverified,
 * GP0 interrupt polarity is auto-detected at self-test.
 */
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

#include "vq_types.h"
#include "vq_sensor.h"
#include "board_pins.h"
#include "spi_bus.h"
#include "expander.h"
#include "led_sched.h"

#include "vq_drv_ad5940.h"
#include "ad5940_regs.h"

/* ------------------------------------------------------------------ */
/* tuning constants                                                    */
/* ------------------------------------------------------------------ */
#define AD5940_SPI_RUN_HZ    8000000u   /* max bus rate for AD5940        */
#define AD5940_SPI_PROBE_HZ  1000000u   /* slow probe used in self-test   */
#define AD5940_EDA_RATE_HZ   5.0f
#define AD5940_SWEAT_RATE_HZ 1.0f

#define AD5940_SYSCLK_HZ     16000000u  /* 32 MHz HFOSC / 2               */
#define AD5940_ADCCLK_HZ     16000000u  /* 32 MHz HFOSC / 2               */
#define AD5940_US2CLK(us)    ((uint32_t)(us) * (AD5940_SYSCLK_HZ / 1000000u))

/* EDA / BioZ excitation */
#define AD5940_EXC_FREQ_HZ   200.0f     /* within spec'd ~100-200 Hz      */
#define AD5940_EXC_MVPP      200.0f     /* sensor-port excitation         */
#define AD5940_DFT_NUM       DFTNUM_4096/* 4096 pts ≈ 2 cycles @200 Hz    */
#define AD5940_HSTIA_RTIA    HSTIARTIA_40K  /* TODO(hw): tune vs Z range  */

/* RCAL + series-R.  TODO(hw): confirm the fitted RCAL value (R97 and
 * neighbours, RCAL0/RCAL1 across B1/C1) — schematic review said ~10 kΩ. */
#define AD5940_RCAL_OHM      10000.0f
/* 51 kΩ defib/protection resistors sit in every electrode lead on J6/J7.
 * Subtracting them from reported |Z| assumes they are inside the
 * measured loop (2-wire-style).  With the true 4-wire kelvin hook-up the
 * sense-lead Rs drop no current and subtracting over-compensates —
 * TODO(hw): re-derive after harness verification; keep per spec for now. */
#define AD5940_SERIES_R_OHM  51000.0f

/* sweat LP-loop bias: Vzero ~1.1 V, Vbias offset +300 mV across cell.
 * TODO(hw): confirm target bias for the actual sweat cell chemistry.   */
#define AD5940_SWEAT_VZERO_MV 1100.0f
#define AD5940_SWEAT_BIAS_MV  300.0f

#define FIFO_DFT_WORDS       4          /* {rcal re,im, z re,im}         */
#define FIFO_RCAL_WORDS      2
#define FIFO_SWEAT_WORDS     4

#define EV_TICK              (1u << 0)
#define EV_IRQ               (1u << 1)

#define SEQ_SRAM_EDA         0x000
#define SEQ_SRAM_BIOZ        0x040
#define SEQ_SRAM_SWEAT       0x080
#define SEQ_SRAM_RCAL        0x0C0
#define SEQ_SRAM_LOOPBACK    0x0E0

/* AFECON assembled values (see ad5940_regs.h bit list). */
#define AFE_EXCITE  (AFECON_DACREFEN | AFECON_SINC2EN | AFECON_WAVEGENEN | \
                     AFECON_TIAEN | AFECON_INAMPEN | AFECON_EXBUFEN |      \
                     AFECON_ADCEN | AFECON_DACEN)                          /* 0x114EC0 */
#define AFE_MEAS    (AFE_EXCITE | AFECON_DFTEN | AFECON_ADCCONVEN)         /* 0x11CFC0 */
#define AFE_LP_SETTLE (AFECON_SINC2EN | AFECON_ADCEN)                      /* 0x10080  */
#define AFE_LP_MEAS   (AFE_LP_SETTLE | AFECON_ADCCONVEN)                   /* 0x10180  */
#define AFE_IDLE      (AFECON_HPREFDIS)                                    /* 0x00020  */

static const char *TAG = "ad5940";

static void gpio0_isr(void *arg);
static void tick_cb(void *arg);

/* ------------------------------------------------------------------ */
/* device state — shared by both logical sensors                       */
/* ------------------------------------------------------------------ */
typedef enum { SUB_EDA = 0, SUB_SWEAT = 1 } sub_id_t;

static struct {
    void               *spi;            /* vq_spi handle                  */
    bool                present;        /* probe() saw a valid CHIPID     */
    bool                inited;         /* chip fully configured          */
    bool                asleep;         /* in hibernate or held in reset  */
    bool                streaming;
    uint16_t            chipid;

    SemaphoreHandle_t   mtx;            /* serialises all chip access     */
    EventGroupHandle_t  evt;
    TaskHandle_t        task;
    esp_timer_handle_t  tick;

    /* ISR timestamp anchor — edge_us is the acquisition stamp contract */
    vq_ts_anchor_t      anchor;
    volatile int64_t    edge_us;
    volatile uint32_t   irq_count;

    /* channel enables + scheduling */
    bool                en_eda;
    bool                en_sweat;
    int64_t             eda_due;
    int64_t             sweat_due;

    /* interrupt polarity: 1 → GP0 active-low / IO14 negedge (default,
     * auto-detected by self-test); 0 → active-high / posedge           */
    int                 int_pol;
    gpio_int_type_t     int_edge;
    bool                isr_attached;
    uint32_t            seq3_len;        /* restore len after loopback   */

    uint8_t             sync_shadow;    /* SYNCEXTDEVICE shadow (NIR bit2)*/

    /* calibration */
    float               rcal_mag_ref;   /* stored DFT mag of RCAL leg     */
    bool                rcal_ref_valid;

    uint32_t            err_count;
    uint32_t            meas_ok;
} s_dev;

static vq_sensor_t s_sens_eda;          /* filled at register()           */
static vq_sensor_t s_sens_sweat;
static vq_sensor_t *s_reg_eda;          /* registry-owned live copies:    */
static vq_sensor_t *s_reg_sweat;        /* sink/counters live there     */
static int         s_priv_eda   = SUB_EDA;
static int         s_priv_sweat = SUB_SWEAT;
static bool        s_registered;

/* ------------------------------------------------------------------ */
/* SPI primitives                                                      */
/* ------------------------------------------------------------------ */

/* Attach (or re-attach) the shared-bus handle at the requested clock.
 * Two-stage detach/attach is also how the self-test drops to 1 MHz. */
static esp_err_t spi_attach(uint32_t hz)
{
    if (s_dev.spi) {
        vq_spi_remove(s_dev.spi);
        s_dev.spi = NULL;
    }
    vq_spi_dev_cfg_t cfg = {
        .dev        = VQ_SPI_DEV_AD5940,
        .cs_gpio    = VQ_CS_AD5940_GPIO,
        .mode       = 0,                 /* SPI mode 0                    */
        .clock_hz   = hz,
        .cs_pre_us  = 0,
        .cs_post_us = 0,
    };
    s_dev.spi = vq_spi_add(&cfg);
    return s_dev.spi ? ESP_OK : ESP_FAIL;
}

static esp_err_t reg_wr(uint16_t addr, uint32_t data)
{
    uint8_t a[3] = { AD5940_SPICMD_SETADDR, (uint8_t)(addr >> 8), (uint8_t)addr };
    esp_err_t err = vq_spi_xfer(s_dev.spi, a, NULL, sizeof(a));   /* CS toggles */
    if (err != ESP_OK)
        return err;
    if (AD5940_REG_WIDE(addr)) {
        uint8_t d[5] = { AD5940_SPICMD_WRITEREG, (uint8_t)(data >> 24),
                         (uint8_t)(data >> 16), (uint8_t)(data >> 8), (uint8_t)data };
        return vq_spi_xfer(s_dev.spi, d, NULL, sizeof(d));
    }
    uint8_t d[3] = { AD5940_SPICMD_WRITEREG, (uint8_t)(data >> 8), (uint8_t)data };
    return vq_spi_xfer(s_dev.spi, d, NULL, sizeof(d));
}

static esp_err_t reg_rd(uint16_t addr, uint32_t *out)
{
    uint8_t a[3] = { AD5940_SPICMD_SETADDR, (uint8_t)(addr >> 8), (uint8_t)addr };
    esp_err_t err = vq_spi_xfer(s_dev.spi, a, NULL, sizeof(a));
    if (err != ESP_OK)
        return err;

    /* frame = READREG cmd + 1 dummy/status byte + 2 or 4 data bytes     */
    if (AD5940_REG_WIDE(addr)) {
        uint8_t tx[6] = { AD5940_SPICMD_READREG, 0, 0, 0, 0, 0 };
        uint8_t rx[6];
        err = vq_spi_xfer(s_dev.spi, tx, rx, sizeof(tx));
        *out = ((uint32_t)rx[2] << 24) | ((uint32_t)rx[3] << 16) |
               ((uint32_t)rx[4] << 8) | rx[5];
    } else {
        uint8_t tx[4] = { AD5940_SPICMD_READREG, 0, 0, 0 };
        uint8_t rx[4];
        err = vq_spi_xfer(s_dev.spi, tx, rx, sizeof(tx));
        *out = ((uint32_t)rx[2] << 8) | rx[3];
    }
    return err;
}

/* Read n FIFO words (vendor quirk: last two words shift 0x44444444). */
static esp_err_t fifo_rd(uint32_t *buf, uint32_t n)
{
    if (n == 0)
        return ESP_OK;
    if (n < 3) {
        /* per-word register reads on DATAFIFORD */
        for (uint32_t i = 0; i < n; i++) {
            esp_err_t err = reg_rd(REG_DATAFIFORD, &buf[i]);
            if (err != ESP_OK)
                return err;
        }
        return ESP_OK;
    }
    /* burst: [0x5f][6 dummy][n words]; last two words tx 0x44444444 */
    uint8_t tx[1 + 6 + 4 * 16], rx[sizeof(tx)];
    if (n > 16)
        return ESP_ERR_INVALID_SIZE;
    memset(tx, 0, sizeof(tx));
    tx[0] = AD5940_SPICMD_READFIFO;
    tx[1 + 6 + 4 * (n - 2)] = 0x44;          /* penultimate word offset  */
    tx[2 + 6 + 4 * (n - 2)] = 0x44;
    tx[3 + 6 + 4 * (n - 2)] = 0x44;
    tx[4 + 6 + 4 * (n - 2)] = 0x44;
    tx[1 + 6 + 4 * (n - 1)] = 0x44;
    tx[2 + 6 + 4 * (n - 1)] = 0x44;
    tx[3 + 6 + 4 * (n - 1)] = 0x44;
    tx[4 + 6 + 4 * (n - 1)] = 0x44;
    esp_err_t err = vq_spi_xfer(s_dev.spi, tx, rx, 1 + 6 + 4 * n);
    if (err != ESP_OK)
        return err;
    for (uint32_t i = 0; i < n; i++)
        buf[i] = ((uint32_t)rx[7 + 4 * i] << 24) | ((uint32_t)rx[8 + 4 * i] << 16) |
                 ((uint32_t)rx[9 + 4 * i] << 8) | rx[10 + 4 * i];
    return ESP_OK;
}

static esp_err_t fifo_count(uint32_t *n)
{
    uint32_t sta;
    esp_err_t err = reg_rd(REG_FIFOCNTSTA, &sta);
    *n = (sta >> 16) & 0x7ff;
    return err;
}

/* ------------------------------------------------------------------ */
/* reset / wake / sleep                                                */
/* ------------------------------------------------------------------ */

static esp_err_t hw_reset_pulse(void)
{
    esp_err_t err;
    if ((err = vq_exp_set(VQ_EXP_AD5940_RST, 0)) != ESP_OK)  /* assert  */
        return err;
    vTaskDelay(pdMS_TO_TICKS(2));   /* TODO(datasheet): min reset width */
    if ((err = vq_exp_set(VQ_EXP_AD5940_RST, 1)) != ESP_OK)  /* release */
        return err;
    vTaskDelay(pdMS_TO_TICKS(5));   /* TODO(datasheet): boot/OTP time   */
    s_dev.asleep = false;
    s_dev.inited = false;
    return ESP_OK;
}

static esp_err_t chip_init(void);        /* full reprogram, fwd decl    */

/* Any SPI access wakes a hibernated AFE; poll ADIID until it answers.
 * A chip held under expander reset (P2 low — e.g. left asserted by a
 * prior expander_walk or a board glitch) can never answer: its SPI is
 * gated at the pin, and once released its SRAM/MMRs are blank anyway.
 * If the hibernate poll fails, run the full init path — the asleep flag
 * covers "hibernate OR held in reset", and only a reprogram recovers
 * the second case. */
static esp_err_t chip_wake(void)
{
    uint32_t id = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        for (int i = 0; i < 40; i++) {
            if (reg_rd(REG_ADIID, &id) == ESP_OK && id == AD5940_ADIID_VAL) {
                s_dev.asleep = false;
                return ESP_OK;
            }
            esp_rom_delay_us(200);
        }
        if (attempt == 0) {
            ESP_LOGW(TAG, "wake poll failed (ADIID=0x%04lx) — re-init",
                     (unsigned long)id);
            if (chip_init() != ESP_OK)
                break;                      /* expander/SPI dead — give up */
        }
    }
    ESP_LOGW(TAG, "wake poll failed (ADIID=0x%04lx)", (unsigned long)id);
    s_dev.err_count++;
    return ESP_ERR_TIMEOUT;
}

/* hibernate: unlock sleep key then toggle SEQTRGSLP — SRAM + MMRs are
 * retained (PWRMOD.RAMRETEN), so sequences survive. */
static esp_err_t hibernate(void)
{
    esp_err_t err;
    if ((err = reg_wr(REG_SEQSLPLOCK, SLPKEY_UNLOCK)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_SEQTRGSLP, 0)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_SEQTRGSLP, 1)) != ESP_OK)
        return err;
    s_dev.asleep = true;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* chip configuration                                                  */
/* ------------------------------------------------------------------ */

static esp_err_t init_table(void)
{
    for (size_t i = 0; i < AD5940_INIT_TABLE_LEN; i++) {
        esp_err_t err = reg_wr(AD5940_INIT_TABLE[i].addr,
                               AD5940_INIT_TABLE[i].data);
        if (err != ESP_OK)
            return err;
    }
    return ESP_OK;
}

/* Internal oscillators only — no crystal fitted on fix_v22 (XTALI/XTALO
 * unpopulated).  HFOSC 32 MHz ÷2 → 16 MHz sysclk; LFOSC for WUT/wakeup. */
static esp_err_t clk_cfg(void)
{
    esp_err_t err;
    uint32_t  osc;

    if ((err = reg_wr(REG_OSCKEY, AD5940_KEY_OSCCON)) != ESP_OK)
        return err;
    if ((err = reg_rd(REG_OSCCON, &osc)) != ESP_OK)
        return err;
    osc |= OSCCON_HFOSCEN | OSCCON_LFOSCEN;
    if ((err = reg_wr(REG_OSCCON, osc)) != ESP_OK)
        return err;

    /* poll HFOSCOK|LFOSCOK — TODO(datasheet): spec'd settle time         */
    for (int i = 0; i < 100; i++) {
        if ((err = reg_rd(REG_OSCCON, &osc)) != ESP_OK)
            return err;
        if ((osc & (OSCCON_HFOSCOK | OSCCON_LFOSCOK)) ==
                   (OSCCON_HFOSCOK | OSCCON_LFOSCOK))
            break;
        esp_rom_delay_us(200);
    }
    if ((osc & (OSCCON_HFOSCOK | OSCCON_LFOSCOK)) !=
            (OSCCON_HFOSCOK | OSCCON_LFOSCOK))
        return ESP_ERR_TIMEOUT;

    /* vendor order (UG-1262/UG-2111): program SYSCLKDIV while the part
     * still runs at 16 MHz, touch ADIID so the write latches, THEN do
     * the 16→32 MHz rate switch — writing CLKCON0 after CLK32MHZEN is
     * the documented "write not persisting" failure mode.              */
    if ((err = reg_wr(REG_CLKCON0,
                      SYSCLKDIV_2 | (ADCCLKDIV_2 << 6))) != ESP_OK)
        return err;
    uint32_t id;
    (void)reg_rd(REG_ADIID, &id);            /* latch the divider write  */
    if ((err = reg_wr(REG_HPOSCCON, 1u << 2)) != ESP_OK)   /* CLK32MHZEN */
        return err;
    esp_rom_delay_us(100);
    /* CLKSEL: SYSCLK=HFOSC(0), ADCCLK=HFOSC(0) — reset value already 0  */
    if ((err = reg_wr(REG_CLKSEL, 0)) != ESP_OK)
        return err;
    return ESP_OK;
}

/* GP0 = INTC0 output → IO14;  GP2 = SYNC function (SYNCEXTDEVICE bit2
 * drives it) — GP2 has NO plain-GPIO mux in the function table, so this
 * is the only controllable route to the NIR gate.  TODO(datasheet):
 * confirm SYNC output persistence across hibernate.                    */
static esp_err_t gpio_cfg(void)
{
    esp_err_t err;
    uint32_t  con = (AGPIO_FUNC_INT << 0) | (AGPIO_FUNC_SYNC << 4);
    if ((err = reg_wr(REG_GP0CON, con)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_GP0OEN, AGPIO_PIN0 | AGPIO_PIN2)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_GP0PE, 0)) != ESP_OK)   /* pulls: none            */
        return err;
    if ((err = reg_wr(REG_GP0IEN, 0)) != ESP_OK)
        return err;
    s_dev.sync_shadow = 0;                        /* GP2 low → LED gate off */
    return reg_wr(REG_SYNCEXTDEVICE, s_dev.sync_shadow);
}

static esp_err_t intc_cfg(void)
{
    esp_err_t err;
    /* INTC0 → GP0: fifo threshold + overflow + end-of-seq + custom-int0 */
    uint32_t sel = AFEINTSRC_DATAFIFOTHRESH | AFEINTSRC_DATAFIFOOF |
                   AFEINTSRC_DATAFIFOUF    | AFEINTSRC_ENDSEQ |
                   AFEINTSRC_SEQTIMEOUTERR | AFEINTSRC_CUSTOMINT0;
    if ((err = reg_wr(REG_INTCSEL0, sel)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_INTCSEL1, AFEINTSRC_ALLINT)) != ESP_OK) /* diag */
        return err;
    if ((err = reg_wr(REG_INTCPOL, s_dev.int_pol)) != ESP_OK)
        return err;   /* TODO(datasheet): confirm asserted level        */
    return reg_wr(REG_INTCCLR, AFEINTSRC_ALLINT);
}

/* cmd/data SRAM split + FIFO source; threshold is re-written per-seq.  */
static esp_err_t fifo_cfg(void)
{
    esp_err_t err;
    if ((err = reg_wr(REG_CMDDATACON, CMDDATACON_DEF)) != ESP_OK)
        return err;
    return reg_wr(REG_FIFOCON,
                  FIFOCON_DATAFIFOEN | (FIFOSRC_DFT << 13));
}

/* write a command list into sequencer SRAM at sram_addr (word index)   */
static esp_err_t seq_write(uint32_t sram_addr, const uint32_t *cmd,
                           uint32_t n)
{
    esp_err_t err;
    if ((err = reg_wr(REG_CMDFIFOWADDR, sram_addr)) != ESP_OK)
        return err;
    for (uint32_t i = 0; i < n; i++)
        if ((err = reg_wr(REG_CMDFIFOWRITE, cmd[i])) != ESP_OK)
            return err;
    return ESP_OK;
}

static esp_err_t seq_bind(uint8_t id, uint32_t sram_addr, uint32_t len)
{
    static const uint16_t info_reg[4] = {
        REG_SEQ0INFO, REG_SEQ1INFO, REG_SEQ2INFO, REG_SEQ3INFO
    };
    return reg_wr(info_reg[id], (len << 16) | (sram_addr & 0x7ff));
}

/* ------------------------------------------------------------------ */
/* sequencer programs                                                  */
/* ------------------------------------------------------------------ */

/* excitation FCW: 30-bit on S2/S3, 26-bit on S1 */
static uint32_t exc_fcw(void)
{
    uint32_t bits = (s_dev.chipid == AD5940_CHIPID_S1) ? AD5940_SIN_FCW_BITS_S1
                                                     : AD5940_SIN_FCW_BITS_S2;
    return (uint32_t)lroundf(AD5940_EXC_FREQ_HZ *
                             (float)(1u << bits) / (float)AD5940_SYSCLK_HZ);
}

/* DFT wait clocks for SINC3 source, OSR=2 — vendor WaitClksCalc:
 * ((N+2)*osr + 1) * 20 * (sysclk/adcclk) + margin                        */
static uint32_t dft_wait(void)
{
    uint32_t n   = 4u << AD5940_DFT_NUM;
    uint32_t clk = ((n + 2) * 2 + 1) * 20 * (AD5940_SYSCLK_HZ / AD5940_ADCCLK_HZ);
    return clk + 25;
}

/* SINC2 wait for `count` samples at OSR 533 (vendor formula).           */
static uint32_t sinc2_wait(uint32_t count)
{
    uint32_t d = (count + 1) * 533 + 1;
    return ((d + 2) * 2 + 1) * 20 * (AD5940_SYSCLK_HZ / AD5940_ADCCLK_HZ) + 25;
}

/* common impedance legs — switch quads (RCAL is inlined in builders)  */
static void sw_eda(uint32_t *d, uint32_t *p, uint32_t *n, uint32_t *t)
{
    /* J6 4-pad kelvin: force CE0, sense RE0 (kelvin at driven site),
     * return SE0→TIA, kelvin DE0 → N node.  TODO(hw): verify J6 pad→
     * site mapping against the flex harness before trusting values.    */
    *d = SWD_CE0; *p = SWP_RE0; *n = SWN_DE0LOAD;
    *t = SWT_SE0LOAD | SWT_TRTIA;
}
static void sw_bioz(uint32_t *d, uint32_t *p, uint32_t *n, uint32_t *t)
{
    /* J7 4-wire: force FP=AIN1/SN=AIN2, sense SP=AIN3/FN=AIN0 — note
     * the audit's "cross-strap" comment: P-side senses AIN3 not AIN1.   */
    *d = SWD_AIN1; *p = SWP_AIN3; *n = SWN_AIN0;
    *t = SWT_AIN2 | SWT_TRTIA;
}

/* build one two-leg impedance sequence (RCAL then unknown) into buf;
 * returns command count.                                                */
static uint32_t seq_build_z(uint32_t *q,
                            void (*sw)(uint32_t *, uint32_t *,
                                       uint32_t *, uint32_t *))
{
    uint32_t d, p, n, t, i = 0;
    sw(&d, &p, &n, &t);

    q[i++] = SEQ_WR(REG_DATAFIFOTHRES, FIFO_DFT_WORDS << 16);
    q[i++] = SEQ_WAIT(AD5940_US2CLK(250));
    /* RCAL leg */
    q[i++] = SEQ_WR(REG_DSWFULLCON, SWD_RCAL0);
    q[i++] = SEQ_WR(REG_PSWFULLCON, SWP_RCAL0);
    q[i++] = SEQ_WR(REG_NSWFULLCON, SWN_RCAL1);
    q[i++] = SEQ_WR(REG_TSWFULLCON, SWT_RCAL1 | SWT_TRTIA);
    q[i++] = SEQ_WR(REG_SWCON, SWCON_SWSOURCESEL);
    q[i++] = SEQ_WR(REG_AFECON, AFE_EXCITE);
    q[i++] = SEQ_WAIT(AD5940_US2CLK(200));
    q[i++] = SEQ_WR(REG_AFECON, AFE_MEAS);
    q[i++] = SEQ_WAIT(dft_wait());
    q[i++] = SEQ_WR(REG_AFECON, AFE_EXCITE);
    /* unknown leg */
    q[i++] = SEQ_WR(REG_DSWFULLCON, d);
    q[i++] = SEQ_WR(REG_PSWFULLCON, p);
    q[i++] = SEQ_WR(REG_NSWFULLCON, n);
    q[i++] = SEQ_WR(REG_TSWFULLCON, t);
    q[i++] = SEQ_WR(REG_SWCON, SWCON_SWSOURCESEL);
    q[i++] = SEQ_WR(REG_AFECON, AFE_MEAS);
    q[i++] = SEQ_WAIT(dft_wait());
    /* park: open electrode switches, keep TIA loop closed internally     */
    q[i++] = SEQ_WR(REG_DSWFULLCON, SWD_OPEN);
    q[i++] = SEQ_WR(REG_PSWFULLCON, SWP_PL | SWP_PL2);
    q[i++] = SEQ_WR(REG_NSWFULLCON, SWN_NL | SWN_NL2);
    q[i++] = SEQ_WR(REG_TSWFULLCON, SWT_TRTIA);
    q[i++] = SEQ_WR(REG_SWCON, SWCON_SWSOURCESEL);
    q[i++] = SEQ_WR(REG_AFECON, AFE_IDLE);
    q[i++] = SEQ_STOP();
    return i;
}

static uint32_t seq_build_rcal(uint32_t *q)
{
    uint32_t i = 0;
    q[i++] = SEQ_WR(REG_DATAFIFOTHRES, FIFO_RCAL_WORDS << 16);
    q[i++] = SEQ_WAIT(AD5940_US2CLK(250));
    q[i++] = SEQ_WR(REG_DSWFULLCON, SWD_RCAL0);
    q[i++] = SEQ_WR(REG_PSWFULLCON, SWP_RCAL0);
    q[i++] = SEQ_WR(REG_NSWFULLCON, SWN_RCAL1);
    q[i++] = SEQ_WR(REG_TSWFULLCON, SWT_RCAL1 | SWT_TRTIA);
    q[i++] = SEQ_WR(REG_SWCON, SWCON_SWSOURCESEL);
    q[i++] = SEQ_WR(REG_AFECON, AFE_EXCITE);
    q[i++] = SEQ_WAIT(AD5940_US2CLK(200));
    q[i++] = SEQ_WR(REG_AFECON, AFE_MEAS);
    q[i++] = SEQ_WAIT(dft_wait());
    q[i++] = SEQ_WR(REG_AFECON, AFE_IDLE);
    q[i++] = SEQ_STOP();
    return i;
}

/* sweat amperometric leg — LP potentiostat loop + SINC2 into FIFO.
 * J11 pins: CE=AFE4, RE=AFE3, WE=AIN6.  The LPTIASW0 bit→pin map for
 * these pins is unverified: 0x2C is the vendor "normal" switch set used
 * on the eval board (CE0/RE0/SE0) — TODO(datasheet): re-derive the
 * switch word for AFE3/AFE4/AIN6 before trusting sweat current data.   */
static uint32_t seq_build_sweat(uint32_t *q)
{
    uint32_t i = 0;
    uint32_t vzero6 = (uint32_t)((AD5940_SWEAT_VZERO_MV - 200.0f) /
                                 DAC6BIT_1LSB_MV + 0.5f);
    uint32_t vbias12 = (uint32_t)(AD5940_SWEAT_BIAS_MV / DAC12BIT_1LSB_MV +
                                  (float)vzero6 * 64.0f + 0.5f);

    q[i++] = SEQ_WR(REG_DATAFIFOTHRES, FIFO_SWEAT_WORDS << 16);
    q[i++] = SEQ_WR(REG_FIFOCON,
                    FIFOCON_DATAFIFOEN | (FIFOSRC_SINC2NOTCH << 13));
    q[i++] = SEQ_WAIT(AD5940_US2CLK(250));
    /* LPDAC: unlock, program Vzero/Vbias, latch                          */
    q[i++] = SEQ_WR(REG_LPDACCON0, LPDACCON0_RSTEN);
    q[i++] = SEQ_WR(REG_LPDACDAT0, (vzero6 << 12) | (vbias12 & 0xfff));
    q[i++] = SEQ_WR(REG_LPDACCON0, 0);
    /* route Vbias→PA input, Vzero→LPTIA ref + Vzero pin                  */
    q[i++] = SEQ_WR(REG_LPDACSW0,
                    LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2LPTIA |
                    LPDACSW_VZERO2PIN);
    q[i++] = SEQ_WR(REG_LPTIASW0,
                    LPTIASW_PABIASSEL | LPTIASW_TIABIASSEL |
                    LPTIASW_NORM);                  /* TODO(datasheet)   */
    q[i++] = SEQ_WR(REG_LPTIACON0,
                    LPTIACON0_RF_100K | LPTIACON0_RL_SHORT |
                    LPTIACON0_RTIA_10K);            /* PA+TIA powered    */
    /* ADC mux → LPTIA output; PGA ×4                                     */
    q[i++] = SEQ_WR(REG_ADCCON,
                    (ADCPGA_4 << 16) | (ADCMUXN_LPTIA0_N << 8) |
                    ADCMUXP_LPTIA0_P);
    q[i++] = SEQ_WR(REG_AFECON, AFE_LP_SETTLE);
    q[i++] = SEQ_WAIT(AD5940_US2CLK(2000));   /* LP loop settle          */
    q[i++] = SEQ_WR(REG_AFECON, AFE_LP_MEAS);
    q[i++] = SEQ_WAIT(sinc2_wait(FIFO_SWEAT_WORDS));
    q[i++] = SEQ_WR(REG_AFECON, AFE_IDLE);
    /* restore default FIFO src for the impedance seqs                    */
    q[i++] = SEQ_WR(REG_FIFOCON,
                    FIFOCON_DATAFIFOEN | (FIFOSRC_DFT << 13));
    q[i++] = SEQ_STOP();
    return i;
}

static esp_err_t program_sequences(void)
{
    uint32_t q[32];
    uint32_t  n;
    esp_err_t err;

    /* HS-loop statics — host writes (persist through hibernate)         */
    if ((err = reg_wr(REG_WGCON, WGTYPE_SIN << 1)) != ESP_OK)
        return err;                       /* sine, DAC cal bypass — TODO */
    if ((err = reg_wr(REG_WGFCW, exc_fcw())) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_WGPHASE, 0)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_WGOFFSET, 0)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_WGAMPLITUDE,
                      (uint32_t)(AD5940_EXC_MVPP / WGAMP_800MVPP * 2047.0f))) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_HSDACCON, (7 << 1))) != ESP_OK)  /* update rate */
        return err;                       /* atten off, exc-buf gain ×2   */
    if ((err = reg_wr(REG_HSTIACON, HSTIABIAS_1P1)) != ESP_OK)
        return err;
    /* TODO(datasheet): CTIA field units — 33 ≈ vendor default           */
    if ((err = reg_wr(REG_HSRTIACON,
                      (33 << 5) | AD5940_HSTIA_RTIA)) != ESP_OK)
        return err;
    /* DSP: ADC 800 kSPS, SINC3 OSR2, SINC2 OSR533, notch bypassed        */
    if ((err = reg_wr(REG_ADCFILTERCON,
                      (ADCSINC3OSR_2 << 12) | (ADCSINC2OSR_533 << 8) |
                      (1u << 4) | ADCRATE_800KHZ)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_DFTCON,
                      (DFTSRC_SINC3 << 20) | (AD5940_DFT_NUM << 4) |
                      DFT_HANNING)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_ADCCON,
                      (ADCPGA_1 << 16) | (ADCMUXN_HSTIA_N << 8) |
                      ADCMUXP_HSTIA_P)) != ESP_OK)
        return err;
    if ((err = reg_wr(REG_LPREFBUFCON, 0)) != ESP_OK)  /* LP refs on      */
        return err;

    n = seq_build_z(q, sw_eda);
    if ((err = seq_write(SEQ_SRAM_EDA, q, n)) != ESP_OK)
        return err;
    if ((err = seq_bind(SEQ_ID_0, SEQ_SRAM_EDA, n)) != ESP_OK)
        return err;

    n = seq_build_z(q, sw_bioz);
    if ((err = seq_write(SEQ_SRAM_BIOZ, q, n)) != ESP_OK)
        return err;
    if ((err = seq_bind(SEQ_ID_1, SEQ_SRAM_BIOZ, n)) != ESP_OK)
        return err;

    n = seq_build_sweat(q);
    if ((err = seq_write(SEQ_SRAM_SWEAT, q, n)) != ESP_OK)
        return err;
    if ((err = seq_bind(SEQ_ID_2, SEQ_SRAM_SWEAT, n)) != ESP_OK)
        return err;

    n = seq_build_rcal(q);
    s_dev.seq3_len = n;
    if ((err = seq_write(SEQ_SRAM_RCAL, q, n)) != ESP_OK)
        return err;
    if ((err = seq_bind(SEQ_ID_3, SEQ_SRAM_RCAL, n)) != ESP_OK)
        return err;

    /* IRQ-loopback sequence — triggered from the self-test via SEQ3INFO */
    q[0] = SEQ_INT0();
    q[1] = SEQ_STOP();
    return seq_write(SEQ_SRAM_LOOPBACK, q, 2);
}

/* ------------------------------------------------------------------ */
/* full chip init                                                      */
/* ------------------------------------------------------------------ */
static esp_err_t chip_init(void)
{
    esp_err_t err;
    uint32_t  id = 0;

    if (!s_dev.spi && (err = spi_attach(AD5940_SPI_RUN_HZ)) != ESP_OK)
        return err;

    /* release/reset the AFE — required both at first boot and after
     * sleep() held the expander reset line low (full SRAM/MMR loss)     */
    if ((err = hw_reset_pulse()) != ESP_OK)
        return err;

    /* IO14 ← AD5940 GP0 (INTC0).  Done here (not only via the sensor
     * init op) so self-test/run_rcal paths get the IRQ edge too.        */
    if (!s_dev.isr_attached) {
        gpio_config_t io = {
            .pin_bit_mask = 1ull << VQ_AD5940_GPIO0_GPIO,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = s_dev.int_edge,
        };
        if ((err = gpio_config(&io)) != ESP_OK)
            return err;
        gpio_install_isr_service(0);      /* already-installed → benign   */
        if ((err = gpio_isr_handler_add(VQ_AD5940_GPIO0_GPIO,
                                        gpio0_isr, NULL)) != ESP_OK)
            return err;
        s_dev.isr_attached = true;
    }

    if ((err = init_table()) != ESP_OK)
        return err;
    if ((err = clk_cfg()) != ESP_OK)
        return err;
    if ((err = reg_rd(REG_CHIPID, &id)) != ESP_OK)
        return err;
    s_dev.chipid = (uint16_t)id;
    if (id != AD5940_CHIPID_S1 && id != AD5940_CHIPID_S2 &&
        id != AD5940_CHIPID_S3) {
        ESP_LOGW(TAG, "init: unexpected CHIPID 0x%04lx",
                 (unsigned long)id);
        return ESP_ERR_NOT_FOUND;
    }
    s_dev.present = true;
    if ((err = gpio_cfg()) != ESP_OK)
        return err;
    if ((err = intc_cfg()) != ESP_OK)
        return err;
    if ((err = fifo_cfg()) != ESP_OK)
        return err;
    if ((err = program_sequences()) != ESP_OK)
        return err;
    s_dev.inited = true;
    s_dev.asleep = false;
    ESP_LOGI(TAG, "AD5940 configured (CHIPID=0x%04x)", s_dev.chipid);
    return ESP_OK;
}

/* lock + wake + lazy-init — all public/work paths enter through here   */
static esp_err_t dev_begin(TickType_t wait)
{
    if (!s_dev.mtx)
        return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_dev.mtx, wait) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    if (!s_dev.inited) {
        esp_err_t err = chip_init();
        if (err != ESP_OK) {
            xSemaphoreGive(s_dev.mtx);
            return err;
        }
    }
    esp_err_t err = chip_wake();
    if (err != ESP_OK) {
        xSemaphoreGive(s_dev.mtx);
        return err;
    }
    return ESP_OK;
}

static void dev_end(bool sleep)
{
    /* keep the AFE awake while the NIR gate is asserted — GP2 is a SYNC-
     * function output; whether the level holds through hibernate is
     * TODO(datasheet), and a dropped gate would cut an LED slot short. */
    if (s_dev.sync_shadow & AGPIO_PIN2)
        sleep = false;
    if (sleep)
        (void)hibernate();
    xSemaphoreGive(s_dev.mtx);
}

/* ------------------------------------------------------------------ */
/* ISR + measurement engine                                            */
/* ------------------------------------------------------------------ */

/* ISR contract: latch the timestamp + flag only; the task does all
 * SPI/FIFO work.  edge_us is the acquisition (data-ready) stamp.       */
static void gpio0_isr(void *arg)
{
    (void)arg;
    s_dev.edge_us = esp_timer_get_time();
    s_dev.irq_count++;
    vq_ts_anchor_edge(&s_dev.anchor);
    if (s_dev.evt) {
        BaseType_t hp = pdFALSE;
        xEventGroupSetBitsFromISR(s_dev.evt, EV_IRQ, &hp);
        portYIELD_FROM_ISR(hp);
    }
}

/* esp_timer default dispatch is TASK context — use the non-ISR set.
 * Never does SPI work here; the driver task owns the device.           */
static void tick_cb(void *arg)
{
    (void)arg;
    if (s_dev.evt)
        xEventGroupSetBits(s_dev.evt, EV_TICK);
}

/* drain whatever is in the data FIFO into buf (≤ cap), return count    */
static esp_err_t fifo_drain(uint32_t *buf, uint32_t cap, uint32_t *got)
{
    uint32_t n = 0;
    esp_err_t err = fifo_count(&n);
    if (err != ESP_OK)
        return err;
    uint32_t use = (n < cap) ? n : cap;
    if (use && (err = fifo_rd(buf, use)) != ESP_OK)
        return err;
    /* drop any remainder (stale/unexpected words)                        */
    while (n > use) {
        uint32_t junk;
        if ((err = fifo_rd(&junk, 1)) != ESP_OK)
            return err;
        n--;
    }
    *got = use;
    return reg_wr(REG_INTCCLR, AFEINTSRC_ALLINT);
}

static int32_t dft18(uint32_t w)
{
    int32_t v = (int32_t)(w & 0x3ffff);
    if (v & 0x20000)
        v -= 0x40000;
    return v;
}

static float dft_mag(uint32_t re_w, uint32_t im_w, float *phase_deg)
{
    float re = (float)dft18(re_w);
    float im = (float)dft18(im_w);
    if (phase_deg)
        *phase_deg = atan2f(-im, re) * 57.2957795f;
    return sqrtf(re * re + im * im);
}

/* run one armed sequence: trigger → wait IRQ → drain.                   */
static esp_err_t run_seq(uint8_t seqid, uint32_t *buf, uint32_t expect,
                         uint32_t *got, int64_t *t_us)
{
    esp_err_t err;

    /* flush stale data, clear flags so the next GP0 edge is clean       */
    uint32_t junk[8], nj;
    if ((err = fifo_drain(junk, sizeof(junk) / 4, &nj)) != ESP_OK)
        return err;
    xEventGroupClearBits(s_dev.evt, EV_IRQ);
    reg_wr(REG_INTCCLR, AFEINTSRC_ALLINT);

    if ((err = reg_wr(REG_SEQCON, 1)) != ESP_OK)          /* SEQEN        */
        return err;
    if ((err = reg_wr(REG_TRIGSEQ, 1u << seqid)) != ESP_OK)
        return err;

    EventBits_t b = xEventGroupWaitBits(s_dev.evt, EV_IRQ,
                                        pdTRUE, pdFALSE,
                                        pdMS_TO_TICKS(80));
    if (!(b & EV_IRQ)) {
        /* halt the (possibly wedged) sequencer so the next trigger
         * starts clean; also clears any half-latched GP0 state          */
        reg_wr(REG_SEQCON, 0);
        reg_wr(REG_INTCCLR, AFEINTSRC_ALLINT);
        s_dev.err_count++;
        return ESP_ERR_TIMEOUT;
    }
    /* data-ready edge = acquisition stamp.  64-bit volatile read on a
     * 32-bit core can tear — double-read until stable.                  */
    int64_t t0, t1;
    do { t0 = s_dev.edge_us; t1 = s_dev.edge_us; } while (t0 != t1);
    *t_us = t0;
    if (s_dev.err_count)
        s_dev.err_count--;   /* transient errors decay on success        */
    return fifo_drain(buf, expect, got);
}

static void emit(vq_sensor_t *s, vq_channel_id_t ch, float val,
                 uint8_t flags, int64_t t_us)
{
    vq_sample_t smp = {
        .channel = ch,
        .t_us  = t_us,
        .value = val,
        .flags = flags,
    };
    if (s->sink)
        s->sink(s->sink_ctx, &smp, 1);
    s->sample_count++;
    s->last_t_us = t_us;
}

/* run a two-leg impedance seq → ohm for the unknown leg                 */
static esp_err_t meas_z(uint8_t seqid, float *ohm, uint8_t *flags,
                        int64_t *t_us)
{
    uint32_t w[FIFO_DFT_WORDS], got = 0;
    esp_err_t err = run_seq(seqid, w, FIFO_DFT_WORDS, &got, t_us);
    if (err != ESP_OK)
        return err;
    if (got < FIFO_DFT_WORDS)
        return ESP_ERR_INVALID_SIZE;

    float ph_r, ph_z;
    float mag_r = dft_mag(w[0], w[1], &ph_r);
    float mag_z = dft_mag(w[2], w[3], &ph_z);
    *flags = 0;

    if (mag_z < 1.0f || mag_r < 1.0f) {
        *flags |= VQ_SAMPLE_F_QUALITY_BAD;
        *ohm = 0.0f;
        return ESP_OK;
    }
    /* saturation sanity — DFT full-scale ≈ 2^17                          */
    if (mag_r > 125000.0f || mag_z > 125000.0f)
        *flags |= VQ_SAMPLE_F_QUALITY_BAD;

    float z_meas = AD5940_RCAL_OHM * mag_r / mag_z;
    float rep    = z_meas - AD5940_SERIES_R_OHM;
    if (rep < 0.0f) {
        rep = 0.0f;
        *flags |= VQ_SAMPLE_F_QUALITY_BAD;
    }
    *ohm = rep;
    (void)ph_r; (void)ph_z;   /* phase available here if needed later    */
    return ESP_OK;
}

static esp_err_t meas_sweat(float *ua_est, uint8_t *flags, int64_t *t_us)
{
    uint32_t w[FIFO_SWEAT_WORDS], got = 0;
    esp_err_t err = run_seq(SEQ_ID_2, w, FIFO_SWEAT_WORDS, &got, t_us);
    if (err != ESP_OK)
        return err;
    if (got < FIFO_SWEAT_WORDS)
        return ESP_ERR_INVALID_SIZE;

    int64_t acc = 0;
    for (int i = 0; i < FIFO_SWEAT_WORDS; i++)
        acc += dft18(w[i]);            /* SINC2 words are also 18-bit?    */
    /* TODO(datasheet): SINC2 code→current scaling (ADC ref, PGA, TIA
     * gain).  Until confirmed we emit the averaged raw SINC2 code —
     * honest engineering units, not fake mmol/L.  The mmol/L mapping is
     * a research-calibration problem, never applied here.               */
    *ua_est = (float)acc / FIFO_SWEAT_WORDS;
    *flags  = 0;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* driver task                                                         */
/* ------------------------------------------------------------------ */

static void ad5940_task(void *arg)
{
    (void)arg;
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_dev.evt, EV_TICK | EV_IRQ,
                                               pdTRUE, pdFALSE,
                                               portMAX_DELAY);
        if (bits & EV_IRQ) {
            /* stray/interleaved assertion — service FIFO so GP0 clears  */
            if (xSemaphoreTake(s_dev.mtx, pdMS_TO_TICKS(10)) == pdTRUE) {
                uint32_t junk[8], n;
                fifo_drain(junk, sizeof(junk) / 4, &n);
                xSemaphoreGive(s_dev.mtx);
            }
        }
        if (!(bits & EV_TICK) || !s_dev.streaming)
            continue;

        if (dev_begin(pdMS_TO_TICKS(200)) != ESP_OK)
            continue;

        int64_t now = esp_timer_get_time();
        if (s_dev.en_eda && now >= s_dev.eda_due) {
            float ohm; uint8_t fl; int64_t t;
            s_dev.eda_due = now + (int64_t)(1e6f / AD5940_EDA_RATE_HZ);
            if (meas_z(SEQ_ID_0, &ohm, &fl, &t) == ESP_OK) {
                emit(s_reg_eda ? s_reg_eda : &s_sens_eda,
                     VQ_CH_EDA, ohm, fl, t);
                s_dev.meas_ok++;
            }
            if (meas_z(SEQ_ID_1, &ohm, &fl, &t) == ESP_OK) {
                emit(s_reg_eda ? s_reg_eda : &s_sens_eda,
                     VQ_CH_BIOZ, ohm, fl, t);
                s_dev.meas_ok++;
            }
        }
        if (s_dev.en_sweat && now >= s_dev.sweat_due) {
            float ua; uint8_t fl; int64_t t;
            s_dev.sweat_due = now + (int64_t)(1e6f / AD5940_SWEAT_RATE_HZ);
            if (meas_sweat(&ua, &fl, &t) == ESP_OK) {
                emit(s_reg_sweat ? s_reg_sweat : &s_sens_sweat,
                     VQ_CH_SWEAT_LACTATE, ua, fl, t);
                s_dev.meas_ok++;
            }
        }
        dev_end(true);   /* hibernate until next tick */
    }
}

/* ------------------------------------------------------------------ */
/* vq_sensor ops — shared by both descriptors                          */
/* ------------------------------------------------------------------ */

/* probe contract says "must not disturb" — a reset pulse is the only
 * reliable way to reach a deterministic CHIPID here because expander P2
 * may hold the AFE in reset from a previous sleep().  Safe: nothing else
 * on this board shares the reset line and the device isn't running yet. */
static vq_err_t ad5940_probe(vq_sensor_t *s)
{
    esp_err_t err;
    uint32_t  id = 0;
    (void)s;

    if (!s_dev.spi && (err = spi_attach(AD5940_SPI_RUN_HZ)) != ESP_OK)
        return err;
    if ((err = hw_reset_pulse()) != ESP_OK)
        return err;
    if ((err = reg_rd(REG_CHIPID, &id)) != ESP_OK)
        return err;

    s_dev.chipid  = (uint16_t)id;
    s_dev.present = (id == AD5940_CHIPID_S1 || id == AD5940_CHIPID_S2 ||
                     id == AD5940_CHIPID_S3);
    if (!s_dev.present) {
        ESP_LOGW(TAG, "probe: unexpected CHIPID 0x%04lx "
                      "(expect 0x5500/0x5501/0x5502)", (unsigned long)id);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "probe ok: CHIPID 0x%04x", s_dev.chipid);
    return ESP_OK;
}

static vq_err_t ad5940_init(vq_sensor_t *s)
{
    (void)s;
    if (!s_dev.present)
        return ESP_ERR_NOT_FOUND;
    if (s_dev.inited)
        return ESP_OK;

    if (!s_dev.mtx)
        s_dev.mtx = xSemaphoreCreateMutex();
    if (!s_dev.evt)
        s_dev.evt = xEventGroupCreate();
    if (!s_dev.mtx || !s_dev.evt)
        return ESP_ERR_NO_MEM;

    esp_err_t err = chip_init();       /* includes IO14/ISR setup        */
    if (err != ESP_OK)
        return err;

    if (!s_dev.tick) {
        esp_timer_create_args_t tc = {
            .callback = tick_cb,
            .name     = "ad5940",
        };
        if ((err = esp_timer_create(&tc, &s_dev.tick)) != ESP_OK)
            return err;
    }
    if (!s_dev.task) {
        if (xTaskCreate(ad5940_task, "ad5940", 4096, NULL, 5,
                        &s_dev.task) != pdTRUE)
            return ESP_FAIL;
    }
    return hibernate();
}

static void tick_reschedule(void)
{
    if (!s_dev.tick)
        return;
    esp_timer_stop(s_dev.tick);
    if (!s_dev.en_eda && !s_dev.en_sweat)
        return;
    uint32_t period_us = s_dev.en_eda
        ? (uint32_t)(1e6f / AD5940_EDA_RATE_HZ / 10.0f)  /* 10× overscan */
        : (uint32_t)(1e6f / AD5940_SWEAT_RATE_HZ / 10.0f);
    if (period_us < 10000)
        period_us = 10000;
    esp_timer_start_periodic(s_dev.tick, period_us);
}

static vq_err_t ad5940_start(vq_sensor_t *s)
{
    if (!s->priv)
        return ESP_ERR_INVALID_STATE;
    if (!s_dev.inited)
        return ESP_ERR_INVALID_STATE;
    int64_t now = esp_timer_get_time();
    if (*(int *)s->priv == SUB_EDA) {
        s_dev.en_eda  = true;
        s_dev.eda_due = now + 100000;   /* first sample ~100 ms out       */
    } else {
        s_dev.en_sweat  = true;
        s_dev.sweat_due = now + 1000000;
    }
    s_dev.streaming = true;
    tick_reschedule();
    return ESP_OK;
}

static vq_err_t ad5940_stop(vq_sensor_t *s)
{
    if (!s->priv)
        return ESP_ERR_INVALID_STATE;
    if (*(int *)s->priv == SUB_EDA)
        s_dev.en_eda = false;
    else
        s_dev.en_sweat = false;
    if (!s_dev.en_eda && !s_dev.en_sweat) {
        s_dev.streaming = false;
        if (s_dev.tick)
            esp_timer_stop(s_dev.tick);
    } else {
        tick_reschedule();
    }
    return ESP_OK;
}

static vq_err_t ad5940_sleep(vq_sensor_t *s)
{
    ad5940_stop(s);
    bool locked = (s_dev.mtx &&
                   xSemaphoreTake(s_dev.mtx, pdMS_TO_TICKS(200)) == pdTRUE);
    if (s_dev.inited)                     /* AFE hibernate — best effort */
        (void)hibernate();
    if (locked)
        xSemaphoreGive(s_dev.mtx);
    /* deepest sleep: hold the AFE in reset — SRAM + MMRs are lost, the
     * next init()/dev_begin() fully reprograms the device              */
    (void)vq_exp_set(VQ_EXP_AD5940_RST, 0);
    s_dev.asleep  = true;
    s_dev.inited  = false;
    s_dev.present = false;               /* require re-probe             */
    return ESP_OK;
}

static bool ad5940_healthy(vq_sensor_t *s)
{
    (void)s;
    return s_dev.present && s_dev.inited && s_dev.err_count < 8;
}

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

esp_err_t vq_ad5940_register(void)
{
    if (s_registered)
        return ESP_OK;
    s_registered = true;
    s_dev.int_pol  = 0;                  /* INTCPOL=0: asserts LOW (Table
                                        * 137: 0=negative-edge output)   */
    s_dev.int_edge = GPIO_INTR_NEGEDGE;

    static const vq_channel_id_t eda_ch[]   = { VQ_CH_EDA, VQ_CH_BIOZ };
    static const vq_channel_id_t sweat_ch[] = { VQ_CH_SWEAT_LACTATE };

    /* ops structs carry name/channels/rate/required + fn pointers; the
     * vq_sensor_t instances carry live sink/priv/counters.  Both share
     * the single physical device state in s_dev.                        */
    static vq_sensor_ops_t ops_eda = {
        .name       = "ad5940.eda",
        .probe      = ad5940_probe,
        .init       = ad5940_init,
        .start      = ad5940_start,
        .stop       = ad5940_stop,
        .sleep      = ad5940_sleep,
        .healthy    = ad5940_healthy,
        .channels   = eda_ch,
        .n_channels = 2,
        .rate_hz    = AD5940_EDA_RATE_HZ,
        .required   = true,            /* core sensing channel            */
    };
    static vq_sensor_ops_t ops_sweat = {
        .name       = "ad5940.sweat",
        /* engineering electrode-current signal only — NOT a clinical or
         * validated lactate measurement (required=false).               */
        .probe      = ad5940_probe,
        .init       = ad5940_init,
        .start      = ad5940_start,
        .stop       = ad5940_stop,
        .sleep      = ad5940_sleep,
        .healthy    = ad5940_healthy,
        .channels   = sweat_ch,
        .n_channels = 1,
        .rate_hz    = AD5940_SWEAT_RATE_HZ,
        .required   = false,
    };

    memset(&s_sens_eda, 0, sizeof(s_sens_eda));
    memset(&s_sens_sweat, 0, sizeof(s_sens_sweat));
    s_sens_eda.ops   = &ops_eda;
    s_sens_eda.priv  = &s_priv_eda;
    s_sens_sweat.ops   = &ops_sweat;
    s_sens_sweat.priv  = &s_priv_sweat;

    esp_err_t err = vq_sensor_register(&s_sens_eda);
    if (err != ESP_OK)
        return err;
    s_reg_eda = vq_sensor_find(s_sens_eda.ops->name);
    if ((err = vq_sensor_register(&s_sens_sweat)) != ESP_OK)
        return err;
    s_reg_sweat = vq_sensor_find(s_sens_sweat.ops->name);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* NIR gate                                                            */
/* ------------------------------------------------------------------ */

/* raw gate primitive — NOT for unscheduled use (see header).  Fail-safe:
 * if the bus is busy with a measurement the request returns
 * ESP_ERR_TIMEOUT instead of blocking the LED scheduler's timer task;
 * the gate stays unchanged (LED-safe: a missed ON keeps LED off).       */
esp_err_t vq_ad5940_nir_gate(bool on)
{
    if (!s_dev.mtx)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = dev_begin(pdMS_TO_TICKS(25));
    if (err != ESP_OK)
        return err;

    if (on)
        s_dev.sync_shadow |= AGPIO_PIN2;
    else
        s_dev.sync_shadow &= ~AGPIO_PIN2;
    err = reg_wr(REG_SYNCEXTDEVICE, s_dev.sync_shadow);

    /* keep chip awake while gate asserted (LED on), else back to sleep  */
    dev_end(!on);
    return err;
}

static void nir_cb_on(void *ctx)  { (void)ctx; vq_ad5940_nir_gate(true);  }
static void nir_cb_off(void *ctx) { (void)ctx; vq_ad5940_nir_gate(false); }

esp_err_t vq_ad5940_nir_submit(uint32_t on_us, uint32_t period_us,
                               int64_t *slot_start_us)
{
    vq_led_request_t req = {
        .chain     = VQ_LED_NIR730,
        .pulse_mA  = 200,        /* ~200 mA class → scheduler duty rules */
        .on_us     = on_us,
        .period_us = period_us,
        .priority  = 0,
    };
    return vq_led_submit(&req, nir_cb_on, nir_cb_off, NULL, slot_start_us);
}

/* ------------------------------------------------------------------ */
/* RCAL calibration                                                    */
/* ------------------------------------------------------------------ */

esp_err_t vq_ad5940_run_rcal(float *ohm)
{
    if (!ohm)
        return ESP_ERR_INVALID_ARG;
    if (!s_dev.mtx)
        return ESP_ERR_INVALID_STATE;

    esp_err_t err = dev_begin(pdMS_TO_TICKS(300));
    if (err != ESP_OK)
        return err;

    uint32_t w[FIFO_RCAL_WORDS], got = 0;
    int64_t  t;
    err = run_seq(SEQ_ID_3, w, FIFO_RCAL_WORDS, &got, &t);
    if (err == ESP_OK && got >= FIFO_RCAL_WORDS) {
        float mag = dft_mag(w[0], w[1], NULL);
        if (mag < 1.0f) {
            err = ESP_ERR_INVALID_RESPONSE;
        } else if (!s_dev.rcal_ref_valid) {
            /* first good RCAL pass anchors the ratio reference           */
            s_dev.rcal_mag_ref   = mag;
            s_dev.rcal_ref_valid = true;
            *ohm = AD5940_RCAL_OHM;      /* self-anchored → nominal       */
        } else {
            *ohm = AD5940_RCAL_OHM * mag / s_dev.rcal_mag_ref;
            /* refresh reference slowly to track drift                    */
            s_dev.rcal_mag_ref = 0.9f * s_dev.rcal_mag_ref + 0.1f * mag;
        }
        ESP_LOGD(TAG, "rcal mag=%.0f → %.1f ohm", (double)mag, (double)*ohm);
    }
    dev_end(true);
    return err;
}

/* ------------------------------------------------------------------ */
/* self-test                                                           */
/* ------------------------------------------------------------------ */

/* trigger the loopback sequence (SEQ_INT0 → CUSTOMINT0 → GP0) and watch
 * IO14.  Tries both polarities — the working one is adopted.            */
static esp_err_t loopback_test(char *o, size_t olen)
{
    esp_err_t err;
    int64_t   t0;

    if ((err = chip_wake()) != ESP_OK)    /* may be hibernated           */
        return err;

    /* Datasheet Table 137: INTCPOL=0 → negative-edge output (asserts
     * LOW); INTCPOL=1 → positive-edge output (asserts HIGH). Try the
     * datasheet default first, then the alternative.                    */
    for (int pol = 0; pol <= 1; pol++) {
        s_dev.int_pol  = pol;
        s_dev.int_edge = pol ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE;
        gpio_set_intr_type(VQ_AD5940_GPIO0_GPIO, s_dev.int_edge);
        if ((err = reg_wr(REG_INTCPOL, pol)) != ESP_OK)
            return err;
        reg_wr(REG_INTCCLR, AFEINTSRC_ALLINT);

        /* the pin must sit at the deasserted level BEFORE triggering —
         * otherwise the asserted-level poll below false-passes at dt≈0 */
        const int idle = pol ? 0 : 1, want = pol ? 1 : 0;
        esp_rom_delay_us(200);
        if (gpio_get_level(VQ_AD5940_GPIO0_GPIO) != idle)
            continue;

        /* rebind SEQ3 to the 2-cmd loopback program                      */
        if ((err = seq_bind(SEQ_ID_3, SEQ_SRAM_LOOPBACK, 2)) != ESP_OK)
            return err;
        t0 = esp_timer_get_time();
        if ((err = reg_wr(REG_SEQCON, 1)) != ESP_OK)
            return err;
        if ((err = reg_wr(REG_TRIGSEQ, 1u << SEQ_ID_3)) != ESP_OK)
            return err;

        /* wait for asserted level */
        int hit = 0;
        int64_t  dt = 0;
        for (int i = 0; i < 500; i++) {
            if (gpio_get_level(VQ_AD5940_GPIO0_GPIO) == want) {
                hit = 1;
                dt  = esp_timer_get_time() - t0;
                break;
            }
            esp_rom_delay_us(100);
        }
        if (hit) {
            reg_wr(REG_INTCCLR, AFEINTSRC_ALLINT);
            esp_rom_delay_us(500);
            int de = (gpio_get_level(VQ_AD5940_GPIO0_GPIO) == idle);
            seq_bind(SEQ_ID_3, SEQ_SRAM_RCAL, s_dev.seq3_len);
            snprintf(o, olen, "irq  : gp0→io14 %s, pol=%s, %lld us%s\n",
                     hit ? "PASS" : "FAIL", pol ? "high" : "low",
                     (long long)dt, de ? "" : " (deassert?)");
            ESP_LOGI(TAG, "loopback OK pol=%d %lldus", pol, (long long)dt);
            return ESP_OK;
        }
    }
    /* both polarities failed — restore defaults and rebind SEQ3         */
    s_dev.int_pol  = 0;
    s_dev.int_edge = GPIO_INTR_NEGEDGE;
    gpio_set_intr_type(VQ_AD5940_GPIO0_GPIO, s_dev.int_edge);
    reg_wr(REG_INTCPOL, 0);
    seq_bind(SEQ_ID_3, SEQ_SRAM_RCAL, s_dev.seq3_len);
    snprintf(o, olen, "irq  : gp0→io14 FAIL (no edge either polarity)\n");
    return ESP_ERR_NOT_FOUND;
}

static size_t ap(char *buf, size_t len, size_t off, const char *fmt, ...)
{
    if (off >= len)
        return off;
    va_list ap_;
    va_start(ap_, fmt);
    off += vsnprintf(buf + off, len - off, fmt, ap_);
    va_end(ap_);
    return off;
}

esp_err_t vq_ad5940_selftest(char *buf, size_t len)
{
    if (!buf || len < 64)
        return ESP_ERR_INVALID_ARG;
    buf[0] = 0;
    if (!s_dev.mtx)
        s_dev.mtx = xSemaphoreCreateMutex();
    if (!s_dev.evt)
        s_dev.evt = xEventGroupCreate();
    if (!s_dev.mtx || !s_dev.evt)
        return ESP_ERR_NO_MEM;
    /* defaults when self-test runs before vq_ad5940_register()           */
    if (s_dev.int_edge == GPIO_INTR_DISABLE) {
        s_dev.int_pol  = 0;
        s_dev.int_edge = GPIO_INTR_NEGEDGE;
    }
    size_t off = 0;
    off = ap(buf, len, off, "AD5940 self-test (research/engineering only)\n");

    /* --- CHIPID @ 1 MHz --------------------------------------------- */
    if (xSemaphoreTake(s_dev.mtx, pdMS_TO_TICKS(500)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    spi_attach(AD5940_SPI_PROBE_HZ);
    hw_reset_pulse();
    uint32_t id1 = 0;
    esp_err_t e1 = reg_rd(REG_CHIPID, &id1);
    bool ok1 = (e1 == ESP_OK) && (id1 == AD5940_CHIPID_S1 ||
                                 id1 == AD5940_CHIPID_S2 ||
                                 id1 == AD5940_CHIPID_S3);
    off = ap(buf, len, off, "chipid @1MHz : 0x%04lx %s\n",
             (unsigned long)id1, ok1 ? "PASS" : "FAIL");

    /* --- CHIPID @ 8 MHz --------------------------------------------- */
    spi_attach(AD5940_SPI_RUN_HZ);
    hw_reset_pulse();
    uint32_t id8 = 0;
    esp_err_t e8 = reg_rd(REG_CHIPID, &id8);
    bool ok8 = (e8 == ESP_OK) && (id8 == AD5940_CHIPID_S1 ||
                                 id8 == AD5940_CHIPID_S2 ||
                                 id8 == AD5940_CHIPID_S3);
    off = ap(buf, len, off, "chipid @8MHz : 0x%04lx %s\n",
             (unsigned long)id8, ok8 ? "PASS" : "FAIL");
    s_dev.chipid  = (uint16_t)id8;
    s_dev.present = ok8;

    if (!ok8) {
        off = ap(buf, len, off, "fatal: no AD5940 — check U7/SPI2/P2\n");
        xSemaphoreGive(s_dev.mtx);
        return ESP_ERR_NOT_FOUND;
    }

    /* --- full init (clocks/gpio/intc/fifo/seqs) ----------------------- */
    esp_err_t err = chip_init();
    off = ap(buf, len, off, "init         : %s\n",
             err == ESP_OK ? "PASS" : "FAIL");
    if (err != ESP_OK) {
        xSemaphoreGive(s_dev.mtx);
        return err;
    }

    /* --- RCAL --------------------------------------------------------- */
    xSemaphoreGive(s_dev.mtx);        /* run_rcal takes its own lock     */
    float ohm = 0;
    esp_err_t er = vq_ad5940_run_rcal(&ohm);
    if (xSemaphoreTake(s_dev.mtx, pdMS_TO_TICKS(500)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    off = ap(buf, len, off, "rcal         : %s %.1f ohm (nom %.0f, ref %s)\n",
             er == ESP_OK ? "PASS" : "FAIL", (double)ohm,
             (double)AD5940_RCAL_OHM,
             s_dev.rcal_ref_valid ? "set" : "unset");

    chip_wake();                          /* run_rcal left it hibernated */
    uint32_t osc2 = 0;
    reg_rd(REG_OSCCON, &osc2);
    off = ap(buf, len, off, "osc          : hfosc%s lfosc%s (int, no xtal)\n",
             (osc2 & OSCCON_HFOSCOK) ? "+ok" : "?",
             (osc2 & OSCCON_LFOSCOK) ? "+ok" : "?");

    /* --- GPIO0 → IO14 loopback --------------------------------------- */
    char ln[96];
    esp_err_t el = loopback_test(ln, sizeof(ln));
    (void)el;
    off = ap(buf, len, off, "%s", ln);

    /* --- summary ------------------------------------------------------ */
    off = ap(buf, len, off,
             "meas_ok=%lu err=%lu irq=%lu | sweat.lactate=RESEARCH-ONLY\n",
             (unsigned long)s_dev.meas_ok, (unsigned long)s_dev.err_count,
             (unsigned long)s_dev.irq_count);

    dev_end(true);
    return ESP_OK;
}
