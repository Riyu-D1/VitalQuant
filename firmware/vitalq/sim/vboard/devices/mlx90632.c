/* mlx90632.c — Melexis MLX90632 non-contact IR temperature node
 * "mlx90632" @0x3A on the 1.8 V island (behind the PCA9306), fix_v22.
 *
 * Register model mirrors what components/vq_drv_island/src/mlx90632.c
 * uses (MLX90632 datasheet §7 Table 7 + §11 DSP5):
 *   0x2405-0x2407 EE_ID0..2    RO  model-fixed identity words
 *   0x2409      EE_PRODUCT_CODE RO = 0x9063 (reported by selftest)
 *   0x240B      EE_VERSION     RO = 0x0105 (medical DSP5 — the driver's
 *                                id check: (ver&0x7FFF)==0x0105,
 *                                (ver&0xFF)==0x05)
 *   0x240C/0E/10/12  P_R/P_G/P_T/P_O  RO s32 (lo word first — the
 *                                driver's rd32 reads reg_lsb then +1)
 *   0x2424-0x242D  Ea/Eb/Fa/Fb/Ga    RO s32 calib (synthesised below)
 *   0x242E/2F/30   Gb/Ka/Kb          RO s16
 *   0x2481/0x2482  Ha/Hb             RW s16 customer-calib (writable)
 *   0x24E1         EE_MEAS_1         RW refresh-rate word (stored)
 *   0x3000      REG_I2C_ADDR   RW  stored
 *   0x3001      REG_CONTROL    RW  SOB b11 | meas_sel[8:4] | SOC b3 |
 *                                mode[2:1] (0 halt, 1 sleep_step,
 *                                2 step, 3 continuous)
 *   0x3005      REG_I2C_CMD    WO  0x0006 = addressed reset
 *   0x3FFF      REG_STATUS     RW  BUSY b10 | EE_BUSY b9 | BRST b8 |
 *                                CYC_POS b6:2 | NEW_DATA b0;
 *                                master-writable bits = ND+BRST
 *   0x4000-0x405F RAM          RO  measurement results
 *
 * Frames: [regH regL] pointer writes, [regH regL dH dL] word writes
 * (auto-increment per 16-bit word); reads stream big-endian words from
 * the pointer — the st_i2c_scan 6-byte read at 0x2405 gets ID0..2.
 *
 * Measurement engine: while CONTROL.mode==CONT one sub-measurement
 * completes every 62.5 ms (~16 Hz): CYC_POS advances (medical
 * meas_sel → 1↔2 alternating; meas_sel==0x11 extended → 1..19 wrap),
 * NEW_DATA sets, and the RAM slots for that cycle are refilled.
 * SOC in STEP/SLEEP_STEP triggers a single cycle.  I2C_CMD 0x0006 and
 * vboard reset()/rail loss return the device to POR (CONTROL=0,
 * STATUS=0, BRST=0 — exactly what the driver's brown-out flag means).
 *
 * Scene synthesis (the interesting half): the EEPROM constants below
 * are CHOSEN so the driver's own DSP5 chain inverts cleanly — the
 * driver-math functions are copied verbatim (mx_*, same fixed-point
 * scaling) and the model binary-searches the raw values that land on
 * the optical_feed temperatures:
 *   amb:  P_T=P_R=P_O=0, Gb=0 → tamb_mc = (AMB<<20)/P_G, AMB ∝ an.
 *         P_G is solved so an=ao=20000 → 30.0 °C; resynth searches an.
 *   obj:  Ka=0 → object ∝ S; Fa tuned so S≈1400 ≙ 33 °C and the int16
 *         range covers ≈ −10…125 °C; resynth binary-searches S (and
 *         the extended S for the Fa/2 variant).
 * Because the search drives the driver's EXACT equations, measured
 * temp.object/temp.ambient track vbmlx_set_object_c/vbmlx_set_ambient_c
 * (defaults 33.0 °C skin / 30.0 °C die) to within quantisation.
 *
 * TODO(datasheet): EE_BUSY is a ~1 ms post-POR pulse, BUSY a short
 * slot-start pulse, and MEAS_1 does not retime the 16 Hz engine —
 * all plausible simplifications the driver never distinguishes.
 *
 * Concurrency: lock-free like the sibling models (firmware serialises
 * I2C on its own mutex; engine steps are idempotent).
 * Powered by VB_RAIL_1V8: rail down → NACK (ESP_ERR_TIMEOUT), restore
 * → POR.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "vboard.h"
#include "optical_feed.h"

#define M_ADDR          0x3Au

/* register map */
#define M_EE_BASE       0x2400u
#define M_EE_WORDS      0x100u                     /* 0x2400-0x24FF    */
#define M_EE_ID0        0x2405u
#define M_EE_PRODUCT    0x2409u
#define M_EE_VERSION    0x240Bu
#define M_EE_P_R        0x240Cu                    /* s32, lo first    */
#define M_EE_P_G        0x240Eu
#define M_EE_P_T        0x2410u
#define M_EE_P_O        0x2412u
#define M_EE_Ea         0x2424u
#define M_EE_Eb         0x2426u
#define M_EE_Fa         0x2428u
#define M_EE_Fb         0x242Au
#define M_EE_Ga         0x242Cu
#define M_EE_Gb         0x242Eu                    /* s16 below        */
#define M_EE_Ka         0x242Fu
#define M_EE_Kb         0x2430u
#define M_EE_Ha         0x2481u                    /* customer R/W     */
#define M_EE_Hb         0x2482u
#define M_EE_MEAS_1     0x24E1u

#define M_REG_ADDR      0x3000u
#define M_REG_CONTROL   0x3001u
#define M_REG_I2C_CMD   0x3005u
#define M_REG_STATUS    0x3FFFu
#define M_RAM_BASE      0x4000u
#define M_RAM_WORDS     0x60u                      /* 0x4000-0x405F    */

/* CONTROL fields — driver's masks (mlx90632.c) */
#define M_CTRL_SOB      0x0800u
#define M_CTRL_SOC      0x0008u
#define M_CTRL_MTYP     0x01F0u                    /* meas_sel[8:4]    */
#define M_CTRL_MODE     0x0006u                    /* mode[2:1]        */
#define M_MODE_HALT     0x00u
#define M_MODE_SLEEP    0x02u
#define M_MODE_STEP     0x04u
#define M_MODE_CONT     0x06u
#define M_MTYP_EXT      0x11u                      /* extended-range   */

/* STATUS fields */
#define M_ST_BUSY       0x0400u
#define M_ST_EE_BUSY    0x0200u
#define M_ST_BRST       0x0100u
#define M_ST_CYC_POS    0x007Cu                    /* pos[6:2]         */
#define M_ST_NEW_DATA   0x0001u
#define M_ST_WR_MASK    (M_ST_NEW_DATA | M_ST_BRST)

#define M_RESET_CMD     0x0006u                    /* REG_I2C_CMD      */

/* RAM slots (indexes = reg − 0x4000) — medical + extended tables      */
#define M_RAM1_1        0x03u   /* object ch1 field1  (RAM_4..6 area) */
#define M_RAM2_1        0x04u
#define M_RAM3_1        0x05u   /* ambient "new"      = RAM_6 0x4005  */
#define M_RAM1_2        0x06u
#define M_RAM2_2        0x07u
#define M_RAM3_2        0x08u   /* ambient "old"      = RAM_9 0x4008  */
#define M_EXT_OBJ1      0x33u   /* RAM_52 = 0x4033    meas 17/18/19   */
#define M_EXT_OBJ2      0x34u
#define M_EXT_AMB1      0x35u   /* RAM_54 = 0x4035                    */
#define M_EXT_OBJ3      0x36u
#define M_EXT_OBJ4      0x37u
#define M_EXT_AMB2      0x38u   /* RAM_57 = 0x4038                    */
#define M_EXT_OBJ5      0x39u   /* RAM_58 = 0x4039                    */
#define M_EXT_OBJ6      0x3Au   /* RAM_59 = 0x403A                    */

/* engine + calib constants */
#define M_MEAS_US       62500        /* ~16 Hz sub-measurement        */
#define M_EE_BUSY_US    1000         /* EEPROM copy after reset/POR   */
#define M_BUSY_US       20000        /* "conversion active" pulse     */
#define M_AMB_REF       20000        /* ambient "old" reference raw   */
#define M_TAMB_REF_MC   30000        /* an=ao=M_AMB_REF → 30.0 °C      */
#define M_EMISS         1000         /* matches driver's MLX_EMISSIVITY */
#define M_REFL_MC       25000        /* matches driver's reflected T   */

/* chosen calib constants (see header note) */
#define M_EA            33554432     /* → kTA = 512000                */
#define M_FA            238400000    /* → mid-range S at skin temps   */
#define M_FB            14913
#define M_GA            (-14567210)
#define M_GB            0
#define M_KA            0            /* → object raw ∝ S exactly      */
#define M_KB            0
#define M_HA            16384        /* → Ha_c = 1e6                  */
#define M_HB            0
#define M_PR            0
#define M_PT            0
#define M_PO            0
#define M_VERSION       0x0105u      /* medical DSP5                  */
#define M_PRODUCT       0x9063u
#define M_MEAS1         0x0002u

typedef struct {
    vboard_dev_t dev;
    uint16_t     ptr;                       /* 16-bit reg pointer     */

    /* register file */
    uint16_t     ee[M_EE_WORDS];            /* EEPROM image           */
    uint16_t     ram[M_RAM_WORDS];
    uint16_t     control, i2c_addr_reg;
    bool         new_data, brst;

    /* solved scene raws */
    bool         ee_ready;
    int32_t      pg, eb;                    /* derived EE constants   */
    int16_t      an, ao, s_med, s_ext;
    float        seen_obj_c, seen_amb_c;    /* last synth'd feed      */

    /* engine */
    uint8_t      cyc_pos;
    int64_t      next_meas_us;
    int64_t      last_meas_us;
    int64_t      ee_busy_until_us;
    bool         powered;
} mlx_t;

/* ================ DSP5 math — copied verbatim from the driver =======
 * components/vq_drv_island/src/mlx90632.c §11 port (renamed mx_*).
 * Keeping the exact integer scaling is what lets the model invert the
 * driver's equations — same code, same results.                        */
static int64_t mx_isqrt64(uint64_t x)
{
    uint64_t r = 0, b = 1ULL << 62;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else            { r >>= 1; }
        b >>= 2;
    }
    return (int64_t)r;
}

static int64_t mx_preproc_amb(int16_t amb_new, int16_t amb_old, int16_t Gb)
{
    int64_t kGb  = ((int64_t)Gb * 1000) >> 10;
    int64_t VR   = (int64_t)amb_old * 1000000 + kGb * ((int64_t)amb_new * 1000 / 12);
    int64_t tmp  = (((int64_t)amb_new * 1000000000000LL) / 12) / VR;
    return (tmp << 19) / 1000;
}

static int64_t mx_preproc_obj(int16_t obj_new, int16_t obj_old,
                              int16_t amb_new, int16_t amb_old, int16_t Ka)
{
    int64_t kKa  = ((int64_t)Ka * 1000) >> 10;
    int64_t VR   = (int64_t)amb_old * 1000000 + kKa * ((int64_t)amb_new * 1000 / 12);
    int64_t S    = ((int32_t)obj_new + obj_old) / 2;
    int64_t tmp  = ((S * 1000000000000LL) / 12) / VR;
    return (tmp << 19) / 1000;
}

static int64_t mx_preproc_obj_ext(int16_t obj_new, int16_t amb_new,
                                  int16_t amb_old, int16_t Ka)
{
    int64_t kKa  = ((int64_t)Ka * 1000) >> 10;
    int64_t VR   = (int64_t)amb_old * 1000000 + kKa * ((int64_t)amb_new * 1000 / 12);
    int64_t tmp  = (((int64_t)obj_new * 1000000000000LL) / 12) / VR;
    return (tmp << 19) / 1000;
}

static int32_t mx_calc_ambient(int16_t amb_new, int16_t amb_old,
                               int32_t PT, int32_t PR, int32_t PG,
                               int32_t PO, int16_t Gb)
{
    int64_t AMB    = mx_preproc_amb(amb_new, amb_old, Gb);
    int64_t Asub   = ((int64_t)PT * 10000000000LL) >> 44;
    int64_t Bsub   = AMB - (((int64_t)PR * 1000) >> 8);
    int64_t Ablock = Asub * Bsub * Bsub;
    int64_t Bblock = ((Bsub * 10000000) / PG) << 20;
    int64_t Cblock = ((int64_t)PO * 10000000000LL) >> 8;
    int64_t sum    = Ablock / 1000000 + Bblock + Cblock;
    return (int32_t)(sum / 10000000);
}

static int64_t mx_calc_ta4(int64_t TAdut, int64_t scale)
{
    int64_t t = TAdut / scale + 27315;
    return t * t * t * t;
}

static int32_t mx_calc_obj_iter(int32_t prev_mc, int64_t object,
                                int64_t TAdut, int64_t TAdut4,
                                int32_t Fa, int32_t Fb, int32_t Ga,
                                int16_t Ha, int16_t Hb, uint16_t emiss)
{
    int64_t Ha_c = ((int64_t)Ha * 1000000) >> 14;
    int64_t Hb_c = ((int64_t)Hb * 100) >> 10;
    int64_t ksTO = ((int64_t)Ga * (prev_mc - 25000) * 1000) >> 36;
    int64_t ksTA = ((int64_t)Fb * (TAdut - 25000000)) >> 36;
    int64_t aCor = (((int64_t)Fa * 10000000000LL) >> 46) * Ha_c / 1000;
    aCor *= (1000000 + ksTO + ksTA);
    aCor = emiss * (aCor / 100000) / 1000;
    int64_t irA  = (object * 10000000) / aCor;
    /* model-only guard: the driver's isqrt64() takes uint64 — a
     * negative radicand is meaningless (it only arises while the
     * solver probes raw values far below any reachable temp).       */
    if (aCor <= 0 || irA * 1000000000000LL + TAdut4 <= 0) {
        return INT32_MIN / 2;
    }
    return (int32_t)((mx_isqrt64(mx_isqrt64(irA * 1000000000000LL + TAdut4))
                      - 27315 - Hb_c) * 10);
}

static int32_t mx_calc_object(int64_t object, int64_t ambient,
                              int32_t Ea, int32_t Eb, int32_t Fa, int32_t Fb,
                              int32_t Ga, int16_t Ha, int16_t Hb, uint16_t emiss)
{
    int64_t kTA    = ((int64_t)Ea * 1000) >> 16;
    int64_t kTA0   = ((int64_t)Eb * 1000) >> 8;
    int64_t TAdut  = ((ambient - kTA0) * 1000000) / kTA + 25000000;
    int64_t TAdut4 = mx_calc_ta4(TAdut, 10000);
    int32_t temp   = 25000;
    for (int i = 0; i < 5; i++)
        temp = mx_calc_obj_iter(temp, object, TAdut, TAdut4,
                                Fa, Fb, Ga, Ha, Hb, emiss);
    return temp;
}

static int32_t mx_calc_object_ext(int64_t object, int64_t ambient,
                                  int64_t reflected_mc,
                                  int32_t Ea, int32_t Eb, int32_t Fa, int32_t Fb,
                                  int32_t Ga, int16_t Ha, int16_t Hb,
                                  uint16_t emiss)
{
    int64_t kTA    = ((int64_t)Ea * 1000) >> 16;
    int64_t kTA0   = ((int64_t)Eb * 1000) >> 8;
    int64_t TAdut  = ((ambient - kTA0) * 1000000) / kTA + 25000000;
    int64_t Tr4    = mx_calc_ta4(reflected_mc, 10);
    int64_t TAdut4 = mx_calc_ta4(TAdut, 10000);
    int64_t TaTr4  = Tr4 - ((Tr4 - TAdut4) / emiss) * 1000;
    int32_t temp   = 25000;
    for (int i = 0; i < 5; i++)
        temp = mx_calc_obj_iter(temp, object, TAdut, TaTr4,
                                Fa / 2, Fb, Ga, Ha, Hb, emiss);
    return temp;
}

/* ================ EE image + scene synthesis ======================= */

static void mx_ee_put32(mlx_t *s, uint16_t reg, int32_t v)
{
    s->ee[reg - M_EE_BASE]       = (uint16_t)((uint32_t)v & 0xFFFFu);
    s->ee[reg - M_EE_BASE + 1]   = (uint16_t)((uint32_t)v >> 16);
}

static void mx_ee_put16(mlx_t *s, uint16_t reg, int16_t v)
{
    s->ee[reg - M_EE_BASE] = (uint16_t)v;
}

/* One-time EEPROM image.  P_G and Eb are *derived* from the forward
 * math so the ambient/object equations invert exactly.               */
static void mx_ee_init(mlx_t *s)
{
    if (s->ee_ready) {
        return;
    }
    memset(s->ee, 0, sizeof s->ee);
    s->ee[M_EE_ID0     - M_EE_BASE] = 0x4C58u;      /* 'LX'           */
    s->ee[M_EE_ID0 + 1 - M_EE_BASE] = 0x0632u;
    s->ee[M_EE_ID0 + 2 - M_EE_BASE] = 0x0001u;
    s->ee[M_EE_PRODUCT - M_EE_BASE] = M_PRODUCT;
    s->ee[M_EE_VERSION - M_EE_BASE] = M_VERSION;
    s->ee[M_EE_MEAS_1  - M_EE_BASE] = M_MEAS1;

    /* P_G so that an=ao=M_AMB_REF ⇒ calc_ambient = M_TAMB_REF_MC:
     * tamb = (AMB<<20)/P_G  ⇒  P_G = (AMB0<<20)/TAMB_REF             */
    int64_t amb0 = mx_preproc_amb(M_AMB_REF, M_AMB_REF, (int16_t)M_GB);
    s->pg = (int32_t)((amb0 << 20) / M_TAMB_REF_MC);
    mx_ee_put32(s, M_EE_P_G, s->pg);
    mx_ee_put32(s, M_EE_P_R, M_PR);
    mx_ee_put32(s, M_EE_P_T, M_PT);
    mx_ee_put32(s, M_EE_P_O, M_PO);

    /* Eb so that kTA0 == AMB0 ⇒ at the reference ambient TAdut sits
     * exactly at 25 °C die (25 000 000).  Refine ±600 around the
     * closed-form estimate to absorb the >>8 truncation.            */
    int64_t best = INT64_MAX;
    int32_t eb0  = (int32_t)(amb0 * 256 / 1000);
    s->eb = eb0;
    for (int32_t eb = eb0 - 600; eb <= eb0 + 600; eb++) {
        int64_t kta0 = ((int64_t)eb * 1000) >> 8;
        int64_t d    = kta0 - amb0;
        if (d < 0) d = -d;
        if (d < best) { best = d; s->eb = eb; }
    }
    mx_ee_put32(s, M_EE_Ea, M_EA);
    mx_ee_put32(s, M_EE_Eb, s->eb);
    mx_ee_put32(s, M_EE_Fa, M_FA);
    mx_ee_put32(s, M_EE_Fb, M_FB);
    mx_ee_put32(s, M_EE_Ga, M_GA);
    mx_ee_put16(s, M_EE_Gb, (int16_t)M_GB);
    mx_ee_put16(s, M_EE_Ka, (int16_t)M_KA);
    mx_ee_put16(s, M_EE_Kb, (int16_t)M_KB);
    mx_ee_put16(s, M_EE_Ha, (int16_t)M_HA);
    mx_ee_put16(s, M_EE_Hb, (int16_t)M_HB);
    s->ee_ready = true;
}

/* monotonic int16 solve: smallest x in [-32768,32767] with f(x) ≥ y,
 * then pick the nearer of {x, x-1}.  f must be non-decreasing.       */
typedef int32_t (*mx_solve_fn)(int16_t x, void *ctx);

static int16_t mx_solve(mx_solve_fn f, int32_t target, void *ctx)
{
    int lo = -32768, hi = 32767;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (f((int16_t)mid, ctx) < target) lo = mid + 1; else hi = mid;
    }
    /* lo is the lower bound; compare with lo-1 for the closest hit   */
    int16_t x = (int16_t)lo;
    if (x > -32768) {
        int64_t d_hi = (int64_t)f(x, ctx) - target;
        int64_t d_lo = target - (int64_t)f((int16_t)(x - 1), ctx);
        if (d_lo >= 0 && d_lo < d_hi) x = (int16_t)(x - 1);
    }
    return x;
}

typedef struct { const mlx_t *s; int16_t ao; } mx_amb_ctx_t;
static int32_t mx_amb_of(int16_t an, void *ctx)
{
    const mx_amb_ctx_t *c = ctx;
    return mx_calc_ambient(an, c->ao, M_PT, M_PR, c->s->pg, M_PO,
                           (int16_t)M_GB);
}

typedef struct { const mlx_t *s; int16_t an, ao; int64_t amb; } mx_obj_ctx_t;
static int32_t mx_obj_of(int16_t v, void *ctx)
{
    const mx_obj_ctx_t *c = ctx;
    int64_t obj = mx_preproc_obj(v, v, c->an, c->ao, (int16_t)M_KA);
    return mx_calc_object(obj, c->amb, M_EA, c->s->eb, M_FA, M_FB, M_GA,
                          (int16_t)M_HA, (int16_t)M_HB, M_EMISS);
}
static int32_t mx_obj_ext_of(int16_t v, void *ctx)
{
    const mx_obj_ctx_t *c = ctx;
    int64_t obj = mx_preproc_obj_ext(v, c->an, c->ao, (int16_t)M_KA);
    return mx_calc_object_ext(obj, c->amb, M_REFL_MC, M_EA, c->s->eb,
                              M_FA, M_FB, M_GA,
                              (int16_t)M_HA, (int16_t)M_HB, M_EMISS);
}

/* Refill the RAM result slots from the solved raws — what the silicon
 * would have written at the last completed cycle.                    */
static void mx_ram_fill(mlx_t *s)
{
    s->ram[M_RAM3_1] = (uint16_t)s->an;         /* ambient new/old    */
    s->ram[M_RAM3_2] = (uint16_t)s->ao;
    s->ram[M_RAM1_1] = s->ram[M_RAM2_1] = (uint16_t)s->s_med;
    s->ram[M_RAM1_2] = s->ram[M_RAM2_2] = (uint16_t)s->s_med;

    s->ram[M_EXT_AMB1] = (uint16_t)s->an;
    s->ram[M_EXT_AMB2] = (uint16_t)s->ao;
    int16_t v = s->s_ext;                       /* (v-v/2-v/2+v)/2     */
    s->ram[M_EXT_OBJ1] = (uint16_t)v;           /*  + v/4+v/4 = v      */
    s->ram[M_EXT_OBJ2] = (uint16_t)(v / 2);
    s->ram[M_EXT_OBJ3] = (uint16_t)(v / 2);
    s->ram[M_EXT_OBJ4] = (uint16_t)v;
    s->ram[M_EXT_OBJ5] = (uint16_t)(v / 4);
    s->ram[M_EXT_OBJ6] = (uint16_t)(v / 4);
}

/* Re-derive the raw raws from the optical_feed temperatures.         */
static void mx_resynth(mlx_t *s)
{
    int32_t targ_amb = lroundf(g_vbopt.mlx_amb_c * 1000.0f);
    int32_t targ_obj = lroundf(g_vbopt.mlx_obj_c * 1000.0f);

    /* ambient pair: AMB ∝ an/ao — pick ao so an stays inside int16.
     * Normal spans use the mid-scale reference 20000; extreme temps
     * shrink ao to keep the ratio reachable.                          */
    double r = targ_amb / (double)M_TAMB_REF_MC;
    double ra = r < 0 ? -r : r;
    int16_t ao = (ra <= 1.4) ? M_AMB_REF
                 : (int16_t)(28000.0 / ra > M_AMB_REF ? M_AMB_REF
                            : 28000.0 / ra);
    mx_amb_ctx_t ac = { .s = s, .ao = ao };
    int16_t an = mx_solve(mx_amb_of, targ_amb, &ac);
    s->ao = ao;
    s->an = an;

    int64_t amb = mx_preproc_amb(an, ao, (int16_t)M_GB);
    mx_obj_ctx_t oc = { .s = s, .an = an, .ao = ao, .amb = amb };
    s->s_med = mx_solve(mx_obj_of,     targ_obj, &oc);
    s->s_ext = mx_solve(mx_obj_ext_of, targ_obj, &oc);
    s->seen_obj_c = g_vbopt.mlx_obj_c;
    s->seen_amb_c = g_vbopt.mlx_amb_c;
    mx_ram_fill(s);
}

/* ================ engine =========================================== */

static void mx_por(mlx_t *s, int64_t now)
{
    s->ptr             = 0;
    s->control         = 0;
    s->i2c_addr_reg    = M_ADDR;
    s->new_data        = false;
    s->brst            = false;
    s->cyc_pos         = 0;
    s->next_meas_us    = 0;
    s->last_meas_us    = 0;
    s->ee_busy_until_us = now + M_EE_BUSY_US;
    memset(s->ram, 0, sizeof s->ram);
}

/* One completed sub-measurement: advance CYC_POS, raise NEW_DATA,
 * refresh RAM slots for the cycle that just finished.                */
static void mx_meas_step(mlx_t *s)
{
    mx_ee_init(s);                      /* idempotent EEPROM image    */
    if (s->seen_obj_c != g_vbopt.mlx_obj_c ||
        s->seen_amb_c != g_vbopt.mlx_amb_c) {
        mx_resynth(s);                  /* feed moved → re-solve raws */
    }
    if (((s->control & M_CTRL_MTYP) >> 4) == M_MTYP_EXT) {
        s->cyc_pos = (uint8_t)(s->cyc_pos >= 19 ? 1 : s->cyc_pos + 1);
    } else {
        s->cyc_pos = (uint8_t)(s->cyc_pos == 1 ? 2 : 1);
    }
    mx_ram_fill(s);
    s->new_data = true;
}

/* Lazy async progression — called from tick() and every I2C path.    */
static void mx_poll(mlx_t *s, int64_t now)
{
    if (!vboard_rail(VB_RAIL_1V8)) {
        if (s->powered) {
            s->powered = false;
            mx_por(s, now);
        }
        return;
    }
    if (!s->powered) {
        s->powered = true;
        mx_por(s, now);
    }
    if (now < s->ee_busy_until_us) {
        return;                          /* EEPROM copy in progress  */
    }
    mx_ee_init(s);
    if ((s->control & M_CTRL_MODE) == M_MODE_CONT) {
        if (s->next_meas_us == 0 || s->next_meas_us < now - 4 * M_MEAS_US) {
            s->next_meas_us = now + M_MEAS_US;   /* (re)arm one slot   */
        }
        while (now >= s->next_meas_us) {
            s->last_meas_us  = s->next_meas_us;
            s->next_meas_us += M_MEAS_US;
            mx_meas_step(s);
        }
    }
}

static uint16_t mx_status(const mlx_t *s, int64_t now)
{
    uint16_t st = 0;
    if (s->new_data)                    st |= M_ST_NEW_DATA;
    if (s->brst)                        st |= M_ST_BRST;
    st |= (uint16_t)((s->cyc_pos << 2) & M_ST_CYC_POS);
    if (now < s->ee_busy_until_us)      st |= M_ST_EE_BUSY;
    /* a conversion is "active" for the first ~20 ms after each slot   */
    if ((s->control & M_CTRL_MODE) == M_MODE_CONT &&
        s->last_meas_us != 0 &&
        now >= s->last_meas_us && now - s->last_meas_us < M_BUSY_US) {
        st |= M_ST_BUSY;
    }
    return st;
}

/* ---- register access ------------------------------------------------ */

static uint16_t mx_reg_read(mlx_t *s, uint16_t reg, int64_t now)
{
    if (reg >= M_RAM_BASE && reg < M_RAM_BASE + M_RAM_WORDS) {
        return s->ram[reg - M_RAM_BASE];
    }
    if (reg >= M_EE_BASE && reg < M_EE_BASE + M_EE_WORDS) {
        return s->ee[reg - M_EE_BASE];
    }
    switch (reg) {
    case M_REG_ADDR:    return s->i2c_addr_reg;
    case M_REG_CONTROL: return s->control;
    case M_REG_STATUS:  return mx_status(s, now);
    case M_REG_I2C_CMD: return 0x0000;
    default:            return 0x0000;
    }
}

static void mx_reg_write(mlx_t *s, uint16_t reg, uint16_t v, int64_t now)
{
    switch (reg) {
    case M_REG_ADDR:
        s->i2c_addr_reg = v;
        break;
    case M_REG_CONTROL: {
        uint16_t old = s->control;
        s->control = v;
        /* SOC rising edge in a step mode → one measurement, then the
         * part drops back to sleep (driver flow: set_meas_type parks
         * in STEP before the global reset).                          */
        if ((v & M_CTRL_SOC) && !(old & M_CTRL_SOC) &&
            (v & M_CTRL_MODE) != M_MODE_CONT &&
            (v & M_CTRL_MODE) != M_MODE_HALT) {
            mx_meas_step(s);
            s->control &= (uint16_t)~M_CTRL_SOC;
        }
        if ((v & M_CTRL_MODE) == M_MODE_CONT &&
            (old & M_CTRL_MODE) != M_MODE_CONT) {
            s->next_meas_us = 0;        /* arm engine on CONT entry   */
        }
        break;
    }
    case M_REG_I2C_CMD:
        if (v == M_RESET_CMD) {
            mx_por(s, now);             /* addressed reset → POR      */
        }
        break;
    case M_REG_STATUS:
        s->new_data = (v & M_ST_NEW_DATA) != 0;
        s->brst     = (v & M_ST_BRST)     != 0;   /* ND/BRST are R/W  */
        break;
    case M_EE_Ha:
        mx_ee_put16(s, M_EE_Ha, (int16_t)v);    /* customer-calib R/W */
        break;
    case M_EE_Hb:
        mx_ee_put16(s, M_EE_Hb, (int16_t)v);
        break;
    case M_EE_MEAS_1:
        s->ee[M_EE_MEAS_1 - M_EE_BASE] = v;
        break;
    default:
        break;                                 /* EE/RAM are RO here */
    }
}

/* ---- I2C interface --------------------------------------------------- */

static esp_err_t m_i2c_write(vboard_dev_t *d, const uint8_t *data, size_t len)
{
    mlx_t *s = (mlx_t *)d->ctx;
    int64_t now = vboard_now_us();
    mx_poll(s, now);
    if (!vboard_rail(VB_RAIL_1V8)) {
        return VB_ESP_ERR_TIMEOUT;              /* rail off → NACK      */
    }
    if (len == 0) {
        return VB_ESP_OK;                       /* bare probe           */
    }
    if (len < 2) {
        return VB_ESP_FAIL;                     /* 16-bit reg addr req'd*/
    }
    s->ptr = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    size_t i = 2;
    while (i + 1 < len) {                       /* word stream          */
        mx_reg_write(s, s->ptr,
                     (uint16_t)(((uint16_t)data[i] << 8) | data[i + 1]),
                     now);
        s->ptr++;
        i += 2;
    }
    /* a trailing odd byte is a malformed half-word — drop it         */
    return VB_ESP_OK;
}

static esp_err_t m_i2c_read(vboard_dev_t *d, uint8_t *data, size_t len)
{
    mlx_t *s = (mlx_t *)d->ctx;
    int64_t now = vboard_now_us();
    mx_poll(s, now);
    if (!vboard_rail(VB_RAIL_1V8)) {
        return VB_ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < len; i += 2) {       /* BE word stream       */
        uint16_t v = mx_reg_read(s, s->ptr, now);
        data[i] = (uint8_t)(v >> 8);
        if (i + 1 < len) {
            data[i + 1] = (uint8_t)(v & 0xFF);
        }
        s->ptr++;
    }
    return VB_ESP_OK;
}

static void m_tick(vboard_dev_t *d, int64_t now_us)
{
    mx_poll((mlx_t *)d->ctx, now_us);
}

static void m_reset(vboard_dev_t *d)
{
    mlx_t *s = (mlx_t *)d->ctx;
    if (vboard_rail(VB_RAIL_1V8)) {
        s->powered = true;
        mx_por(s, vboard_now_us());
    } else {
        s->powered = false;
    }
}

/* --------------------------------------------------------------- */

static mlx_t s_mlx = {
    .dev = {
        .name      = "mlx90632",
        .i2c_addr  = M_ADDR,
        .i2c_write = m_i2c_write,
        .i2c_read  = m_i2c_read,
        .spi_cs    = -1,
        .tick      = m_tick,
        .reset     = m_reset,
    },
};

void vbdev_mlx90632_register(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    s_mlx.dev.ctx = &s_mlx;
    vboard_register(&s_mlx.dev);
}

/* Self-register at load so the default population exists even before
 * vboard_init() wires anything explicitly (idempotent via `done`).   */
__attribute__((constructor))
static void m_ctor(void)
{
    vbdev_mlx90632_register();
}
