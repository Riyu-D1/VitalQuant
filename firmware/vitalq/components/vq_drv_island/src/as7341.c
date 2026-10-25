/* as7341.c — AMS-OSRAM AS7341 11-channel spectral sensor (I2C 0x39, 1.8 V
 * island). Register/SMUX facts: DS000504 v3-00 + app note AN000666 v1-01.
 *
 * Channel truth table (this driver's contract):
 *   bank A SMUX → ADC0..5 = F1,F2,F3,F4,CLEAR,NIR
 *   bank B SMUX → ADC0..5 = F5,F6,F7,F8,CLEAR,FLICKER
 * The two banks are SEQUENTIAL integrations — emitted samples keep
 * per-bank acquisition stamps (parallel_groups=2 honesty, docs/06 §1).
 * CLEAR appears in both banks as the cross-bank reference.
 *
 * Sequence per 2 Hz tick: dark frame (LED off) → led_sched-granted LED-on
 * frame → dark-subtracted counts → sink. The LDR-driven LED is the only
 * illumination this driver controls (~92 mA code; NIR730 lives on the
 * AD5940 driver's GPIO2 — different component, different led chain).
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "vq_drv_island.h"
#include "vq_island_priv.h"
#include "i2c_bus.h"
#include "led_sched.h"
#include "board_pins.h"

static const char *TAG = "as7341";

/* ---------------- register map (DS000504 §10) ------------------------- */
#define AS7341_REG_ENABLE     0x80   /* PON b0, SP_EN b1, WEN b3, SMUXEN b4,
                                        FDEN b6                              */
#define AS7341_REG_ATIME      0x81
#define AS7341_REG_WTIME      0x83
#define AS7341_REG_AUXID      0x90
#define AS7341_REG_REVID      0x91
#define AS7341_REG_ID         0x92
#define AS7341_REG_STATUS     0x93
#define AS7341_REG_CH0_DATA_L 0x95   /* CH0..CH5 little-endian u16, 0x95-0xA0 */
#define AS7341_REG_STATUS2    0xA3   /* AVALID b6, ASAT_DIG b4, ASAT_ANA b3   */
#define AS7341_REG_CFG0       0xA9   /* LOW_POWER b5, REG_BANK b4, WLONG b2   */
#define AS7341_REG_CFG1       0xAA   /* AGAIN[4:0]                            */
#define AS7341_REG_CFG6       0xAF   /* SMUX_CMD[4:3]                         */
#define AS7341_REG_ASTEP_L    0xCA
#define AS7341_REG_ASTEP_H    0xCB
#define AS7341_REG_AZ_CONFIG  0xD6   /* auto-zero every Nth integration      */
/* bank-1 (0x60-0x74, needs CFG0.REG_BANK=1) */
#define AS7341_REG_CONFIG     0x70   /* LED_SEL b3                            */
#define AS7341_REG_LED        0x74   /* LED_ACT b7, LED_DRIVE[6:0]            */
#define AS7341_SMUX_RAM       0x00   /* SMUX pixel-config RAM, 20 bytes       */

#define AS7341_EN_PON     0x01
#define AS7341_EN_SP_EN   0x02
#define AS7341_EN_WEN     0x08
#define AS7341_EN_SMUXEN  0x10
#define AS7341_EN_FDEN    0x40

#define AS7341_CFG0_LOW_POWER  0x20
#define AS7341_CFG0_REG_BANK   0x10

#define AS7341_CFG6_SMUX_WRITE 0x10  /* SMUX_CMD=2: RAM → SMUX chain         */

#define AS7341_ST2_AVALID   0x40
#define AS7341_ST2_ASAT_DIG 0x10
#define AS7341_ST2_ASAT_ANA 0x08

#define AS7341_CONFIG_LED_SEL 0x08
#define AS7341_LED_ACT        0x80

#define AS7341_ID_VALUE   0x24   /* ID[7:2]=001001 → (reg & 0xFC) == 0x24  */
#define AS7341_ID_MASK    0xFC

/* hw profile (hw_v2.yaml spectral:): AGAIN=64x → AGAIN[4:0]=7;
 * tint=(ATIME+1)(ASTEP+1)·2.78 µs → 27.8 ms: ATIME=0, ASTEP=9999.        */
#define AS7341_ATIME_27MS8   0
#define AS7341_ASTEP_27MS8   9999u
#define AS7341_AGAIN_64X     7
/* Datasheet optical data is taken with auto-zero every integration
 * (AZ_NTH=1 ≈ +15 ms per cycle — accounted in the LED window size).      */
#define AS7341_AZ_EVERY_CYCLE 1

/* ~91 mA class on-chip LED: I[mA] = 4 + 2·LED_DRIVE, code 0..127.
 * Nearest step to the ~91 mA design point is 44 → 92 mA (reported to the
 * LED scheduler as pulse_mA so the power budget stays honest).           */
#define AS7341_LED_CODE      44u
#define AS7341_LED_MA        92u

#define AS7341_INTEG_MS      28u     /* 27.8 ms nominal                      */
#define AS7341_AZ_MS         15u     /* auto-zero per cycle when AZ_NTH=1    */
#define AS7341_BANK_MS       (AS7341_INTEG_MS + AS7341_AZ_MS + 20u)
/* LED window must cover: i2c-led-on + settle + 2 banks + i2c-led-off.    */
#define AS7341_LED_ON_US     ((2 * AS7341_BANK_MS + 40) * 1000)
#define AS7341_SLOT_WAIT_MS  400     /* max wait for a led_sched slot      */
#define AS7341_AVALID_WAIT_MS 200
#define AS7341_PERIOD_MS     500     /* 2 Hz                               */

/* Ambient-leakage gate: CLEAR-with-LED-off far above dark floor ⇒ frame
 * dominated by ambient, flag QUALITY_BAD (docs/06 spectral chain).       */
#define AS7341_DARK_CLEAR_MAX 4000u
#define AS7341_ADC_SAT       0xFFF0u /* near-fullscale ⇒ treat as saturated */

/* SMUX RAM maps (AN000666 §2/§4): 20 bytes, two 3-bit fields per byte —
 * field value = ADC# + 1, 0 = pixel grounded/disconnected.
 * Bank A (AN example 1): F1-F4 + CLEAR + NIR.
 * Bank B: AN example 2 (F5-F8+CLEAR+NIR) with 0x13 → 0x60, routing the
 * FLICKER diode to ADC5 instead of a second NIR (datasheet §8.4: "the
 * flicker diode needs to be configured to ADC5").                        */
static const uint8_t s_smux_bank_a[20] = {
    0x30, /*00 F3L→ADC2   */ 0x01, /*01 F1L→ADC0   */
    0x00, /*02 reserved   */ 0x00, /*03 F8L off     */
    0x00, /*04 F6L off    */ 0x42, /*05 F4L→ADC3/F2L→ADC1 */
    0x00, /*06 F5L off    */ 0x00, /*07 F7L off     */
    0x50, /*08 CLEAR→ADC4 */ 0x00, /*09 F5R off     */
    0x00, /*0A F7R off    */ 0x00, /*0B reserved    */
    0x20, /*0C F2R→ADC1   */ 0x04, /*0D F4R→ADC3    */
    0x00, /*0E F6R/F8R off*/ 0x30, /*0F F3R→ADC2    */
    0x01, /*10 F1R→ADC0   */ 0x50, /*11 CLEARR→ADC4 */
    0x00, /*12 reserved   */ 0x06, /*13 NIR→ADC5    */
};
static const uint8_t s_smux_bank_b[20] = {
    0x00, /*00 F3L off    */ 0x00, /*01 F1L off     */
    0x00, /*02 reserved   */ 0x40, /*03 F8L→ADC3    */
    0x02, /*04 F6L→ADC1   */ 0x00, /*05 F4L/F2L off  */
    0x10, /*06 F5L→ADC0   */ 0x03, /*07 F7L→ADC2    */
    0x50, /*08 CLEAR→ADC4 */ 0x10, /*09 F5R→ADC0    */
    0x03, /*0A F7R→ADC2   */ 0x00, /*0B reserved    */
    0x00, /*0C F2R off    */ 0x00, /*0D F4R off     */
    0x24, /*0E F8R→ADC3/F6R→ADC1 (pixel28→ADC3, pixel29→ADC1) */
    0x00, /*0F F3R off    */ 0x00, /*10 F1R off     */
    0x50, /*11 CLEARR→ADC4*/ 0x00, /*12 reserved    */
    0x60, /*13 FLICKER→ADC5 (datasheet §8.4; replaces NIR)     */
};

typedef struct {
    TaskHandle_t      task;
    SemaphoreHandle_t mtx;
    StaticSemaphore_t mtx_buf;
    volatile bool     streaming;      /* start/stop switch for the task   */
    volatile bool     led_slot_on;    /* set in led_sched cb_on           */
    volatile bool     led_slot_off;   /* set in led_sched cb_off          */
    bool              configured;
    bool              present;
    uint16_t          dark[12], lit[12];   /* last raw frames, ADC order  */
    int64_t           dark_t[2], lit_t[2]; /* per-bank avalid stamps      */
    int64_t           last_ok_us;
    uint32_t          consec_err;
} as7341_priv_t;

static as7341_priv_t s_priv;
static vq_sensor_t  s_sensor;

/* ---------------- bus helpers ----------------------------------------- */
static esp_err_t wr8(uint8_t reg, uint8_t val)
{
    return vq_i2c_write_reg(VQ_ADDR_AS7341, reg, &val, 1);
}
static esp_err_t rd8(uint8_t reg, uint8_t *val)
{
    return vq_i2c_read_reg(VQ_ADDR_AS7341, reg, val, 1);
}
static esp_err_t rdn(uint8_t reg, uint8_t *buf, size_t n)
{
    return vq_i2c_read_reg(VQ_ADDR_AS7341, reg, buf, n);
}
static esp_err_t wrn(uint8_t reg, const uint8_t *buf, size_t n)
{
    return vq_i2c_write_reg(VQ_ADDR_AS7341, reg, buf, n);
}

esp_err_t as7341_read_ids(uint8_t *id, uint8_t *auxid, uint8_t *revid)
{
    esp_err_t e;
    if ((e = rd8(AS7341_REG_ID, id)) != ESP_OK)    return e;
    if ((e = rd8(AS7341_REG_AUXID, auxid)) != ESP_OK) return e;
    return rd8(AS7341_REG_REVID, revid);
}

/* ---------------- device config --------------------------------------- */
static esp_err_t as7341_hw_config(void)
{
    esp_err_t e;
    /* PON first (datasheet §10.2.1: set PON=1, then configure, then SP_EN) */
    if ((e = wr8(AS7341_REG_ENABLE, AS7341_EN_PON)) != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(2));              /* internal LDO/osc settle  */

    if ((e = wr8(AS7341_REG_ATIME, AS7341_ATIME_27MS8)) != ESP_OK) return e;
    if ((e = wr8(AS7341_REG_ASTEP_L, AS7341_ASTEP_27MS8 & 0xFF)) != ESP_OK) return e;
    if ((e = wr8(AS7341_REG_ASTEP_H, AS7341_ASTEP_27MS8 >> 8)) != ESP_OK) return e;
    if ((e = wr8(AS7341_REG_CFG1, AS7341_AGAIN_64X)) != ESP_OK) return e;
    /* low-power idle between measurements; REG_BANK stays 0 for 0x80+    */
    if ((e = wr8(AS7341_REG_CFG0, AS7341_CFG0_LOW_POWER)) != ESP_OK) return e;
    if ((e = wr8(AS7341_REG_AZ_CONFIG, AS7341_AZ_EVERY_CYCLE)) != ESP_OK) return e;

    /* Bank-1 regs: LED_SEL=1 lets register 0x74 drive the LDR pin; LED off */
    if ((e = wr8(AS7341_REG_CFG0, AS7341_CFG0_LOW_POWER | AS7341_CFG0_REG_BANK)) != ESP_OK) return e;
    if ((e = wr8(AS7341_REG_CONFIG, AS7341_CONFIG_LED_SEL)) != ESP_OK) return e;
    if ((e = wr8(AS7341_REG_LED, AS7341_LED_CODE)) != ESP_OK) return e;
    if ((e = wr8(AS7341_REG_CFG0, AS7341_CFG0_LOW_POWER)) != ESP_OK) return e;

    s_priv.configured = true;
    return ESP_OK;
}

static esp_err_t as7341_ensure_ready(void)
{
    if (s_priv.configured) return ESP_OK;
    uint8_t id;
    if (rd8(AS7341_REG_ID, &id) != ESP_OK || (id & AS7341_ID_MASK) != AS7341_ID_VALUE)
        return ESP_ERR_NOT_FOUND;
    return as7341_hw_config();
}

static esp_err_t as7341_led(bool on)
{
    /* LED reg is bank-1 only — always restore bank-0 afterwards. */
    esp_err_t e;
    if ((e = wr8(AS7341_REG_CFG0, AS7341_CFG0_LOW_POWER | AS7341_CFG0_REG_BANK)) != ESP_OK)
        return e;
    e = wr8(AS7341_REG_LED, (on ? AS7341_LED_ACT : 0) | AS7341_LED_CODE);
    esp_err_t e2 = wr8(AS7341_REG_CFG0, AS7341_CFG0_LOW_POWER);
    return (e != ESP_OK) ? e : e2;
}

/* SMUX: program one bank's RAM map, start SMUX cmd, wait self-clear.    */
static esp_err_t as7341_smux(int bank)
{
    esp_err_t e;
    if ((e = wr8(AS7341_REG_ENABLE, AS7341_EN_PON)) != ESP_OK) return e;
    if ((e = wr8(AS7341_REG_CFG6, AS7341_CFG6_SMUX_WRITE)) != ESP_OK) return e;
    /* AN000666: write all 20 RAM bytes in ONE page write.               */
    if ((e = wrn(AS7341_SMUX_RAM, bank == 0 ? s_smux_bank_a : s_smux_bank_b, 20)) != ESP_OK)
        return e;
    if ((e = wr8(AS7341_REG_ENABLE, AS7341_EN_PON | AS7341_EN_SMUXEN)) != ESP_OK)
        return e;
    for (int i = 0; i < 50; i++) {                 /* SMUXEN self-clears   */
        uint8_t en;
        if ((e = rd8(AS7341_REG_ENABLE, &en)) != ESP_OK) return e;
        if (!(en & AS7341_EN_SMUXEN)) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

/* One 6-ADC integration: SP_EN → poll AVALID → read 12 B. *t_us is the
 * data-ready observation stamp (acquisition end per the FIFO contract —
 * INT pin is NC, polling lag ≲3 ms is inherent, documented).            */
static esp_err_t as7341_integrate(uint16_t adc[6], int64_t *t_us, uint8_t *st2)
{
    esp_err_t e;
    if ((e = wr8(AS7341_REG_ENABLE, AS7341_EN_PON | AS7341_EN_SP_EN)) != ESP_OK)
        return e;

    uint8_t s2 = 0;
    int64_t deadline = esp_timer_get_time() + AS7341_AVALID_WAIT_MS * 1000;
    do {
        if ((e = rd8(AS7341_REG_STATUS2, &s2)) != ESP_OK) return e;
        if (s2 & AS7341_ST2_AVALID) break;
        vTaskDelay(pdMS_TO_TICKS(2));
    } while (esp_timer_get_time() < deadline);
    *t_us = esp_timer_get_time();
    if (!(s2 & AS7341_ST2_AVALID)) return ESP_ERR_TIMEOUT;
    if (st2) *st2 = s2;

    uint8_t raw[12];
    if ((e = rdn(AS7341_REG_CH0_DATA_L, raw, sizeof raw)) != ESP_OK) return e;
    for (int i = 0; i < 6; i++)
        adc[i] = (uint16_t)raw[2 * i] | ((uint16_t)raw[2 * i + 1] << 8);

    /* SP_EN=0 ends the cycle; PON stays for the next bank.               */
    return wr8(AS7341_REG_ENABLE, AS7341_EN_PON);
}

static esp_err_t as7341_frame(uint16_t out[12], int64_t t_us[2], uint8_t *sat)
{
    esp_err_t e;
    uint8_t st2a = 0, st2b = 0;
    if ((e = as7341_smux(0)) != ESP_OK)                    return e;
    if ((e = as7341_integrate(&out[0], &t_us[0], &st2a)) != ESP_OK) return e;
    if ((e = as7341_smux(1)) != ESP_OK)                    return e;
    if ((e = as7341_integrate(&out[6], &t_us[1], &st2b)) != ESP_OK) return e;
    if (sat) *sat = (st2a | st2b) & (AS7341_ST2_ASAT_DIG | AS7341_ST2_ASAT_ANA);
    return ESP_OK;
}

/* ---------------- LED scheduler gate ----------------------------------- */
static void led_cb_on(void *ctx)
{
    (void)ctx;
    s_priv.led_slot_on = true;
}
static void led_cb_off(void *ctx)
{
    (void)ctx;
    s_priv.led_slot_off = true;
}

/* Reserve a one-shot LED window and switch the on-chip LED inside it.
 * Cbs only set flags — the actual I2C switching happens here, in task
 * context, keeping the led_sched callback path short. On timeout or a
 * truncated window the LED is forced off before returning an error.     */
static esp_err_t as7341_led_window(void)
{
    vq_led_request_t req = {
        .chain     = VQ_LED_AS7341,
        .pulse_mA  = AS7341_LED_MA,
        .on_us     = AS7341_LED_ON_US,
        .period_us = 0,
        .priority  = 0,
    };
    s_priv.led_slot_on = s_priv.led_slot_off = false;
    int64_t slot_us;
    esp_err_t e = vq_led_submit(&req, led_cb_on, led_cb_off, NULL, &slot_us);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "led_sched submit: %s", esp_err_to_name(e));
        return e;
    }

    int64_t deadline = esp_timer_get_time() + AS7341_SLOT_WAIT_MS * 1000;
    while (!s_priv.led_slot_on) {
        if (esp_timer_get_time() > deadline) {
            vq_led_cancel(VQ_LED_AS7341);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if ((e = as7341_led(true)) != ESP_OK) {          /* LED on            */
        vq_led_cancel(VQ_LED_AS7341);
        return e;
    }
    vTaskDelay(pdMS_TO_TICKS(2));                    /* emitter settle    */

    e = as7341_frame(s_priv.lit, s_priv.lit_t, NULL);

    esp_err_t e2 = as7341_led(false);                /* LED off, always   */
    vq_led_cancel(VQ_LED_AS7341);                    /* release the slot  */
    if (s_priv.led_slot_off)
        ESP_LOGD(TAG, "slot closed mid-capture (scheduler window end)");
    return (e != ESP_OK) ? e : e2;
}

/* Full dark+illuminated capture into s_priv.dark/lit. Caller holds mtx. */
static esp_err_t as7341_capture_locked(void)
{
    esp_err_t e;
    if ((e = as7341_led(false)) != ESP_OK)           /* guarantee dark    */
        return e;
    if ((e = as7341_frame(s_priv.dark, s_priv.dark_t, NULL)) != ESP_OK)
        return e;
    return as7341_led_window();
}

void as7341_early_init(void)
{
    if (!s_priv.mtx)
        s_priv.mtx = xSemaphoreCreateMutexStatic(&s_priv.mtx_buf);
}

esp_err_t as7341_capture_frames(uint16_t dark[12], uint16_t lit[12],
                                int64_t t_us[4])
{
    if (!s_priv.mtx) {
        s_priv.mtx = xSemaphoreCreateMutexStatic(&s_priv.mtx_buf);
        if (!s_priv.mtx) return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_priv.mtx, pdMS_TO_TICKS(2000)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t e = as7341_ensure_ready();
    if (e == ESP_OK) e = as7341_capture_locked();
    if (e == ESP_OK) {
        memcpy(dark, s_priv.dark, sizeof s_priv.dark);
        memcpy(lit,  s_priv.lit,  sizeof s_priv.lit);
        t_us[0] = s_priv.dark_t[0]; t_us[1] = s_priv.dark_t[1];
        t_us[2] = s_priv.lit_t[0];  t_us[3] = s_priv.lit_t[1];
    }
    xSemaphoreGive(s_priv.mtx);
    return e;
}

/* Contract note: st_priv.h declares this weak symbol as
 * vq_as7341_capture(uint16_t counts[11], bool illum_on) and st_optical()
 * calls it as (dark,false) then (lit,true) INSIDE an active IR_GATE /
 * NIR730 scheduler slot — i.e. illum_on selects the frame type, never
 * drives OUR LDR LED:
 *   false → force our LED off, integrate → RAW dark-frame counts
 *   true  → integrate NOW, no LED toggling, no scheduler request — the
 *           caller's scheduled chain (IR_GATE / NIR730) is illuminating
 * Both return RAW counts; the caller computes dark↔lit deltas itself.
 * For the driver's own dark→LDR-illuminated pair see
 * as7341_capture_frames() (internal, used by vq_island_selftest).       */
esp_err_t vq_as7341_capture(uint16_t *ch11, bool illum_on)
{
    if (!ch11) return ESP_ERR_INVALID_ARG;
    if (!s_priv.mtx) {                      /* same lazy path as above  */
        s_priv.mtx = xSemaphoreCreateMutexStatic(&s_priv.mtx_buf);
        if (!s_priv.mtx) return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_priv.mtx, pdMS_TO_TICKS(2000)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t e = as7341_ensure_ready();
    uint16_t raw[12] = {0};
    int64_t  t[2] = {0};
    if (e == ESP_OK) e = as7341_led(false);      /* only the caller's
                                                  * chain may be lit   */
    if (e == ESP_OK) e = as7341_frame(raw, t, NULL);
    xSemaphoreGive(s_priv.mtx);
    if (e != ESP_OK) return e;
    /* ADC order → channel order (RAW — no dark subtraction here)        */
    static const uint8_t map[11] = {0,1,2,3, 6,7,8,9, 4,5, 11};
    for (int i = 0; i < 11; i++)
        ch11[i] = raw[map[i]];
    (void)illum_on;    /* frame type already handled by the caller's
                        * illumination window — see contract note        */
    return ESP_OK;
}

/* ---------------- vq_sensor ops ---------------------------------------- */
static esp_err_t as7341_probe(vq_sensor_t *s)
{
    (void)s;
    esp_err_t e = vq_i2c_probe(VQ_ADDR_AS7341);
    if (e != ESP_OK) return ESP_ERR_NOT_FOUND;
    uint8_t id;
    if (rd8(AS7341_REG_ID, &id) != ESP_OK || (id & AS7341_ID_MASK) != AS7341_ID_VALUE)
        return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

static esp_err_t as7341_init(vq_sensor_t *s)
{
    /* No memset: the pipeline retries init() while the task (and its mtx)
     * may still exist — keep those handles, reset only what init owns.   */
    s->priv = &s_priv;
    if (!s_priv.mtx) {
        s_priv.mtx = xSemaphoreCreateMutexStatic(&s_priv.mtx_buf);
        if (!s_priv.mtx) return ESP_ERR_NO_MEM;
    }
    s_priv.streaming  = false;
    s_priv.configured = false;
    s_priv.consec_err = 0;
    esp_err_t e = as7341_hw_config();
    if (e != ESP_OK) return e;
    s_priv.present = true;
    return ESP_OK;
}

static void as7341_emit(vq_sensor_t *s, bool degraded)
{
    if (!s->sink) return;
    vq_sample_t smp[11];
    static const vq_channel_id_t ch[11] = {
        VQ_CH_SPEC_F1, VQ_CH_SPEC_F2, VQ_CH_SPEC_F3, VQ_CH_SPEC_F4,
        VQ_CH_SPEC_F5, VQ_CH_SPEC_F6, VQ_CH_SPEC_F7, VQ_CH_SPEC_F8,
        VQ_CH_SPEC_CLEAR, VQ_CH_SPEC_NIR, VQ_CH_SPEC_FLICKER,
    };
    static const uint8_t src[11] = {0,1,2,3, 6,7,8,9, 4,5, 11};
    for (int i = 0; i < 11; i++) {
        int32_t v = (int32_t)s_priv.lit[src[i]] - (int32_t)s_priv.dark[src[i]];
        if (v < 0) v = 0;
        smp[i] = (vq_sample_t){
            .channel = ch[i],
            .t_us    = s_priv.lit_t[src[i] >= 6 ? 1 : 0],
            .value   = (float)v,
            .flags   = 0,
        };
    }
    /* quality flags: saturation in the lit frame, ambient leakage in the
     * dark frame, or a degraded (partial) capture.                        */
    uint8_t fl = degraded ? VQ_SAMPLE_F_QUALITY_BAD : 0;
    if (s_priv.dark[4] > AS7341_DARK_CLEAR_MAX ||
        s_priv.dark[10] > AS7341_DARK_CLEAR_MAX)
        fl |= VQ_SAMPLE_F_QUALITY_BAD;         /* ambient leak (CLEAR)    */
    for (int i = 0; i < 12; i++)
        if (s_priv.lit[i] >= AS7341_ADC_SAT) fl |= VQ_SAMPLE_F_QUALITY_BAD;
    for (int i = 0; i < 11; i++) smp[i].flags = fl;
    s->sink(s->sink_ctx, smp, 11);
}

static void as7341_task(void *arg)
{
    vq_sensor_t *s = arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (!s_priv.streaming) {
            vTaskDelay(pdMS_TO_TICKS(50));
            last = xTaskGetTickCount();
            continue;
        }
        if (xSemaphoreTake(s_priv.mtx, pdMS_TO_TICKS(300)) == pdTRUE) {
            esp_err_t e = as7341_ensure_ready();
            if (e == ESP_OK) e = as7341_capture_locked();
            if (e == ESP_OK) {
                s_priv.last_ok_us = esp_timer_get_time();
                s_priv.consec_err = 0;
            } else {
                s_priv.consec_err++;
                s->err_count++;
                ESP_LOGW(TAG, "capture: %s", esp_err_to_name(e));
            }
            xSemaphoreGive(s_priv.mtx);
            if (e == ESP_OK) as7341_emit(s, false);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(AS7341_PERIOD_MS));
    }
}

static esp_err_t as7341_start(vq_sensor_t *s)
{
    if (!s_priv.present) return ESP_ERR_INVALID_STATE;
    if (!s_priv.task) {
        if (xTaskCreate(as7341_task, "as7341", 3072, s, 5, &s_priv.task) != pdPASS)
            return ESP_ERR_NO_MEM;
    }
    s_priv.streaming = true;
    return ESP_OK;
}

static esp_err_t as7341_stop(vq_sensor_t *s)
{
    (void)s;
    s_priv.streaming = false;
    /* park hardware: LED off, SP_EN off, keep PON for quick resume       */
    if (s_priv.mtx && xSemaphoreTake(s_priv.mtx, pdMS_TO_TICKS(300)) == pdTRUE) {
        as7341_led(false);
        wr8(AS7341_REG_ENABLE, AS7341_EN_PON);
        xSemaphoreGive(s_priv.mtx);
    }
    return ESP_OK;
}

static esp_err_t as7341_sleep(vq_sensor_t *s)
{
    as7341_stop(s);
    if (s_priv.mtx && xSemaphoreTake(s_priv.mtx, pdMS_TO_TICKS(300)) == pdTRUE) {
        wr8(AS7341_REG_ENABLE, 0);               /* PON=0 → standby        */
        s_priv.configured = false;               /* re-config on wake      */
        xSemaphoreGive(s_priv.mtx);
    }
    return ESP_OK;
}

static bool as7341_healthy(vq_sensor_t *s)
{
    (void)s;
    return s_priv.present && s_priv.consec_err < 4;
}

static const vq_channel_id_t s_channels[] = {
    VQ_CH_SPEC_F1, VQ_CH_SPEC_F2, VQ_CH_SPEC_F3, VQ_CH_SPEC_F4,
    VQ_CH_SPEC_F5, VQ_CH_SPEC_F6, VQ_CH_SPEC_F7, VQ_CH_SPEC_F8,
    VQ_CH_SPEC_CLEAR, VQ_CH_SPEC_NIR, VQ_CH_SPEC_FLICKER,
};

static const vq_sensor_ops_t s_ops = {
    .name       = "as7341",
    .probe      = as7341_probe,
    .init       = as7341_init,
    .start      = as7341_start,
    .stop       = as7341_stop,
    .sleep      = as7341_sleep,
    .healthy    = as7341_healthy,
    .channels   = s_channels,
    .n_channels = 11,
    .rate_hz    = 2.0f,
    .required   = true,
};

vq_sensor_t *vq_island_as7341_sensor(void)
{
    s_sensor.ops = &s_ops;
    return &s_sensor;
}
