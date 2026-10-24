/* vq_power.c — power_mgr.h implementation: battery/charge/sleep governor.
 *
 * THE CELL IS ASSUMED UNPROTECTED — firmware (this module) IS the cutoff.
 *
 * --------------------------------------------------------------------------
 * State machine (5 s status task + gauge-alert edge wake):
 *
 *                         soc<15% | vcell<3.45 | tboard>40C
 *              NORMAL ───────────────────────────────────▶ CONSERVE
 *                ▲                                              │
 *                └──── soc>=18% & vcell>=3.50 & temp<=38 ───────┘
 *                          (hysteresis band)
 *
 *   any state ──vcell<=3.20V OR gauge ALRT──▶ LOW_CUTOFF ──▶ kill LEDs,
 *                                                            net-off flag,
 *                                                            save logs,
 *                                                            deep sleep
 *   any state ──vcell<=3.00V───────────────▶ EMERGENCY  ──▶ kill LEDs,
 *                                                            best-effort log,
 *                                                            immediate sleep
 *
 *   LOW_CUTOFF / EMERGENCY are terminal — they end in esp_deep_sleep_start().
 * --------------------------------------------------------------------------
 * Wake map (fix_v22, ESP32-S3):
 *   IO6 GAUGE_ALRT  open-drain LOW  → EXT1 ANY_LOW   (skipped when lowbatt
 *   IO3 EXP_INT     open-drain LOW  → EXT1 ANY_LOW    latch set, see below)
 *   IO4 LSM6_INT1   push-pull HIGH  → EXT0 HIGH
 *   S3 EXT1 cannot mix per-pin levels (ESP_ERR_NOT_ALLOWED when attempted —
 *   no SOC_PM_SUPPORT_EXT1_WAKEUP_MODE_PER_PIN), so the lone active-high
 *   source rides EXT0, which is an independent trigger.
 *
 * Low-battery RTC latch: MAX17048 ALRT re-asserts while the cell stays flat,
 * so an EXT1-LOW wake on IO6 would bounce us right back out of deep sleep in
 * a boot loop. RTC_DATA_ATTR flag set before battery-triggered sleeps → IO6
 * is dropped from the EXT1 mask; cleared on non-deep-sleep reset or once the
 * cell recovers above the CONSERVE-exit voltage.
 * --------------------------------------------------------------------------
 * USB budget (BQ25170, R_ISET → VQ_CHG_CURRENT_MA=200 mA):
 *   charge 200 + system ~120 + burst must stay under VQ_USB_BUDGET_MA=500.
 *   vq_power_led_ok()        — denied in LOW_CUTOFF/EMERGENCY, denied when
 *                              vcell<3.3 V (TX_5V boost floor), denied when
 *                              pulse would push a VBUS+charging total >500.
 *   vq_power_wifi_burst_ok() — denied while TX_5V window on (shared rail),
 *                              denied in LOW_CUTOFF+, denied when a ~400 mA
 *                              Wi-Fi burst would exceed budget while charging
 *                              → callers use the CHG_DIS strategy instead:
 *                              burst_begin() inhibits charge while vbus is
 *                              present (refcounted) so the burst fits.
 * --------------------------------------------------------------------------
 */

#include <math.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"

#include "power_mgr.h"
#include "board_pins.h"
#include "expander.h"
#include "led_sched.h"
#include "vq_sensor.h"
#include "pipeline.h"
#include "log_store.h"

/* MAX17048 gauge API — owned by vq_drv_aux (components/vq_drv_aux/include/
 * vq_drv_aux.h). If the header is not on the include path yet, use the
 * contract prototypes so this file still compiles; linking resolves against
 * the real component once it lands. */
#if defined(__has_include)
#  if __has_include("vq_drv_aux.h")
#    include "vq_drv_aux.h"
#    define VQ_PWR_HAVE_DRV_AUX_H 1
#  endif
#endif
#if !defined(VQ_PWR_HAVE_DRV_AUX_H)
typedef void (*vq_gauge_alert_cb_t)(void *ctx);
extern esp_err_t vq_gauge_read(float *vcell_v, float *soc_pct);
extern esp_err_t vq_gauge_config_alerts(void);
extern esp_err_t vq_gauge_on_alert(vq_gauge_alert_cb_t cb, void *ctx);
extern esp_err_t vq_gauge_clear_alert(void);
#endif

/* ------------------------------------------------------------------ */
/* Tunables                                                            */
/* ------------------------------------------------------------------ */

#define VQ_PWR_PERIOD_MS        5000        /* status-task cadence        */
#define VQ_PWR_TASK_STACK       4096
#define VQ_PWR_TASK_PRIO        10
#define VQ_PWR_CB_MAX           8

/* State thresholds — low/emergency come from board_pins.h rails; conserve
 * and the hysteresis band live here (module-private constants). */
#define VQ_CONSERVE_VCELL_MV    3450        /* ~3.45 V: reduce duty        */
#define VQ_CONSERVE_SOC_PCT     15.0f
#define VQ_RECOVER_VCELL_MV     3500        /* CONSERVE→NORMAL hysteresis  */
#define VQ_RECOVER_SOC_PCT      18.0f
#define VQ_TEMP_CONSERVE_C      40.0f       /* board hot → CONSERVE        */
#define VQ_TEMP_RECOVER_C       38.0f
#define VQ_TEMP_INHIBIT_C       45.0f       /* >45 C → charge inhibit      */
#define VQ_TEMP_RELEASE_C       43.0f       /* 2 C hysteresis on release   */

/* USB budget estimates (mA) — see header block + hw budget table.        */
#define VQ_BASE_SYS_MA          120         /* always-on ~120 mA estimate  */
#define VQ_WIFI_BURST_MA        400         /* Wi-Fi TX burst class        */

/* Board-temp probe list: pipeline channel ring for slow sensors keeps
 * ~300 s; a window that large always returns the newest sample.         */
#define VQ_TEMP_WINDOW_US       ((int64_t)300 * 1000 * 1000)

#define VQ_SLEEP_LATCH_MAGIC    0xB4A7u     /* RTC low-batt sleep marker   */
#define VQ_LOW_CUTOFF_SETTLE_MS 150         /* let cb/log side-effects land */

static const char *TAG = "vq_power";
static const char *const s_state_names[] = {
    "NORMAL", "CONSERVE", "LOW_CUTOFF", "EMERGENCY"
};

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

static portMUX_TYPE      s_lock = portMUX_INITIALIZER_UNLOCKED;
static vq_power_status_t s_st = {
    .vcell_v = NAN, .soc_pct = NAN, .charge_rate = 0.0f,
    .vbus = false, .charging = false, .charge_inhibited = false,
    .board_temp_c = NAN, .state = VQ_PWR_NORMAL,
};

static struct { vq_pwr_cb_t cb; void *ctx; } s_cbs[VQ_PWR_CB_MAX];
static size_t      s_cb_n;

static TaskHandle_t s_task;
static volatile bool s_alert_pending;     /* ISR/alert-cb → task flag    */
static volatile bool s_net_off_req;       /* LOW_CUTOFF: net must stop   */
static uint32_t  s_burst_refs;            /* heavy-burst refcount        */
static uint32_t  s_burst_est_mA;          /* last burst estimate (debug) */
static bool      s_thermal_inhibit;       /* >45 C charge-inhibit latch  */
static bool      s_inhibit_applied;       /* CHG_DIS currently driven    */
static bool      s_gauge_seen;            /* at least one good read      */
static uint32_t  s_gauge_errs;

static temperature_sensor_handle_t s_tsens;
static bool      s_tsens_on;
static uint32_t  s_tsens_retry;
static bool      s_init_done;

/* Survives deep sleep; 0 unless a battery-fault sleep armed it.          */
RTC_DATA_ATTR static uint32_t s_lowbatt_latch;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void lock_state(void)
{
    if (xPortInIsrContext()) {
        portENTER_CRITICAL_ISR(&s_lock);
    } else {
        portENTER_CRITICAL(&s_lock);
    }
}

static void unlock_state(void)
{
    if (xPortInIsrContext()) {
        portEXIT_CRITICAL_ISR(&s_lock);
    } else {
        portEXIT_CRITICAL(&s_lock);
    }
}

/* Wake the status task early (alert edge). Callable from task or ISR.   */
static void IRAM_ATTR poke_task(void)
{
    if (s_task == NULL) {
        return;
    }
    if (xPortInIsrContext()) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_task, &woken);
        if (woken) {
            portYIELD_FROM_ISR();
        }
    } else {
        xTaskNotifyGive(s_task);
    }
}

/* Event record appended to the ring before shutdown/sleep. Best-effort:
 * vq_log_append fails soft when vq_logstore is not up yet (main inits
 * power before logstore). */
typedef struct {
    uint32_t event_id;          /* VQ_EVT_*                              */
    int64_t  t_us;              /* esp_timer µs                          */
    int32_t  vcell_mv;          /* -1 when unknown                       */
    int32_t  soc_x100;
    int32_t  board_c_x100;
    uint32_t flags;             /* b0 vbus, b1 charging, b2 inhibit,
                                 * b3 gauge-alert, b4 net-off req        */
    uint32_t state;
} vq_pwr_log_rec_t;

static void log_pwr_event(vq_event_id_t evt)
{
    vq_power_status_t s;
    vq_power_status(&s);
    vq_pwr_log_rec_t rec = {
        .event_id    = (uint32_t)evt,
        .t_us        = esp_timer_get_time(),
        .vcell_mv    = isnan(s.vcell_v) ? -1 : (int32_t)(s.vcell_v * 1000.0f),
        .soc_x100    = isnan(s.soc_pct) ? -1 : (int32_t)(s.soc_pct * 100.0f),
        .board_c_x100= isnan(s.board_temp_c) ? INT32_MIN
                                            : (int32_t)(s.board_temp_c * 100.0f),
        .flags       = (s.vbus ? 0x01u : 0) | (s.charging ? 0x02u : 0) |
                       (s.charge_inhibited ? 0x04u : 0) |
                       (s_alert_pending ? 0x08u : 0) |
                       (s_net_off_req ? 0x10u : 0),
        .state       = (uint32_t)s.state,
    };
    esp_err_t e = vq_log_append(VQ_LOG_EVENT, &rec, sizeof(rec));
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE && e != ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "log append evt=%" PRIu32 " failed: %s",
                 (uint32_t)evt, esp_err_to_name(e));
    }
}

/* Registered transition listeners — invoked from the status task only.  */
static void set_state(vq_pwr_state_t next)
{
    lock_state();
    vq_pwr_state_t prev = s_st.state;
    if (next == prev) {
        unlock_state();
        return;
    }
    s_st.state = next;
    unlock_state();
    ESP_LOGW(TAG, "power %s -> %s (vcell=%.3fV soc=%.1f%% t=%.1fC vbus=%d chg=%d inh=%d)",
             s_state_names[prev], s_state_names[next],
             (double)s_st.vcell_v, (double)s_st.soc_pct,
             (double)s_st.board_temp_c, s_st.vbus, s_st.charging,
             s_st.charge_inhibited);
    for (size_t i = 0; i < s_cb_n; i++) {
        s_cbs[i].cb(next, s_cbs[i].ctx);
    }
}

/* Charge-inhibit driver: inhibit while a heavy burst overlaps VBUS, and
 * while the board is >45 C. Reconciled every tick + on burst edges.     */
static void apply_charge_inhibit(void)
{
    vq_power_status_t s;
    vq_power_status(&s);
    bool want = s_thermal_inhibit || (s.vbus && s_burst_refs > 0);
    if (want == s_inhibit_applied) {
        return;
    }
    esp_err_t e = vq_exp_charge_inhibit(want);
    if (e == ESP_OK) {
        s_inhibit_applied = want;
        s.charge_inhibited = want;
        lock_state();
        s_st.charge_inhibited = want;
        unlock_state();
        ESP_LOGI(TAG, "charge_inhibit=%d (refs=%" PRIu32 " thermal=%d)",
                 want, s_burst_refs, s_thermal_inhibit);
        log_pwr_event(VQ_EVT_CHARGE_STATE);
    } else {
        ESP_LOGW(TAG, "vq_exp_charge_inhibit(%d) failed: %s",
                 want, esp_err_to_name(e));
    }
}

/* Newest sample from a named board-temp sensor via the pipeline ring.
 * Returns NAN when the driver is absent or no sample exists.           */
static float last_temp_sample(const char *name, vq_channel_id_t ch)
{
    if (vq_sensor_find(name) == NULL) {
        return NAN;
    }
    vq_sample_t buf[4];
    size_t n = vq_pipeline_window(ch, VQ_TEMP_WINDOW_US, buf, 4);
    if (n == 0) {
        return NAN;
    }
    return buf[n - 1].value;   /* ring returns oldest→newest */
}

/* (Re)install the ESP tsens. Called at init and lazily after a
 * prepare_sleep()/selftest cycle uninstalled it — ~1/min retry bound.   */
static void ensure_tsens(void)
{
    if (s_tsens_on) {
        return;
    }
    if (++s_tsens_retry < 12) {       /* first retry on 12th tick ≈ 1 min */
        return;
    }
    s_tsens_retry = 0;
    if (s_tsens != NULL) {            /* installed earlier, enable failed */
        if (temperature_sensor_enable(s_tsens) == ESP_OK) {
            s_tsens_on = true;
        }
        return;
    }
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tcfg, &s_tsens) == ESP_OK &&
        temperature_sensor_enable(s_tsens) == ESP_OK) {
        s_tsens_on = true;
    }
}

/* board_temp_c = warmest of {tmp117.dome, tmp117.skin, tmp117.tail,
 * ESP internal tsens}; NAN when every source is missing.                */
static float read_board_temp_c(void)
{
    static const struct {
        const char     *name;
        vq_channel_id_t ch;
    } probes[] = {
        { "tmp117.dome", VQ_CH_TEMP_DOME   },   /* spec-named source       */
        { "tmp117.skin", VQ_CH_TEMP_SKIN   },
        { "tmp117.tail", VQ_CH_TEMP_DISTAL },   /* optional tail           */
    };
    float warm = NAN;
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        float t = last_temp_sample(probes[i].name, probes[i].ch);
        if (!isnan(t) && (isnan(warm) || t > warm)) {
            warm = t;
        }
    }
    if (s_tsens_on) {
        float c;
        if (temperature_sensor_get_celsius(s_tsens, &c) == ESP_OK &&
            (isnan(warm) || c > warm)) {
            warm = c;
        }
    }
    return warm;
}

/* ------------------------------------------------------------------ */
/* State evaluation                                                    */
/* ------------------------------------------------------------------ */

static vq_pwr_state_t eval_state(bool alert)
{
    const float vcell_mv = s_st.vcell_v * 1000.0f;
    const bool  v_ok     = !isnan(s_st.vcell_v);
    const bool  s_ok     = !isnan(s_st.soc_pct);
    const bool  t_ok     = !isnan(s_st.board_temp_c);

    if (v_ok && vcell_mv <= (float)VQ_BATT_EMERG_MV) {
        return VQ_PWR_EMERGENCY;
    }
    if (alert || (v_ok && vcell_mv <= (float)VQ_BATT_LOW_MV)) {
        return VQ_PWR_LOW_CUTOFF;
    }
    if (s_st.state == VQ_PWR_CONSERVE) {
        /* Leave only past the hysteresis band — avoids flapping at the
         * boundary every 5 s while the cell sits near 3.45 V. */
        bool recover = true;
        if (s_ok && s_st.soc_pct < VQ_RECOVER_SOC_PCT)      recover = false;
        if (v_ok && vcell_mv < (float)VQ_RECOVER_VCELL_MV)  recover = false;
        if (t_ok && s_st.board_temp_c > VQ_TEMP_RECOVER_C)  recover = false;
        return recover ? VQ_PWR_NORMAL : VQ_PWR_CONSERVE;
    }
    if ((s_ok && s_st.soc_pct < VQ_CONSERVE_SOC_PCT) ||
        (v_ok && vcell_mv < (float)VQ_CONSERVE_VCELL_MV) ||
        (t_ok && s_st.board_temp_c > VQ_TEMP_CONSERVE_C)) {
        return VQ_PWR_CONSERVE;
    }
    return VQ_PWR_NORMAL;
}

/* ------------------------------------------------------------------ */
/* Terminal shutdown paths (noreturn via deep sleep)                   */
/* ------------------------------------------------------------------ */

/* LOW_CUTOFF: LED kill → net-off flag → save logs → deep sleep. */
static void enter_low_cutoff(void)
{
    vq_led_kill_all();
    s_net_off_req = true;
    log_pwr_event(VQ_EVT_LOW_BATTERY);
    /* Release the ALRT line: it will re-assert while the cell stays flat,
     * but the latch keeps IO6 out of the EXT1 mask for this sleep so it
     * cannot bounce us straight back out. */
    vq_gauge_clear_alert();
    s_lowbatt_latch = VQ_SLEEP_LATCH_MAGIC;
    log_pwr_event(VQ_EVT_DEEP_SLEEP_ENTER);
    /* Brief settle so registered listeners (LED kill, net back-off,
     * decision skip) and the flash append can finish before the park. */
    vTaskDelay(pdMS_TO_TICKS(VQ_LOW_CUTOFF_SETTLE_MS));
    vq_power_enter_deep_sleep();   /* noreturn */
}

/* EMERGENCY: kill → best-effort log append → immediate deep sleep. */
static void enter_emergency(void)
{
    s_net_off_req = true;
    vq_led_kill_all();
    s_lowbatt_latch = VQ_SLEEP_LATCH_MAGIC;
    log_pwr_event(VQ_EVT_EMERGENCY_SHUTDOWN);
    vq_power_enter_deep_sleep();   /* noreturn */
}

/* ------------------------------------------------------------------ */
/* 5 s status task                                                     */
/* ------------------------------------------------------------------ */

static void power_tick(void)
{
    ensure_tsens();          /* lazy reinstall after prepare_sleep/selftest */

    /* --- gauge ------------------------------------------------------- */
    float v = NAN, soc = NAN;
    if (vq_gauge_read(&v, &soc) == ESP_OK && v > 0.0f && v < 6.0f) {
        s_gauge_seen = true;
        lock_state();
        s_st.vcell_v = v;
        s_st.soc_pct = soc;
        unlock_state();
    } else {
        s_gauge_errs++;
        if (s_gauge_errs == 1 || (s_gauge_errs % 12) == 0) {  /* ~1/min */
            ESP_LOGW(TAG, "gauge read failed (#%" PRIu32 ", %s)",
                     s_gauge_errs, s_gauge_seen ? "comms lost" : "never seen");
        }
    }

    /* --- charger inputs: VBUS_DET(P6)/CHG_STAT(P5) via expander,
     *     CHG_PG on IO7 (low = VBUS good) ------------------------------- */
    bool pg_good  = gpio_get_level(VQ_CHG_PG_GPIO) == 0;
    bool vbus     = vq_exp_vbus_present() || pg_good;
    bool charging = vq_exp_charging();
    bool inhibited = vq_exp_get_out(VQ_EXP_CHG_DIS);   /* shadow read, no I2C */
    float bt       = read_board_temp_c();   /* I2C/tsens — outside the lock */
    bool prev_vbus = s_st.vbus;
    bool prev_chg  = s_st.charging;

    lock_state();
    s_st.vbus             = vbus;
    s_st.charging         = charging;
    s_st.charge_inhibited = inhibited;
    s_st.charge_rate      = charging ? (float)VQ_CHG_CURRENT_MA : 0.0f;
    s_st.board_temp_c     = bt;
    unlock_state();
    s_inhibit_applied = inhibited;

    if (vbus != prev_vbus) {
        log_pwr_event(vbus ? VQ_EVT_VBUS_IN : VQ_EVT_VBUS_OUT);
    }
    if (charging != prev_chg) {
        log_pwr_event(VQ_EVT_CHARGE_STATE);
    }

    /* --- gauge alert: flag from cb/ISR, verified against line level --- */
    bool alert = s_alert_pending;
    s_alert_pending = false;
    if (gpio_get_level(VQ_GAUGE_ALRT_GPIO) == 0) {   /* open-drain active-low */
        alert = true;
    }

    /* --- thermal rule: >40 C forces CONSERVE (eval below); >45 C
     *     charge-inhibits hard loads (2 C release hysteresis). ---------- */
    if (!isnan(bt)) {
        if (!s_thermal_inhibit && bt > VQ_TEMP_INHIBIT_C) {
            s_thermal_inhibit = true;
            ESP_LOGW(TAG, "board %.1fC > %.0fC — charge inhibit",
                     (double)bt, (double)VQ_TEMP_INHIBIT_C);
        } else if (s_thermal_inhibit && bt <= VQ_TEMP_RELEASE_C) {
            s_thermal_inhibit = false;
        }
    }
    apply_charge_inhibit();

    /* Clear the RTC latch only when the cell has genuinely recovered.   */
    if (s_lowbatt_latch == VQ_SLEEP_LATCH_MAGIC &&
        !isnan(s_st.vcell_v) &&
        s_st.vcell_v * 1000.0f >= (float)VQ_RECOVER_VCELL_MV) {
        s_lowbatt_latch = 0;
        ESP_LOGI(TAG, "low-batt latch cleared (vcell=%.3fV)",
                 (double)s_st.vcell_v);
    }

    /* --- state machine ------------------------------------------------ */
    vq_pwr_state_t next = eval_state(alert);
    if (next != s_st.state) {
        set_state(next);
    }
    if (s_st.state == VQ_PWR_EMERGENCY) {
        enter_emergency();      /* noreturn */
    }
    if (s_st.state == VQ_PWR_LOW_CUTOFF) {
        enter_low_cutoff();     /* noreturn */
    }
}

static void power_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* 5 s cadence; alert edges poke the task for early eval. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VQ_PWR_PERIOD_MS));
        power_tick();
    }
}

/* ------------------------------------------------------------------ */
/* Gauge alert hookup                                                  */
/* ------------------------------------------------------------------ */

/* Driver-provided path (task context, presumably). */
static void gauge_alert_cb(void *ctx)
{
    (void)ctx;
    s_alert_pending = true;
    poke_task();
}

/* Own-ISR fallback: IO6 negedge when the gauge driver does not own it. */
static void IRAM_ATTR gauge_alrt_isr(void *arg)
{
    (void)arg;
    s_alert_pending = true;
    poke_task();
}

static void hook_gauge_alert(void)
{
    if (vq_gauge_on_alert(gauge_alert_cb, NULL) == ESP_OK) {
        return;    /* gauge driver owns the pin */
    }
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "gpio_install_isr_service: %s", esp_err_to_name(e));
    }
    if (gpio_set_intr_type(VQ_GAUGE_ALRT_GPIO, GPIO_INTR_NEGEDGE) == ESP_OK &&
        gpio_isr_handler_add(VQ_GAUGE_ALRT_GPIO, gauge_alrt_isr, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "GAUGE_ALRT hooked via local ISR (IO6 negedge)");
    } else {
        ESP_LOGW(TAG, "GAUGE_ALRT ISR hook failed — line is still polled");
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

vq_err_t vq_power_init(void)
{
    if (s_init_done) {
        return ESP_OK;
    }

    /* IO7 CHG_PG + IO6 GAUGE_ALRT: inputs w/ pull-up (open-drain; external
     * pulls exist per fab BOM, internal pull is belt-and-suspenders).     */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << VQ_CHG_PG_GPIO) | (1ULL << VQ_GAUGE_ALRT_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    /* RTC latch hygiene: only a deep-sleep wake keeps it set; any other
     * reset reason means a fresh power session. */
    esp_reset_reason_t rr = esp_reset_reason();
    if (rr != ESP_RST_DEEPSLEEP) {
        s_lowbatt_latch = 0;
    }
    ESP_LOGI(TAG, "power init (reset=%d wake=%d latch=%" PRIu32 ")",
             (int)rr, (int)esp_sleep_get_wakeup_cause(), s_lowbatt_latch);

    /* Gauge: first read now so gates have real data before the task runs. */
    float v = NAN, soc = NAN;
    if (vq_gauge_read(&v, &soc) == ESP_OK && v > 0.0f && v < 6.0f) {
        s_gauge_seen = true;
        s_st.vcell_v = v;
        s_st.soc_pct = soc;
        ESP_LOGI(TAG, "gauge: %.3fV %.1f%%", (double)v, (double)soc);
    } else {
        ESP_LOGW(TAG, "gauge read failed at boot — gates run on last-known/ALRT");
    }
    /* VALRT low ≈ 3.2 V + ATHD ≈ 5 % SOC (thresholds live in the gauge
     * driver; VQ_BATT_LOW_MV/VQ_SOC_LOW_PCT are the board constants).     */
    esp_err_t e = vq_gauge_config_alerts();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "gauge alert config: %s", esp_err_to_name(e));
    }

    /* ESP internal temperature sensor for the board-thermal rule. */
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tcfg, &s_tsens) == ESP_OK &&
        temperature_sensor_enable(s_tsens) == ESP_OK) {
        s_tsens_on = true;
    } else {
        s_tsens = NULL;
        s_tsens_on = false;
        ESP_LOGW(TAG, "esp tsens unavailable — board temp = TMP117 only");
    }

    /* Status task before alert hookup so poke_task() has a handle.      */
    if (xTaskCreate(power_task, "vq_power", VQ_PWR_TASK_STACK,
                    NULL, VQ_PWR_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }
    hook_gauge_alert();

    s_init_done = true;
    /* First evaluation immediately — a flat cell must not wait 5 s.     */
    xTaskNotifyGive(s_task);
    return ESP_OK;
}

void vq_power_status(vq_power_status_t *out)
{
    if (out == NULL) {
        return;
    }
    lock_state();
    *out = s_st;
    unlock_state();
}

vq_pwr_state_t vq_power_state(void)
{
    lock_state();
    vq_pwr_state_t s = s_st.state;
    unlock_state();
    return s;
}

/* Permission gate — LED scheduler asks before every pulse window.       */
bool vq_power_led_ok(uint32_t pulse_mA)
{
    vq_power_status_t s;
    vq_power_status(&s);

    if (s.state == VQ_PWR_LOW_CUTOFF || s.state == VQ_PWR_EMERGENCY) {
        return false;                                  /* shutting down    */
    }
    /* TX_5V boost must never run below 3.3 V — every scheduled LED window
     * owns the boost, so the floor applies to all chains.               */
    if (!isnan(s.vcell_v) && s.vcell_v * 1000.0f < (float)VQ_BATT_BOOST_MIN_MV) {
        return false;
    }
    /* USB budget: charge 200 + base ~120 + pulse <= 500 mA. When a burst
     * already inhibited charge, `charging` is false and the term drops
     * out — that is the intended CHG_DIS strategy path.                 */
    if (s.vbus && s.charging) {
        uint32_t est = (uint32_t)VQ_CHG_CURRENT_MA + VQ_BASE_SYS_MA + pulse_mA;
        if (est > (uint32_t)VQ_USB_BUDGET_MA) {
            return false;
        }
    }
    return true;
}

/* Permission gate — net layer asks before Wi-Fi TX bursts.              */
bool vq_power_wifi_burst_ok(void)
{
    vq_power_status_t s;
    vq_power_status(&s);

    if (s.state == VQ_PWR_LOW_CUTOFF || s.state == VQ_PWR_EMERGENCY ||
        s_net_off_req) {
        return false;
    }
    if (vq_led_tx5_on()) {
        return false;          /* shared-rail: never burst inside LED win */
    }
    /* Budget-tight while charging on VBUS: 200 + ~120 + ~400 > 500.
     * Callers then switch to the CHG_DIS strategy (burst_begin).        */
    if (s.vbus && s.charging) {
        uint32_t est = (uint32_t)VQ_CHG_CURRENT_MA + VQ_BASE_SYS_MA +
                       VQ_WIFI_BURST_MA;
        if (est > (uint32_t)VQ_USB_BUDGET_MA) {
            return false;
        }
    }
    return true;
}

void vq_power_burst_begin(uint32_t est_mA)
{
    lock_state();
    s_burst_refs++;
    s_burst_est_mA = est_mA;
    uint32_t refs = s_burst_refs;
    unlock_state();
    /* While VBUS is present, inhibit charge so the burst fits the
     * 500 mA USB budget; released on matching end / by tick reconcile.  */
    apply_charge_inhibit();
    ESP_LOGD(TAG, "burst_begin est=%" PRIu32 "mA refs=%" PRIu32, est_mA, refs);
}

void vq_power_burst_end(void)
{
    lock_state();
    if (s_burst_refs == 0) {
        unlock_state();
        ESP_LOGW(TAG, "burst_end without begin");
        return;
    }
    s_burst_refs--;
    s_burst_est_mA = 0;
    unlock_state();
    apply_charge_inhibit();
}

vq_err_t vq_power_on_change(vq_pwr_cb_t cb, void *ctx)
{
    if (cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_state();
    if (s_cb_n >= VQ_PWR_CB_MAX) {
        unlock_state();
        return ESP_ERR_NO_MEM;
    }
    s_cbs[s_cb_n].cb  = cb;
    s_cbs[s_cb_n].ctx = ctx;
    s_cb_n++;
    unlock_state();
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* §4.4 sleep checklist — also callable from `selftest sleep_current`.  */
/* ------------------------------------------------------------------ */

vq_err_t vq_power_prepare_sleep(void)
{
    /* 1. Park every registered sensor at its deepest sleep state.        */
    size_t n = 0;
    vq_sensor_t *ss = vq_sensors(&n);
    for (size_t i = 0; i < n; i++) {
        if (ss[i].ops && ss[i].ops->sleep) {
            esp_err_t e = ss[i].ops->sleep(&ss[i]);
            if (e != ESP_OK) {
                ESP_LOGW(TAG, "sleep(%s): %s",
                         ss[i].ops->name ? ss[i].ops->name : "?",
                         esp_err_to_name(e));
            }
        }
    }

    /* 2. W25Q512 power-down (B9h) — log ring must not keep drawing.     */
    vq_log_sleep();

    /* 3. All five CS asserted HIGH (only one may ever be low anyway).   */
    static const gpio_num_t cs_pins[] = {
        VQ_CS_ADS1292_GPIO, VQ_CS_AD5940_GPIO, VQ_CS_FLASH_GPIO,
        VQ_CS_AFE4900_GPIO, VQ_CS_MAX86141_GPIO,
    };
    for (size_t i = 0; i < sizeof(cs_pins) / sizeof(cs_pins[0]); i++) {
        gpio_set_direction(cs_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(cs_pins[i], 1);
    }

    /* 4. Status LED off, PDM mic clock low.                             */
    gpio_set_direction(VQ_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(VQ_STATUS_LED_GPIO, 0);
    gpio_set_direction(VQ_MIC_CLK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(VQ_MIC_CLK_GPIO, 0);

    /* 5. TX_5V boost off — through the scheduler so its bookkeeping
     *    stays coherent across the sleep boundary.                      */
    vq_led_kill_all();

    /* Charger is autonomous: CHG_DIS / STAT / PG left as-is.            */

    /* 6. tsens off — µA class, and nothing samples it while asleep.
     *    If this returns without sleeping (selftest path), the status
     *    task lazily reinstalls it next tick.                            */
    if (s_tsens_on) {
        temperature_sensor_disable(s_tsens);
        temperature_sensor_uninstall(s_tsens);
        s_tsens = NULL;
        s_tsens_on = false;
    }
    s_tsens_retry = 11;      /* next tick retries immediately */

    /* 7. Drain pending wake-edge sources so a level-triggered wake can
     *    not fire the instant sleep starts: reading TCA6408 inputs
     *    releases its ~INT; clearing the gauge alert releases IO6.      */
    uint8_t exp_in = 0;
    vq_exp_read_inputs(&exp_in);
    if (gpio_get_level(VQ_GAUGE_ALRT_GPIO) == 0) {
        vq_gauge_clear_alert();
    }

    /* 8. RTC GPIO holds: CS×5 + IO17 + IO40 keep their driven levels
     *    through deep sleep. RTC pins (10/15/16/17/18) hold via RTC_IO;
     *    digital pins (38/40) hold via RTC_CNTL_DIG_PAD_HOLD — both need
     *    gpio_hold_en + the sleep-wide gpio_deep_sleep_hold_en.         */
    for (size_t i = 0; i < sizeof(cs_pins) / sizeof(cs_pins[0]); i++) {
        gpio_hold_en(cs_pins[i]);
    }
    gpio_hold_en(VQ_STATUS_LED_GPIO);
    gpio_hold_en(VQ_MIC_CLK_GPIO);
    gpio_deep_sleep_hold_en();

    /* 9. Wake sources — S3 RTC-capable pins are GPIO0–21; IO3/4/6 all
     *    qualify (esp_sleep_is_valid_wakeup_gpio double-checked).       */
    uint64_t ext1_mask = 0;
    if (esp_sleep_is_valid_wakeup_gpio(VQ_EXP_INT_GPIO)) {      /* IO3 LOW */
        ext1_mask |= (1ULL << VQ_EXP_INT_GPIO);
    }
    /* IO6 GAUGE_ALRT is armed only when we did NOT fall asleep because
     * the battery is flat: the MAX17048 re-asserts ALRT while the cell
     * stays <3.2 V, which would otherwise instant-wake (boot loop).     */
    if (s_lowbatt_latch != VQ_SLEEP_LATCH_MAGIC &&
        esp_sleep_is_valid_wakeup_gpio(VQ_GAUGE_ALRT_GPIO)) {
        ext1_mask |= (1ULL << VQ_GAUGE_ALRT_GPIO);              /* IO6 LOW */
    }
    esp_err_t e = ext1_mask
        ? esp_sleep_enable_ext1_wakeup_io(ext1_mask, ESP_EXT1_WAKEUP_ANY_LOW)
        : ESP_ERR_INVALID_STATE;
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "EXT1 arm failed: %s", esp_err_to_name(e));
    }
    /* IO4 LSM6_INT1 is active-HIGH: EXT1 on S3 cannot mix levels in one
     * mask (ESP_ERR_NOT_ALLOWED), so the high-level source rides EXT0.  */
    e = esp_sleep_enable_ext0_wakeup(VQ_LSM6_INT1_GPIO, 1);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "EXT0 (LSM6 IO4) arm failed: %s — low/high wakes still armed",
                 esp_err_to_name(e));
    }
    return ESP_OK;
}

void vq_power_enter_deep_sleep(void)
{
    /* Best-effort: failures inside the checklist are logged, never fatal —
     * the battery cutoff must not be blocked by a parked/missing sensor.  */
    vq_power_prepare_sleep();
    ESP_LOGW(TAG, "entering deep sleep");
    esp_deep_sleep_start();    /* noreturn */
    /* unreachable */
    for (;;) { }
}
