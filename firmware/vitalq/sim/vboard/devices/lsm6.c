/* lsm6.c — virtual LSM6DSV80X 6-axis IMU for the vboard host sim.
 *
 * Register model mirrors what the firmware actually drives
 * (components/vq_drv_imu/vq_drv_imu.c, components/vq_selftest/st_core.c):
 *
 *   - WHO_AM_I 0x0F reads 0x73 (driver accepts the 0x70 family).
 *   - CTRL3 0x12: SW_RESET bit0 triggers a full config reset (auto-
 *     clears); BDU/IF_INC stored.  IF_CFG 0x03 stores i2c_i3c_disable /
 *     h_lactive / pp_od (h_lactive inverts the INT1 line — the firmware
 *     never sets it, push-pull active-high is the board default).
 *   - CTRL1 0x10 / CTRL2 0x11: op_mode[6:4] + odr[3:0].  The tick
 *     engine converts the pair to a sample rate: HAODR (op_mode==1)
 *     uses the HAODR_CFG(0x62).HAODR_SEL band table (sel=3: nibble 5 =
 *     52 Hz — the driver's stream rate); every other op_mode uses the
 *     classic nibble table (3 = 15 Hz — the driver's wake-watch rate).
 *   - STATUS_REG 0x1E: xlda/gda set on each produced sample, cleared
 *     when the last byte of the matching OUTx..OUTz block is read.
 *   - OUTx_L_G 0x22..0x27 / OUTx_L_A 0x28..0x2D: latest fed sample,
 *     quantised per FS (CTRL8.fs_xl[1:0] → ±2/4/8/16 g; CTRL6.fs_g[2:0]
 *     → 125/250/500/1000/2000/4000 dps, ST nominal sensitivities).
 *   - FIFO: FIFO_CTRL1 wtm, FIFO_CTRL3 bdr nibble pair, FIFO_CTRL4
 *     fifo_mode[2:0].  Bypass (0) flushes; mode 1 stops on full, modes
 *     2/6 drop the oldest word (continuous), anything else behaves as
 *     FIFO mode.  Words are tag + 6 data bytes: GY tag 0x01, XL tag
 *     0x02 (batched when their BDR nibble is non-zero while the sensor
 *     runs — the firmware always sets bdr == odr).  Burst reads at
 *     FIFO_DATA_OUT_TAG 0x78 pop words; the window wraps 0x7E -> 0x78.
 *     FIFO_STATUS1/2 (0x1B/0x1C) report diff_fifo + latched
 *     WTM_IA(0x80)/OVR_IA(0x40)/FULL(0x20)/OVR_LATCHED(0x08) — the
 *     latched flags clear on a FIFO_STATUS2 read, like silicon.
 *   - INT1_CTRL 0x0D routes drdy_xl(0)/drdy_g(1)/fifo_th(3)/
 *     fifo_ovr(4)/fifo_full(5) onto INT1 (IO4, push-pull active-high).
 *     INT1 is a LEVEL wire: asserted while any routed cause is pending,
 *     exactly the stuck-start case vq_drv_imu arm-before-ODR avoids.
 *   - Wake-on-motion: when WAKE_UP_THS.wk_ths[5:0] is non-zero and the
 *     accel runs, each sample's max |Δa| per axis is compared to
 *     wk_ths × FS_XL/64 g.  WAKE_UP_DUR.wake_dur[6:5] gives the
 *     consecutive-frame requirement.  An event sets WAKE_UP_SRC 0x45
 *     (z/y/x_wu bits0-2 + wu_ia bit3) and, when MD1_CFG.int1_wu(0x5E
 *     bit5) + FUNCTIONS_ENABLE.interrupts_enable(0x50 bit7) are both
 *     routed, asserts INT1 — latched while TAP_CFG0.lir(0x56 bit0)=1
 *     until WAKE_UP_SRC is read (the driver's sleep path), else a
 *     ~64 µs pulse.
 *   - CTRL4.drdy_pulsed (0x13 bit5) switches DRDY-routed edges from the
 *     default latched (hold until the OUT block is read) to ~65 µs
 *     pulses — the firmware leaves it cleared.
 *   - ALL_INT_SRC 0x1D reports wu_ia on bit1 (the driver never reads
 *     it — harmless plausibility).
 *
 * Power: NACKs every transaction while VB_RAIL_3V3 is down; a rail
 * off→on transition is a full POR (also registered as a rail callback,
 * so a mid-scenario brownout re-arms POR-equivalent defaults).
 *
 * Motion feed: vbimu_set_motion() (imu_feed.h), default {0,0,1} g.
 *
 * Datasheet-uncertain corners carry TODO(datasheet) like the firmware;
 * nothing here claims silicon accuracy — it is protocol-faithful.
 */
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "vboard.h"
#include "imu_feed.h"

/* ------------------------- register map (driver view) ---------------- */
enum {
    R_IF_CFG         = 0x03,
    R_ODR_TRIG_CFG   = 0x06,
    R_FIFO_CTRL1     = 0x07,
    R_FIFO_CTRL2     = 0x08,
    R_FIFO_CTRL3     = 0x09,
    R_FIFO_CTRL4     = 0x0A,
    R_INT1_CTRL      = 0x0D,
    R_INT2_CTRL      = 0x0E,
    R_WHO_AM_I       = 0x0F,
    R_CTRL1          = 0x10,
    R_CTRL2          = 0x11,
    R_CTRL3          = 0x12,
    R_CTRL4          = 0x13,
    R_CTRL6          = 0x15,
    R_CTRL8          = 0x17,
    R_CTRL_STATUS    = 0x1A,
    R_FIFO_STATUS1   = 0x1B,
    R_FIFO_STATUS2   = 0x1C,
    R_ALL_INT_SRC    = 0x1D,
    R_STATUS_REG     = 0x1E,
    R_OUT_TEMP       = 0x20,   /* 0x20-0x21 */
    R_OUTX_L_G       = 0x22,   /* 0x22-0x27 */
    R_OUTX_L_A       = 0x28,   /* 0x28-0x2D */
    R_OUT_LAST       = 0x2D,
    R_TIMESTAMP0     = 0x40,   /* 0x40-0x43 */
    R_WAKE_UP_SRC    = 0x45,
    R_TAP_SRC        = 0x46,
    R_FUNCTIONS_EN   = 0x50,
    R_TAP_CFG0       = 0x56,
    R_WAKE_UP_THS    = 0x5B,
    R_WAKE_UP_DUR    = 0x5C,
    R_MD1_CFG        = 0x5E,
    R_MD2_CFG        = 0x5F,
    R_HAODR_CFG      = 0x62,
    R_FIFO_OUT_TAG   = 0x78,
    R_FIFO_OUT_END   = 0x7E,
    R_NREGS          = 0x80,
};

#define WHO_AM_I_VAL      0x73    /* LSM6DSV80X datasheet ID            */

/* CTRL3 bits */
#define C3_SW_RESET       0x01
#define C3_IF_INC         0x04
#define C3_BDU            0x40
#define C3_BOOT           0x80
#define C3_DEFAULT        (C3_BDU | C3_IF_INC)

/* IF_CFG bits */
#define IFC_H_LACTIVE     0x10
#define IFC_PP_OD         0x08

/* CTRL4 bits */
#define C4_DRDY_PULSED    0x20

/* INT1_CTRL bits (AN6281 table) */
#define I1C_DRDY_XL       0x01
#define I1C_DRDY_G        0x02
#define I1C_FIFO_TH       0x08
#define I1C_FIFO_OVR      0x10
#define I1C_FIFO_FULL     0x20
#define I1C_CNT_BDR       0x40

/* STATUS_REG bits */
#define ST_XLDA           0x01
#define ST_GDA            0x02

/* FIFO_STATUS2 bits */
#define F2_DIFF_B8        0x01
#define F2_OVR_LATCHED    0x08
#define F2_FULL_IA        0x20
#define F2_OVR_IA         0x40
#define F2_WTM_IA         0x80

/* FIFO tags (driver decodes w[0] >> 3) */
#define TAG_GY_NC         0x01
#define TAG_XL_NC         0x02

/* FIFO_CTRL4.fifo_mode */
#define FM_BYPASS         0x00
#define FM_FIFO           0x01
#define FM_CONT           0x06    /* continuous-to-fifo also streams   */
#define FM_CONT_WTM       0x02    /* continuous                        */

/* MD1_CFG / FUNCTIONS_ENABLE / TAP_CFG0 bits the driver uses */
#define MD1_INT1_WU       0x20
#define FE_INT_ENABLE     0x80
#define TAP0_LIR          0x01

#define FIFO_WORD_B       7       /* 1 tag + 6 data                    */
#define FIFO_WORDS        511     /* diff_fifo is 9 bits               */

#define INT1_GPIO         4       /* VQ_LSM6_INT1_GPIO — push-pull     */
#define WU_PULSE_US       64      /* unlatched event pulse ≈ drdy_pulsed*/
#define DRDY_PULSE_US     65      /* CTRL4.drdy_pulsed pulse width      */

/* ----------------------------- motion feed -------------------------- */
/* Board-level motion the model "senses".  Global (there is one body in
 * the sim world regardless of how many models are registered).        */
static struct {
    pthread_mutex_t lock;
    float           acc[3];
    float           gyr[3];
} s_feed = {
    PTHREAD_MUTEX_INITIALIZER,
    { 0.0f, 0.0f, 1.0f },   /* static gravity, board face-up           */
    { 0.0f, 0.0f, 0.0f },
};

void vbimu_set_motion(const float accel_g[3], const float gyro_dps[3])
{
    pthread_mutex_lock(&s_feed.lock);
    if (accel_g) {
        memcpy(s_feed.acc, accel_g, sizeof s_feed.acc);
    } else {
        s_feed.acc[0] = 0.0f; s_feed.acc[1] = 0.0f; s_feed.acc[2] = 1.0f;
    }
    if (gyro_dps) {
        memcpy(s_feed.gyr, gyro_dps, sizeof s_feed.gyr);
    } else {
        memset(s_feed.gyr, 0, sizeof s_feed.gyr);
    }
    pthread_mutex_unlock(&s_feed.lock);
}

void vbimu_get_motion(float accel_g[3], float gyro_dps[3])
{
    pthread_mutex_lock(&s_feed.lock);
    if (accel_g) memcpy(accel_g, s_feed.acc, sizeof s_feed.acc);
    if (gyro_dps) memcpy(gyro_dps, s_feed.gyr, sizeof s_feed.gyr);
    pthread_mutex_unlock(&s_feed.lock);
}

static void feed_snapshot(float acc[3], float gyr[3])
{
    vbimu_get_motion(acc, gyr);
}

/* ----------------------------- model state -------------------------- */
typedef struct {
    bool            powered;        /* VB_RAIL_3V3                      */
    uint8_t         reg[R_NREGS];   /* config register image            */
    uint8_t         regptr;         /* last write-phase pointer         */

    int16_t         acc[3];         /* OUTX_L_A.. quantised latch       */
    int16_t         gyr[3];         /* OUTX_L_G.. quantised latch       */
    uint8_t         st;             /* STATUS_REG xlda/gda              */
    bool            xl_drdy_pend;   /* drdy cause pending (latched)     */
    bool            gy_drdy_pend;
    int64_t         xl_drdy_rel_us; /* drdy_pulsed release deadlines    */
    int64_t         gy_drdy_rel_us;

    int64_t         xl_next_us;     /* next accel sample (0 = stopped)  */
    int64_t         gy_next_us;

    /* wake-on-motion engine */
    float           prev_acc[3];
    bool            have_prev;
    uint8_t         wu_src;         /* WAKE_UP_SRC image                */
    uint8_t         wu_streak;      /* consecutive over-threshold frames*/
    bool            wu_pend;        /* event pending on INT1            */
    bool            wu_latched;     /* LIR: stays till WAKE_UP_SRC read */
    int64_t         wu_until_us;    /* pulse end when unlatched         */

    /* FIFO */
    uint8_t         f2_flags;       /* latched OVR_L/OVR_IA/WTM_IA      */
    uint8_t         fifo[FIFO_WORDS * FIFO_WORD_B];
    uint16_t        fifo_n;         /* words queued                     */
    uint8_t         fw[FIFO_WORD_B];/* word being streamed out          */

    /* INT1 line (push-pull; idle low) */
    bool            i1_lvl;
    bool            i1_dirty;
    int8_t          i1_pin;         /* level actually driven, -1 fresh */

    pthread_mutex_t lock;
} lsm6_t;

/* ------------------------- ODR decode -------------------------------- */
/* HAODR band table — datasheet Table 20.  Only the sel==3 row is
 * exercised by the firmware (52 Hz), the rest is plausibility.        */
static float lsm6_odr_hz(const lsm6_t *s, uint8_t ctrl)
{
    const uint8_t odr = ctrl & 0x0F;
    const uint8_t op  = (ctrl >> 4) & 0x07;
    if (odr == 0) return 0.0f;
    static const float norm[16] = {
        0, 1.875f, 7.5f, 15.0f, 30.0f, 60.0f, 120.0f, 240.0f,
        480.0f, 960.0f, 1920.0f, 3840.0f, 7680.0f, 0, 0, 0,
    };
    static const float band[4][10] = {  /* nibble 3..12 per HAODR_SEL    */
        { 15.0f, 30.0f, 60.0f, 120.0f, 240.0f, 480.0f, 960.0f,
          1920.0f, 3840.0f, 7680.0f },
        { 15.625f, 31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f,
          2000.0f, 4000.0f, 8000.0f },
        { 12.5f, 25.0f, 50.0f, 100.0f, 200.0f, 400.0f, 800.0f,
          1600.0f, 3200.0f, 6400.0f },
        { 13.0f, 26.0f, 52.0f, 104.0f, 208.0f, 417.0f, 833.0f,
          1667.0f, 3333.0f, 6667.0f },
    };
    if (op == 0x01 /* HAODR */) {
        const uint8_t sel = s->reg[R_HAODR_CFG] & 0x03;
        return (odr >= 3 && odr <= 12) ? band[sel][odr - 3] : 0.0f;
    }
    return norm[odr];
}

static int64_t odr_period_us(float hz)
{
    return (hz > 0.0f) ? (int64_t)(1000000.0f / hz) : 0;
}

/* fs_xl[1:0] → ±g full-scale;  fs_g[2:0] → ±dps full-scale            */
static float xl_fs_g(const lsm6_t *s)
{
    static const float fs[4] = { 2.0f, 4.0f, 8.0f, 16.0f };
    return fs[s->reg[R_CTRL8] & 0x03];
}
static float xl_g_per_lsb(const lsm6_t *s)  { return xl_fs_g(s) / 32768.0f; }
/* ST nominal mdps/LSB (what the driver divides by), not fs/32768      */
static float g_dps_per_lsb(const lsm6_t *s)
{
    static const float mdps[8] = { 4.375f, 8.75f, 17.5f, 35.0f,
                                   70.0f, 140.0f, 17.5f, 17.5f };
    return mdps[s->reg[R_CTRL6] & 0x07] * 1e-3f;
}

/* ----------------------------- INT1 ---------------------------------- */
/* The line is a level OR of every *routed, pending* cause.  Evaluated
 * under lock; the actual pin drive is deferred to lsm6_int1_pump() so
 * the ISR dispatch never runs while we hold the model mutex.          */
static int lsm6_int1_want(const lsm6_t *s)
{
    const uint8_t i1 = s->reg[R_INT1_CTRL];
    int lvl = 0;
    if (s->xl_drdy_pend && (i1 & I1C_DRDY_XL))   lvl = 1;
    if (s->gy_drdy_pend && (i1 & I1C_DRDY_G))    lvl = 1;
    if ((s->f2_flags & F2_WTM_IA) && (i1 & I1C_FIFO_TH))   lvl = 1;
    if ((s->f2_flags & F2_OVR_IA) && (i1 & I1C_FIFO_OVR))  lvl = 1;
    if (s->fifo_n >= FIFO_WORDS && (i1 & I1C_FIFO_FULL))   lvl = 1;
    if (s->wu_pend && (s->reg[R_MD1_CFG] & MD1_INT1_WU) &&
        (s->reg[R_FUNCTIONS_EN] & FE_INT_ENABLE))          lvl = 1;
    if (s->reg[R_IF_CFG] & IFC_H_LACTIVE) lvl = !lvl;  /* active-low   */
    return lvl;
}

static void lsm6_int1_eval(lsm6_t *s)
{
    const int want = lsm6_int1_want(s);
    if (want != s->i1_lvl) {
        s->i1_lvl   = (bool)want;
        s->i1_dirty = true;
    }
}

static void lsm6_int1_pump(lsm6_t *s)
{
    pthread_mutex_lock(&s->lock);
    const bool fire = s->i1_dirty;
    const int  lvl  = s->i1_lvl;
    const int  prev = s->i1_pin;
    s->i1_dirty = false;
    s->i1_pin   = (int8_t)lvl;
    pthread_mutex_unlock(&s->lock);
    if (!fire || prev == lvl) return;
    vboard_gpio_set(INT1_GPIO, lvl);
    /* a first-time low drive is the idle baseline, not an edge       */
    if (prev >= 0 || lvl) vboard_irq_pulse(INT1_GPIO, lvl);
}

/* ----------------------------- POR ------------------------------------ */
static void lsm6_por(lsm6_t *s)
{
    memset(s->reg, 0, sizeof s->reg);
    s->reg[R_CTRL3] = C3_DEFAULT;           /* if_inc + bdu ship set    */
    memset(s->acc, 0, sizeof s->acc);
    memset(s->gyr, 0, sizeof s->gyr);
    s->st             = 0;
    s->xl_drdy_pend   = s->gy_drdy_pend = false;
    s->xl_next_us     = s->gy_next_us = 0;
    memset(s->prev_acc, 0, sizeof s->prev_acc);
    s->have_prev      = false;
    s->wu_src         = 0;
    s->wu_streak      = 0;
    s->wu_pend        = s->wu_latched = false;
    s->f2_flags       = 0;
    s->fifo_n         = 0;
    memset(s->fw, 0, sizeof s->fw);
    s->i1_lvl         = false;
    s->i1_dirty       = true;              /* re-drive the idle level  */
    /* regptr / lock / i1_pin preserved */
}

/* ----------------------------- FIFO ----------------------------------- */
static uint8_t fifo_mode(const lsm6_t *s) { return s->reg[R_FIFO_CTRL4] & 0x07; }

static void fifo_push(lsm6_t *s, uint8_t tag, const int16_t v[3])
{
    if (s->fifo_n >= FIFO_WORDS) {
        if (fifo_mode(s) == FM_CONT_WTM || fifo_mode(s) == FM_CONT) {
            /* continuous: drop the oldest word */
            memmove(s->fifo, s->fifo + FIFO_WORD_B,
                    (FIFO_WORDS - 1) * FIFO_WORD_B);
            s->fifo_n = (uint16_t)(FIFO_WORDS - 1);
        } else {
            s->f2_flags |= (uint8_t)(F2_OVR_IA | F2_OVR_LATCHED);
            return;                          /* stop-on-full          */
        }
    }
    uint8_t *w = &s->fifo[s->fifo_n * FIFO_WORD_B];
    w[0] = (uint8_t)(tag << 3);
    w[1] = (uint8_t)(v[0] & 0xFF); w[2] = (uint8_t)(v[0] >> 8);
    w[3] = (uint8_t)(v[1] & 0xFF); w[4] = (uint8_t)(v[1] >> 8);
    w[5] = (uint8_t)(v[2] & 0xFF); w[6] = (uint8_t)(v[2] >> 8);
    s->fifo_n++;
    if (s->fifo_n >= s->reg[R_FIFO_CTRL1]) s->f2_flags |= F2_WTM_IA;
    if (s->fifo_n >= FIFO_WORDS)
        s->f2_flags |= (uint8_t)(F2_OVR_IA | F2_OVR_LATCHED);
}

static uint8_t fifo_pop_byte(lsm6_t *s, uint8_t idx /*0..6*/)
{
    if (idx == 0) {                        /* TAG byte → pop a word    */
        if (s->fifo_n > 0) {
            memcpy(s->fw, s->fifo, FIFO_WORD_B);
            memmove(s->fifo, s->fifo + FIFO_WORD_B,
                    (size_t)(s->fifo_n - 1) * FIFO_WORD_B);
            s->fifo_n--;
        } else {
            memset(s->fw, 0, sizeof s->fw);  /* empty → zeros, like HW */
        }
    }
    return s->fw[idx];
}

/* ----------------------------- sampling ------------------------------- */
static int16_t quant(float phys, float per_lsb)
{
    const long l = lrintf(phys / per_lsb);
    if (l >  32767) return  32767;
    if (l < -32768) return -32768;
    return (int16_t)l;
}

static void wake_eval(lsm6_t *s, const float a[3], int64_t now)
{
    const uint8_t ths = s->reg[R_WAKE_UP_THS] & 0x3F;
    if (ths == 0 || !s->have_prev) {
        memcpy(s->prev_acc, a, sizeof s->prev_acc);
        s->have_prev = true;
        s->wu_streak = 0;
        return;
    }
    const float ths_g = (float)ths * xl_fs_g(s) / 64.0f;
    uint8_t axes = 0;
    for (int i = 0; i < 3; i++) {
        const float d = fabsf(a[i] - s->prev_acc[i]);
        if (d > ths_g) axes |= (uint8_t)(1u << (2 - i));  /* x=bit2..z=bit0 */
    }
    memcpy(s->prev_acc, a, sizeof s->prev_acc);
    const uint8_t need = (uint8_t)(((s->reg[R_WAKE_UP_DUR] >> 5) & 0x03) + 1);
    if (axes == 0) { s->wu_streak = 0; return; }
    if (++s->wu_streak < need) return;
    s->wu_src |= (uint8_t)(axes | 0x08);   /* x/y/z_wu + wu_ia          */
    if ((s->reg[R_MD1_CFG] & MD1_INT1_WU) &&
        (s->reg[R_FUNCTIONS_EN] & FE_INT_ENABLE)) {
        s->wu_pend    = true;
        s->wu_latched = (s->reg[R_TAP_CFG0] & TAP0_LIR) != 0;
        s->wu_until_us = now + WU_PULSE_US;
    }
}

static void xl_sample(lsm6_t *s, int64_t now)
{
    float a[3], g[3];
    feed_snapshot(a, g);
    wake_eval(s, a, now);
    const float lsb = xl_g_per_lsb(s);
    for (int i = 0; i < 3; i++) s->acc[i] = quant(a[i], lsb);
    s->st |= ST_XLDA;
    s->xl_drdy_pend = true;
    s->xl_drdy_rel_us = now + DRDY_PULSE_US;
    if (fifo_mode(s) != FM_BYPASS && (s->reg[R_FIFO_CTRL3] & 0x0F))
        fifo_push(s, TAG_XL_NC, s->acc);
}

static void gy_sample(lsm6_t *s, int64_t now)
{
    float a[3], g[3];
    feed_snapshot(a, g);
    const float lsb = g_dps_per_lsb(s);
    for (int i = 0; i < 3; i++) s->gyr[i] = quant(g[i], lsb);
    s->st |= ST_GDA;
    s->gy_drdy_pend = true;
    s->gy_drdy_rel_us = now + DRDY_PULSE_US;
    if (fifo_mode(s) != FM_BYPASS && (s->reg[R_FIFO_CTRL3] & 0xF0))
        fifo_push(s, TAG_GY_NC, s->gyr);
}

/* --------------------------- register writes --------------------------- */
static bool lsm6_readonly(uint8_t a)
{
    return a == R_WHO_AM_I || a == R_CTRL_STATUS ||
           a == R_FIFO_STATUS1 || a == R_FIFO_STATUS2 ||
           a == R_ALL_INT_SRC || a == R_STATUS_REG ||
           (a >= R_OUT_TEMP && a <= R_OUT_LAST) ||
           (a >= 0x34 && a <= 0x39) ||                 /* HG out        */
           (a >= R_TIMESTAMP0 && a <= 0x4C) ||         /* ts + src regs */
           (a >= R_FIFO_OUT_TAG && a <= R_FIFO_OUT_END);
}

static void lsm6_reg_write(lsm6_t *s, uint8_t a, uint8_t v, int64_t now)
{
    if (a >= R_NREGS || lsm6_readonly(a)) return;
    switch (a) {
    case R_CTRL3:
        if (v & C3_SW_RESET) {                  /* auto-clearing        */
            lsm6_por(s);
            return;
        }
        /* BOOT reboots NVM on silicon — modelled as auto-clear no-op  */
        s->reg[a] = (uint8_t)(v & ~(C3_SW_RESET | C3_BOOT));
        break;
    case R_CTRL1: {
        s->reg[a] = v;
        const float hz = lsm6_odr_hz(s, v);
        s->xl_next_us = (hz > 0.0f) ? now + odr_period_us(hz) : 0;
        break;
    }
    case R_CTRL2: {
        s->reg[a] = v;
        const float hz = lsm6_odr_hz(s, v);
        s->gy_next_us = (hz > 0.0f) ? now + odr_period_us(hz) : 0;
        break;
    }
    case R_FIFO_CTRL4:
        s->reg[a] = v;
        if ((v & 0x07) == FM_BYPASS) {          /* bypass flushes FIFO  */
            s->fifo_n   = 0;
            s->f2_flags = 0;
        }
        break;
    default:
        s->reg[a] = v;
        break;
    }
}

/* ---------------------------- register reads -------------------------- */
static uint8_t lsm6_reg_read(lsm6_t *s, uint8_t a)
{
    if (a >= R_NREGS) return 0x00;
    switch (a) {
    case R_WHO_AM_I:     return WHO_AM_I_VAL;
    case R_FIFO_STATUS1: return (uint8_t)(s->fifo_n & 0xFF);
    case R_FIFO_STATUS2: {
        const uint8_t v = (uint8_t)(((s->fifo_n >> 8) & F2_DIFF_B8) |
                                   s->f2_flags |
                                   (s->fifo_n >= FIFO_WORDS ? F2_FULL_IA : 0));
        s->f2_flags &= (uint8_t)~(F2_WTM_IA | F2_OVR_IA | F2_OVR_LATCHED);
        return v;
    }
    case R_ALL_INT_SRC:
        return (uint8_t)(s->wu_pend ? 0x02 : 0x00);     /* wu_ia bit1    */
    case R_STATUS_REG:
        return s->st;
    case R_WAKE_UP_SRC: {
        const uint8_t v = s->wu_src;
        s->wu_src  = 0;                                 /* read clears   */
        s->wu_pend = false;
        s->wu_latched = false;
        return v;
    }
    default:
        break;
    }
    if (a >= R_OUTX_L_G && a <= R_OUTX_L_G + 5)
        return (uint8_t)(s->gyr[(a - R_OUTX_L_G) >> 1] >> ((a & 1) ? 8 : 0));
    if (a >= R_OUTX_L_A && a <= R_OUTX_L_A + 5)
        return (uint8_t)(s->acc[(a - R_OUTX_L_A) >> 1] >> ((a & 1) ? 8 : 0));
    return s->reg[a];
}

/* latch-clear on data-block reads (BDU-style read-clear semantics)      */
static void lsm6_read_sidefx(lsm6_t *s, uint8_t a)
{
    /* drdy causes drain only when the data block itself is read —
     * polling STATUS_REG does not release INT1 on the real part      */
    if (a == R_OUTX_L_G + 5) { s->st &= (uint8_t)~ST_GDA;  s->gy_drdy_pend = false; }
    if (a == R_OUTX_L_A + 5) { s->st &= (uint8_t)~ST_XLDA; s->xl_drdy_pend = false; }
    if (a == R_WAKE_UP_SRC || a == R_FIFO_STATUS2 || a == R_STATUS_REG ||
        a == R_ALL_INT_SRC)
        lsm6_int1_eval(s);
}

/* ------------------------------ vboard ops ----------------------------- */
static esp_err_t lsm6_write(vboard_dev_t *d, const uint8_t *data, size_t len)
{
    lsm6_t *s = (lsm6_t *)d->ctx;
    if (!vboard_rail(VB_RAIL_3V3)) return VB_ESP_ERR_TIMEOUT;   /* NACK */
    const int64_t now = vboard_now_us();
    pthread_mutex_lock(&s->lock);
    if (len > 0) {
        s->regptr = data[0];
        for (size_t i = 1; i < len; i++) {
            lsm6_reg_write(s, s->regptr, data[i], now);
            if (s->reg[R_CTRL3] & C3_IF_INC)
                s->regptr = (uint8_t)((s->regptr + 1) & 0x7F);
        }
        lsm6_int1_eval(s);
    }
    pthread_mutex_unlock(&s->lock);
    lsm6_int1_pump(s);
    return VB_ESP_OK;
}

static esp_err_t lsm6_read(vboard_dev_t *d, uint8_t *data, size_t len)
{
    lsm6_t *s = (lsm6_t *)d->ctx;
    if (!vboard_rail(VB_RAIL_3V3)) return VB_ESP_ERR_TIMEOUT;   /* NACK */
    pthread_mutex_lock(&s->lock);
    for (size_t i = 0; i < len; i++) {
        const uint8_t a = s->regptr;
        if (a >= R_FIFO_OUT_TAG && a <= R_FIFO_OUT_END) {
            data[i] = fifo_pop_byte(s, (uint8_t)(a - R_FIFO_OUT_TAG));
            s->regptr = (a == R_FIFO_OUT_END) ? R_FIFO_OUT_TAG
                                              : (uint8_t)(a + 1);
        } else {
            data[i] = lsm6_reg_read(s, a);
            lsm6_read_sidefx(s, a);
            if (s->reg[R_CTRL3] & C3_IF_INC)
                s->regptr = (uint8_t)((a + 1) & 0x7F);
        }
    }
    lsm6_int1_eval(s);
    pthread_mutex_unlock(&s->lock);
    lsm6_int1_pump(s);
    return VB_ESP_OK;
}

static void lsm6_tick(vboard_dev_t *d, int64_t now)
{
    lsm6_t *s = (lsm6_t *)d->ctx;
    pthread_mutex_lock(&s->lock);
    if (s->powered) {
        /* unlatched pulses expire on their deadline                  */
        if (s->wu_pend && !s->wu_latched && now >= s->wu_until_us)
            s->wu_pend = false;
        if ((s->reg[R_CTRL4] & C4_DRDY_PULSED)) {
            if (s->xl_drdy_pend && now >= s->xl_drdy_rel_us) s->xl_drdy_pend = false;
            if (s->gy_drdy_pend && now >= s->gy_drdy_rel_us) s->gy_drdy_pend = false;
        }
        const float xr = lsm6_odr_hz(s, s->reg[R_CTRL1]);
        const float gr = lsm6_odr_hz(s, s->reg[R_CTRL2]);
        const int64_t xp = odr_period_us(xr), gp = odr_period_us(gr);
        if (xr > 0.0f && s->xl_next_us == 0) s->xl_next_us = now + xp;
        if (gr > 0.0f && s->gy_next_us == 0) s->gy_next_us = now + gp;
        while (xp > 0 && s->xl_next_us <= now) {
            xl_sample(s, now);
            s->xl_next_us += xp;
        }
        while (gp > 0 && s->gy_next_us <= now) {
            gy_sample(s, now);
            s->gy_next_us += gp;
        }
        if (xr <= 0.0f) s->xl_next_us = 0;
        if (gr <= 0.0f) s->gy_next_us = 0;
        lsm6_int1_eval(s);
    }
    pthread_mutex_unlock(&s->lock);
    lsm6_int1_pump(s);
}

static void lsm6_reset(vboard_dev_t *d)
{
    lsm6_t *s = (lsm6_t *)d->ctx;
    pthread_mutex_lock(&s->lock);
    lsm6_por(s);
    pthread_mutex_unlock(&s->lock);
    lsm6_int1_pump(s);
}

/* VDD collapse/restore — the part dies with the rail and PORs on its
 * return; INT1 is pulled to the dead-rail level.                        */
static void lsm6_rail_cb(vboard_rail_t r, bool on, void *ctx)
{
    (void)r;
    lsm6_t *s = (lsm6_t *)ctx;
    pthread_mutex_lock(&s->lock);
    if (on) {
        if (!s->powered) { s->powered = true; lsm6_por(s); }
    } else {
        s->powered = false;
        if (s->i1_lvl) { s->i1_lvl = false; s->i1_dirty = true; }
    }
    pthread_mutex_unlock(&s->lock);
    lsm6_int1_pump(s);
}

static lsm6_t       s_lsm6;
static vboard_dev_t s_lsm6_dev = {
    .name      = "lsm6",
    .i2c_addr  = 0x6A,
    .i2c_write = lsm6_write,
    .i2c_read  = lsm6_read,
    .spi_cs    = -1,
    .spi_xfer  = NULL,
    .tick      = lsm6_tick,
    .reset     = lsm6_reset,
    .ctx       = &s_lsm6,
};

void vbdev_lsm6_register(void)
{
    static bool done;
    if (done) return;
    done = true;
    pthread_mutex_init(&s_lsm6.lock, NULL);
    s_lsm6.i1_pin  = -1;
    s_lsm6.powered = vboard_rail(VB_RAIL_3V3);
    lsm6_por(&s_lsm6);
    vboard_on_rail(VB_RAIL_3V3, lsm6_rail_cb, &s_lsm6);
    vboard_register(&s_lsm6_dev);
    /* idle-low baseline on INT1 (internal pull-down keeps it there)   */
    lsm6_int1_pump(&s_lsm6);
}

/* Self-register at load so the default population exists even before
 * vboard_init() wires anything explicitly (idempotent via `done`).    */
#if defined(__GNUC__)
__attribute__((constructor))
static void lsm6_ctor(void)
{
    vbdev_lsm6_register();
}
#endif
