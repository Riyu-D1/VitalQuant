/* sleep_sim.c — esp_sleep / esp_pm host-sim implementation (ESP32-S3).
 *
 * THE SLEEP MODEL (host-sim, not silicon):
 *   * arm calls (ext0/ext1/timer/gpio/…) only RECORD what the firmware
 *     asked for — into g_vsim_sleep (the vsim.h contract) plus private
 *     mirrors for fields the contract struct does not carry.
 *   * esp_deep_sleep_start() latches deep_sleep_entered and PARKS the
 *     calling thread on a condvar.  It never returns to firmware.
 *   * A scenario injects the wake (vsim_trigger_wakeup / …_timer /
 *     …_force, or the vsim.h alias vsim_sleep_wake_with_gpio).  The
 *     parked thread then runs the reboot path: "VB-WAKE reboot" on
 *     stderr, vboard_reset_all() (EN pulse — device models to power-on),
 *     then scenario-chosen restart: reboot fn → longjmp checkpoint →
 *     _exit(0), in that precedence order.
 *   * If a valid wake is triggered before the firmware parks, it is
 *     latched and the next esp_deep_sleep_start() falls straight
 *     through to the reboot path — mirrors "wake source already active
 *     at sleep entry" and keeps scenario/firmware races benign.
 *   * An armed timer also auto-wakes: a detached thread sleeps
 *     vboard_clock_sleep_us(us) then triggers ESP_SLEEP_WAKEUP_TIMER.
 *     A generation counter keeps a stale timer from a previous sleep
 *     cycle from injecting a spurious wake into the next one.
 *   * Wake causes are sticky across the simulated reboot — the process
 *     survives, so esp_sleep_get_wakeup_cause() in the re-run
 *     fw_app_main reports the injected source, and
 *     g_vsim_sleep_woke_from_deep lets the esp_system shim answer
 *     ESP_RST_DEEPSLEEP.
 *
 * Only pins in armed masks wake the part: an unarmed gpio passed to
 * vsim_trigger_wakeup() logs a warning and does NOT release the sleep —
 * firmware bugs in the wake map must not be silently masked.
 *
 * GPIO hold recording (g_vsim_sleep.holds/n_holds) is written by the
 * gpio shim's gpio_hold_en — this file never touches those fields.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include <pthread.h>
#include <unistd.h>

#include "esp_sleep.h"
#include "esp_pm.h"
#include "vsim.h"

/* vboard.h declarations used here, spelled out instead of #include — the
 * header is owned by the vboard agent and may be mid-flight; the ABI is
 * fixed by the contract below.                                       */
extern void vboard_clock_sleep_us(int64_t virt_us);  /* scaled sleep    */
extern void vboard_reset_all(void);                  /* EN pulse → reset */

/* ------------------------------------------------------------------ */
/* Contract state (vsim.h)                                             */
/* ------------------------------------------------------------------ */
/* Weak so a parallel definition elsewhere in the sim build wins
 * instead of failing the link; nobody else should define it, but the
 * symbol is the shared contract either way.                            */
__attribute__((weak)) vsim_sleep_state_t g_vsim_sleep;

/* Reboot checkpoint — armed by whoever runs the fw_app_main thread.   */
jmp_buf      g_vsim_sleep_reboot_jb;
volatile int g_vsim_sleep_reboot_armed;

/* Sticky "the last reset was a deep-sleep wake" flag — the esp_system
 * shim's esp_reset_reason() should return ESP_RST_DEEPSLEEP when set.  */
int g_vsim_sleep_woke_from_deep;

/* ------------------------------------------------------------------ */
/* Private mirrors for state the contract struct does not carry        */
/* ------------------------------------------------------------------ */
static pthread_mutex_t s_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_cv  = PTHREAD_COND_INITIALIZER;

static bool     s_parked;               /* fw blocked in deep_sleep_start */
static bool     s_wake_pending;         /* wake latched (consumed once)   */
static uint32_t s_sleep_gen;            /* bumped per deep_sleep_start    */

static esp_sleep_source_t         s_cause = ESP_SLEEP_WAKEUP_UNDEFINED;
static uint64_t                   s_ext1_status;
static uint64_t                   s_gpio_status;
static int                        s_ext0_level;
static esp_sleep_ext1_wakeup_mode_t s_ext1_mode = ESP_EXT1_WAKEUP_ANY_LOW;
static uint64_t                   s_timer_us;
static uint64_t                   s_gpio_wakeup_mask;
static bool                       s_gpio_wakeup_en;
static bool                       s_ulp_en, s_touch_en, s_uart_en,
                                  s_wifi_en, s_bt_en;
static bool                       s_light_sleep_entered;
static uint8_t                    s_pd_opts[ESP_PD_DOMAIN_MAX];
static vsim_reboot_fn_t           s_reboot_fn;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static bool mask_rtc_only(uint64_t mask)
{
    /* every set bit must be within GPIO0..VSIM_RTC_GPIO_MAX */
    return (mask & ~(((uint64_t)1 << (VSIM_RTC_GPIO_MAX + 1)) - 1)) == 0;
}

/* Auto-wake for an armed timer — sleeps scaled virtual time, then fires
 * the timer source if this sleep cycle is still parked.               */
typedef struct { uint32_t gen; uint64_t us; } sleep_timer_arg_t;

static void *sleep_timer_main(void *p)
{
    sleep_timer_arg_t a = *(sleep_timer_arg_t *)p;
    free(p);
    vboard_clock_sleep_us((int64_t)a.us);
    pthread_mutex_lock(&s_mtx);
    if (s_parked && a.gen == s_sleep_gen && !s_wake_pending) {
        s_cause        = ESP_SLEEP_WAKEUP_TIMER;
        s_wake_pending = true;
        pthread_cond_broadcast(&s_cv);
    }
    pthread_mutex_unlock(&s_mtx);
    return NULL;
}

static void arm_timer_thread(uint32_t gen, uint64_t us)
{
    sleep_timer_arg_t *a = malloc(sizeof(*a));
    if (a == NULL) {
        return;                          /* no timer wake — not fatal */
    }
    a->gen = gen;
    a->us  = us;
    pthread_t th;
    if (pthread_create(&th, NULL, sleep_timer_main, a) == 0) {
        pthread_detach(th);
    } else {
        free(a);
    }
}

/* ------------------------------------------------------------------ */
/* Wakeup-source arming                                                */
/* ------------------------------------------------------------------ */
esp_err_t esp_sleep_enable_ext0_wakeup(VSIM_SLEEP_GPIO_T gpio_num, int level)
{
    if (!esp_sleep_is_valid_wakeup_gpio(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (level != 0 && level != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    g_vsim_sleep.armed_ext0 = true;
    g_vsim_sleep.ext0_gpio  = (int)gpio_num;
    s_ext0_level            = level;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t io_mask,
                                          esp_sleep_ext1_wakeup_mode_t mode)
{
    if (io_mask == 0 || !mask_rtc_only(io_mask)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mode != ESP_EXT1_WAKEUP_ALL_LOW &&
        mode != ESP_EXT1_WAKEUP_ANY_LOW &&
        mode != ESP_EXT1_WAKEUP_ANY_HIGH) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    pthread_mutex_lock(&s_mtx);
    g_vsim_sleep.armed_ext1 = true;
    g_vsim_sleep.ext1_mask  = io_mask;
    s_ext1_mode             = mode;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t io_mask,
                                       esp_sleep_ext1_wakeup_mode_t mode)
{
    return esp_sleep_enable_ext1_wakeup_io(io_mask, mode);
}

esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us)
{
    if (time_in_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    s_timer_us = time_in_us;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_sleep_enable_gpio_wakeup(void)
{
    pthread_mutex_lock(&s_mtx);
    s_gpio_wakeup_en = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t gpio_pin_mask,
                                            esp_deep_sleep_gpio_wake_up_mode_t mode)
{
    if (gpio_pin_mask == 0 || !mask_rtc_only(gpio_pin_mask)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mode != ESP_GPIO_WAKEUP_GPIO_LOW && mode != ESP_GPIO_WAKEUP_GPIO_HIGH) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    s_gpio_wakeup_en   = true;
    s_gpio_wakeup_mask = gpio_pin_mask;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_sleep_enable_touchpad_wakeup(void)
{
    pthread_mutex_lock(&s_mtx);
    s_touch_en = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_sleep_enable_ulp_wakeup(void)
{
    pthread_mutex_lock(&s_mtx);
    s_ulp_en = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_sleep_enable_uart_wakeup(int uart_num)
{
    (void)uart_num;
    pthread_mutex_lock(&s_mtx);
    s_uart_en = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_sleep_enable_wifi_wakeup(void)
{
    pthread_mutex_lock(&s_mtx);
    s_wifi_en = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_sleep_enable_bt_wakeup(void)
{
    pthread_mutex_lock(&s_mtx);
    s_bt_en = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source)
{
    pthread_mutex_lock(&s_mtx);
    switch (source) {
    case ESP_SLEEP_WAKEUP_EXT0:
        g_vsim_sleep.armed_ext0 = false;
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
        g_vsim_sleep.armed_ext1 = false;
        g_vsim_sleep.ext1_mask  = 0;
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        s_timer_us = 0;
        break;
    case ESP_SLEEP_WAKEUP_GPIO:
        s_gpio_wakeup_en   = false;
        s_gpio_wakeup_mask = 0;
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: s_touch_en = false; break;
    case ESP_SLEEP_WAKEUP_ULP:      s_ulp_en   = false; break;
    case ESP_SLEEP_WAKEUP_UART:     s_uart_en  = false; break;
    case ESP_SLEEP_WAKEUP_WIFI:     s_wifi_en  = false; break;
    case ESP_SLEEP_WAKEUP_BT:       s_bt_en    = false; break;
    default:
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

bool esp_sleep_is_valid_wakeup_gpio(VSIM_SLEEP_GPIO_T gpio_num)
{
    int g = (int)gpio_num;
    return g >= 0 && g <= VSIM_RTC_GPIO_MAX;
}

esp_err_t esp_sleep_pd_config(esp_sleep_pd_domain_t domain,
                              esp_sleep_pd_option_t option)
{
    if (domain < 0 || domain >= ESP_PD_DOMAIN_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (option != ESP_PD_OPTION_OFF && option != ESP_PD_OPTION_ON &&
        option != ESP_PD_OPTION_AUTO) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    s_pd_opts[domain] = (uint8_t)option;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Sleep entry                                                         */
/* ------------------------------------------------------------------ */
esp_err_t esp_light_sleep_start(void)
{
    pthread_mutex_lock(&s_mtx);
    s_light_sleep_entered = true;
    /* consume a latched pending wake, like a real light-sleep wake edge */
    s_wake_pending = false;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

void esp_deep_sleep_start(void)
{
    pthread_mutex_lock(&s_mtx);
    g_vsim_sleep.deep_sleep_entered = true;
    s_parked   = true;
    uint32_t gen = ++s_sleep_gen;
    fprintf(stderr,
            "VB-SLEEP deep-sleep armed ext0=%d(gpio%d,lvl%d) "
            "ext1=%d(mask=0x%llx,mode%d) timer_us=%llu gpio_en=%d\n",
            (int)g_vsim_sleep.armed_ext0, g_vsim_sleep.ext0_gpio,
            s_ext0_level,
            (int)g_vsim_sleep.armed_ext1,
            (unsigned long long)g_vsim_sleep.ext1_mask, (int)s_ext1_mode,
            (unsigned long long)s_timer_us, (int)s_gpio_wakeup_en);
    if (s_timer_us != 0) {
        arm_timer_thread(gen, s_timer_us);   /* auto-wake: RTC timer   */
    }
    while (s_parked && !s_wake_pending) {
        pthread_cond_wait(&s_cv, &s_mtx);    /* parked until scenario  */
    }
    s_wake_pending = false;                  /* consumed               */
    s_parked       = false;
    g_vsim_sleep_woke_from_deep = 1;
    esp_sleep_source_t cause = s_cause;
    pthread_mutex_unlock(&s_mtx);

    fprintf(stderr, "VB-WAKE reboot cause=%d\n", (int)cause);
    vboard_reset_all();                     /* EN pulse → power-on     */

    if (s_reboot_fn != NULL) {
        s_reboot_fn();                      /* scenario reboot hook    */
    } else if (g_vsim_sleep_reboot_armed) {
        longjmp(g_vsim_sleep_reboot_jb, 1); /* back into vsim_boot     */
    }
    /* no restart path armed — let the harness inspect g_vsim_sleep */
    fprintf(stderr, "VB-SLEEP no reboot hook armed — _exit(0)\n");
    _exit(0);
}

/* ------------------------------------------------------------------ */
/* Post-wake queries                                                   */
/* ------------------------------------------------------------------ */
esp_sleep_source_t esp_sleep_get_wakeup_cause(void)
{
    pthread_mutex_lock(&s_mtx);
    esp_sleep_source_t c = s_cause;
    pthread_mutex_unlock(&s_mtx);
    return c;
}

uint64_t esp_sleep_get_ext1_wakeup_status(void)
{
    pthread_mutex_lock(&s_mtx);
    uint64_t m = s_ext1_status;
    pthread_mutex_unlock(&s_mtx);
    return m;
}

uint64_t esp_sleep_get_gpio_wakeup_status(void)
{
    pthread_mutex_lock(&s_mtx);
    uint64_t m = s_gpio_status;
    pthread_mutex_unlock(&s_mtx);
    return m;
}

/* ------------------------------------------------------------------ */
/* Host-sim control surface                                            */
/* ------------------------------------------------------------------ */
/* Set the cause+status for an injected gpio, validating against what
 * the firmware actually armed.  Returns the resolved source, or
 * ESP_SLEEP_WAKEUP_UNDEFINED when the pin was not armed (no wake).    */
static esp_sleep_source_t resolve_gpio_cause(int gpio)
{
    if (gpio < 0 || gpio > 63) {
        return ESP_SLEEP_WAKEUP_UNDEFINED;
    }
    uint64_t bit = (uint64_t)1 << gpio;
    if (g_vsim_sleep.armed_ext0 && gpio == g_vsim_sleep.ext0_gpio) {
        return ESP_SLEEP_WAKEUP_EXT0;
    }
    if (g_vsim_sleep.armed_ext1 && (g_vsim_sleep.ext1_mask & bit)) {
        s_ext1_status = bit;
        return ESP_SLEEP_WAKEUP_EXT1;
    }
    if (s_gpio_wakeup_en &&
        (s_gpio_wakeup_mask == 0 || (s_gpio_wakeup_mask & bit))) {
        s_gpio_status = bit;
        return ESP_SLEEP_WAKEUP_GPIO;
    }
    return ESP_SLEEP_WAKEUP_UNDEFINED;
}

void vsim_trigger_wakeup(int gpio)
{
    pthread_mutex_lock(&s_mtx);
    esp_sleep_source_t c = resolve_gpio_cause(gpio);
    if (c == ESP_SLEEP_WAKEUP_UNDEFINED) {
        pthread_mutex_unlock(&s_mtx);
        fprintf(stderr,
                "VB-WAKE ignored: gpio%d not armed (ext0=%d gpio%d, "
                "ext1=%d mask=0x%llx)\n",
                gpio, (int)g_vsim_sleep.armed_ext0, g_vsim_sleep.ext0_gpio,
                (int)g_vsim_sleep.armed_ext1,
                (unsigned long long)g_vsim_sleep.ext1_mask);
        return;
    }
    s_cause        = c;
    s_wake_pending = true;                 /* latched if not yet parked */
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);
}

/* vsim.h contract name — weak alias so a different owner of vsim.c can
 * override without a duplicate-symbol clash.                          */
void vsim_sleep_wake_with_gpio(int gpio)
    __attribute__((weak, alias("vsim_trigger_wakeup")));

void vsim_trigger_wakeup_timer(void)
{
    pthread_mutex_lock(&s_mtx);
    s_cause        = ESP_SLEEP_WAKEUP_TIMER;
    s_wake_pending = true;
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);
}

void vsim_trigger_wakeup_force(esp_sleep_source_t cause, uint64_t gpio_mask)
{
    pthread_mutex_lock(&s_mtx);
    s_cause = cause;
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        s_ext1_status = gpio_mask;
    } else if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        s_gpio_status = gpio_mask;
    }
    s_wake_pending = true;
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);
}

void vsim_sleep_set_reboot_fn(vsim_reboot_fn_t fn)
{
    pthread_mutex_lock(&s_mtx);
    s_reboot_fn = fn;
    pthread_mutex_unlock(&s_mtx);
}

bool vsim_sleep_parked(void)
{
    pthread_mutex_lock(&s_mtx);
    bool p = s_parked;
    pthread_mutex_unlock(&s_mtx);
    return p;
}

int vsim_sleep_woke_from_deep(void)
{
    pthread_mutex_lock(&s_mtx);
    int v = g_vsim_sleep_woke_from_deep;
    pthread_mutex_unlock(&s_mtx);
    return v;
}

uint64_t vsim_sleep_timer_us(void)
{
    pthread_mutex_lock(&s_mtx);
    uint64_t us = s_timer_us;
    pthread_mutex_unlock(&s_mtx);
    return us;
}

void vsim_sleep_state_clear(void)
{
    pthread_mutex_lock(&s_mtx);
    memset(&g_vsim_sleep, 0, sizeof(g_vsim_sleep));
    s_cause        = ESP_SLEEP_WAKEUP_UNDEFINED;
    s_ext1_status  = 0;
    s_gpio_status  = 0;
    s_timer_us     = 0;
    s_gpio_wakeup_mask = 0;
    s_gpio_wakeup_en   = false;
    s_ulp_en = s_touch_en = s_uart_en = s_wifi_en = s_bt_en = false;
    s_wake_pending = false;
    g_vsim_sleep_woke_from_deep = 0;
    pthread_mutex_unlock(&s_mtx);
}

/* ------------------------------------------------------------------ */
/* esp_pm — record-only stubs                                          */
/* ------------------------------------------------------------------ */
struct esp_pm_lock {
    esp_pm_lock_type_t type;
    int                arg;
    char               name[24];
    int                held;             /* refcount >0 while acquired  */
    bool               deleted;
};

#define VSIM_PM_MAX_LOCKS 16
static struct esp_pm_lock  s_pm_locks[VSIM_PM_MAX_LOCKS];
static bool                s_pm_configured;
static esp_pm_config_t     s_pm_cfg;

esp_err_t esp_pm_configure(const void *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    s_pm_cfg        = *(const esp_pm_config_t *)config;
    s_pm_configured = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_pm_get_configuration(void *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    if (!s_pm_configured) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    *(esp_pm_config_t *)config = s_pm_cfg;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_pm_lock_create(esp_pm_lock_type_t lock_type, int arg,
                             const char *name, esp_pm_lock_handle_t *out_handle)
{
    if (out_handle == NULL || lock_type < 0 || lock_type > ESP_PM_LOCK_TYPE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    for (size_t i = 0; i < VSIM_PM_MAX_LOCKS; i++) {
        if (!s_pm_locks[i].deleted && s_pm_locks[i].name[0] != '\0') {
            continue;                    /* slot in use                */
        }
        struct esp_pm_lock *l = &s_pm_locks[i];
        memset(l, 0, sizeof(*l));
        l->type = lock_type;
        l->arg  = arg;
        snprintf(l->name, sizeof(l->name), "%s", name ? name : "?");
        *out_handle = l;
        pthread_mutex_unlock(&s_mtx);
        return ESP_OK;
    }
    pthread_mutex_unlock(&s_mtx);
    return ESP_ERR_NO_MEM;
}

static struct esp_pm_lock *pm_lock_ptr(esp_pm_lock_handle_t h)
{
    if (h == NULL) {
        return NULL;
    }
    struct esp_pm_lock *l = (struct esp_pm_lock *)h;
    if (l < s_pm_locks || l >= s_pm_locks + VSIM_PM_MAX_LOCKS || l->deleted) {
        return NULL;
    }
    return l;
}

esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle)
{
    pthread_mutex_lock(&s_mtx);
    struct esp_pm_lock *l = pm_lock_ptr(handle);
    if (l == NULL) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    l->held++;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle)
{
    pthread_mutex_lock(&s_mtx);
    struct esp_pm_lock *l = pm_lock_ptr(handle);
    if (l == NULL) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    if (l->held == 0) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    l->held--;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle)
{
    pthread_mutex_lock(&s_mtx);
    struct esp_pm_lock *l = pm_lock_ptr(handle);
    if (l == NULL) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    l->deleted = true;
    l->held    = 0;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_pm_dump_locks(FILE *stream)
{
    FILE *f = stream ? stream : stderr;
    pthread_mutex_lock(&s_mtx);
    fprintf(f, "pm: configured=%d max=%dMHz min=%dMHz lightsleep=%d\n",
            (int)s_pm_configured, s_pm_cfg.max_freq_mhz,
            s_pm_cfg.min_freq_mhz, (int)s_pm_cfg.light_sleep_enable);
    for (size_t i = 0; i < VSIM_PM_MAX_LOCKS; i++) {
        if (!s_pm_locks[i].deleted && s_pm_locks[i].name[0] != '\0') {
            fprintf(f, "  lock[%zu] type=%d arg=%d held=%d name=%s\n",
                    i, (int)s_pm_locks[i].type, s_pm_locks[i].arg,
                    s_pm_locks[i].held, s_pm_locks[i].name);
        }
    }
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}
