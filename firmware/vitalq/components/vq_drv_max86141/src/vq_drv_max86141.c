/* vq_drv_max86141.c — MAX86141ENP+ satellite PPG AFE driver (J10 tail).
 *
 * Board truth (fix_v22): CS=IO38 doubles as the U26 MISO level-shifter OE
 * (active-low) → all traffic via spi_bus.h, CS idle high. INT=IO39
 * open-drain, R113 pulls it to +3V3. Powered from +1V8 rails. LED cathodes
 * drive the off-board SFH7050A tail emitters; common anode = TX_5V via R114
 * → LED on-time is declared to vq_led as ~200 mA class.
 *
 * Streaming model:
 *   AFE free-runs at PPG_SR=0x04 (199.805 sps on its internal 32768 Hz osc)
 *   with a 4-exposure sequence LED1(530)/LED2(660)/LED3(940)/DIRECT-AMBIENT.
 *   Each exposure pushes one 24-bit FIFO word tagged by its LEDC code.
 *   FIFO_A_FULL watermark = 40 words → INT negedge ~20 Hz. ISR latches
 *   esp_timer via vq_ts_anchor_edge; drain task bursts the FIFO, decodes
 *   tags, back-stamps each word from the edge anchor, pushes samples.
 *
 *   The AFE pulses LEDs autonomously — the scheduler cannot interleave its
 *   pulses at per-slot granularity (GPIO1 sync input is unconnected). We
 *   therefore (a) declare true LED duty with a periodic vq_led_submit and
 *   (b) hold TX_5V for the session via vq_led_tx5_acquire — without LED_SUP
 *   present the LED drivers sink into a dead rail and only ambient words
 *   would be produced.
 *
 * Non-medical research prototype: ppg.satellite is an engineering perfusion
 * signal only.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "vq_types.h"
#include "vq_sensor.h"
#include "board_pins.h"
#include "spi_bus.h"
#include "led_sched.h"

#include "vq_drv_max86141.h"
#include "max86141_regs.h"

static const char *TAG = "max86141";

/* ---------------- Tunables ------------------------------------------------ */

/* Sequence: LED1(530) → LED2(660) → LED3(940) → AMBIENT, repeat @ ~200 sps.
 * One FIFO word per exposure → word rate = 4 × frame rate. */
#define MX_WORDS_PER_FRAME      4
#define MX_FRAME_RATE_HZ        199.805f            /* PPG_SR 0x04 @32768 Hz  */
#define MX_WORD_RATE_HZ         (MX_FRAME_RATE_HZ * MX_WORDS_PER_FRAME)

/* FIFO almost-full watermark: interrupt when count >= MX_DRAIN_WORDS. */
#define MX_DRAIN_WORDS          40                  /* 10 frames, ~20 Hz INTs */
#define MX_FIFO_A_FULL          (MX_FIFO_DEPTH - MX_DRAIN_WORDS)   /* 0x58 */

/* LED drive (modest): 124 mA full-scale range, PA = code/255 × 124 mA.
 * Green (LED1) is the least efficient in SFH7050A → slightly higher code. */
#define MX_LED1_PA              0x30                /* ~23.3 mA (530 nm)      */
#define MX_LED2_PA              0x20                /* ~15.5 mA (660 nm)      */
#define MX_LED3_PA              0x20                /* ~15.5 mA (940 nm)      */
#define MX_LED_SELFTEST_PA      0x08                /* ~3.9 mA min pulse      */

/* Scheduler declaration: ~200 mA class, exposure energy per 5 ms frame. */
#define MX_LED_CLASS_MA         200
#define MX_LED_ON_US_PER_FRAME  560                 /* 4×(12µs settle+117µs tint) */
#define MX_LED_PERIOD_US        5000                /* 200 Hz frame period    */

#define MX_TASK_STACK           4096
#define MX_TASK_PRIO            12
#define MX_DRAIN_TIMEOUT_MS     1000                /* also a stall watchdog  */
#define MX_ERR_HEALTH_LIMIT     64                  /* cumulative → unhealthy */

/* ---------------- Private state ------------------------------------------- */

typedef struct {
    void           *hspi_probe;   /* 1 MHz handle — probe + selftest        */
    void           *hspi_run;     /* 8 MHz handle — streaming                */

    TaskHandle_t    task;
    volatile bool   running;      /* streaming + drains enabled             */
    volatile bool   task_exit;    /* drain task requests its own exit       */
    bool            probed;       /* PART_ID verified                       */
    bool            streaming;
    bool            led_granted;  /* scheduler accepted our window          */
    bool            tx5_held;

    vq_ts_anchor_t  anchor;       /* INT-edge ↔ device-word anchor          */
    int32_t         word_seq;     /* cumulative exposure words since start  */
    int32_t         last_edge_seq;/* anchor.edge_seq consumed by last drain */
    volatile bool   draining;     /* mx_drain in progress (stop() waits)    */

    /* health / stats */
    uint32_t        spi_err;
    uint32_t        tag_err;      /* unexpected FIFO tag                    */
    uint32_t        ovf_err;      /* OVF_COUNTER non-zero drains            */
    uint32_t        alc_ovf;      /* ALC_OVF status bits seen               */
    uint32_t        led_compb;    /* LED_COMPB status bits seen             */
    uint32_t        vdd_oor;      /* VDD_OOR status bits seen               */
    uint32_t        edges;        /* INT negedges seen                      */
    uint32_t        last_tag;     /* debug: last tag decoded                */
    uint32_t        last_val;     /* debug: last LED1 value decoded         */

    uint8_t         fifo_buf[MX_FIFO_DEPTH * 3];    /* burst-read staging     */
    vq_sample_t     out[MX_FIFO_DEPTH];             /* decoded batch          */
} mx_state_t;

static mx_state_t    s_st;
static vq_sensor_t   s_sensor;

/* ---------------- SPI framing ----------------------------------------------
 * MAX86141 frame = [reg][0x00=W | 0xFF=R][data] — 3 bytes, CS held across.
 * spi_bus.h maps cleanly: reg_write cmd = {reg, WRITE_EN}; write_read for
 * reads and the FIFO burst. */

static vq_err_t mx_w8(void *h, uint8_t reg, uint8_t val)
{
    uint8_t cmd[2] = { reg, MX_SPI_WRITE_EN };
    return vq_spi_reg_write(h, cmd, sizeof(cmd), &val, 1);
}

static vq_err_t mx_r8(void *h, uint8_t reg, uint8_t *val)
{
    uint8_t cmd[2] = { reg, MX_SPI_READ_EN };
    return vq_spi_write_read(h, cmd, sizeof(cmd), val, 1);
}

static vq_err_t mx_fifo_burst(void *h, uint8_t *buf, int nwords)
{
    uint8_t cmd[2] = { MX_REG_FIFO_DATA, MX_SPI_READ_EN };
    return vq_spi_write_read(h, cmd, sizeof(cmd), buf, (size_t)nwords * 3u);
}

/* Lazily create the two device handles; clock is clamped to the 8 MHz cap
 * by spi_bus regardless. */
static void *mx_handle(int hz)
{
    void **slot = (hz <= 1000000) ? &s_st.hspi_probe : &s_st.hspi_run;
    if (*slot) {
        return *slot;
    }
    vq_spi_dev_cfg_t cfg = {
        .dev        = VQ_SPI_DEV_MAX86141,
        .cs_gpio    = VQ_CS_MAX86141_GPIO,
        .mode       = 0,                        /* board spec: SPI mode 0   */
        .clock_hz   = (uint32_t)hz,
        /* CSB→SCLK setup is 20 ns for the part; give the U26 shifter OE a
         * couple of µs to turn its output on after CS asserts. */
        .cs_pre_us  = 2,
        .cs_post_us = 1,
    };
    *slot = vq_spi_add(&cfg);
    if (!*slot) {
        s_st.spi_err++;
        ESP_LOGE(TAG, "vq_spi_add failed");
    }
    return *slot;
}

/* ---------------- INT ISR → timestamp anchor ------------------------------- */

static void mx_isr(void *arg)
{
    (void)arg;
    vq_ts_anchor_edge(&s_st.anchor);          /* latch esp_timer at negedge */
    s_st.edges++;
    if (s_st.task) {
        BaseType_t hp = pdFALSE;
        vTaskNotifyGiveFromISR(s_st.task, &hp);
        if (hp == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

/* ---------------- Register-level helpers ----------------------------------- */

static vq_err_t mx_clear_interrupts(void *h)
{
    uint8_t dummy = 0;
    vq_err_t e1 = mx_r8(h, MX_REG_INT_STAT_1, &dummy);
    vq_err_t e2 = mx_r8(h, MX_REG_INT_STAT_2, &dummy);
    return (e1 != ESP_OK) ? e1 : e2;
}

static vq_err_t mx_soft_reset(void *h)
{
    /* Datasheet pseudo: RESET then ≥1 ms, then SHDN. */
    vq_err_t err = mx_w8(h, MX_REG_MODE_CONFIG, MX_MODE_RESET);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    return ESP_OK;
}

static vq_err_t mx_flush_fifo(void *h)
{
    /* FLUSH_FIFO is a write-1 strobe; follow with the persistent config. */
    vq_err_t err = mx_w8(h, MX_REG_FIFO_CONFIG_2, MX_FIFO2_FLUSH_FIFO);
    if (err != ESP_OK) {
        return err;
    }
    return mx_w8(h, MX_REG_FIFO_CONFIG_2,
                 MX_FIFO2_STAT_CLR | MX_FIFO2_A_FULL_TYPE | MX_FIFO2_FIFO_RO);
}

/* Program the full streaming configuration, device left in SHDN. */
static vq_err_t mx_program_config(void *h)
{
    static const struct { uint8_t reg, val; } cfg[] = {
        { MX_REG_PPG_SYNC_CTRL,   0x00 },  /* internal 32768 Hz osc, no GPIO */
        /* PPG_TINT=117.3µs, PPG1/2_ADC_RGE=16µA (ALC active, offset on) */
        { MX_REG_PPG_CONFIG_1,    (MX_ADC_RGE_16UA << 4) |
                                  (MX_ADC_RGE_16UA << 2) | MX_TINT_117US },
        /* PPG_SR=200 sps, SMP_AVE=1 */
        { MX_REG_PPG_CONFIG_2,    (MX_SR_200SPS << 3) | MX_SMP_AVE_1 },
        /* LED_SETLNG=12µs; CDM filter (default) */
        { MX_REG_PPG_CONFIG_3,    (MX_LED_SETLNG_12US << 6) },
        { MX_REG_PROX_INT_THRESH, 0x00 },  /* proximity mode disabled        */
        { MX_REG_PD_BIAS,         0x01 },  /* PD1 bias for Cpd ≤ 65 pF       */
        { MX_REG_PICKET_FENCE,    0x00 },  /* picket-fence off               */
        /* Exposure slots: LEDC1..6 = [LED1, LED2, LED3, AMB, none, none] */
        { MX_REG_LED_SEQ_1,       (MX_LEDC_LED2 << 4) | MX_LEDC_LED1 },
        { MX_REG_LED_SEQ_2,       (MX_LEDC_AMBIENT << 4) | MX_LEDC_LED3 },
        { MX_REG_LED_SEQ_3,       0x00 },
        { MX_REG_LED1_PA,         MX_LED1_PA },
        { MX_REG_LED2_PA,         MX_LED2_PA },
        { MX_REG_LED3_PA,         MX_LED3_PA },
        { MX_REG_LED4_PA,         0x00 },
        { MX_REG_LED5_PA,         0x00 },
        { MX_REG_LED6_PA,         0x00 },
        { MX_REG_LED_PILOT_PA,    0x00 },
        /* all three drivers on the 124 mA full-scale range */
        { MX_REG_LED_RANGE_1,     0x3F },
        { MX_REG_LED_RANGE_2,     0x00 },
        /* FIFO watermark: A_FULL when >= MX_DRAIN_WORDS present */
        { MX_REG_FIFO_CONFIG_1,   MX_FIFO_A_FULL },
    };
    for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        vq_err_t err = mx_w8(h, cfg[i].reg, cfg[i].val);
        if (err != ESP_OK) {
            s_st.spi_err++;
            return err;
        }
    }
    return mx_flush_fifo(h);
}

/* ---------------- LED scheduler integration --------------------------------
 * The AFE pulses LEDs by itself once out of SHDN; scheduler slots cannot
 * gate individual pulses (no wired sync). We declare the honest duty —
 * ~560 µs of LED on-time per 5 ms frame at ~200 mA class — and hold TX_5V
 * for the whole session so the pulses actually emit light. */

static void mx_led_cb_on(void *ctx)  { (void)ctx; }
static void mx_led_cb_off(void *ctx) { (void)ctx; }

static vq_err_t mx_led_acquire(void)
{
    vq_led_request_t req = {
        .chain     = VQ_LED_MAX86141,
        .pulse_mA  = MX_LED_CLASS_MA,
        .on_us     = MX_LED_ON_US_PER_FRAME,
        .period_us = MX_LED_PERIOD_US,          /* repeating window          */
        .priority  = 1,
    };
    vq_err_t err = vq_led_submit(&req, mx_led_cb_on, mx_led_cb_off,
                                 &s_st, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "led_submit rejected (0x%x) — not pulsing", err);
        return err;
    }
    s_st.led_granted = true;
    /* LED_SUP = TX_5V must be live for the session (see file header). */
    err = vq_led_tx5_acquire();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tx5_acquire failed (0x%x)", err);
        vq_led_cancel(VQ_LED_MAX86141);
        s_st.led_granted = false;
        return err;
    }
    s_st.tx5_held = true;
    return ESP_OK;
}

static void mx_led_release(void)
{
    if (s_st.led_granted) {
        vq_led_cancel(VQ_LED_MAX86141);
        s_st.led_granted = false;
    }
    if (s_st.tx5_held) {
        vq_led_tx5_release();
        s_st.tx5_held = false;
    }
}

/* ---------------- FIFO decode + timestamp ---------------------------------- */

/* tag → human wavelength for logging; returns 0 for ambient/unknown use. */
static int mx_tag_nm(uint8_t tag)
{
    switch (tag) {
    case MX_LEDC_LED1: return 530;
    case MX_LEDC_LED2: return 660;
    case MX_LEDC_LED3: return 940;
    default:           return 0;
    }
}

/* FIFO TAG[4:0] is the *exposure-slot index*, not the LEDC code:
 * 1..6 → PPG1 LEDC1..6 slots, 7..12 → PPG2 slots, 25/26 → proximity.
 * We program LEDC1..4 = {LED1, LED2, LED3, AMBIENT} via LED_SEQ_1/2 and
 * run SINGLE_PPG, so valid tags are exactly 1..4. Translate slot → the
 * LEDC code occupying it. */
static uint8_t mx_slot_ledc(uint8_t tag)
{
    switch (tag) {
    case 1: return MX_LEDC_LED1;
    case 2: return MX_LEDC_LED2;
    case 3: return MX_LEDC_LED3;
    case 4: return MX_LEDC_AMBIENT;
    default: return 0xFF;
    }
}

/* Drain the FIFO fully and push stamped samples. Called from the drain
 * task after an INT edge or stall-timeout. */
static vq_err_t mx_drain(void)
{
    void *h = s_st.hspi_run ? s_st.hspi_run : s_st.hspi_probe;
    if (!h) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 1) latch + clear interrupt status (read-to-clear) */
    uint8_t is1 = 0, is2 = 0;
    if (mx_r8(h, MX_REG_INT_STAT_1, &is1) != ESP_OK ||
        mx_r8(h, MX_REG_INT_STAT_2, &is2) != ESP_OK) {
        s_st.spi_err++;
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (is1 & MX_INT1_ALC_OVF)   { s_st.alc_ovf++; }
    if (is1 & MX_INT1_LED_COMPB) { s_st.led_compb++; }
    if (is1 & MX_INT1_VDD_OOR)   { s_st.vdd_oor++; }
    if (is1 & MX_INT1_PWR_RDY)   { ESP_LOGW(TAG, "PWR_RDY: VDD dipped UVLO"); }
    if (is2)                     { ESP_LOGD(TAG, "INT2=0x%02x", is2); }

    uint8_t cnt = 0, ovf = 0;
    if (mx_r8(h, MX_REG_FIFO_DATA_COUNT, &cnt) != ESP_OK) {
        s_st.spi_err++;
        return ESP_ERR_INVALID_RESPONSE;
    }
    (void)mx_r8(h, MX_REG_OVF_COUNTER, &ovf);
    if (ovf) {
        s_st.ovf_err += ovf;
    }
    if (cnt == 0) {
        return ESP_OK;                       /* spurious edge — nothing to do */
    }
    if (cnt > MX_FIFO_DEPTH) {
        cnt = MX_FIFO_DEPTH;
    }

    /* 2) burst the FIFO: nwords × 3 bytes */
    if (mx_fifo_burst(h, s_st.fifo_buf, cnt) != ESP_OK) {
        s_st.spi_err++;
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* 3) anchor. A_FULL (TYPE=1) produces exactly one negedge per crossing:
     * the edge latched in the ISR coincides with the MX_DRAIN_WORDS-th word
     * of the current fill (index MX_DRAIN_WORDS-1 of this batch when the
     * FIFO was empty at fill start — always true: we drain to empty).
     *
     * Only a FRESH edge (edge_seq advanced since last drain) re-anchors;
     * partial/interleaved batches keep the previous anchor so word seqs
     * continue stamping from it — the anchor struct carries continuity.
     * edge_us is 64-bit non-atomic vs the ISR → snapshot with a seq check. */
    int32_t base = s_st.word_seq;
    vq_ts_anchor_t snap;
    int32_t es1, es2;
    do {
        es1 = s_st.anchor.edge_seq;
        snap.edge_us    = s_st.anchor.edge_us;
        snap.seq_anchor = s_st.anchor.seq_anchor;
        snap.rate_hz    = s_st.anchor.rate_hz;
        es2 = s_st.anchor.edge_seq;
    } while (es1 != es2);

    if (es1 != s_st.last_edge_seq) {
        /* fresh edge: the threshold word sits at index MX_DRAIN_WORDS-1 —
         * clamp to the newest word if the batch is short (best-effort).  */
        int32_t edge_idx = (int32_t)MX_DRAIN_WORDS - 1;
        if (edge_idx > (int32_t)cnt - 1) {
            edge_idx = (int32_t)cnt - 1;
        }
        snap.seq_anchor = base + edge_idx;
        snap.rate_hz    = MX_WORD_RATE_HZ;
        s_st.anchor.seq_anchor = snap.seq_anchor;
        s_st.last_edge_seq     = es1;
    }

    /* 4) decode words → samples */
    int n_out = 0;
    for (int i = 0; i < (int)cnt; i++) {
        const uint8_t *w = &s_st.fifo_buf[i * 3];
        uint8_t  tag = w[0] >> 3;
        uint32_t val = ((uint32_t)w[0] << 16 | (uint32_t)w[1] << 8 | w[2])
                       & 0x7FFFFu;                 /* 19-bit ADC counts      */
        int32_t seq = base + i;

        uint8_t flags = VQ_SAMPLE_F_ESTIMATED_TS;
        uint8_t ledc = mx_slot_ledc(tag);
        if (ledc == MX_LEDC_AMBIENT) {
            flags |= VQ_MX86141_F_AMBIENT;
        } else if (ledc == 0xFF || mx_tag_nm(ledc) == 0) {
            s_st.tag_err++;                        /* unexpected slot tag    */
        }
        if ((is1 & (MX_INT1_ALC_OVF | MX_INT1_LED_COMPB)) != 0) {
            flags |= VQ_SAMPLE_F_QUALITY_BAD;
        }
        if (ledc == MX_LEDC_LED1) {
            s_st.last_val = val;
        }
        s_st.last_tag = tag;

        vq_sample_t *sm = &s_st.out[n_out++];
        sm->channel = VQ_CH_PPG_SAT;
        sm->value   = (float)val;
        sm->flags   = flags;
        sm->t_us    = vq_ts_anchor_stamp(&snap, seq);
    }
    s_st.word_seq = base + cnt;

    /* 5) push — contract: samples stamped at acquisition, sink gets them */
    if (s_sensor.sink && n_out > 0) {
        s_sensor.sink(s_sensor.sink_ctx, s_st.out, (size_t)n_out);
        s_sensor.sample_count += (uint32_t)n_out;
        s_sensor.last_t_us    = s_st.out[n_out - 1].t_us;
    }
    return ESP_OK;
}

/* ---------------- Drain task ------------------------------------------------ */

static void mx_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MX_DRAIN_TIMEOUT_MS));
        if (s_st.task_exit) {
            break;
        }
        if (!s_st.running) {
            continue;
        }
        /* Drain until empty; also catches edges missed while the task was
         * busy (INT is level-held until status read, and A_FULL_TYPE=1
         * re-arms only on a new crossing). Timeout path = stall watchdog. */
        s_st.draining = true;
        for (int i = 0; i < 4 && s_st.running; i++) {
            uint8_t cnt = 0;
            if (mx_drain() != ESP_OK) {
                break;
            }
            if (mx_r8(s_st.hspi_run ? s_st.hspi_run : s_st.hspi_probe,
                      MX_REG_FIFO_DATA_COUNT, &cnt) != ESP_OK || cnt == 0) {
                break;
            }
        }
        s_st.draining = false;
        /* Stall detect: running but INT line is low and FIFO has data →
         * means an edge was lost; the drain above already handled it.   */
        if (gpio_get_level(VQ_MAX86141_INT_GPIO) == 0) {
            ESP_LOGD(TAG, "INT still low after drain");
        }
    }
    s_st.task = NULL;
    vTaskDelete(NULL);
}

static vq_err_t mx_task_start(void)
{
    if (s_st.task) {
        return ESP_OK;
    }
    s_st.task_exit = false;
    if (xTaskCreate(mx_task, "mx86141", MX_TASK_STACK, NULL,
                    MX_TASK_PRIO, &s_st.task) != pdPASS) {
        s_st.task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void mx_task_stop(void)
{
    if (s_st.task) {
        s_st.task_exit = true;
        xTaskNotifyGive(s_st.task);
        for (int i = 0; i < 20 && s_st.task; i++) {   /* ≤ ~100 ms */
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        s_st.task = NULL;      /* even if it didn't exit yet, drop handle */
    }
}

/* ---------------- GPIO INT arming ------------------------------------------- */

static vq_err_t mx_int_gpio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << VQ_MAX86141_INT_GPIO,
        .mode         = GPIO_MODE_INPUT,
        /* external R113 10k to +3V3; internal pull-up is a harmless backup */
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    vq_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    /* Shared service — another driver may have installed it first. */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "gpio_install_isr_service: 0x%x", err);
    }
    return gpio_isr_handler_add(VQ_MAX86141_INT_GPIO, mx_isr, NULL);
}

/* ---------------- Sensor ops ------------------------------------------------ */

static vq_err_t mx_probe(vq_sensor_t *s)
{
    (void)s;
    void *h = mx_handle(VQ_SPI_PROBE_HZ);          /* 1 MHz — gentle probe   */
    if (!h) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t id = 0;
    if (mx_r8(h, MX_REG_PART_ID, &id) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;                  /* SPI dead → absent      */
    }
    if (id != MX_PART_ID_MAX86141 && id != MX_PART_ID_MAX86140) {
        ESP_LOGW(TAG, "PART_ID=0x%02x — tail absent or wrong part", id);
        return ESP_ERR_NOT_FOUND;                  /* tolerated: required=0  */
    }
    s_st.probed = true;
    ESP_LOGI(TAG, "MAX8614x found, PART_ID=0x%02x", id);
    return ESP_OK;
}

static vq_err_t mx_init(vq_sensor_t *s)
{
    (void)s;
    if (!s_st.probed) {
        return ESP_ERR_INVALID_STATE;
    }
    void *h = mx_handle(VQ_SPI_MAX86141_HZ);       /* 8 MHz — streaming      */
    if (!h) {
        return ESP_FAIL;
    }

    /* soft reset → SHDN → program → flush; part stays in SHDN until start */
    vq_err_t err = mx_soft_reset(h);
    if (err == ESP_OK) { err = mx_w8(h, MX_REG_MODE_CONFIG, MX_MODE_SHDN); }
    if (err == ESP_OK) { err = mx_clear_interrupts(h); }
    if (err == ESP_OK) { err = mx_program_config(h); }
    /* write-verify: read back the exposure-sequence register */
    uint8_t rb = 0;
    if (err == ESP_OK) {
        err = mx_r8(h, MX_REG_LED_SEQ_1, &rb);
        if (err == ESP_OK &&
            rb != ((MX_LEDC_LED2 << 4) | MX_LEDC_LED1)) {
            ESP_LOGE(TAG, "LED_SEQ_1 readback 0x%02x != 0x21", rb);
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (err != ESP_OK) {
        s_st.spi_err++;
        return err;
    }

    err = mx_int_gpio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "INT gpio init failed (0x%x) — polling only", err);
        /* non-fatal: drain task still polls on its 1 s watchdog           */
    }
    return mx_task_start();
}

static vq_err_t mx_start(vq_sensor_t *s)
{
    (void)s;
    if (!s_st.probed || s_st.streaming) {
        return s_st.streaming ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    void *h = s_st.hspi_run;
    if (!h) {
        return ESP_ERR_INVALID_STATE;
    }

    /* LED scheduler gate FIRST — no light without a granted window. */
    vq_err_t err = mx_led_acquire();
    if (err != ESP_OK) {
        return err;
    }

    s_st.word_seq      = 0;
    s_st.last_edge_seq = s_st.anchor.edge_seq;  /* stale edges are consumed  */
    s_st.anchor.edge_us    = esp_timer_get_time(); /* sane stamps pre-1st INT */
    s_st.anchor.seq_anchor = 0;
    s_st.anchor.rate_hz    = MX_WORD_RATE_HZ;

    mx_flush_fifo(h);
    mx_clear_interrupts(h);

    /* A_FULL + ALC_OVF + LED_COMPB + VDD_OOR routed to the open-drain INT */
    err = mx_w8(h, MX_REG_INT_EN_1,
                MX_INT1_A_FULL | MX_INT1_ALC_OVF |
                MX_INT1_LED_COMPB | MX_INT1_VDD_OOR);
    if (err == ESP_OK) {
        /* leave SHDN: LP_MODE + SINGLE_PPG (PD1 only — PD2_IN unconnected) */
        err = mx_w8(h, MX_REG_MODE_CONFIG,
                    MX_MODE_LP_MODE | MX_MODE_SINGLE_PPG);
    }
    if (err != ESP_OK) {
        s_st.spi_err++;
        mx_led_release();
        return err;
    }
    s_st.running   = true;
    s_st.streaming = true;
    return ESP_OK;
}

static vq_err_t mx_stop(vq_sensor_t *s)
{
    (void)s;
    if (!s_st.streaming) {
        return ESP_OK;
    }
    s_st.running = false;                       /* drain task idles          */
    /* wait out any in-flight drain so the residue sweep can't interleave */
    for (int i = 0; i < 20 && s_st.draining; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    void *h = s_st.hspi_run;
    if (h) {
        mx_w8(h, MX_REG_INT_EN_1, 0x00);        /* stop routing INT          */
        mx_w8(h, MX_REG_MODE_CONFIG, MX_MODE_SHDN | MX_MODE_LP_MODE |
                                     MX_MODE_SINGLE_PPG);
        mx_drain();                             /* sweep any residue         */
    }
    s_st.streaming = false;
    mx_led_release();
    return ESP_OK;
}

static vq_err_t mx_sleep(vq_sensor_t *s)
{
    (void)s;
    s_st.running = false;
    s_st.streaming = false;
    /* deepest available state short of rail removal: SHDN + LP_MODE */
    void *h = s_st.hspi_run ? s_st.hspi_run : s_st.hspi_probe;
    if (h && s_st.probed) {
        mx_w8(h, MX_REG_INT_EN_1, 0x00);
        mx_w8(h, MX_REG_MODE_CONFIG, MX_MODE_SHDN | MX_MODE_LP_MODE);
    }
    mx_led_release();
    mx_task_stop();
    if (s_st.probed) {
        gpio_isr_handler_remove(VQ_MAX86141_INT_GPIO);
    }
    return ESP_OK;
}

static bool mx_healthy(vq_sensor_t *s)
{
    (void)s;
    if (!s_st.probed) {
        return false;
    }
    uint32_t errs = s_st.spi_err + s_st.tag_err + s_st.vdd_oor;
    return errs < MX_ERR_HEALTH_LIMIT;
}

/* ---------------- Registration ---------------------------------------------- */

static const vq_channel_id_t s_channels[] = { VQ_CH_PPG_SAT };

static const vq_sensor_ops_t s_ops = {
    .name       = "max86141",
    .probe      = mx_probe,
    .init       = mx_init,
    .start      = mx_start,
    .stop       = mx_stop,
    .sleep      = mx_sleep,
    .healthy    = mx_healthy,
    .channels   = s_channels,
    .n_channels = 1,
    .rate_hz    = 200.0f,
    .required   = false,        /* J10 tail may be unpopulated — never fatal */
};

esp_err_t vq_max86141_register(void)
{
    memset(&s_st, 0, sizeof(s_st));
    s_sensor.ops = &s_ops;
    s_sensor.priv = &s_st;
    return vq_sensor_register(&s_sensor);
}

esp_err_t vq_max86141_read_partid(uint8_t *id)
{
    if (!id) {
        return ESP_ERR_INVALID_ARG;
    }
    void *h = mx_handle(VQ_SPI_PROBE_HZ);
    if (!h) {
        return ESP_ERR_INVALID_STATE;
    }
    return mx_r8(h, MX_REG_PART_ID, id);
}

/* ---------------- Self-test --------------------------------------------------
 * Standalone only (refused while streaming). Exercises:
 *   - PART_ID at 1 MHz then 8 MHz
 *   - INT line idle-high, assert on A_FULL, release on status read
 *   - FIFO fill/drain with correct tags and sane word count
 *   - minimum-current LED pulse + PD response vs direct-ambient word
 * Restores SHDN state afterwards; a missing tail reports ABSENT, not fail. */

static int mx_st_append(char *buf, size_t len, int off, const char *fmt, ...)
{
    if (off < 0 || (size_t)off >= len) {
        return off;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + off, len - (size_t)off, fmt, ap);
    va_end(ap);
    return (n < 0) ? off : off + n;
}

esp_err_t vq_max86141_selftest(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_st.streaming) {
        snprintf(buf, len, "max86141: SKIP (streaming)\n");
        return ESP_ERR_INVALID_STATE;
    }
    buf[0] = '\0';
    int off = 0;
    bool pass = true;

    void *h1 = mx_handle(VQ_SPI_PROBE_HZ);
    void *h8 = mx_handle(VQ_SPI_MAX86141_HZ);
    if (!h1 || !h8) {
        snprintf(buf, len, "max86141: FAIL spi handle\n");
        return ESP_FAIL;
    }

    /* selftest may run before init() armed the pin — make sure it's a
     * pulled-up input so the level checks are meaningful. */
    gpio_set_direction(VQ_MAX86141_INT_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(VQ_MAX86141_INT_GPIO, GPIO_PULLUP_ONLY);

    /* --- 1. PART_ID @1 MHz ------------------------------------------------ */
    uint8_t id1 = 0, id8 = 0;
    vq_err_t e1 = mx_r8(h1, MX_REG_PART_ID, &id1);
    if (e1 != ESP_OK || (id1 != MX_PART_ID_MAX86141 &&
                         id1 != MX_PART_ID_MAX86140)) {
        off = mx_st_append(buf, len, off,
            "max86141: ABSENT/ FAIL id@1MHz=0x%02x err=0x%x "
            "(tail unpopulated is OK)\n", id1, e1);
        return ESP_ERR_NOT_FOUND;
    }
    off = mx_st_append(buf, len, off, "partid@1MHz: 0x%02x PASS\n", id1);

    /* --- 2. PART_ID @8 MHz ------------------------------------------------- */
    vq_err_t e8 = mx_r8(h8, MX_REG_PART_ID, &id8);
    if (e8 != ESP_OK || id8 != id1) {
        off = mx_st_append(buf, len, off,
            "partid@8MHz: 0x%02x err=0x%x FAIL\n", id8, e8);
        pass = false;
        goto out;
    }
    off = mx_st_append(buf, len, off, "partid@8MHz: 0x%02x PASS\n", id8);

    /* --- 3. reset → configure minimal sequence for fill/drain test -------- */
    mx_soft_reset(h8);
    mx_w8(h8, MX_REG_MODE_CONFIG, MX_MODE_SHDN);
    mx_clear_interrupts(h8);
    /* single-exposure sequence [LED1] @400 sps for a quick fill;           *
     * LED at min current — PD response measured separately below.          */
    mx_w8(h8, MX_REG_PPG_CONFIG_1, (MX_ADC_RGE_16UA << 2) | MX_TINT_117US);
    mx_w8(h8, MX_REG_PPG_CONFIG_2, (MX_SR_400SPS << 3) | MX_SMP_AVE_1);
    mx_w8(h8, MX_REG_PPG_CONFIG_3, (MX_LED_SETLNG_12US << 6));
    mx_w8(h8, MX_REG_PD_BIAS, 0x01);
    mx_w8(h8, MX_REG_LED_SEQ_1, MX_LEDC_LED1);           /* LEDC1 = LED1    */
    mx_w8(h8, MX_REG_LED_SEQ_2, 0x00);
    mx_w8(h8, MX_REG_LED_SEQ_3, 0x00);
    mx_w8(h8, MX_REG_LED_RANGE_1, 0x3F);
    mx_w8(h8, MX_REG_LED1_PA, MX_LED_SELFTEST_PA);       /* ~3.9 mA         */
    mx_w8(h8, MX_REG_FIFO_CONFIG_1, 128 - 16);           /* A_FULL @16 words*/
    mx_flush_fifo(h8);

    /* INT idle check: with status cleared the open-drain line should sit
     * high on R113. */
    int lvl_idle = gpio_get_level(VQ_MAX86141_INT_GPIO);
    off = mx_st_append(buf, len, off, "int_idle: %s (lvl=%d)\n",
                       lvl_idle == 1 ? "PASS" : "FAIL", lvl_idle);
    if (lvl_idle != 1) {
        pass = false;
    }

    /* --- 4. run ~60 ms @400 sps → expect ~24 words; INT must assert ------- */
    mx_w8(h8, MX_REG_INT_EN_1, MX_INT1_A_FULL);
    mx_w8(h8, MX_REG_MODE_CONFIG, MX_MODE_SINGLE_PPG);   /* SHDN=0 → run    */
    vTaskDelay(pdMS_TO_TICKS(60));

    int lvl_low = gpio_get_level(VQ_MAX86141_INT_GPIO);
    uint8_t cnt = 0, is1 = 0;
    mx_r8(h8, MX_REG_INT_STAT_1, &is1);                  /* clears the line */
    mx_r8(h8, MX_REG_FIFO_DATA_COUNT, &cnt);
    off = mx_st_append(buf, len, off,
        "fifo_fill: count=%u (exp ~24) is1=0x%02x %s\n",
        (unsigned)cnt, is1, (cnt >= 8 && cnt <= 40) ? "PASS" : "FAIL");
    if (cnt < 8 || cnt > 40) {
        pass = false;
    }
    off = mx_st_append(buf, len, off, "int_assert: %s (lvl=%d)\n",
                       lvl_low == 0 ? "PASS" : "FAIL", lvl_low);
    if (lvl_low != 0) {
        pass = false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
    int lvl_rel = gpio_get_level(VQ_MAX86141_INT_GPIO);
    off = mx_st_append(buf, len, off, "int_release: %s (lvl=%d)\n",
                       lvl_rel == 1 ? "PASS" : "FAIL", lvl_rel);
    if (lvl_rel != 1) {
        pass = false;      /* with FIFO_STAT_CLR, drain clears it below too */
    }

    /* --- 5. drain + tag check --------------------------------------------- */
    uint32_t led_sum = 0, amb_sum = 0;
    int led_n = 0, amb_n = 0, bad_tag = 0;
    if (cnt > 0 && cnt <= MX_FIFO_DEPTH &&
        mx_fifo_burst(h8, s_st.fifo_buf, cnt) == ESP_OK) {
        for (int i = 0; i < (int)cnt; i++) {
            const uint8_t *w = &s_st.fifo_buf[i * 3];
            uint8_t  tag = w[0] >> 3;
            uint32_t val = ((uint32_t)w[0] << 16 | (uint32_t)w[1] << 8 | w[2])
                           & 0x7FFFFu;
            if (tag == MX_LEDC_LED1) {
                led_sum += val; led_n++;
            } else if (tag == MX_LEDC_AMBIENT) {
                amb_sum += val; amb_n++;
            } else {
                bad_tag++;
            }
        }
    }
    off = mx_st_append(buf, len, off,
        "fifo_drain: n=%u led_words=%d bad_tag=%d %s\n",
        (unsigned)cnt, led_n, bad_tag, bad_tag == 0 ? "PASS" : "FAIL");
    if (bad_tag) {
        pass = false;
    }

    /* --- 6. LED pulse vs ambient (PD response) ------------------------------
     * Sequence [LED1, AMB] at minimum current; compare mean LED-phase counts
     * vs ambient-phase counts. The emitter can only light with TX_5V up, so
     * declare the pulse to the scheduler and hold TX_5V first — if the LED
     * scheduler isn't up yet (selftest may run pre-pipeline) proceed anyway
     * at this minimal current and annotate tx5=0. */
    mx_w8(h8, MX_REG_MODE_CONFIG, MX_MODE_SHDN | MX_MODE_SINGLE_PPG);
    mx_clear_interrupts(h8);
    mx_w8(h8, MX_REG_LED_SEQ_1, (MX_LEDC_AMBIENT << 4) | MX_LEDC_LED1);
    mx_flush_fifo(h8);

    vq_led_request_t req = {
        .chain = VQ_LED_MAX86141,
        .pulse_mA = (MX_LED_SELFTEST_PA * 486 + 500) / 1000,    /* ≈4 mA    */
        .on_us = 200, .period_us = 0, .priority = 0,
    };
    (void)vq_led_submit(&req, NULL, NULL, &s_st, NULL);
    bool tx5 = (vq_led_tx5_acquire() == ESP_OK);

    mx_w8(h8, MX_REG_MODE_CONFIG, MX_MODE_SINGLE_PPG);   /* run burst        */
    vTaskDelay(pdMS_TO_TICKS(40));                       /* ~8 frames @400   */
    mx_w8(h8, MX_REG_MODE_CONFIG, MX_MODE_SHDN | MX_MODE_SINGLE_PPG);
    mx_r8(h8, MX_REG_INT_STAT_1, &is1);
    mx_r8(h8, MX_REG_FIFO_DATA_COUNT, &cnt);
    led_sum = amb_sum = 0; led_n = amb_n = 0;
    if (cnt > 0 && cnt <= MX_FIFO_DEPTH &&
        mx_fifo_burst(h8, s_st.fifo_buf, cnt) == ESP_OK) {
        for (int i = 0; i < (int)cnt; i++) {
            const uint8_t *w = &s_st.fifo_buf[i * 3];
            uint8_t  tag = w[0] >> 3;
            uint32_t val = ((uint32_t)w[0] << 16 | (uint32_t)w[1] << 8 | w[2])
                           & 0x7FFFFu;
            if (tag == MX_LEDC_LED1)        { led_sum += val; led_n++; }
            else if (tag == MX_LEDC_AMBIENT) { amb_sum += val; amb_n++; }
        }
    }
    uint32_t led_avg = led_n ? led_sum / (uint32_t)led_n : 0;
    uint32_t amb_avg = amb_n ? amb_sum / (uint32_t)amb_n : 0;
    bool pd_resp = (led_n > 0 && amb_n > 0 && led_avg > amb_avg);
    off = mx_st_append(buf, len, off,
        "led_pulse: led_avg=%" PRIu32 " amb_avg=%" PRIu32
        " led_n=%d amb_n=%d tx5=%d → %s\n",
        led_avg, amb_avg, led_n, amb_n, (int)tx5,
        pd_resp ? "PD_RESP PASS" :
                  "PD_RESP FAIL (tail/optics absent or dark)");
    /* PD response absence is reported, not fatal — tail may be unpopulated */
    if (tx5) {
        vq_led_tx5_release();
    }
    vq_led_cancel(VQ_LED_MAX86141);

    /* --- 7. park the part --------------------------------------------------- */
    mx_w8(h8, MX_REG_INT_EN_1, 0x00);
    mx_w8(h8, MX_REG_MODE_CONFIG, MX_MODE_SHDN | MX_MODE_SINGLE_PPG);
    mx_clear_interrupts(h8);
    mx_flush_fifo(h8);

out:
    off = mx_st_append(buf, len, off, "max86141: %s\n",
                       pass ? "PASS" : "FAIL");
    return pass ? ESP_OK : ESP_FAIL;
}
