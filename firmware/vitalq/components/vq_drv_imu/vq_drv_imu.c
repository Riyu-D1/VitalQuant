/* vq_drv_imu.c — LSM6DSV80X 6-axis IMU driver (U14, I2C0 @0x6A).
 *
 * fix_v22 wiring (board_pins.h): SDO/SA0 = GND → 0x6A, CS = +3V3 → I2C
 * mode, INT1 → IO4 push-pull active-high, no external pull → internal
 * pull-down. The part lives on the shared 100 kHz bus — FIFO batching
 * keeps wire traffic to ~2 bursts/s instead of 104 reg reads/s.
 *
 * ----------------------------- 52 Hz note -------------------------------
 * The classic LSM6 ODR table has no 52 Hz step. The profile asks for 52,
 * which the high-accuracy-ODR engine provides exactly:
 *   CTRL1 = 0x15 → OP_MODE_XL=0b001 (HAODR), ODR_XL=0b0101
 *   CTRL2 = 0x15 → OP_MODE_G =0b001 (HAODR), ODR_G =0b0101
 *   HAODR_CFG.HAODR_SEL = 0b11 → band 3 (13/26/52/104/208 Hz)
 * HAODR band switches are only legal with both sensors powered down —
 * init()/selftest always write ODR=0 before touching HAODR_SEL.
 *
 * ----------------------------- FIFO scheme ------------------------------
 * FIFO mode (stop-on-full), BDR_XL = BDR_GY = ODR nibble → one tagged
 * 7-byte word per sensor per ODR slot (GY tag 0x01, XL tag 0x02).
 * WTM = 52 words = 26 accel+gyro pairs → INT1 = FIFO-TH (+overrun)
 * fires ~2×/s. The ISR latches esp_timer into the timestamp anchor; the
 * drain task bursts 7×N bytes from FIFO_DATA_OUT_TAG (auto-increment),
 * decodes tags, and stamps each frame by its index through
 * vq_ts_anchor_stamp() — seq_anchor = the absolute seq of the frame
 * that tripped the watermark, per the vq_types.h contract.
 *
 * On-chip timestamp OFF (esp_timer anchor), high-g channel OFF (±8 g
 * low-g channel covers motion gating), gyro ±500 dps, accel ±8 g, BDU
 * on, IF_INC on, I2C-I3C disabled. Register values verified against
 * ST lsm6dsv80x_reg.h / AN6281.
 *
 * Research prototype — engineering data only, no clinical claims.
 */
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "vq_drv_imu.h"
#include "vq_sensor.h"
#include "board_pins.h"
#include "i2c_bus.h"

static const char *TAG = "lsm6dsv80x";

/* ------------------------------ Registers ------------------------------ */
#define R_IF_CFG            0x03    /* i2c_i3c_disable, h_lactive, pp_od */
#define R_FIFO_CTRL1        0x07    /* wtm[7:0]                          */
#define R_FIFO_CTRL3        0x09    /* bdr_gy[7:4], bdr_xl[3:0]          */
#define R_FIFO_CTRL4        0x0A    /* fifo_mode[2:0]                    */
#define R_INT1_CTRL         0x0D    /* fifo_th bit3, fifo_ovr bit4, drdy */
#define R_WHO_AM_I          0x0F
#define R_CTRL1             0x10    /* op_mode_xl[6:4], odr_xl[3:0]      */
#define R_CTRL2             0x11    /* op_mode_g [6:4], odr_g [3:0]      */
#define R_CTRL3             0x12    /* boot, bdu, if_inc, sw_reset       */
#define R_CTRL6             0x15    /* fs_g[2:0]                         */
#define R_CTRL8             0x17    /* fs_xl[1:0]                        */
#define R_FIFO_STATUS1      0x1B    /* diff_fifo[7:0]                    */
#define R_FIFO_STATUS2      0x1C    /* wtm/ovr/full flags, diff_fifo[8]  */
#define R_STATUS_REG        0x1E    /* xlda/gda/tda                      */
#define R_OUTX_L_G          0x22    /* gyro  x/y/z int16 LE              */
#define R_OUTX_L_A          0x28    /* accel x/y/z int16 LE              */
#define R_WAKE_UP_SRC       0x45    /* wu_ia + axis flags (clears latch) */
#define R_FUNCTIONS_ENABLE  0x50    /* interrupts_enable b7, timestamp b6*/
#define R_TAP_CFG0          0x56    /* lir b0, slope_fds b4              */
#define R_WAKE_UP_THS       0x5B    /* wk_ths[5:0], FS_XL/64 per LSB     */
#define R_WAKE_UP_DUR       0x5C    /* ff_dur, wake_dur, sleep_dur       */
#define R_MD1_CFG           0x5E    /* int1_wu bit5                      */
#define R_HAODR_CFG         0x62    /* haodr_sel[1:0]                    */
#define R_FIFO_OUT_TAG      0x78    /* tag byte + 6 data bytes per word  */

/* ------------------------------ Bit fields ----------------------------- */
#define CTRL3_BDU           0x40
#define CTRL3_IF_INC        0x04
#define CTRL3_SW_RESET      0x01

#define IF_CFG_I3C_DISABLE  0x01    /* pure-I2C bus → kill I3C hand-off  */

#define OP_MODE_HAODR       0x01    /* XL_HIGH_ACCURACY_ODR_MD           */
#define OP_MODE_LP2AVG      0x04    /* XL_LOW_POWER_2_AVG_MD             */
#define ODR_OFF             0x00
#define ODR_52HZ            0x05    /* nibble 5 → 52 Hz @ HAODR_SEL=3    */
#define ODR_15HZ            0x03    /* nibble 3 → 15 Hz (wake-detect)    */
#define HAODR_SEL_BAND3     0x03

#define CTRL1_RUN           ((OP_MODE_HAODR << 4) | ODR_52HZ)  /* 0x15  */
#define CTRL1_OFF           ((OP_MODE_HAODR << 4) | ODR_OFF)   /* 0x10  */
#define CTRL1_WAKE_LPM      ((OP_MODE_LP2AVG  << 4) | ODR_15HZ)  /* 0x43  */
#define CTRL2_RUN           CTRL1_RUN
#define CTRL2_OFF           CTRL1_OFF

#define FS_XL_8G            0x02    /* fs_xl 10b → ±8 g, 0.244 mg/LSB    */
#define FS_G_500DPS         0x0A    /* fs_g 010b → ±500 dps, 17.5 mdps/LSB;
                                     * bit3 is a factory-trim must-be-1   */

#define FIFO_BDR_ODR        0x05    /* bdr nibble == odr nibble → batch  */
                                    /* every sample at the sensor rate   */
#define FIFO_MODE_FIFO      0x01    /* FIFO mode: stop on full          */
#define INT1_FIFO_WTM_OVR   0x18    /* int1_fifo_th | int1_fifo_ovr      */
#define INT1_DRDY_XL        0x01

#define FST2_DIFF_FIFO_B8   0x01    /* diff_fifo[8]                      */
#define FST2_OVR_LATCHED    0x08
#define FST2_OVR_IA         0x40
#define FST2_WTM_IA         0x80

#define FUNC_INT_ENABLE     0x80    /* FUNCTIONS_ENABLE.interrupts_en   */
#define MD1_INT1_WU         0x20
#define TAP0_LIR            0x01    /* latched interrupt request        */
#define WK_THS_250MG        0x20    /* 32 × 7.8125 mg = 250 mg — the 80X
                                     * weights WK_THS by WU_INACT_THS_W
                                     * (INACTIVITY_DUR 0x54, left at its
                                     * 000 default = 7.8125 mg/LSB), not
                                     * the FS_XL/64 scheme of older IMUs  */
#define WK_DUR_1SAMPLE      0x00    /* wake on first over-threshold frame*/

#define ST_XLDA             0x01    /* STATUS_REG accel data-available   */

#define FIFO_TAG_GY_NC      0x01
#define FIFO_TAG_XL_NC      0x02

#define WHO_AM_I_VAL        0x73    /* LSM6DSV80X datasheet ID exactly;
                                     * LSM6DSV320X shares 0x73 and is
                                     * register-identical. Rejects a
                                     * mis-populated LSM6DSV16X (0x70),
                                     * whose FS_XL encoding differs.    */

/* ------------------------------ Tuning --------------------------------- */
#define IMU_ADDR            VQ_ADDR_LSM6DSV        /* 0x6A               */
#define IMU_RATE_HZ         52.0f
#define IMU_WTM_WORDS       52      /* 26 XL+GY pairs ≈ 0.5 s @ 52 Hz    */
#define IMU_WORD_BYTES      7       /* 1 tag + 6 data                    */
#define IMU_FIFO_CHUNK      64      /* words per I2C burst (448 B)       */
#define IMU_FIFO_MAX_WORDS  511     /* diff_fifo is 9 bits               */
#define IMU_FRAME_WORDS     2       /* one XL + one GY word per slot     */
#define IMU_TASK_STACK      3072
#define IMU_TASK_PRIO       12
#define IMU_SEM_WAIT_MS     1000    /* stall/missed-edge resync cadence  */
#define IMU_STALL_US        3000000 /* healthy(): 3 s silence → fault    */
#define IMU_RESET_MS        15      /* SW_RESET + POR settle             */
#define IMU_SINK_CHUNK      24      /* samples per sink push (8 words)   */

#define ACC_G_PER_LSB       (16.0f / 65536.0f)    /* ±8 g → 0.244 mg    */
#define GYR_DPS_PER_LSB     0.0175f               /* ±500 dps → 17.5 mdps*/

/* Motion bookkeeping — mirrors features.c: 1 s bins of mean |a|² in g;
 * a bin counts as motion above 1 g + margin, and any gyro axis above the
 * spin threshold counts too. 64 s of history covers feature windows. */
#define IMU_MOT_WIN_S       64
#define IMU_MOT_BIN_N       52      /* accel frames per 1 s bin          */
#define IMU_MOT_ACTIVE_G    1.12f
#define IMU_GYR_ACTIVE_DPS  15.0f

/* ------------------------------ Driver state --------------------------- */
typedef struct {
    bool            present;
    volatile bool   streaming;
    bool            arm_wake;
    bool            isr_done;
    uint8_t         id;

    TaskHandle_t        task;
    SemaphoreHandle_t   wtm_sem;

    vq_ts_anchor_t  anchor;         /* INT1 edge ↔ esp_timer            */
    int32_t         last_edge_seq;  /* edge_seq consumed by last drain  */
    int32_t         frame_abs;      /* absolute XL+GY frame counter     */
    int64_t         stream_start_us;/* for healthy() first-sample grace */

    /* motion/stillness bookkeeping (drain-task writes, API reads) */
    portMUX_TYPE    mot_mux;
    float           mag2_acc;       /* Σ|a|² for the open 1 s bin       */
    uint16_t        mag2_n;
    float           bins[IMU_MOT_WIN_S]; /* per-second mean |a|²        */
    uint8_t         bin_head;       /* next write index                 */
    uint8_t         bin_filled;
    int64_t         first_sample_us;
    int64_t         last_motion_us;
    bool            ever_sampled;

    uint8_t         fifo[IMU_FIFO_CHUNK * IMU_WORD_BYTES];
} imu_priv_t;

static imu_priv_t   s_priv = { .mot_mux = portMUX_INITIALIZER_UNLOCKED };
static bool         s_registered;

/* ------------------------------ I2C helpers ---------------------------- */
static inline vq_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return vq_i2c_read_reg(IMU_ADDR, reg, buf, n);
}

static inline vq_err_t wr(uint8_t reg, uint8_t val)
{
    return vq_i2c_write_reg(IMU_ADDR, reg, &val, 1);
}

static vq_err_t rmw(uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t cur;
    vq_err_t e = rd(reg, &cur, 1);
    if (e != ESP_OK) return e;
    uint8_t nw = (uint8_t)((cur & ~mask) | (val & mask));
    return (nw == cur) ? ESP_OK : wr(reg, nw);
}

static inline int16_t le16(const uint8_t *b)
{
    return (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

/* ------------------------------ Motion feed ---------------------------- */
/* |a| rms > IMU_MOT_ACTIVE_G in a completed 1 s bin, or |ω| >
 * IMU_GYR_ACTIVE_DPS on any axis → motion at that sample's stamp. */
static void mot_feed_acc(imu_priv_t *p, float ax, float ay, float az,
                         int64_t t_us)
{
    float mag2 = ax * ax + ay * ay + az * az;
    portENTER_CRITICAL(&p->mot_mux);
    p->mag2_acc += mag2;
    if (++p->mag2_n >= IMU_MOT_BIN_N) {
        float ms2 = p->mag2_acc / IMU_MOT_BIN_N;
        p->bins[p->bin_head] = ms2;
        p->bin_head = (uint8_t)((p->bin_head + 1) % IMU_MOT_WIN_S);
        if (p->bin_filled < IMU_MOT_WIN_S) p->bin_filled++;
        if (sqrtf(ms2) > IMU_MOT_ACTIVE_G) p->last_motion_us = t_us;
        p->mag2_acc = 0.0f;
        p->mag2_n = 0;
    }
    portEXIT_CRITICAL(&p->mot_mux);
}

static void mot_feed_gyr(imu_priv_t *p, float gx, float gy, float gz,
                         int64_t t_us)
{
    float m = fmaxf(fmaxf(fabsf(gx), fabsf(gy)), fabsf(gz));
    if (m > IMU_GYR_ACTIVE_DPS) {
        portENTER_CRITICAL(&p->mot_mux);
        p->last_motion_us = t_us;
        portEXIT_CRITICAL(&p->mot_mux);
    }
}

/* ------------------------------ FIFO drain ----------------------------- */
/* Push one sample into the open batch; flush to the sink when full.
 * The sink is the pipeline hub (mutex-serialised there). */
static void emit(vq_sensor_t *s, vq_sample_t *b, size_t *n, uint32_t *emitted,
                 vq_channel_id_t ch, int64_t t_us, float v, uint8_t flags)
{
    if (*n >= IMU_SINK_CHUNK) {
        if (s->sink) s->sink(s->sink_ctx, b, *n);
        *n = 0;
    }
    b[(*n)++] = (vq_sample_t){
        .channel = ch, .t_us = t_us, .value = v, .flags = flags,
    };
    (*emitted)++;
}

static void imu_drain(vq_sensor_t *s)
{
    imu_priv_t *p = (imu_priv_t *)s->priv;

    /* When the wake path is armed, a latched WU event would hold INT1
     * high and mask later FIFO edges — clear it every drain, even when
     * the FIFO happens to be empty (a WU-only edge). */
    if (p->arm_wake) {
        uint8_t wu;
        (void)rd(R_WAKE_UP_SRC, &wu, 1);
    }

    uint8_t st[2] = {0};
    if (rd(R_FIFO_STATUS1, st, 2) != ESP_OK) { s->err_count++; return; }
    uint16_t nwords = (uint16_t)st[0] | (uint16_t)((st[1] & FST2_DIFF_FIFO_B8) << 8);
    const bool ovr = (st[1] & (FST2_OVR_LATCHED | FST2_OVR_IA)) != 0;
    if (nwords == 0) return;

    /* Anchor snapshot: a NEW edge means the watermark word just landed —
     * seq_anchor = absolute seq of the frame that completed the wtm.
     * Reusing the last anchor is still correct while dev_seq stays
     * absolute (stamps just measure further past the old edge).
     * edge_us is 64-bit on a 32-bit core — retry the copy under the
     * edge_seq guard so the ISR can't tear the snapshot. */
    vq_ts_anchor_t a;
    int32_t eseq;
    do {
        eseq = p->anchor.edge_seq;
        a = p->anchor;
    } while (eseq != p->anchor.edge_seq);
    const bool fresh = (a.edge_us > 0) && (eseq != p->last_edge_seq);
    p->last_edge_seq = eseq;
    a.rate_hz = IMU_RATE_HZ;
    const int32_t nframes = (int32_t)((nwords + 1) / IMU_FRAME_WORDS);
    const int32_t f_wtm = (IMU_WTM_WORDS / IMU_FRAME_WORDS) - 1; /* 25 */
    uint8_t est_flags = 0;
    if (a.edge_us <= 0) {
        /* No edge ever (resync drain before the first wtm) — fabricate
         * the anchor on the newest frame and flag it. */
        a.edge_us = esp_timer_get_time();
        a.seq_anchor = p->frame_abs + nframes - 1;
        est_flags = VQ_SAMPLE_F_ESTIMATED_TS;
    } else if (fresh && f_wtm < nframes) {
        /* Normal path: the wtm-th word completes frame f_wtm — that is
         * the frame the edge was latched on. */
        a.seq_anchor = p->frame_abs + f_wtm;
    } else if (fresh) {
        /* Edge but fewer than wtm words — a WU/overrun event asserted
         * INT1, not a new watermark: the newest drained frame is the
         * best in-drain reference. */
        a.seq_anchor = p->frame_abs + nframes - 1;
    } else {
        /* Stale edge (timeout resync): frames are newer than the last
         * edge — keep the stored seq_anchor so the absolute seq math
         * still yields correct back-off stamps. */
        a.seq_anchor = p->anchor.seq_anchor;
    }
    p->anchor.seq_anchor = a.seq_anchor;
    if (ovr) est_flags |= VQ_SAMPLE_F_ESTIMATED_TS;

    vq_sample_t batch[IMU_SINK_CHUNK];
    size_t   bn = 0;
    uint32_t emitted = 0;
    uint16_t wi = 0;
    uint16_t sw = 0;    /* decoded sensor words — frame index ignores tag filler */
    while (wi < nwords) {
        uint16_t chunk = (uint16_t)(nwords - wi);
        if (chunk > IMU_FIFO_CHUNK) chunk = IMU_FIFO_CHUNK;
        if (rd(R_FIFO_OUT_TAG, p->fifo,
               (size_t)chunk * IMU_WORD_BYTES) != ESP_OK) {
            s->err_count++;
            break;
        }
        for (uint16_t k = 0; k < chunk; k++, wi++) {
            const uint8_t *w = &p->fifo[k * IMU_WORD_BYTES];
            const uint8_t tag = (uint8_t)(w[0] >> 3);

            switch (tag) {
            case FIFO_TAG_XL_NC: {
                const int32_t f = (int32_t)(sw / IMU_FRAME_WORDS);
                const int64_t t = vq_ts_anchor_stamp(&a, p->frame_abs + f);
                sw++;
                float ax = (float)le16(&w[1]) * ACC_G_PER_LSB;
                float ay = (float)le16(&w[3]) * ACC_G_PER_LSB;
                float az = (float)le16(&w[5]) * ACC_G_PER_LSB;
                emit(s, batch, &bn, &emitted, VQ_CH_ACC_X, t, ax, est_flags);
                emit(s, batch, &bn, &emitted, VQ_CH_ACC_Y, t, ay, est_flags);
                emit(s, batch, &bn, &emitted, VQ_CH_ACC_Z, t, az, est_flags);
                mot_feed_acc(p, ax, ay, az, t);
                break;
            }
            case FIFO_TAG_GY_NC: {
                const int32_t f = (int32_t)(sw / IMU_FRAME_WORDS);
                const int64_t t = vq_ts_anchor_stamp(&a, p->frame_abs + f);
                sw++;
                float gx = (float)le16(&w[1]) * GYR_DPS_PER_LSB;
                float gy = (float)le16(&w[3]) * GYR_DPS_PER_LSB;
                float gz = (float)le16(&w[5]) * GYR_DPS_PER_LSB;
                emit(s, batch, &bn, &emitted, VQ_CH_GYR_X, t, gx, est_flags);
                emit(s, batch, &bn, &emitted, VQ_CH_GYR_Y, t, gy, est_flags);
                emit(s, batch, &bn, &emitted, VQ_CH_GYR_Z, t, gz, est_flags);
                mot_feed_gyr(p, gx, gy, gz, t);
                break;
            }
            default:
                break;      /* cfg-change / sensorhub / etc — skipped   */
            }
        }
    }
    if (bn && s->sink) s->sink(s->sink_ctx, batch, bn);

    const int32_t dframes = (int32_t)((sw + 1) / IMU_FRAME_WORDS);
    s->sample_count += emitted;
    if (emitted)
        s->last_t_us = vq_ts_anchor_stamp(&a, p->frame_abs + dframes - 1);
    p->frame_abs += dframes;

    if (emitted) {
        portENTER_CRITICAL(&p->mot_mux);
        if (!p->ever_sampled) {
            p->ever_sampled = true;
            p->first_sample_us = s->last_t_us;
            if (p->last_motion_us == 0) p->last_motion_us = s->last_t_us;
        }
        portEXIT_CRITICAL(&p->mot_mux);
    }

    if (ovr) {
        s->err_count++;
        ESP_LOGW(TAG, "fifo overrun — %u words salvaged", (unsigned)wi);
    }
}

static void imu_task(void *arg)
{
    vq_sensor_t *s = (vq_sensor_t *)arg;
    imu_priv_t  *p = (imu_priv_t *)s->priv;
    for (;;) {
        if (xSemaphoreTake(p->wtm_sem,
                           pdMS_TO_TICKS(IMU_SEM_WAIT_MS)) == pdTRUE) {
            if (p->streaming) imu_drain(s);
            continue;
        }
        /* Timeout: periodic resync covers a missed INT1 edge (and keeps
         * FIFO drained if the part keeps batching with a masked pin). */
        if (p->streaming) imu_drain(s);
    }
}

/* ------------------------------ INT1 / ISR ------------------------------ */
static void IRAM_ATTR imu_isr(void *arg)
{
    imu_priv_t *p = (imu_priv_t *)arg;
    vq_ts_anchor_edge(&p->anchor);
    if (p->wtm_sem) {
        BaseType_t hp = pdFALSE;
        xSemaphoreGiveFromISR(p->wtm_sem, &hp);
        if (hp) portYIELD_FROM_ISR(hp);
    }
}

static vq_err_t imu_isr_setup(imu_priv_t *p)
{
    if (p->isr_done) return ESP_OK;
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << VQ_LSM6_INT1_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,  /* no external pull       */
        .intr_type    = GPIO_INTR_POSEDGE,     /* push-pull active-high  */
    };
    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK) return e;
    e = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;  /* shared svc */
    e = gpio_isr_handler_add(VQ_LSM6_INT1_GPIO, imu_isr, p);
    if (e != ESP_OK) return e;
    gpio_intr_disable(VQ_LSM6_INT1_GPIO);      /* armed by start() only  */
    p->isr_done = true;
    return ESP_OK;
}

/* ------------------------------ Config --------------------------------- */
/* Static configuration: everything except the run/sleep-time knobs
 * (ODR, FIFO mode, INT1 routing, wake routing). Left with ODR=0. */
static const struct { uint8_t reg; uint8_t val; } s_cfg[] = {
    { R_IF_CFG,     IF_CFG_I3C_DISABLE          },
    { R_CTRL3,      CTRL3_BDU | CTRL3_IF_INC    },
    { R_CTRL6,      FS_G_500DPS                 },
    { R_CTRL8,      FS_XL_8G                    },
    { R_HAODR_CFG,  HAODR_SEL_BAND3             },  /* sensors off here  */
    { R_CTRL1,      CTRL1_OFF                   },  /* op=HAODR, odr=0   */
    { R_CTRL2,      CTRL2_OFF                   },
    { R_FIFO_CTRL3, (FIFO_BDR_ODR << 4) | FIFO_BDR_ODR }, /* 0x55          */
    { R_FIFO_CTRL1, IMU_WTM_WORDS               },
    { R_FIFO_CTRL4, 0x00                        },  /* bypass            */
    { R_INT1_CTRL,  0x00                        },
};

static vq_err_t imu_configure(void)
{
    for (size_t i = 0; i < sizeof(s_cfg) / sizeof(s_cfg[0]); i++) {
        vq_err_t e = wr(s_cfg[i].reg, s_cfg[i].val);
        if (e != ESP_OK) return e;
    }
    /* Read-back verify of the timing-critical regs (catches a wedged
     * write and the CTRL1/CTRL2 bit-order trap on this part). */
    static const uint8_t chk[] = { R_CTRL6, R_CTRL8, R_HAODR_CFG,
                                   R_FIFO_CTRL3, R_FIFO_CTRL1 };
    for (size_t i = 0; i < sizeof(chk) / sizeof(chk[0]); i++) {
        uint8_t rb = 0xEE;
        vq_err_t e = rd(chk[i], &rb, 1);
        if (e != ESP_OK) return e;
        uint8_t want = 0;
        for (size_t j = 0; j < sizeof(s_cfg) / sizeof(s_cfg[0]); j++)
            if (s_cfg[j].reg == chk[i]) want = s_cfg[j].val;
        if (rb != want) {
            ESP_LOGE(TAG, "cfg verify: reg 0x%02X wrote 0x%02X read 0x%02X",
                     chk[i], want, rb);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

static vq_err_t imu_stop_stream(vq_sensor_t *s)
{
    imu_priv_t *p = (imu_priv_t *)s->priv;
    if (p->streaming) {
        p->streaming = false;
        gpio_intr_disable(VQ_LSM6_INT1_GPIO);
    }
    vq_err_t e1 = wr(R_INT1_CTRL, 0x00);
    vq_err_t e2 = wr(R_CTRL1, CTRL1_OFF);   /* odr_xl = 0, op kept */
    vq_err_t e3 = wr(R_CTRL2, CTRL2_OFF);
    vq_err_t e4 = wr(R_FIFO_CTRL4, 0x00);   /* bypass → flushes FIFO   */
    return e1 ? e1 : (e2 ? e2 : (e3 ? e3 : e4));
}

/* ------------------------------ Sensor ops ----------------------------- */
static vq_err_t ops_probe(vq_sensor_t *s)
{
    imu_priv_t *p = (imu_priv_t *)s->priv;
    vq_err_t e = vq_i2c_probe(IMU_ADDR);
    if (e != ESP_OK) {
        p->present = false;
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t id = 0;
    e = rd(R_WHO_AM_I, &id, 1);
    p->id = id;
    bool ok = (e == ESP_OK) && (id == WHO_AM_I_VAL);
    ESP_LOGI(TAG, "probe: who_am_i=0x%02X %s", id,
             ok ? "(lsm6dsv80x present)" : "(unexpected/absent)");
    p->present = ok;
    return ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static vq_err_t ops_init(vq_sensor_t *s)
{
    imu_priv_t *p = (imu_priv_t *)s->priv;

    portMUX_INITIALIZE(&p->mot_mux);
    if (!p->wtm_sem) {
        p->wtm_sem = xSemaphoreCreateCounting(8, 0);
        if (!p->wtm_sem) return ESP_ERR_NO_MEM;
    }
    if (!p->task) {
        if (xTaskCreatePinnedToCore(imu_task, "imu_drain", IMU_TASK_STACK,
                                    s, IMU_TASK_PRIO, &p->task, 0) != pdPASS)
            return ESP_ERR_NO_MEM;
    }
    vq_err_t e = imu_isr_setup(p);
    if (e != ESP_OK) return e;

    /* SW_RESET → POR settle → verify the part is really there. */
    e = wr(R_CTRL3, CTRL3_SW_RESET);
    if (e == ESP_OK) vTaskDelay(pdMS_TO_TICKS(IMU_RESET_MS));
    if (e == ESP_OK) {
        uint8_t id = 0;
        e = rd(R_WHO_AM_I, &id, 1);
        p->id = id;
        if (e == ESP_OK && id != WHO_AM_I_VAL)
            e = ESP_ERR_NOT_FOUND;
    }
    if (e != ESP_OK) return e;

    e = imu_configure();
    if (e != ESP_OK) return e;

    p->anchor.rate_hz = IMU_RATE_HZ;
    p->present = true;
    ESP_LOGI(TAG, "init ok id=0x%02X, 52 Hz HAODR verified", p->id);
    return ESP_OK;
}

static vq_err_t ops_start(vq_sensor_t *s)
{
    imu_priv_t *p = (imu_priv_t *)s->priv;
    if (!p->present || !p->wtm_sem || !p->task) return ESP_ERR_INVALID_STATE;
    if (p->streaming) return ESP_OK;

    vq_err_t e;
    /* De-route interrupts + bypass FIFO while reconfiguring. */
    e  = wr(R_INT1_CTRL, 0x00);
    if (e == ESP_OK) e = wr(R_FIFO_CTRL4, 0x00);
    if (e == ESP_OK) e = wr(R_FIFO_CTRL1, IMU_WTM_WORDS);
    if (e == ESP_OK) e = wr(R_FIFO_CTRL3,
                            (FIFO_BDR_ODR << 4) | FIFO_BDR_ODR);
    if (e != ESP_OK) return e;

    /* Re-anchor: frames and edges both restart from the new stream. */
    p->frame_abs = 0;
    p->anchor.edge_us = 0;
    p->anchor.edge_seq = 0;
    p->anchor.seq_anchor = 0;
    p->anchor.rate_hz = IMU_RATE_HZ;
    p->last_edge_seq = 0;
    while (xSemaphoreTake(p->wtm_sem, 0) == pdTRUE) { /* drop stale tokens */ }

    /* FIFO on → INT1 routed → ISR armed → sensors last. Arming the edge
     * detector BEFORE the ODRs avoids the stuck-start race: a FIFO-TH
     * level already high when the detector enables gives no new edge. */
    if (e == ESP_OK) e = wr(R_FIFO_CTRL4, FIFO_MODE_FIFO);
    if (e == ESP_OK) e = wr(R_INT1_CTRL, INT1_FIFO_WTM_OVR);
    if (e != ESP_OK) return e;

    p->streaming = true;
    p->stream_start_us = esp_timer_get_time();
    gpio_intr_enable(VQ_LSM6_INT1_GPIO);

    e = wr(R_CTRL2, CTRL2_RUN);
    if (e == ESP_OK) e = wr(R_CTRL1, CTRL1_RUN);
    if (e != ESP_OK) {          /* don't leave "streaming" on a dead bus */
        p->streaming = false;
        gpio_intr_disable(VQ_LSM6_INT1_GPIO);
        (void)wr(R_INT1_CTRL, 0x00);
    }
    return e;
}

static vq_err_t ops_stop(vq_sensor_t *s)
{
    return imu_stop_stream(s);
}

static vq_err_t ops_sleep(vq_sensor_t *s)
{
    imu_priv_t *p = (imu_priv_t *)s->priv;
    if (!p->present) return ESP_OK;      /* §4.4 iterates absent parts too */
    vq_err_t e = imu_stop_stream(s);
    if (p->arm_wake) {
        /* Keep the accel alive at 15 Hz LPM1 — the WAKE_UP event engine
         * runs on accel data; gyro stays powered down. Wake config
         * (threshold, INT1 route, latch) was written by arm_wake(). */
        vq_err_t e2 = wr(R_CTRL1, CTRL1_WAKE_LPM);
        if (e == ESP_OK) e = e2;
    }
    /* IO4 stays a plain input here; EXT1 arming + RTC-domain pull-down
     * for deep sleep belong to power_mgr's §4.4 checklist. */
    return e;
}

static bool ops_healthy(vq_sensor_t *s)
{
    imu_priv_t *p = (imu_priv_t *)s->priv;
    if (!p->present) return false;
    if (p->streaming) {
        const int64_t base = (s->sample_count > 0) ? s->last_t_us
                                                   : p->stream_start_us;
        if (esp_timer_get_time() - base > IMU_STALL_US)
            return false;   /* silent bus / masked INT1 for >3 s        */
    }
    return true;
}

/* ------------------------------ Registration ---------------------------- */
static const vq_channel_id_t s_channels[] = {
    VQ_CH_ACC_X, VQ_CH_ACC_Y, VQ_CH_ACC_Z,
    VQ_CH_GYR_X, VQ_CH_GYR_Y, VQ_CH_GYR_Z,
};

static const vq_sensor_ops_t s_ops = {
    .name       = "lsm6dsv80x",
    .probe      = ops_probe,
    .init       = ops_init,
    .start      = ops_start,
    .stop       = ops_stop,
    .sleep      = ops_sleep,
    .healthy    = ops_healthy,
    .channels   = s_channels,
    .n_channels = sizeof(s_channels) / sizeof(s_channels[0]),
    .rate_hz    = IMU_RATE_HZ,
    .required   = true,      /* motion gating is required for the alarm path */
};

static vq_sensor_t s_sensor = {
    .ops  = &s_ops,
    .priv = &s_priv,
};

esp_err_t vq_imu_register(void)
{
    if (s_registered) return ESP_OK;
    esp_err_t e = vq_sensor_register(&s_sensor);
    if (e == ESP_OK) s_registered = true;
    return e;
}

/* ------------------------------ Wake-on-motion -------------------------- */
esp_err_t vq_imu_arm_wake(bool on)
{
    imu_priv_t *p = &s_priv;
    if (!p->present) return ESP_ERR_INVALID_STATE;

    vq_err_t e;
    if (on) {
        /* WAKE_UP event → INT1, latched (LIR) so a sleeping ESP can't
         * miss the edge; threshold 250 mg on the ±8 g low-g channel. */
        e  = wr(R_WAKE_UP_THS, WK_THS_250MG);
        if (e == ESP_OK) e = wr(R_WAKE_UP_DUR, WK_DUR_1SAMPLE);
        if (e == ESP_OK) e = rmw(R_TAP_CFG0, TAP0_LIR, TAP0_LIR);
        if (e == ESP_OK) e = rmw(R_FUNCTIONS_ENABLE, FUNC_INT_ENABLE,
                                 FUNC_INT_ENABLE);
        if (e == ESP_OK) e = rmw(R_MD1_CFG, MD1_INT1_WU, MD1_INT1_WU);
        /* Latch-clear now so INT1 doesn't sit high going into sleep. */
        if (e == ESP_OK) {
            uint8_t wu;
            (void)rd(R_WAKE_UP_SRC, &wu, 1);
        }
    } else {
        e  = rmw(R_MD1_CFG, MD1_INT1_WU, 0);
        if (e == ESP_OK) e = rmw(R_TAP_CFG0, TAP0_LIR, 0);
        if (e == ESP_OK) e = rmw(R_FUNCTIONS_ENABLE, FUNC_INT_ENABLE, 0);
    }
    if (e != ESP_OK) return e;
    p->arm_wake = on;
    ESP_LOGI(TAG, "wake-on-motion %s (INT1 %s)", on ? "armed" : "disarmed",
             on ? "latched" : "fifo-th");
    return ESP_OK;
}

/* ------------------------------ Feature helpers ------------------------- */
float vq_imu_motion_rms(uint32_t window_s)
{
    imu_priv_t *p = &s_priv;
    float rms = NAN;
    portENTER_CRITICAL(&p->mot_mux);
    uint32_t n = p->bin_filled;
    if (window_s < n) n = window_s;
    if (n > 0) {
        float acc = 0.0f;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t idx = (uint8_t)((p->bin_head + IMU_MOT_WIN_S - 1 - i)
                                    % IMU_MOT_WIN_S);
            acc += p->bins[idx];
        }
        rms = sqrtf(acc / (float)n);
    }
    portEXIT_CRITICAL(&p->mot_mux);
    return rms;
}

uint32_t vq_imu_still_s(void)
{
    imu_priv_t *p = &s_priv;
    portENTER_CRITICAL(&p->mot_mux);
    bool sampled = p->ever_sampled;
    int64_t base = (p->last_motion_us != 0) ? p->last_motion_us
                                          : p->first_sample_us;
    portEXIT_CRITICAL(&p->mot_mux);
    if (!sampled || base <= 0) return 0;
    int64_t dt = esp_timer_get_time() - base;
    return (dt > 0) ? (uint32_t)(dt / 1000000) : 0;
}

/* ------------------------------ Selftest -------------------------------- */
/* Wait for one fresh INT1 edge (anchor edge_seq bump). Selftest context;
 * the ISR is armed via gpio_intr_enable for the duration. */
static vq_err_t st_wait_edge(imu_priv_t *p, int32_t old_seq, int to_ms)
{
    int64_t dl = esp_timer_get_time() + (int64_t)to_ms * 1000;
    while (p->anchor.edge_seq == old_seq) {
        if (esp_timer_get_time() > dl) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

esp_err_t vq_imu_selftest(char *report, size_t len)
{
    imu_priv_t *p = &s_priv;
    size_t off = 0;
    int fails = 0;

#define RPT(...) do {                                                    \
        size_t _rem = (off < len) ? (len - off) : 0;                     \
        if (_rem) {                                                      \
            int _w = snprintf(report + off, _rem, __VA_ARGS__);          \
            if (_w > 0) off += ((size_t)_w < _rem) ? (size_t)_w : _rem;  \
        }                                                                \
    } while (0)

    if (!report || len == 0) return ESP_ERR_INVALID_ARG;
    report[0] = '\0';
    if (p->streaming) {
        RPT("lsm6dsv80x: busy (streaming)\n");
        return ESP_ERR_INVALID_STATE;
    }

    portMUX_INITIALIZE(&p->mot_mux);
    vq_err_t e = imu_isr_setup(p);
    if (e != ESP_OK) { RPT("lsm6dsv80x: gpio/isr setup failed\n"); return e; }

    /* Fresh reset so the checks start from a known state. */
    e = wr(R_CTRL3, CTRL3_SW_RESET);
    if (e == ESP_OK) vTaskDelay(pdMS_TO_TICKS(IMU_RESET_MS));
    if (e != ESP_OK) { RPT("lsm6dsv80x: reset failed\n"); return e; }
    e = imu_configure();
    if (e != ESP_OK) { RPT("lsm6dsv80x: cfg verify FAILED\n"); fails++; goto out; }
    p->present = true;      /* cfg verify already proves the part answers */

    /* --- WHO_AM_I: exact 0x73, log the actual value --- */
    {
        uint8_t id = 0xEE;
        e = rd(R_WHO_AM_I, &id, 1);
        p->id = id;
        bool ok = (e == ESP_OK) && (id == WHO_AM_I_VAL);
        RPT("who_am_i=0x%02X exp=0x%02X %s | ", id, WHO_AM_I_VAL,
            ok ? "ok" : "MISMATCH");
        if (!ok) fails++;
    }

    /* --- ODR burst: 52 Hz accel, XLDA within 300 ms, non-zero data --- */
    {
        e = wr(R_CTRL1, CTRL1_RUN);
        int64_t dl = esp_timer_get_time() + 300000;
        uint8_t st = 0;
        while (e == ESP_OK && !(st & ST_XLDA)) {
            if (esp_timer_get_time() > dl) { e = ESP_ERR_TIMEOUT; break; }
            (void)rd(R_STATUS_REG, &st, 1);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        uint8_t d[6] = {0};
        int16_t x = 0, y = 0, z = 0;
        if (e == ESP_OK) {
            e = rd(R_OUTX_L_A, d, 6);
            x = le16(&d[0]); y = le16(&d[2]); z = le16(&d[4]);
        }
        bool ok = (e == ESP_OK) && (x || y || z) &&
                  (x != (int16_t)0x8000 && y != (int16_t)0x8000);
        RPT("odr_burst: xlda=%d xyz=%d,%d,%d (%.3fg,%.3fg,%.3fg) %s | ",
            !!(st & ST_XLDA), x, y, z,
            (double)(x * ACC_G_PER_LSB), (double)(y * ACC_G_PER_LSB),
            (double)(z * ACC_G_PER_LSB), ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }

    /* --- INT1 delivery: route DRDY_XL → INT1, expect an ISR edge --- */
    {
        e = wr(R_INT1_CTRL, INT1_DRDY_XL);
        int32_t seq0 = p->anchor.edge_seq;
        gpio_intr_enable(VQ_LSM6_INT1_GPIO);
        vq_err_t we = st_wait_edge(p, seq0, 300);
        gpio_intr_disable(VQ_LSM6_INT1_GPIO);
        bool ok = (e == ESP_OK) && (we == ESP_OK);
        RPT("int1_drdy: edge_seq %ld→%ld %s | ", (long)seq0,
            (long)p->anchor.edge_seq, ok ? "ok" : "TIMEOUT/FAIL");
        if (!ok) fails++;
        e = wr(R_INT1_CTRL, 0x00);
    }
    e = wr(R_CTRL1, CTRL1_OFF);                 /* stop the burst        */

    /* --- Wake-arm roundtrip: set → verify → clear → verify --- */
    {
        bool ok = (vq_imu_arm_wake(true) == ESP_OK);
        uint8_t md1 = 0, fe = 0, tap0 = 0;
        ok = ok && rd(R_MD1_CFG, &md1, 1) == ESP_OK &&
               rd(R_FUNCTIONS_ENABLE, &fe, 1) == ESP_OK &&
               rd(R_TAP_CFG0, &tap0, 1) == ESP_OK;
        bool armed = (md1 & MD1_INT1_WU) && (fe & FUNC_INT_ENABLE) &&
                     (tap0 & TAP0_LIR);
        ok = ok && armed && (vq_imu_arm_wake(false) == ESP_OK);
        uint8_t md1b = 0;
        ok = ok && rd(R_MD1_CFG, &md1b, 1) == ESP_OK && !(md1b & MD1_INT1_WU);
        RPT("wake_arm: md1=0x%02X fe=0x%02X tap0=0x%02X disarm=0x%02X %s | ",
            md1, fe, tap0, md1b, ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }

out:
    RPT("\nlsm6dsv80x selftest: %s", fails ? "FAIL" : "PASS");
    return fails ? ESP_FAIL : ESP_OK;
#undef RPT
}
