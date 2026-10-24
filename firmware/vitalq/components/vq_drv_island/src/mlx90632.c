/* mlx90632.c — Melexis MLX90632 non-contact IR temperature sensor.
 * I2C 0x3A on the 1.8 V island (ADDR strap = GND — NOT hw_v1's 0x3B).
 *
 * Register protocol: 16-bit register address + 16-bit data, big-endian.
 * Math: DSP5 medical measurement equation set, MLX90632 datasheet §11.1,
 * ported verbatim (same fixed-point scaling) from the reference Melexis/
 * Linux driver so results track the datasheet worked example (§11.1.4).
 * The extended-range variant (§11.2) is implemented too — it only
 * activates on extended-range silicon or out-of-range medical reads.
 *
 * RAM flow (medical): continuous mode → REG_STATUS.new_data →
 *   ambient  = RAM_3(1)=RAM_6, RAM_3(2)=RAM_9
 *   object   = (RAM_1(ch)+RAM_2(ch))/2 with ch = last completed cycle 1|2
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "vq_drv_island.h"
#include "vq_island_priv.h"
#include "i2c_bus.h"
#include "board_pins.h"

static const char *TAG = "mlx90632";

/* ---------------- memory map (datasheet §7, Table 7) ------------------ */
#define MLX_RAM_BASE        0x4000
#define MLX_RAM_1(m)        (MLX_RAM_BASE + 3 * (m))
#define MLX_RAM_2(m)        (MLX_RAM_BASE + 3 * (m) + 1)
#define MLX_RAM_3(m)        (MLX_RAM_BASE + 3 * (m) + 2)
/* medical measurement table */
#define MLX_RAM_AMB_NEW     MLX_RAM_3(1)    /* RAM_6  = 0x4005 */
#define MLX_RAM_AMB_OLD     MLX_RAM_3(2)    /* RAM_9  = 0x4008 */
/* extended-range table: meas 17/18/19 → RAM_52..RAM_60 */
#define MLX_RAM_EXT_AMB_1   MLX_RAM_3(17)   /* RAM_54 = 0x4035 */
#define MLX_RAM_EXT_AMB_2   MLX_RAM_3(18)   /* RAM_57 = 0x4038 */
#define MLX_RAM_EXT_OBJ_1   MLX_RAM_1(17)   /* RAM_52 */
#define MLX_RAM_EXT_OBJ_2   MLX_RAM_2(17)   /* RAM_53 */
#define MLX_RAM_EXT_OBJ_3   MLX_RAM_1(18)   /* RAM_55 */
#define MLX_RAM_EXT_OBJ_4   MLX_RAM_2(18)   /* RAM_56 */
#define MLX_RAM_EXT_OBJ_5   MLX_RAM_1(19)   /* RAM_58 */
#define MLX_RAM_EXT_OBJ_6   MLX_RAM_2(19)   /* RAM_59 */

#define MLX_EE_ID0          0x2405
#define MLX_EE_PRODUCT_CODE 0x2409
#define MLX_EE_VERSION      0x240B
#define MLX_EE_P_R          0x240C
#define MLX_EE_P_G          0x240E
#define MLX_EE_P_T          0x2410
#define MLX_EE_P_O          0x2412
#define MLX_EE_Ea           0x2424
#define MLX_EE_Eb           0x2426
#define MLX_EE_Fa           0x2428
#define MLX_EE_Fb           0x242A
#define MLX_EE_Ga           0x242C
#define MLX_EE_Gb           0x242E
#define MLX_EE_Ka           0x242F
#define MLX_EE_Kb           0x2430
#define MLX_EE_Ha           0x2481          /* customer-calib, R/W        */
#define MLX_EE_Hb           0x2482
#define MLX_EE_MEAS_1       0x24E1

#define MLX_REG_I2C_ADDR    0x3000
#define MLX_REG_CONTROL     0x3001
#define MLX_REG_I2C_CMD     0x3005
#define MLX_REG_STATUS      0x3FFF

#define MLX_CTRL_SOB        0x0800          /* bit11: start of burst      */
#define MLX_CTRL_SOC        0x0008          /* bit3 : start of conv.      */
#define MLX_CTRL_MTYP_SHIFT 4               /* bits8:4 meas_select        */
#define MLX_MODE_HALT       0x00            /* bits2:1 mode               */
#define MLX_MODE_SLEEP_STEP 0x02
#define MLX_MODE_STEP       0x04
#define MLX_MODE_CONT       0x06

#define MLX_MTYP_MEDICAL    0x00
#define MLX_MTYP_EXTENDED   0x11

#define MLX_STAT_BUSY       0x0400
#define MLX_STAT_EE_BUSY    0x0200
#define MLX_STAT_BRST       0x0100
#define MLX_STAT_CYC_POS    0x007C          /* bits6:2                    */
#define MLX_STAT_NEW_DATA   0x0001

#define MLX_RESET_CMD       0x0006          /* REG_I2C_CMD addressed rst  */

#define MLX_ID_MASK         0x7FFF
#define MLX_ID_MEDICAL      0x0105
#define MLX_ID_CONSUMER     0x0205
#define MLX_ID_EXTENDED     0x0505
#define MLX_DSP_VERSION     0x05
#define MLX_DSP_MASK        0x00FF

#define MLX_REF_12          12              /* ResCtrlRef ch1/ch2         */
#define MLX_REF_3           12              /* ResCtrlRef ch3             */
#define MLX_EXT_LIMIT       27000           /* medical→extended switch    */
#define MLX_MEAS_WAIT_MS    2100            /* MEAS_MAX_TIME at lowest
                                               * EE refresh rate         */
#define MLX_PERIOD_MS       1000            /* 1 Hz                       */
#define MLX_EMISSIVITY      1000            /* ε = 1.0 (×1000)            */
#define MLX_OBJ_AMB_MC      25000           /* extended reflected T, m°C  */

typedef struct {
    TaskHandle_t      task;
    SemaphoreHandle_t mtx;
    StaticSemaphore_t mtx_buf;
    volatile bool  streaming;
    bool           configured;
    bool           present;
    uint8_t        mtyp;                    /* active meas_select         */
    /* EEPROM calib constants (signed per datasheet §11.1.3)             */
    int32_t        PR, PG, PT, PO;
    int32_t        Ea, Eb, Fa, Fb, Ga;
    int16_t        Gb, Ka, Kb, Ha, Hb;      /* Kb: stored, unused by DSP5 */
    int32_t        reflected_mc;            /* extended-range Tr, m°C     */
    uint16_t       emiss;                   /* ε × 1000                   */
    int64_t        last_ok_us;
    uint32_t       consec_err;
} mlx_priv_t;

static mlx_priv_t  s_priv;
static vq_sensor_t s_sensor;

/* ---------------- bus helpers: 16-bit reg, big-endian ------------------ */
static esp_err_t rd16(uint16_t reg, uint16_t *val)
{
    uint8_t w[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    uint8_t r[2];
    esp_err_t e = vq_i2c_write_read(VQ_ADDR_MLX90632, w, sizeof w, r, sizeof r);
    if (e == ESP_OK) *val = (uint16_t)((r[0] << 8) | r[1]);
    return e;
}
static esp_err_t wr16(uint16_t reg, uint16_t val)
{
    uint8_t w[4] = { (uint8_t)(reg >> 8), (uint8_t)reg,
                     (uint8_t)(val >> 8), (uint8_t)val };
    return vq_i2c_write(VQ_ADDR_MLX90632, w, sizeof w);
}
static esp_err_t rd32(uint16_t reg_lsb, int32_t *val)
{
    uint16_t lo, hi;
    esp_err_t e = rd16(reg_lsb, &lo);
    if (e != ESP_OK) return e;
    if ((e = rd16(reg_lsb + 1, &hi)) != ESP_OK) return e;
    *val = (int32_t)(((uint32_t)hi << 16) | lo);
    return ESP_OK;
}
static esp_err_t status_rmw(uint16_t set, uint16_t clr)
{
    uint16_t st;
    esp_err_t e = rd16(MLX_REG_STATUS, &st);
    if (e != ESP_OK) return e;
    return wr16(MLX_REG_STATUS, (st | set) & ~clr);
}

esp_err_t mlx90632_read_ids(uint16_t *product_code, uint16_t *ee_version)
{
    esp_err_t e = rd16(MLX_EE_PRODUCT_CODE, product_code);
    if (e != ESP_OK) return e;
    return rd16(MLX_EE_VERSION, ee_version);
}

/* ---------------- DSP5 fixed-point math (datasheet §11) ----------------
 * Ported 1:1 from the Melexis reference implementation — the scaling
 * constants below are the datasheet's fractional powers-of-two rendered
 * in int64 so no float is needed and results are bit-reproducible.       */
static int64_t isqrt64(uint64_t x)
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

static int64_t preproc_amb(int16_t amb_new, int16_t amb_old, int16_t Gb)
{
    int64_t kGb  = ((int64_t)Gb * 1000) >> 10;
    int64_t VR   = (int64_t)amb_old * 1000000 + kGb * ((int64_t)amb_new * 1000 / MLX_REF_3);
    int64_t tmp  = (((int64_t)amb_new * 1000000000000LL) / MLX_REF_3) / VR;
    return (tmp << 19) / 1000;
}

static int64_t preproc_obj(int16_t obj_new, int16_t obj_old,
                           int16_t amb_new, int16_t amb_old, int16_t Ka)
{
    int64_t kKa  = ((int64_t)Ka * 1000) >> 10;
    int64_t VR   = (int64_t)amb_old * 1000000 + kKa * ((int64_t)amb_new * 1000 / MLX_REF_3);
    int64_t S    = ((int32_t)obj_new + obj_old) / 2;
    int64_t tmp  = ((S * 1000000000000LL) / MLX_REF_12) / VR;
    return (tmp << 19) / 1000;
}

static int64_t preproc_obj_ext(int16_t obj_new, int16_t amb_new,
                               int16_t amb_old, int16_t Ka)
{
    int64_t kKa  = ((int64_t)Ka * 1000) >> 10;
    int64_t VR   = (int64_t)amb_old * 1000000 + kKa * ((int64_t)amb_new * 1000 / MLX_REF_3);
    int64_t tmp  = (((int64_t)obj_new * 1000000000000LL) / MLX_REF_12) / VR;
    return (tmp << 19) / 1000;
}

/* Ta[°C] = P_O + (AMB − P_R)/P_G + P_T·(AMB − P_R)²  → returns m°C        */
static int32_t calc_ambient(int16_t amb_new, int16_t amb_old,
                            int32_t PT, int32_t PR, int32_t PG, int32_t PO,
                            int16_t Gb)
{
    int64_t AMB    = preproc_amb(amb_new, amb_old, Gb);
    int64_t Asub   = ((int64_t)PT * 10000000000LL) >> 44;
    int64_t Bsub   = AMB - (((int64_t)PR * 1000) >> 8);
    int64_t Ablock = Asub * Bsub * Bsub;
    int64_t Bblock = ((Bsub * 10000000) / PG) << 20;
    int64_t Cblock = ((int64_t)PO * 10000000000LL) >> 8;
    int64_t sum    = Ablock / 1000000 + Bblock + Cblock;
    return (int32_t)(sum / 10000000);
}

static int64_t calc_ta4(int64_t TAdut, int64_t scale)
{
    int64_t t = TAdut / scale + 27315;
    return t * t * t * t;
}

static int32_t calc_obj_iter(int32_t prev_mc, int64_t object,
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
    return (int32_t)((isqrt64(isqrt64(irA * 1000000000000LL + TAdut4))
                      - 27315 - Hb_c) * 10);
}

/* To[°C] medical equation (datasheet §11.1.3), 5 iterations. m°C out.    */
static int32_t calc_object(int64_t object, int64_t ambient,
                           int32_t Ea, int32_t Eb, int32_t Fa, int32_t Fb,
                           int32_t Ga, int16_t Ha, int16_t Hb, uint16_t emiss)
{
    int64_t kTA    = ((int64_t)Ea * 1000) >> 16;
    int64_t kTA0   = ((int64_t)Eb * 1000) >> 8;
    int64_t TAdut  = ((ambient - kTA0) * 1000000) / kTA + 25000000;
    int64_t TAdut4 = calc_ta4(TAdut, 10000);
    int32_t temp   = 25000;
    for (int i = 0; i < 5; i++)
        temp = calc_obj_iter(temp, object, TAdut, TAdut4,
                             Fa, Fb, Ga, Ha, Hb, emiss);
    return temp;
}

/* Extended-range variant (datasheet §11.2.3): Fa/2 term + reflected-T⁴
 * blend TaTr4 = Tr⁴ − (Tr⁴ − TAdut⁴)/ε.                                  */
static int32_t calc_object_ext(int64_t object, int64_t ambient,
                               int64_t reflected_mc,
                               int32_t Ea, int32_t Eb, int32_t Fa, int32_t Fb,
                               int32_t Ga, int16_t Ha, int16_t Hb, uint16_t emiss)
{
    int64_t kTA    = ((int64_t)Ea * 1000) >> 16;
    int64_t kTA0   = ((int64_t)Eb * 1000) >> 8;
    int64_t TAdut  = ((ambient - kTA0) * 1000000) / kTA + 25000000;
    int64_t Tr4    = calc_ta4(reflected_mc, 10);
    int64_t TAdut4 = calc_ta4(TAdut, 10000);
    int64_t TaTr4  = Tr4 - ((Tr4 - TAdut4) / emiss) * 1000;
    int32_t temp   = 25000;
    for (int i = 0; i < 5; i++)
        temp = calc_obj_iter(temp, object, TAdut, TaTr4,
                             Fa / 2, Fb, Ga, Ha, Hb, emiss);
    return temp;
}

/* ---------------- measurement flow ------------------------------------- */
static esp_err_t wait_new_data(int *cyc_pos, int64_t *ready_us)
{
    esp_err_t e = status_rmw(0, MLX_STAT_NEW_DATA);
    if (e != ESP_OK) return e;
    uint16_t st = 0;
    int64_t deadline = esp_timer_get_time() + MLX_MEAS_WAIT_MS * 1000;
    do {
        if ((e = rd16(MLX_REG_STATUS, &st)) != ESP_OK) return e;
        if (st & MLX_STAT_NEW_DATA) {
            *ready_us = esp_timer_get_time();
            *cyc_pos = (st & MLX_STAT_CYC_POS) >> 2;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    } while (esp_timer_get_time() < deadline);
    *ready_us = esp_timer_get_time();
    return ESP_ERR_TIMEOUT;
}

static esp_err_t read_ch(int16_t *out, uint16_t reg)
{
    uint16_t v;
    esp_err_t e = rd16(reg, &v);
    if (e == ESP_OK) *out = (int16_t)v;
    return e;
}

/* signed EEPROM word → int16_t out */
static esp_err_t rd_s16(uint16_t reg, int16_t *out)
{
    return read_ch(out, reg);
}

static esp_err_t measure_extended(int32_t *tobj_mc, int64_t *t_us);

/* Medical measurement → both temps in m°C. Blocks until a fresh dataset. */
static esp_err_t measure_medical(int32_t *tobj_mc, int32_t *tamb_mc,
                                 int64_t *t_us)
{
    int64_t ready_us;
    int cyc = 0;
    esp_err_t e = wait_new_data(&cyc, &ready_us);
    if (e != ESP_OK) return e;
    if (cyc != 1 && cyc != 2)                    /* medical meas = 1 or 2 */
        return ESP_ERR_INVALID_RESPONSE;
    int ch_new = cyc;
    int ch_old = (cyc == 1) ? 2 : 1;

    int16_t an, ao, o1n, o2n, o1o, o2o;
    if ((e = read_ch(&an, MLX_RAM_AMB_NEW)) != ESP_OK)  return e;
    if ((e = read_ch(&ao, MLX_RAM_AMB_OLD)) != ESP_OK)  return e;
    /* read RAM_2 before RAM_1 inside each channel, as the reference does */
    if ((e = read_ch(&o2n, MLX_RAM_2(ch_new))) != ESP_OK) return e;
    if ((e = read_ch(&o1n, MLX_RAM_1(ch_new))) != ESP_OK) return e;
    if ((e = read_ch(&o2o, MLX_RAM_2(ch_old))) != ESP_OK) return e;
    if ((e = read_ch(&o1o, MLX_RAM_1(ch_old))) != ESP_OK) return e;
    int16_t obj_new = (int16_t)(((int32_t)o1n + o2n) / 2);
    int16_t obj_old = (int16_t)(((int32_t)o1o + o2o) / 2);

    *tamb_mc = calc_ambient(an, ao, s_priv.PT, s_priv.PR, s_priv.PG,
                            s_priv.PO, s_priv.Gb);

    if (obj_new > MLX_EXT_LIMIT && s_priv.mtyp == MLX_MTYP_EXTENDED) {
        /* out-of-range medical frame → rerun in extended mode           */
        return measure_extended(tobj_mc, t_us);  /* forward decl below   */
    }
    int64_t amb = preproc_amb(an, ao, s_priv.Gb);
    int64_t obj = preproc_obj(obj_new, obj_old, an, ao, s_priv.Ka);
    *tobj_mc = calc_object(obj, amb, s_priv.Ea, s_priv.Eb, s_priv.Fa,
                           s_priv.Fb, s_priv.Ga, s_priv.Ha, s_priv.Hb,
                           s_priv.emiss);
    *t_us = ready_us;
    return ESP_OK;
}

/* meas_select switch per datasheet §8.4 sequence (reset → halt+cfg → go).
 * Caveat from the datasheet: a device in SLEEPING step must first be put
 * into step mode before a global reset — done unconditionally here.      */
static esp_err_t set_meas_type(uint8_t mtyp)
{
    esp_err_t e;
    uint16_t ctl;
    if ((e = rd16(MLX_REG_CONTROL, &ctl)) != ESP_OK) return e;
    if ((e = wr16(MLX_REG_CONTROL, (ctl & ~0x06) | MLX_MODE_STEP)) != ESP_OK)
        return e;
    if ((e = wr16(MLX_REG_I2C_CMD, MLX_RESET_CMD)) != ESP_OK) return e;
    esp_rom_delay_us(200);                       /* reset settle          */
    if ((e = rd16(MLX_REG_CONTROL, &ctl)) != ESP_OK) return e;
    ctl &= ~(0x1F << MLX_CTRL_MTYP_SHIFT | 0x06);
    if ((e = wr16(MLX_REG_CONTROL, ctl | ((uint16_t)mtyp << MLX_CTRL_MTYP_SHIFT)
                  | MLX_MODE_HALT)) != ESP_OK) return e;
    if ((e = rd16(MLX_REG_CONTROL, &ctl)) != ESP_OK) return e;
    ctl &= ~0x06;
    return wr16(MLX_REG_CONTROL, ctl | MLX_MODE_CONT);
}

static esp_err_t measure_extended(int32_t *tobj_mc, int64_t *t_us)
{
    esp_err_t e;
    if (s_priv.mtyp != MLX_MTYP_EXTENDED) {
        if ((e = set_meas_type(MLX_MTYP_EXTENDED)) != ESP_OK) return e;
        s_priv.mtyp = MLX_MTYP_EXTENDED;
    }
    /* extended dataset = 3 measurements; wait for pos==19 + data ready   */
    uint16_t st = 0;
    int64_t deadline = esp_timer_get_time() + MLX_MEAS_WAIT_MS * 3 * 1000;
    do {
        if ((e = rd16(MLX_REG_STATUS, &st)) != ESP_OK) return e;
        if ((st & MLX_STAT_NEW_DATA) &&
            ((st & MLX_STAT_CYC_POS) >> 2) == 19) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (esp_timer_get_time() < deadline);
    *t_us = esp_timer_get_time();
    if (!((st & MLX_STAT_NEW_DATA) &&
          ((st & MLX_STAT_CYC_POS) >> 2) == 19)) return ESP_ERR_TIMEOUT;

    int16_t an, ao, s1, s2, s3, s4, s5, s6;
    if ((e = read_ch(&an, MLX_RAM_EXT_AMB_1)) != ESP_OK) return e;
    if ((e = read_ch(&ao, MLX_RAM_EXT_AMB_2)) != ESP_OK) return e;
    if ((e = read_ch(&s1, MLX_RAM_EXT_OBJ_1)) != ESP_OK) return e;
    if ((e = read_ch(&s2, MLX_RAM_EXT_OBJ_2)) != ESP_OK) return e;
    if ((e = read_ch(&s3, MLX_RAM_EXT_OBJ_3)) != ESP_OK) return e;
    if ((e = read_ch(&s4, MLX_RAM_EXT_OBJ_4)) != ESP_OK) return e;
    if ((e = read_ch(&s5, MLX_RAM_EXT_OBJ_5)) != ESP_OK) return e;
    if ((e = read_ch(&s6, MLX_RAM_EXT_OBJ_6)) != ESP_OK) return e;
    /* S = (RAM_52−RAM_53−RAM_55+RAM_56)/2 + RAM_58 + RAM_59 (datasheet)   */
    int32_t S = ((int32_t)s1 - s2 - s3 + s4) / 2 + s5 + s6;
    if (S > 32767 || S < -32768)                 /* reference: -ERANGE     */
        return ESP_ERR_INVALID_RESPONSE;

    int64_t amb = preproc_amb(an, ao, s_priv.Gb);
    int64_t obj = preproc_obj_ext((int16_t)S, an, ao, s_priv.Ka);
    *tobj_mc = calc_object_ext(obj, amb, s_priv.reflected_mc,
                               s_priv.Ea, s_priv.Eb, s_priv.Fa, s_priv.Fb,
                               s_priv.Ga, s_priv.Ha, s_priv.Hb, s_priv.emiss);
    return ESP_OK;
}

static esp_err_t mlx_hw_config(void)
{
    esp_err_t e;
    /* EEPROM copy completes at POR — wait EE_BUSY clear before reads.    */
    uint16_t st = 0;
    int64_t deadline = esp_timer_get_time() + 500 * 1000;
    do {
        if ((e = rd16(MLX_REG_STATUS, &st)) != ESP_OK) return e;
        if (!(st & MLX_STAT_EE_BUSY)) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    } while (esp_timer_get_time() < deadline);
    if (st & MLX_STAT_EE_BUSY) return ESP_ERR_TIMEOUT;

    uint16_t ver, prod;
    if ((e = rd16(MLX_EE_VERSION, &ver)) != ESP_OK) return e;
    if ((e = rd16(MLX_EE_PRODUCT_CODE, &prod)) != ESP_OK) return e;
    uint16_t id = ver & MLX_ID_MASK;
    s_priv.mtyp = MLX_MTYP_MEDICAL;
    if (id == MLX_ID_MEDICAL || id == MLX_ID_CONSUMER) {
        /* medical/consumer DSP5 — medical measurement table             */
    } else if (id == MLX_ID_EXTENDED) {
        s_priv.mtyp = MLX_MTYP_EXTENDED;
    } else if ((ver & MLX_DSP_MASK) != MLX_DSP_VERSION) {
        ESP_LOGE(TAG, "EE_VERSION 0x%04x: not DSP5", ver);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "id=0x%04x prod=0x%04x mtyp=%u", id, prod, s_priv.mtyp);

    /* calib constants — PR/PG/PT/PO/Ea/Eb/Fa/Fb/Ga (s32) + Gb/Ka/Kb/Ha/Hb */
    if ((e = rd32(MLX_EE_P_R, &s_priv.PR)) != ESP_OK) return e;
    if ((e = rd32(MLX_EE_P_G, &s_priv.PG)) != ESP_OK) return e;
    if ((e = rd32(MLX_EE_P_T, &s_priv.PT)) != ESP_OK) return e;
    if ((e = rd32(MLX_EE_P_O, &s_priv.PO)) != ESP_OK) return e;
    if ((e = rd32(MLX_EE_Ea, &s_priv.Ea)) != ESP_OK) return e;
    if ((e = rd32(MLX_EE_Eb, &s_priv.Eb)) != ESP_OK) return e;
    if ((e = rd32(MLX_EE_Fa, &s_priv.Fa)) != ESP_OK) return e;
    if ((e = rd32(MLX_EE_Fb, &s_priv.Fb)) != ESP_OK) return e;
    if ((e = rd32(MLX_EE_Ga, &s_priv.Ga)) != ESP_OK) return e;
    if ((e = rd_s16(MLX_EE_Gb, &s_priv.Gb)) != ESP_OK) return e;
    if ((e = rd_s16(MLX_EE_Ka, &s_priv.Ka)) != ESP_OK) return e;
    if ((e = rd_s16(MLX_EE_Kb, &s_priv.Kb)) != ESP_OK) return e;
    if ((e = rd_s16(MLX_EE_Ha, &s_priv.Ha)) != ESP_OK) return e;
    if ((e = rd_s16(MLX_EE_Hb, &s_priv.Hb)) != ESP_OK) return e;

    /* brown_out flag: set to 1 so a later 0 evidences a device reset     */
    if ((e = status_rmw(MLX_STAT_BRST, 0)) != ESP_OK) return e;

    /* continuous mode + meas_select (extended needs the reset routine)   */
    if (s_priv.mtyp == MLX_MTYP_EXTENDED)
        e = set_meas_type(MLX_MTYP_EXTENDED);
    else {
        uint16_t ctl;
        if ((e = rd16(MLX_REG_CONTROL, &ctl)) != ESP_OK) return e;
        ctl &= ~0x06;
        e = wr16(MLX_REG_CONTROL, ctl | MLX_MODE_CONT);
    }
    if (e != ESP_OK) return e;
    s_priv.configured = true;
    return ESP_OK;
}

static esp_err_t mlx_ensure_ready(void)
{
    if (s_priv.configured) return ESP_OK;
    return mlx_hw_config();
}

void mlx90632_early_init(void)
{
    if (!s_priv.mtx)
        s_priv.mtx = xSemaphoreCreateMutexStatic(&s_priv.mtx_buf);
}

/* Serialise status/RAM sequences between the streaming task and callers
 * (NEW_DATA clear + channel readout is a multi-transaction sequence).    */
static SemaphoreHandle_t mlx_mtx(void)
{
    if (!s_priv.mtx)
        s_priv.mtx = xSemaphoreCreateMutexStatic(&s_priv.mtx_buf);
    return s_priv.mtx;
}

/* One measurement → °C. Used by the task, selftest and feature code.     */
esp_err_t mlx90632_measure(float *tobj_c, float *tamb_c)
{
    if (!tobj_c || !tamb_c) return ESP_ERR_INVALID_ARG;
    SemaphoreHandle_t m = mlx_mtx();
    if (!m || xSemaphoreTake(m, pdMS_TO_TICKS(MLX_MEAS_WAIT_MS + 500)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t e = mlx_ensure_ready();
    int32_t tobj = 0, tamb = 0;
    int64_t t_us;
    if (e == ESP_OK) e = measure_medical(&tobj, &tamb, &t_us);
    xSemaphoreGive(m);
    if (e != ESP_OK) return e;
    *tobj_c = tobj / 1000.0f;
    *tamb_c = tamb / 1000.0f;
    return ESP_OK;
}

/* ---------------- vq_sensor ops ---------------------------------------- */
static esp_err_t mlx_probe(vq_sensor_t *s)
{
    (void)s;
    if (vq_i2c_probe(VQ_ADDR_MLX90632) != ESP_OK) return ESP_ERR_NOT_FOUND;
    /* EE copy may still run right after POR — bounded wait, still cheap. */
    uint16_t st = 0, ver = 0;
    for (int i = 0; i < 20; i++) {
        if (rd16(MLX_REG_STATUS, &st) != ESP_OK) return ESP_ERR_NOT_FOUND;
        if (!(st & MLX_STAT_EE_BUSY)) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (st & MLX_STAT_EE_BUSY) return ESP_ERR_NOT_FOUND;
    if (rd16(MLX_EE_VERSION, &ver) != ESP_OK) return ESP_ERR_NOT_FOUND;
    if ((ver & MLX_DSP_MASK) != MLX_DSP_VERSION) return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

static esp_err_t mlx_init(vq_sensor_t *s)
{
    /* No memset: the pipeline retries init() while the task may still
     * exist — keep its handle, reset only what init owns.                */
    s->priv = &s_priv;
    s_priv.streaming    = false;
    s_priv.configured   = false;
    s_priv.consec_err   = 0;
    s_priv.emiss        = MLX_EMISSIVITY;
    s_priv.reflected_mc = MLX_OBJ_AMB_MC;
    s_priv.mtyp         = MLX_MTYP_MEDICAL;
    esp_err_t e = mlx_hw_config();
    if (e != ESP_OK) return e;
    s_priv.present = true;
    return ESP_OK;
}

static void mlx_task(void *arg)
{
    vq_sensor_t *s = arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (!s_priv.streaming) {
            vTaskDelay(pdMS_TO_TICKS(50));
            last = xTaskGetTickCount();
            continue;
        }
        esp_err_t e = ESP_ERR_TIMEOUT;
        SemaphoreHandle_t m = mlx_mtx();
        int32_t tobj = 0, tamb = 0;
        int64_t t_us = esp_timer_get_time();
        if (m && xSemaphoreTake(m, pdMS_TO_TICKS(MLX_MEAS_WAIT_MS + 500)) == pdTRUE) {
            e = mlx_ensure_ready();
            if (e == ESP_OK) e = measure_medical(&tobj, &tamb, &t_us);
            xSemaphoreGive(m);
        }
        if (e == ESP_OK) {
            s_priv.last_ok_us = t_us;
            s_priv.consec_err = 0;
            if (s->sink) {
                vq_sample_t smp[2] = {
                    { VQ_CH_TEMP_OBJ, t_us, tobj / 1000.0f, 0 },
                    { VQ_CH_TEMP_AMB, t_us, tamb / 1000.0f, 0 },
                };
                s->sink(s->sink_ctx, smp, 2);
            }
        } else {
            s_priv.consec_err++;
            s->err_count++;
            ESP_LOGW(TAG, "measure: %s", esp_err_to_name(e));
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(MLX_PERIOD_MS));
    }
}

static esp_err_t mlx_start(vq_sensor_t *s)
{
    if (!s_priv.present) return ESP_ERR_INVALID_STATE;
    esp_err_t e;
    uint16_t ctl;
    if ((e = rd16(MLX_REG_CONTROL, &ctl)) != ESP_OK) return e;
    if ((e = wr16(MLX_REG_CONTROL, (ctl & ~0x06) | MLX_MODE_CONT)) != ESP_OK)
        return e;
    if (!s_priv.task) {
        if (xTaskCreate(mlx_task, "mlx90632", 3072, s, 5, &s_priv.task) != pdPASS)
            return ESP_ERR_NO_MEM;
    }
    s_priv.streaming = true;
    return ESP_OK;
}

static esp_err_t mlx_set_mode(uint16_t mode_bits)
{
    uint16_t ctl;
    esp_err_t e = rd16(MLX_REG_CONTROL, &ctl);
    if (e != ESP_OK) return e;
    return wr16(MLX_REG_CONTROL, (ctl & ~0x06) | mode_bits);
}

static esp_err_t mlx_stop(vq_sensor_t *s)
{
    (void)s;
    s_priv.streaming = false;
    return mlx_set_mode(MLX_MODE_HALT);    /* no measurements, quick wake */
}

static esp_err_t mlx_sleep(vq_sensor_t *s)
{
    (void)s;
    s_priv.streaming = false;
    return mlx_set_mode(MLX_MODE_SLEEP_STEP); /* device sleeps between cmds */
}

static bool mlx_healthy(vq_sensor_t *s)
{
    (void)s;
    return s_priv.present && s_priv.consec_err < 4;
}

static const vq_channel_id_t s_channels[] = {
    VQ_CH_TEMP_OBJ, VQ_CH_TEMP_AMB,
};

static const vq_sensor_ops_t s_ops = {
    .name       = "mlx90632",
    .probe      = mlx_probe,
    .init       = mlx_init,
    .start      = mlx_start,
    .stop       = mlx_stop,
    .sleep      = mlx_sleep,
    .healthy    = mlx_healthy,
    .channels   = s_channels,
    .n_channels = 2,
    .rate_hz    = 1.0f,
    .required   = true,
};

vq_sensor_t *vq_island_mlx90632_sensor(void)
{
    s_sensor.ops = &s_ops;
    return &s_sensor;
}
