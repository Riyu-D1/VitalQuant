/* vq_drv_ads1292.c — ADS1292R two-channel biopotential AFE driver.
 *
 * Channels: ch1 -> ecg.ii (lead II electrode pair on IN1P/IN1N, gain 6),
 *           ch2 -> resp.waveform (ADS1292R respiration demodulator output
 *           routed onto the powered-down channel-2 slot). 500 sps,
 *           continuous conversion, RDATAC readout on ~DRDY.
 *
 * ----------------------------------------------------------------------------
 * Clocking / decode-timing strategy (SBAS502 §8.5.1.2, §8.5.2.10)
 * ----------------------------------------------------------------------------
 * The ADS1292R internal oscillator is 512 kHz, so 4*tCLK = 7.8125 us is the
 * minimum time the SPI state machine needs to decode each COMMAND byte
 * (tSDECODE) and the maximum SCLK for register reads/writes is 2*fCLK
 * (1.024 MHz). Two spi_bus handles are therefore used on the same CS (IO10):
 *
 *   spi_cmd  @ 1 MHz  — all opcodes + WREG/RREG frames. At 1 MHz one byte
 *                       takes 8 us >= 4*tCLK, so contiguous SCLK satisfies
 *                       tSDECODE without fabricated inter-byte gaps, and
 *                       cs_pre/cs_post (16 us) space out separate command
 *                       frames far beyond the required 4*tCLK.
 *   spi_data @ 4 MHz  — pure RDATAC payload clocking (9-byte frames, DIN
 *                       held at 0). Shifting conversion data performs no
 *                       command decode, so tSDECODE does not apply inside
 *                       the frame. Board cap is 4 MHz (VQ_SPI_ADS1292_HZ).
 *
 * If the bus layer refuses a second handle on the same CS, spi_data falls
 * back to spi_cmd (72 us/frame — still trivial at 500 sps).
 *
 * Frame format (RDATAC): 24-bit status + 2 x 24-bit channel, MSB first.
 * Status word = {4'b1100, LOFF_STAT[4:0], GPIO[1:0], 13'b0}; the 1100
 * nibble is used as a per-frame integrity check.
 *
 * Timestamp contract (vq_types.h): the ~DRDY falling edge is latched in an
 * IRAM ISR via vq_ts_anchor_edge(); each edge is one device sample (this
 * part has no FIFO/sample counter), so the driver keeps a software dev_seq
 * and writes anchor.seq_anchor at every edge. Frames drained late are
 * back-stamped through vq_ts_anchor_stamp() and flagged
 * VQ_SAMPLE_F_ESTIMATED_TS.
 *
 * NOTE: the ADS1292R register map has NO CONFIG3 — RLD reference selection
 * lives in RESP2.RLDREF_INT and RLD_SEnSE in RLD_SENS (this differs from
 * the 4/6/8-channel ADS129x map).
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"

#include "vq_drv_ads1292.h"
#include "vq_sensor.h"
#include "board_pins.h"
#include "spi_bus.h"
#include "expander.h"

static const char *TAG = "ads1292r";

/* ------------------------------ SPI opcodes ------------------------------ */
#define ADS_OP_WAKEUP   0x02
#define ADS_OP_STANDBY  0x04
#define ADS_OP_RESET    0x06
#define ADS_OP_START    0x08    /* only conversion trigger — START pin is GND */
#define ADS_OP_STOP     0x0A
#define ADS_OP_RDATAC   0x10
#define ADS_OP_SDATAC   0x11
#define ADS_OP_RDATA    0x12
#define ADS_OP_RREG     0x20    /* | addr[4:0] */
#define ADS_OP_WREG     0x40    /* | addr[4:0] */

/* ------------------------------ Registers -------------------------------- */
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

/* CONFIG1: bit7 SINGLE_SHOT(0=continuous), bits[2:0] DR. 010 = 500 sps at
 * fCLK = 512 kHz (this is also the reset value 0x02). */
#define ADS_CFG1_RUN      0x02

/* CONFIG2: bit7 must=1, bit6 PDB_LOFF_COMP (1=lead-off comparator on),
 * bit5 PDB_REFBUF (1=internal reference buffer on), bit4 VREF_4V (0=2.42 V),
 * bit3 CLK_EN, bit1 INT_TEST, bit0 TEST_FREQ. */
#define ADS_CFG2_RUN      0xE0    /* 0x80 | LOFF_COMP | REFBUF            */
#define ADS_CFG2_TEST     0xE3    /* RUN | INT_TEST | TEST_FREQ           */

/* LOFF: bits[7:5] COMP_TH (000 = 95%/5%), bit4 must=1, bits[3:2] ILEAD_OFF
 * (00 = 6 nA), bit0 FLEAD_OFF (0 = DC lead-off detect).
 * TODO(datasheet): confirm 6 nA source vs electrode impedance budget. */
#define ADS_LOFF_RUN      0x10

/* CHnSET: bit7 PDn, bits[6:4] GAIN (000=6x ... 110=12x), bits[3:0] MUX
 * (0000 normal electrodes, 0001 input shorted, 0101 internal test signal). */
#define ADS_CHSET_GAIN6   0x00
#define ADS_CHSET_PD      0x80
#define ADS_MUX_NORMAL    0x00
#define ADS_MUX_SHORT     0x01
#define ADS_MUX_TEST      0x05

/* CH1 = lead II electrodes, gain 6, normal input. */
#define ADS_CH1_RUN       (ADS_CHSET_GAIN6 | ADS_MUX_NORMAL)          /* 0x00 */
/* CH2 powered down: the respiration demodulator output is then routed onto
 * the channel-2 output slot (SBAS502 respiration section). MUX=0001 mirrors
 * the TI EVM-derived configuration.
 * TODO(datasheet): confirm CH2 PD+MUX encoding vs resp-demod routing. */
#define ADS_CH2_RUN       (ADS_CHSET_PD | ADS_CHSET_GAIN6 | ADS_MUX_SHORT) /* 0x81 */
#define ADS_CH_TEST       (ADS_CHSET_GAIN6 | ADS_MUX_TEST)            /* 0x05 */

/* RLD_SENS 0x00 — no RLD drive/sense in the 2-electrode lead-II harness. */
#define ADS_RLD_SENS_RUN  0x00

/* LOFF_SENS: bit3 LOFF2N, bit2 LOFF2P, bit1 LOFF1N, bit0 LOFF1P (bits[7:6]=0,
 * bits[5:4] = FLIP current direction). Enable DC lead-off sense on the CH1
 * electrode pair only; CH2 electrodes are unused (resp shares the CH1 pair).
 * TODO(datasheet): verify FLIP polarity once electrode drive direction is
 * confirmed on the bench. */
#define ADS_LOFF_SENS_RUN 0x03

/* RESP1 (ADS1292R only): bit7 RESP_DEMOD_EN1, bit6 RESP_MOD_EN,
 * bits[5:2] RESP_PH (demod phase; 1010 = 112.5 deg @ 32 kHz — TI EVM
 * default), bit1 must=1, bit0 RESP_CTRL (0 = internal resp w/ internal clk).
 * TODO(datasheet): RESP_PHASE must be bench-tuned to the harness impedance
 * angle; EVM default 0xEA used as a conservative starting point. */
#define ADS_RESP1_RUN     0xEA

/* RESP2: bit7 CALIB_ON(0), bits[6:3]=0, bit2 RESP_FREQ (0 = 32 kHz),
 * bit1 RLDREF_INT (1 = internal (AVDD-AVSS)/2), bit0 must=1. */
#define ADS_RESP2_RUN     0x03

/* GPIO: bits[3:2] GPIOC (1 = input), bits[1:0] GPIOD. Both pins configured
 * as inputs — safest for unconnected pads; GPIO is unavailable in some
 * respiration clock modes anyway. */
#define ADS_GPIO_RUN      0x0C

/* ID register: xxx1_00xx, REV_ID in bits[7:5]+[1:0]. ADS1292R reads
 * ~0x73/0x74 depending on die revision; accept the whole 0x7x nibble and
 * log the actual value. */
static inline bool ads_id_ok(uint8_t id)
{
    return id != 0x00 && id != 0xFF && (id & 0xF0) == 0x70;
}

/* ------------------------------ Scaling ---------------------------------- */
/* LSB weight = VREF / (GAIN * (2^23 - 1)) — SBAS502 data format.
 * Internal reference = 2.42 V (CONFIG2.VREF_4V = 0, PDB_REFBUF = 1). */
#define ADS_VREF_V        2.42f
#define ADS_FULL_SCALE    8388607.0f
#define ADS_LSB_UV(g)     (ADS_VREF_V * 1e6f / ((g) * ADS_FULL_SCALE))
#define ADS_LSB_UV_CH1    ADS_LSB_UV(6.0f)   /* ~48.1 nV/count, gain 6    */
#define ADS_LSB_UV_CH2    ADS_LSB_UV(1.0f)   /* resp demod slot — uV-equiv,
                                              * nominal scale; the demod
                                              * output does not pass the
                                              * CH2 PGA.
                                              * TODO(datasheet): resp demod
                                              * transfer gain to ohms.     */

/* ------------------------------ Timing ----------------------------------- */
#define ADS_T_POWERON_MS  35   /* PWDN high -> interface ready. Datasheet:
                                * power-down -> DRDY low typ 10 ms; full POR
                                * -> DRDY ~32 ms incl. VCAP settle. */
#define ADS_T_RESET_MS    2    /* >= 18 tCLK (~35 us) after RESET opcode     */
#define ADS_T_STABLE_MS   60   /* lead-off comp + sinc settle before LOFF_STAT */
#define ADS_FRAME_LEN     9    /* 24-bit status + 2 x 24-bit channels        */
#define ADS_RING_DEPTH    32   /* ISR->task seq ring: 32 frames = 64 ms slack */
#define ADS_SEM_WAIT_MS   250  /* drain task block (stall detect)            */
#define ADS_STALL_US      20000 /* healthy(): no frame for 10 periods        */
#define ADS_START_GRACE_US 100000 /* healthy(): first-sample deadline         */
#define ADS_TASK_STACK    3072
#define ADS_TASK_PRIO     12
#define ADS_RATE_HZ       500.0f

/* Selftest test-signal capture */
#define ADS_ST_SKIP       8    /* sinc-filter settling frames to discard     */
#define ADS_ST_FRAMES     48   /* captured frames (~96 ms at 500 sps)        */
#define ADS_ST_DRDY_US    20000
/* Test signal = internal ~1 mV (VREF/2400-ish) square/DC through the full
 * chain at gain 6. Pass = clearly above the uV-scale noise floor and below
 * full scale. TODO(datasheet): tighten once bench amplitude is measured. */
#define ADS_ST_MIN_UV     150.0f
#define ADS_ST_MAX_UV     20000.0f

/* ------------------------------ Driver state ----------------------------- */
typedef struct {
    void            *spi_cmd;        /* 1 MHz: opcodes + register R/W        */
    void            *spi_data;       /* 4 MHz: RDATAC payload (may be NULL)  */
    bool             data_tried;
    bool             powered;
    bool             present;
    bool             isr_done;
    uint8_t          id;

    TaskHandle_t     task;
    SemaphoreHandle_t drdy_sem;

    vq_ts_anchor_t   anchor;         /* {edge_us <-> dev_seq} per contract   */
    volatile int32_t dev_seq;        /* software sample counter (1/edge)     */
    /* SPSC ring of pending sample seqs, ISR writes / task drains */
    volatile int32_t seq_ring[ADS_RING_DEPTH];
    volatile uint16_t ring_head, ring_tail;
    volatile uint32_t isr_dropped;

    volatile bool    streaming;
    int64_t          stream_since_us;  /* start() time — first-sample grace  */
} ads_priv_t;

static ads_priv_t   s_priv;
static bool         s_registered;

/* ------------------------------ SPI helpers ------------------------------ */
/* Every helper below runs on spi_cmd (1 MHz) unless explicitly noted:
 * contiguous bytes at 1 MHz are ~8 us apart >= 4*tCLK — SBAS502 §8.5.2.10. */

static vq_err_t ads_cmd(void *h, uint8_t opcode)
{
    uint8_t junk;
    return vq_spi_xfer(h, &opcode, &junk, 1);
}

static vq_err_t ads_wreg(void *h, uint8_t addr, const uint8_t *vals, size_t n)
{
    if (!h || !vals || n == 0 || n > 8) return ESP_ERR_INVALID_ARG;
    uint8_t tx[10], rx[10];
    tx[0] = ADS_OP_WREG | (addr & 0x1F);
    tx[1] = (uint8_t)(n - 1);
    memcpy(tx + 2, vals, n);
    return vq_spi_xfer(h, tx, rx, n + 2);
}

static vq_err_t ads_rreg(void *h, uint8_t addr, uint8_t *out, size_t n)
{
    if (!h || !out || n == 0 || n > 8) return ESP_ERR_INVALID_ARG;
    uint8_t tx[10] = {0}, rx[10] = {0};
    tx[0] = ADS_OP_RREG | (addr & 0x1F);
    tx[1] = (uint8_t)(n - 1);
    vq_err_t e = vq_spi_xfer(h, tx, rx, n + 2);
    if (e == ESP_OK) memcpy(out, rx + 2, n);
    return e;
}

static int32_t ads_s24(const uint8_t *b)   /* 24-bit two's complement, MSB first */
{
    uint32_t v = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    if (v & 0x00800000UL) v |= 0xFF000000UL;
    return (int32_t)v;
}

/* ------------------------------ Power / reset ---------------------------- */
static vq_err_t ads_power_up(ads_priv_t *p)
{
    /* PWDN/~RESET on TCA6408 P1 — default low keeps the AFE powered down. */
    vq_err_t e = vq_exp_set(VQ_EXP_ADS_PWDN, true);
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(ADS_T_POWERON_MS));
    p->powered = true;
    return ESP_OK;
}

static vq_err_t ads_power_down(ads_priv_t *p)
{
    vq_err_t e = vq_exp_set(VQ_EXP_ADS_PWDN, false);
    if (e == ESP_OK) p->powered = false;
    return e;
}

/* RESET opcode then >=18 tCLK, then leave RDATAC (power-up default) so the
 * register interface accepts commands. */
static vq_err_t ads_chip_reset(ads_priv_t *p)
{
    vq_err_t e = ads_cmd(p->spi_cmd, ADS_OP_RESET);
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(ADS_T_RESET_MS));
    return ads_cmd(p->spi_cmd, ADS_OP_SDATAC);
}

/* ------------------------------ Config table ----------------------------- */
static const struct { uint8_t addr; uint8_t val; } s_run_cfg[] = {
    { ADS_REG_CONFIG1,   ADS_CFG1_RUN     },
    { ADS_REG_CONFIG2,   ADS_CFG2_RUN     },
    { ADS_REG_LOFF,      ADS_LOFF_RUN     },
    { ADS_REG_CH1SET,    ADS_CH1_RUN      },
    { ADS_REG_CH2SET,    ADS_CH2_RUN      },
    { ADS_REG_RLD_SENS,  ADS_RLD_SENS_RUN },
    { ADS_REG_LOFF_SENS, ADS_LOFF_SENS_RUN},
    { ADS_REG_RESP1,     ADS_RESP1_RUN    },
    { ADS_REG_RESP2,     ADS_RESP2_RUN    },
    { ADS_REG_GPIO,      ADS_GPIO_RUN     },
};

static vq_err_t ads_configure(ads_priv_t *p)
{
    size_t i;
    for (i = 0; i < sizeof(s_run_cfg) / sizeof(s_run_cfg[0]); i++) {
        vq_err_t e = ads_wreg(p->spi_cmd, s_run_cfg[i].addr, &s_run_cfg[i].val, 1);
        if (e != ESP_OK) return e;
    }
    /* Read-back verify — catches open/short SPI nets and decode misses. */
    for (i = 0; i < sizeof(s_run_cfg) / sizeof(s_run_cfg[0]); i++) {
        uint8_t rb = 0xEE;
        vq_err_t e = ads_rreg(p->spi_cmd, s_run_cfg[i].addr, &rb, 1);
        if (e != ESP_OK) return e;
        if (rb != s_run_cfg[i].val) {
            ESP_LOGE(TAG, "cfg verify: reg 0x%02X wrote 0x%02X read 0x%02X",
                     s_run_cfg[i].addr, s_run_cfg[i].val, rb);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

/* ------------------------------ SPI handles ------------------------------ */
static vq_err_t ads_ensure_handles(ads_priv_t *p)
{
    if (!p->spi_cmd) {
        const vq_spi_dev_cfg_t c = {
            .dev        = VQ_SPI_DEV_ADS1292,
            .cs_gpio    = VQ_CS_ADS1292_GPIO,
            .mode       = 1,                    /* CPOL0 / CPHA1            */
            .clock_hz   = VQ_SPI_PROBE_HZ,      /* 1 MHz: see clock strategy */
            .cs_pre_us  = 16,                   /* >= 4*tCLK + margin        */
            .cs_post_us = 16,
        };
        p->spi_cmd = vq_spi_add(&c);
        if (!p->spi_cmd) {
            ESP_LOGE(TAG, "spi cmd handle alloc failed");
            return ESP_ERR_NO_MEM;
        }
    }
    if (!p->spi_data && !p->data_tried) {
        const vq_spi_dev_cfg_t d = {
            .dev        = VQ_SPI_DEV_ADS1292,
            .cs_gpio    = VQ_CS_ADS1292_GPIO,
            .mode       = 1,
            .clock_hz   = VQ_SPI_ADS1292_HZ,    /* 4 MHz: payload only       */
            .cs_pre_us  = 8,                    /* >=4*tCLK after last SCLK  */
            .cs_post_us = 8,                    /*   before CS high          */
        };
        p->spi_data = vq_spi_add(&d);
        p->data_tried = true;
        if (!p->spi_data)
            ESP_LOGW(TAG, "no 4 MHz handle (same-CS add refused?) — "
                          "payloads fall back to 1 MHz");
    }
    return ESP_OK;
}

/* ------------------------------ DRDY path -------------------------------- */
/* One device sample per DRDY edge; anchor.seq_anchor = dev_seq lets
 * vq_ts_anchor_stamp() back-date late-drained frames at 500 sps. */
static void IRAM_ATTR ads_drdy_feed(ads_priv_t *p)
{
    int32_t seq = p->dev_seq + 1;
    p->dev_seq = seq;
    vq_ts_anchor_edge(&p->anchor);
    p->anchor.seq_anchor = seq;

    uint16_t next = (uint16_t)((p->ring_head + 1) % ADS_RING_DEPTH);
    if (next != p->ring_tail) {
        p->seq_ring[p->ring_head] = seq;
        p->ring_head = next;
        BaseType_t hp = pdFALSE;
        xSemaphoreGiveFromISR(p->drdy_sem, &hp);
        if (hp) portYIELD_FROM_ISR(hp);
    } else {
        p->isr_dropped++;      /* drain task >=64 ms behind — counted later */
    }
}

static void IRAM_ATTR ads_drdy_isr(void *arg)
{
    ads_priv_t *p = (ads_priv_t *)arg;
    if (!p->streaming) return;
    ads_drdy_feed(p);
}

static vq_err_t ads_isr_setup(ads_priv_t *p)
{
    if (p->isr_done) return ESP_OK;
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << VQ_ADS1292_DRDY_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,   /* R99 100k external pull-up */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,     /* active-low data ready     */
    };
    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK) return e;
    e = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;  /* shared svc */
    e = gpio_isr_handler_add(VQ_ADS1292_DRDY_GPIO, ads_drdy_isr, p);
    if (e != ESP_OK) return e;
    gpio_intr_disable(VQ_ADS1292_DRDY_GPIO);   /* armed by start() only      */
    p->isr_done = true;
    return ESP_OK;
}

static void ads_flush_pending(ads_priv_t *p)
{
    while (xSemaphoreTake(p->drdy_sem, 0) == pdTRUE)
        p->ring_tail = (uint16_t)((p->ring_tail + 1) % ADS_RING_DEPTH);
    p->ring_head = p->ring_tail = 0;
}

/* ------------------------------ Drain task ------------------------------- */
static uint8_t s_zeros[ADS_FRAME_LEN];   /* DIN held low during readback    */

static void ads_drain_task(void *arg)
{
    vq_sensor_t *s = (vq_sensor_t *)arg;
    ads_priv_t  *p = (ads_priv_t *)s->priv;
    uint8_t rx[ADS_FRAME_LEN];
    uint8_t stall_log = 0;

    for (;;) {
        if (xSemaphoreTake(p->drdy_sem, pdMS_TO_TICKS(ADS_SEM_WAIT_MS))
                != pdTRUE) {
            if (p->streaming && p->present) {
                /* 250 ms of silence at 500 sps — pipe stalled. */
                s->err_count++;
                if ((stall_log++ & 0x07) == 0)
                    ESP_LOGW(TAG, "drdy stall while streaming");
            }
            continue;
        }
        stall_log = 0;

        int32_t seq = p->seq_ring[p->ring_tail];
        p->ring_tail = (uint16_t)((p->ring_tail + 1) % ADS_RING_DEPTH);
        if (!p->streaming) continue;             /* flushed stop() residue   */

        void *h = p->spi_data ? p->spi_data : p->spi_cmd;
        if (vq_spi_xfer(h, s_zeros, rx, ADS_FRAME_LEN) != ESP_OK) {
            s->err_count++;
            continue;
        }
        uint32_t stat = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];
        if ((stat >> 20) != 0xC) {               /* 1100 sync nibble          */
            s->err_count++;                      /* corrupt frame — discard   */
            continue;
        }

        int64_t t_us = vq_ts_anchor_stamp(&p->anchor, seq);
        uint8_t flags = 0;
        if (seq != p->anchor.edge_seq)
            flags |= VQ_SAMPLE_F_ESTIMATED_TS;   /* back-dated, not edge-latched */
        /* LOFF_STAT[4:0] in status[19:15]; bits[1:0]=IN1N/IN1P off — CH1
         * electrode pair also carries respiration, so flag both channels. */
        if (stat & (0x3u << 15))
            flags |= VQ_SAMPLE_F_QUALITY_BAD;
        if (p->isr_dropped) {
            s->err_count += p->isr_dropped;
            p->isr_dropped = 0;
            flags |= VQ_SAMPLE_F_ESTIMATED_TS;
        }

        vq_sample_t out[2] = {
            { .channel = VQ_CH_ECG_II, .t_us = t_us,
              .value = (float)ads_s24(&rx[3]) * ADS_LSB_UV_CH1, .flags = flags },
            { .channel = VQ_CH_RESP,   .t_us = t_us,
              .value = (float)ads_s24(&rx[6]) * ADS_LSB_UV_CH2, .flags = flags },
        };
        if (s->sink) {
            s->sink(s->sink_ctx, out, 2);
            s->sample_count += 2;   /* count delivered, not produced  */
        }
        s->last_t_us = t_us;
    }
}

/* ------------------------------ Sensor ops ------------------------------- */
static vq_err_t ops_probe(vq_sensor_t *s)
{
    ads_priv_t *p = (ads_priv_t *)s->priv;
    vq_err_t e = ads_ensure_handles(p);
    if (e != ESP_OK) return e;

    if (!p->powered) { e = ads_power_up(p); if (e != ESP_OK) return e; }
    e = ads_chip_reset(p);
    uint8_t id = 0;
    if (e == ESP_OK) e = ads_rreg(p->spi_cmd, ADS_REG_ID, &id, 1);
    bool ok = (e == ESP_OK) && ads_id_ok(id);
    p->id = id;
    ESP_LOGI(TAG, "probe: id=0x%02X %s", id,
             ok ? "(ads1292r present)" : "(unexpected/absent)");

    ads_power_down(p);                 /* restore default powered-off state */
    p->present = ok;
    return ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static vq_err_t ops_init(vq_sensor_t *s)
{
    ads_priv_t *p = (ads_priv_t *)s->priv;
    vq_err_t e = ads_ensure_handles(p);
    if (e != ESP_OK) return e;

    if (!p->drdy_sem) {
        p->drdy_sem = xSemaphoreCreateCounting(ADS_RING_DEPTH, 0);
        if (!p->drdy_sem) return ESP_ERR_NO_MEM;
    }
    if (!p->task) {
        if (xTaskCreatePinnedToCore(ads_drain_task, "ads1292_drain",
                                  ADS_TASK_STACK, s, ADS_TASK_PRIO,
                                  &p->task, 0) != pdPASS)
            return ESP_ERR_NO_MEM;
    }
    e = ads_isr_setup(p);
    if (e != ESP_OK) return e;

    if (!p->powered) { e = ads_power_up(p); if (e != ESP_OK) return e; }
    e = ads_chip_reset(p);
    if (e != ESP_OK) goto fail;
    {
        uint8_t id = 0;
        e = ads_rreg(p->spi_cmd, ADS_REG_ID, &id, 1);
        p->id = id;
        if (e != ESP_OK || !ads_id_ok(id)) { e = ESP_ERR_NOT_FOUND; goto fail; }
    }
    e = ads_configure(p);
    if (e != ESP_OK) goto fail;

    p->anchor.rate_hz = ADS_RATE_HZ;
    p->present = true;
    ESP_LOGI(TAG, "init ok id=0x%02X, cfg verified", p->id);
    return ESP_OK;
fail:
    ads_power_down(p);
    return e;
}

static vq_err_t ops_start(vq_sensor_t *s)
{
    ads_priv_t *p = (ads_priv_t *)s->priv;
    if (!p->present) return ESP_ERR_INVALID_STATE;
    if (p->streaming) return ESP_OK;

    /* Re-power path (e.g. after sleep()/selftest left the part powered down). */
    if (!p->powered) {
        vq_err_t e = ads_power_up(p);
        if (e != ESP_OK) return e;
        e = ads_chip_reset(p);
        if (e != ESP_OK) return e;
        e = ads_configure(p);
        if (e != ESP_OK) return e;
    }

    ads_flush_pending(p);
    p->dev_seq = 0;
    p->isr_dropped = 0;
    p->anchor.edge_us = 0;
    p->anchor.edge_seq = 0;
    p->anchor.seq_anchor = 0;
    p->anchor.rate_hz = ADS_RATE_HZ;

    p->streaming = true;
    p->stream_since_us = esp_timer_get_time();
    gpio_intr_enable(VQ_ADS1292_DRDY_GPIO);
    ads_cmd(p->spi_cmd, ADS_OP_RDATAC);
    vq_err_t e = ads_cmd(p->spi_cmd, ADS_OP_START);  /* START pin is tied GND */

    /* Paranoia: if DRDY was already low before the ISR was armed, the
     * NEGEDGE was missed — synthesize the edge so the frame is not stuck. */
    if (e == ESP_OK && gpio_get_level(VQ_ADS1292_DRDY_GPIO) == 0)
        ads_drdy_feed(p);
    return e;
}

static vq_err_t ops_stop(vq_sensor_t *s)
{
    ads_priv_t *p = (ads_priv_t *)s->priv;
    if (!p->streaming) return ESP_OK;
    gpio_intr_disable(VQ_ADS1292_DRDY_GPIO);
    p->streaming = false;
    vq_err_t e1 = ads_cmd(p->spi_cmd, ADS_OP_STOP);
    vq_err_t e2 = ads_cmd(p->spi_cmd, ADS_OP_SDATAC);
    ads_flush_pending(p);
    return (e1 != ESP_OK) ? e1 : e2;
}

static vq_err_t ops_sleep(vq_sensor_t *s)
{
    ads_priv_t *p = (ads_priv_t *)s->priv;
    if (p->streaming) ops_stop(s);
    gpio_intr_disable(VQ_ADS1292_DRDY_GPIO);
    if (p->powered) {
        ads_cmd(p->spi_cmd, ADS_OP_SDATAC);   /* leave read-continuous mode  */
        return ads_power_down(p);           /* PWDN low via TCA6408 P1     */
    }
    return ESP_OK;
}

static bool ops_healthy(vq_sensor_t *s)
{
    ads_priv_t *p = (ads_priv_t *)s->priv;
    if (!p->present) return false;
    if (p->streaming) {
        /* Reference = last sample ts, or stream start before the first
         * sample arrives (catches a dead DRDY line — no edge ever). */
        if (s->sample_count > 0) {
            if (esp_timer_get_time() - s->last_t_us > ADS_STALL_US)
                return false;
        } else if (esp_timer_get_time() - p->stream_since_us >
                   ADS_START_GRACE_US) {
            return false;
        }
    }
    return true;
}

/* ------------------------------ Registration ----------------------------- */
static const vq_channel_id_t s_channels[] = { VQ_CH_ECG_II, VQ_CH_RESP };

static const vq_sensor_ops_t s_ops = {
    .name       = "ads1292r",
    .probe      = ops_probe,
    .init       = ops_init,
    .start      = ops_start,
    .stop       = ops_stop,
    .sleep      = ops_sleep,
    .healthy    = ops_healthy,
    .channels   = s_channels,
    .n_channels = sizeof(s_channels) / sizeof(s_channels[0]),
    .rate_hz    = ADS_RATE_HZ,
    .required   = true,
};

static vq_sensor_t s_sensor = {
    .ops = &s_ops,
    .priv = &s_priv,
};

esp_err_t vq_ads1292_register(void)
{
    if (s_registered) return ESP_OK;
    esp_err_t e = vq_sensor_register(&s_sensor);
    if (e == ESP_OK) s_registered = true;
    return e;
}

/* ------------------------------ Public ID read --------------------------- */
esp_err_t vq_ads1292_read_id(uint8_t *id)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    ads_priv_t *p = &s_priv;
    if (p->streaming) return ESP_ERR_INVALID_STATE;

    vq_err_t e = ads_ensure_handles(p);
    if (e != ESP_OK) return e;
    bool was_powered = p->powered;
    if (!was_powered) {
        e = ads_power_up(p);
        if (e != ESP_OK) return e;
        ads_chip_reset(p);
    }
    e = ads_rreg(p->spi_cmd, ADS_REG_ID, id, 1);
    if (!was_powered) ads_power_down(p);
    return e;
}

/* ------------------------------ Selftest --------------------------------- */
/* Poll ~DRDY low with an esp_timer deadline. Called only from the selftest
 * (console task context) with the GPIO interrupt masked — bounded spin, ~2 ms
 * typical per frame at 500 sps. */
static vq_err_t ads_wait_drdy(int timeout_us)
{
    int64_t dl = esp_timer_get_time() + (int64_t)timeout_us;
    int spin = 0;
    while (gpio_get_level(VQ_ADS1292_DRDY_GPIO) != 0) {
        if (esp_timer_get_time() > dl) return ESP_ERR_TIMEOUT;
        if ((++spin & 0x3FF) == 0) taskYIELD();
    }
    return ESP_OK;
}

esp_err_t vq_ads1292_selftest(char *buf, size_t len)
{
    ads_priv_t *p = &s_priv;
    size_t off = 0;
    int fails = 0;

#define RPT(...) do {                                                    \
        size_t _rem = (off < len) ? (len - off) : 0;                     \
        if (_rem) {                                                      \
            int _w = snprintf(buf + off, _rem, __VA_ARGS__);             \
            if (_w > 0) off += ((size_t)_w < _rem) ? (size_t)_w : _rem;  \
        }                                                                \
    } while (0)

    if (!buf || len == 0) return ESP_ERR_INVALID_ARG;
    buf[0] = '\0';
    if (p->streaming) {
        RPT("ads1292r: busy (streaming)\n");
        return ESP_ERR_INVALID_STATE;
    }

    vq_err_t e = ads_ensure_handles(p);
    if (e != ESP_OK) { RPT("ads1292r: spi handle alloc failed\n"); return e; }
    gpio_intr_disable(VQ_ADS1292_DRDY_GPIO);   /* selftest polls DRDY        */

    bool was_powered = p->powered;
    if (!was_powered) {
        e = ads_power_up(p);
        if (e != ESP_OK) { RPT("ads1292r: expander pwdn failed\n"); return e; }
    }
    e = ads_chip_reset(p);
    if (e != ESP_OK) { RPT("ads1292r: reset/SDATAC failed\n"); goto out; }

    /* --- ID at the 1 MHz probe clock (authoritative) --- */
    uint8_t id1 = 0xEE;
    {
        e = ads_rreg(p->spi_cmd, ADS_REG_ID, &id1, 1);
        bool ok = (e == ESP_OK) && ads_id_ok(id1);
        p->id = id1;
        RPT("id@1MHz=0x%02X %s | ", id1, ok ? "ok" : "MISMATCH");
        if (!ok) fails++;
    }

    /* --- ID at the 4 MHz operating clock. A 2-byte RREG at 4 MHz puts
     * command bytes ~2 us apart < 4*tCLK, so a mismatch here is expected
     * (tSDECODE) and reported as informational only; the 1 MHz read above
     * is authoritative. --- */
    if (p->spi_data) {
        uint8_t id4 = 0xEE;
        vq_err_t e4 = ads_rreg(p->spi_data, ADS_REG_ID, &id4, 1);
        RPT("id@4MHz=0x%02X (%s) | ", id4,
            (e4 == ESP_OK && id4 == id1) ? "match" : "decode-timing marginal");
    } else {
        RPT("id@4MHz=n/a (single handle) | ");
    }

    /* --- normal config, brief conversion run, electrode lead-off status --- */
    e = ads_configure(p);
    if (e != ESP_OK) { RPT("cfg verify FAILED\n"); fails++; goto out; }
    ads_cmd(p->spi_cmd, ADS_OP_RDATAC);
    ads_cmd(p->spi_cmd, ADS_OP_START);
    vTaskDelay(pdMS_TO_TICKS(ADS_T_STABLE_MS));
    ads_cmd(p->spi_cmd, ADS_OP_STOP);
    ads_cmd(p->spi_cmd, ADS_OP_SDATAC);
    {
        uint8_t loff = 0xEE;
        if (ads_rreg(p->spi_cmd, ADS_REG_LOFF_STAT, &loff, 1) != ESP_OK) {
            fails++;
            RPT("loff_stat READ FAILED | ");
        }
        RPT("loff_stat=0x%02X in1p_off=%d in1n_off=%d in2p_off=%d "
            "in2n_off=%d rld_off=%d | ",
            loff, loff & 0x01, !!(loff & 0x02), !!(loff & 0x04),
            !!(loff & 0x08), !!(loff & 0x10));
    }

    /* --- internal test signal: CHnSET MUX=0101 + CONFIG2 INT_TEST --- */
    {
        uint8_t v = ADS_CFG2_TEST;
        if (ads_wreg(p->spi_cmd, ADS_REG_CONFIG2, &v, 1) != ESP_OK) goto out;
        v = ADS_CH_TEST;
        if (ads_wreg(p->spi_cmd, ADS_REG_CH1SET, &v, 1) != ESP_OK) goto out;
        if (ads_wreg(p->spi_cmd, ADS_REG_CH2SET, &v, 1) != ESP_OK) goto out;
    }
    {
        void *dh = p->spi_data ? p->spi_data : p->spi_cmd;
        int n = 0, bad = 0;
        float lo1 = 1e9f, hi1 = -1e9f, sa1 = 0;
        float lo2 = 1e9f, hi2 = -1e9f, sa2 = 0;

        ads_cmd(p->spi_cmd, ADS_OP_RDATAC);
        ads_cmd(p->spi_cmd, ADS_OP_START);
        for (int i = 0; i < ADS_ST_SKIP + ADS_ST_FRAMES; i++) {
            if (ads_wait_drdy(ADS_ST_DRDY_US) != ESP_OK) { bad++; break; }
            uint8_t rx[ADS_FRAME_LEN];
            if (vq_spi_xfer(dh, s_zeros, rx, ADS_FRAME_LEN) != ESP_OK) { bad++; continue; }
            uint32_t stat = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];
            if ((stat >> 20) != 0xC) { bad++; continue; }   /* 4 MHz path check */
            if (i < ADS_ST_SKIP) continue;                  /* sinc settling   */
            float v1 = (float)ads_s24(&rx[3]) * ADS_LSB_UV_CH1;
            float v2 = (float)ads_s24(&rx[6]) * ADS_LSB_UV_CH2;
            if (v1 < lo1) lo1 = v1;
            if (v1 > hi1) hi1 = v1;
            if (v2 < lo2) lo2 = v2;
            if (v2 > hi2) hi2 = v2;
            sa1 += (v1 < 0) ? -v1 : v1;
            sa2 += (v2 < 0) ? -v2 : v2;
            n++;
        }
        ads_cmd(p->spi_cmd, ADS_OP_STOP);
        ads_cmd(p->spi_cmd, ADS_OP_SDATAC);

        float pkpk1 = (n > 0) ? hi1 - lo1 : 0, pkpk2 = (n > 0) ? hi2 - lo2 : 0;
        float mab1  = (n > 0) ? sa1 / (float)n : 0;
        float mab2  = (n > 0) ? sa2 / (float)n : 0;
        RPT("test-sig: %d frames (%d bad) ch1 mean|.|=%.0fuV pkpk=%.0fuV "
            "ch2 mean|.|=%.0fuV pkpk=%.0fuV | ",
            n, bad, (double)mab1, (double)pkpk1, (double)mab2, (double)pkpk2);

        bool sig1 = n >= ADS_ST_FRAMES / 2 &&
                    (pkpk1 > ADS_ST_MIN_UV || mab1 > ADS_ST_MIN_UV) &&
                    pkpk1 < ADS_ST_MAX_UV;
        bool sig2 = n >= ADS_ST_FRAMES / 2 &&
                    (pkpk2 > ADS_ST_MIN_UV || mab2 > ADS_ST_MIN_UV) &&
                    pkpk2 < ADS_ST_MAX_UV;
        if (!sig1 || !sig2) {
            fails++;
            RPT("test-sig MISMATCH (noise floor / flatline) | ");
        } else {
            RPT("test-sig ok | ");
        }
        if (bad) RPT("frame hdr/sync errs=%d | ", bad);
    }

out:
    /* Restore: if the device was already configured/powered, leave it in the
     * run configuration; otherwise return to the default powered-down state. */
    if (was_powered && p->powered)
        ads_configure(p);
    else if (p->powered)
        ads_power_down(p);

    RPT("\nads1292r selftest: %s", fails ? "FAIL" : "PASS");
    return fails ? ESP_FAIL : ESP_OK;
#undef RPT
}
