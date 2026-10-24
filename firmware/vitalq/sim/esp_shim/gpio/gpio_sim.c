/* gpio_sim.c — host-sim implementation of driver/gpio.h.
 *
 * Output levels are pushed to the virtual board via
 * vboard_gpio_from_firmware() (device models may couple onto them —
 * e.g. CS gating a level shifter); input levels come from
 * vboard_gpio_get().  Interrupt edges are injected by the vboard tick
 * thread calling gpio_sim_irq(), so registered ISRs execute on a
 * genuinely different thread — real FromISR concurrency.
 *
 * SAFETY TRIPWIRE — fix_v22 board truth: GPIO19/20 (native USB D-/D+),
 * GPIO26-32 (internal SPI flash), GPIO35-37 (octal PSRAM) and
 * GPIO45/46/47/48 (straps/USB) must never be configured as outputs,
 * and non-strap members of that set must not be touched at all.
 * Strap pins IO0/IO3/IO45/IO46 may be configured as *inputs*; any
 * output-side use is a violation.  Every violation prints
 * "VB-GPIO-FORBIDDEN cfg gpioN" on stderr and bumps a counter that
 * scenarios read via gpio_forbidden_count().  Violations are counted
 * and logged, but the operation still proceeds — one bad call should
 * not mask later ones.
 */
#include "driver/gpio.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "vboard.h"

/* The sleep shim declares (as a weak symbol) that g_vsim_sleep.holds[]
 * is populated by the gpio shim's gpio_hold_en — mirror our registry
 * into it when the vsim object is linked in.  The reference is weak so
 * this file still links standalone.                                */
#if defined(__has_include)
#  if __has_include("vsim.h")
#    include "vsim.h"
#    define GPIO_HAVE_VSIM_SLEEP 1
#  endif
#endif
#ifdef GPIO_HAVE_VSIM_SLEEP
extern vsim_sleep_state_t g_vsim_sleep __attribute__((weak));
#endif

#define N_PINS 49   /* GPIO0..48 */

/* ------------------------------------------------------------ *
 *  Forbidden / strap pin tables (see board_pins.h §FORBIDDEN).   *
 * ------------------------------------------------------------ */
static bool pin_forbidden(int g)
{
    if (g == 19 || g == 20)            return true;   /* USB D-/D+      */
    if (g >= 26 && g <= 32)            return true;   /* SPI flash      */
    if (g == 35 || g == 36 || g == 37) return true;   /* octal PSRAM    */
    if (g == 45 || g == 46 || g == 47 || g == 48) return true; /* strap/USB */
    return false;
}

static bool pin_strap(int g)
{
    return g == 0 || g == 3 || g == 45 || g == 46;
}

static uint64_t g_gpio_forbidden_cfg;

uint64_t gpio_forbidden_count(void)
{
    return g_gpio_forbidden_cfg;
}

/* output_cfg == true for ops that drive the pad (output direction /
 * gpio_set_level); false for input-side config (pull modes). */
static void vb_tripwire(int g, bool output_cfg)
{
    bool bad;
    if (pin_strap(g)) {
        bad = output_cfg;            /* straps: inputs fine, outputs bad */
    } else {
        bad = pin_forbidden(g);      /* non-strap: any config is bad   */
    }
    if (bad) {
        fprintf(stderr, "VB-GPIO-FORBIDDEN cfg gpio%d\n", g);
        g_gpio_forbidden_cfg++;
    }
}

/* ------------------------------------------------------------ *
 *  Per-pin state
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t     mode;          /* gpio_mode_t; GPIO_MODE_DISABLE = unset */
    uint8_t     level;         /* last driven output level               */
    uint8_t     pull_up;       /* internal pull-up engaged               */
    uint8_t     pull_down;     /* internal pull-down engaged             */
    uint8_t     intr_type;     /* gpio_int_type_t                        */
    uint8_t     intr_en;       /* interrupt unmasked                     */
    uint8_t     hold;          /* gpio_hold_en                           */
    uint8_t     sleep_sel;     /* gpio_sleep_sel_en                      */
    uint8_t     sleep_hold;    /* gpio_sleep_hold_en                     */
    uint8_t     sleep_mode;    /* gpio_sleep_set_direction               */
    uint8_t     sleep_pull;    /* gpio_sleep_set_pull_mode (pull_mode_t) */
    uint8_t     drive_cap;     /* gpio_drive_cap_t                       */
    uint8_t     wake_type;     /* gpio_int_type_t; 0/DISABLE = none      */
    int         iomux_in_sig;  /* gpio_iomux_in signal +1; 0 = none      */
    int         iomux_out_fn;  /* gpio_iomux_out func +1; 0 = none       */
    gpio_isr_t  isr;
    void       *isr_arg;
} pin_t;

static pin_t            s_pins[N_PINS] = {
    [0 ... N_PINS - 1] = { .drive_cap = GPIO_DRIVE_CAP_DEFAULT },
};
static pthread_mutex_t  s_lock = PTHREAD_MUTEX_INITIALIZER;
static bool             s_isr_service;
static int              s_isr_flags;
static bool             s_deep_sleep_hold;
static void           (*s_gisr)(void *);   /* gpio_isr_register slot   */
static void            *s_gisr_arg;
static int              s_gisr_flags;

static bool pin_valid(int g)
{
    return g >= 0 && g < N_PINS && GPIO_IS_VALID_GPIO(g);
}

static void pin_clear(pin_t *p)
{
    /* The ISR slot is driver-side state: IDF's gpio_reset_pin leaves a
     * registered handler in place, so preserve it here too.           */
    gpio_isr_t isr = p->isr; void *arg = p->isr_arg;
    memset(p, 0, sizeof(*p));
    p->isr       = isr;
    p->isr_arg   = arg;
    p->drive_cap = GPIO_DRIVE_CAP_DEFAULT;
}

/* ------------------------------------------------------------ *
 *  g_vsim_sleep.holds[] mirror — the sleep shim reads these fields
 *  (it writes them itself "never"); call with s_lock held.
 * ------------------------------------------------------------ */
static void mirror_hold_add(int g)
{
#ifdef GPIO_HAVE_VSIM_SLEEP
    if (&g_vsim_sleep == NULL) return;          /* weak ref: absent   */
    const int cap = (int)(sizeof(g_vsim_sleep.holds) /
                          sizeof(g_vsim_sleep.holds[0]));
    for (int i = 0; i < g_vsim_sleep.n_holds; i++)
        if (g_vsim_sleep.holds[i] == g) return;
    if (g_vsim_sleep.n_holds >= 0 && g_vsim_sleep.n_holds < cap)
        g_vsim_sleep.holds[g_vsim_sleep.n_holds++] = g;
#else
    (void)g;
#endif
}

static void mirror_hold_del(int g)
{
#ifdef GPIO_HAVE_VSIM_SLEEP
    if (&g_vsim_sleep == NULL) return;
    for (int i = 0; i < g_vsim_sleep.n_holds; i++) {
        if (g_vsim_sleep.holds[i] != g) continue;
        g_vsim_sleep.holds[i] = g_vsim_sleep.holds[--g_vsim_sleep.n_holds];
        return;
    }
#else
    (void)g;
#endif
}

static void mirror_hold_clear(void)
{
#ifdef GPIO_HAVE_VSIM_SLEEP
    if (&g_vsim_sleep == NULL) return;
    g_vsim_sleep.n_holds = 0;
#endif
}

/* ------------------------------------------------------------ *
 *  Configuration
 * ------------------------------------------------------------ */
esp_err_t gpio_config(const gpio_config_t *c)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    for (int g = 0; g < 64; g++) {
        if (!((c->pin_bit_mask >> g) & 1ULL)) continue;
        if (!pin_valid(g) || (unsigned)c->intr_type >= GPIO_INTR_MAX)
            return ESP_ERR_INVALID_ARG;
        const bool out = (c->mode & GPIO_MODE_DEF_OUTPUT) != 0;
        vb_tripwire(g, out);
        pthread_mutex_lock(&s_lock);
        s_pins[g].mode      = (uint8_t)c->mode;
        s_pins[g].pull_up   = (c->pull_up_en == GPIO_PULLUP_ENABLE);
        s_pins[g].pull_down = (c->pull_down_en == GPIO_PULLDOWN_ENABLE);
        s_pins[g].intr_type = (uint8_t)c->intr_type;
        s_pins[g].intr_en   = (c->intr_type != GPIO_INTR_DISABLE);
        pthread_mutex_unlock(&s_lock);
    }
    return ESP_OK;
}

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    pin_clear(&s_pins[g]);
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    vb_tripwire(g, (mode & GPIO_MODE_DEF_OUTPUT) != 0);
    pthread_mutex_lock(&s_lock);
    s_pins[g].mode = (uint8_t)mode;
    if (mode == GPIO_MODE_DISABLE) {
        s_pins[g].intr_en   = 0;
        s_pins[g].intr_type = GPIO_INTR_DISABLE;
    }
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    vb_tripwire(g, false);   /* input-side config: straps OK */
    pthread_mutex_lock(&s_lock);
    s_pins[g].pull_up   = (pull == GPIO_PULLUP_ONLY || pull == GPIO_PULLUP_PULLDOWN);
    s_pins[g].pull_down = (pull == GPIO_PULLDOWN_ONLY || pull == GPIO_PULLUP_PULLDOWN);
    pthread_mutex_unlock(&s_lock);
    /* internal pull = weak board bias: resolves the level when the line
     * floats (e.g. the shared-MISO float check); active drivers win.  */
    if (pull == GPIO_PULLUP_ONLY)
        vboard_gpio_pull(g, 1);
    else if (pull == GPIO_PULLDOWN_ONLY)
        vboard_gpio_pull(g, 0);
    return ESP_OK;
}

esp_err_t gpio_pullup_en(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    vb_tripwire(g, false);
    pthread_mutex_lock(&s_lock);
    s_pins[g].pull_up = 1;
    pthread_mutex_unlock(&s_lock);
    vboard_gpio_pull(g, 1);              /* weak bias resolves the line */
    return ESP_OK;
}

esp_err_t gpio_pullup_dis(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].pull_up = 0;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_pulldown_en(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    vb_tripwire(g, false);
    pthread_mutex_lock(&s_lock);
    s_pins[g].pull_down = 1;
    pthread_mutex_unlock(&s_lock);
    vboard_gpio_pull(g, 0);              /* weak bias resolves the line */
    return ESP_OK;
}

esp_err_t gpio_pulldown_dis(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].pull_down = 0;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_set_drive_capability(gpio_num_t gpio_num, gpio_drive_cap_t s)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g) || s >= GPIO_DRIVE_CAP_MAX) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].drive_cap = (uint8_t)s;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_get_drive_capability(gpio_num_t gpio_num, gpio_drive_cap_t *s)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g) || !s) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    *s = (gpio_drive_cap_t)s_pins[g].drive_cap;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_iomux_in(uint32_t gpio_num, uint32_t signal_idx)
{
    if (!pin_valid((int)gpio_num)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[gpio_num].iomux_in_sig = (int)signal_idx + 1;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_iomux_out(uint8_t gpio_num, int func, bool oen_inv)
{
    (void)oen_inv;   /* recorded nowhere meaningful in the sim */
    if (!pin_valid((int)gpio_num)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[gpio_num].iomux_out_fn = func + 1;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

/* ------------------------------------------------------------ *
 *  Levels — the vboard coupling.
 * ------------------------------------------------------------ */
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    vb_tripwire(g, true);              /* driving a pad = output op   */
    pthread_mutex_lock(&s_lock);
    s_pins[g].level = level ? 1 : 0;
    pthread_mutex_unlock(&s_lock);
    vboard_gpio_from_firmware(g, level ? 1 : 0);   /* ESP -> board     */
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return 0;
    return vboard_gpio_get(g);         /* board/ESP read             */
}

/* ------------------------------------------------------------ *
 *  Interrupts
 * ------------------------------------------------------------ */
esp_err_t gpio_set_intr_type(gpio_num_t gpio_num, gpio_int_type_t intr_type)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g) || intr_type >= GPIO_INTR_MAX || intr_type < 0)
        return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].intr_type = (uint8_t)intr_type;
    s_pins[g].intr_en   = (intr_type != GPIO_INTR_DISABLE);
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_intr_enable(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].intr_en = 1;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_intr_disable(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].intr_en = 0;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_install_isr_service(int intr_alloc_flags)
{
    pthread_mutex_lock(&s_lock);
    if (s_isr_service) {
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_INVALID_STATE;   /* "already up" — firmware treats
                                           this as benign               */
    }
    s_isr_service = true;
    s_isr_flags   = intr_alloc_flags;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

void gpio_uninstall_isr_service(void)
{
    pthread_mutex_lock(&s_lock);
    s_isr_service = false;
    s_isr_flags   = 0;
    s_gisr        = NULL;
    s_gisr_arg    = NULL;
    for (int g = 0; g < N_PINS; g++) {
        s_pins[g].isr      = NULL;
        s_pins[g].isr_arg  = NULL;
        s_pins[g].intr_en  = 0;
    }
    pthread_mutex_unlock(&s_lock);
}

esp_err_t gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g) || !isr_handler) return ESP_ERR_INVALID_ARG;
    esp_err_t rc = ESP_OK;
    pthread_mutex_lock(&s_lock);
    if (!s_isr_service) {
        rc = ESP_ERR_INVALID_STATE;   /* service not installed          */
    } else if (s_pins[g].isr) {
        rc = ESP_ERR_INVALID_STATE;   /* slot busy — selftest falls back */
    } else {
        s_pins[g].isr     = isr_handler;
        s_pins[g].isr_arg = args;
    }
    pthread_mutex_unlock(&s_lock);
    return rc;
}

esp_err_t gpio_isr_handler_remove(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].isr     = NULL;
    s_pins[g].isr_arg = NULL;
    s_pins[g].intr_en = 0;      /* IDF masks the pin on remove          */
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_isr_register(void (*fn)(void *), void *arg,
                            int intr_alloc_flags, gpio_isr_handle_t *handle)
{
    if (!fn) return ESP_ERR_INVALID_ARG;
    static int s_gisr_cookie;
    pthread_mutex_lock(&s_lock);
    s_gisr       = fn;
    s_gisr_arg   = arg;
    s_gisr_flags = intr_alloc_flags;
    pthread_mutex_unlock(&s_lock);
    if (handle) *handle = (gpio_isr_handle_t)&s_gisr_cookie;
    return ESP_OK;
}

/* Deliver one edge.  edge: GPIO_INTR_POSEDGE / GPIO_INTR_NEGEDGE
 * (level-triggered types are mapped onto the corresponding edge).
 * Runs the registered handler on the CALLER's thread (the vboard tick
 * thread) with the lock dropped, so the ISR can freely call back into
 * gpio_intr_disable() etc. without deadlocking.                      */
static void deliver_edge(int g, int edge)
{
    gpio_isr_t isr;
    void      *arg;
    void     (*gisr)(void *);
    void      *garg;

    pthread_mutex_lock(&s_lock);
    const pin_t *p = &s_pins[g];
    bool fire = p->intr_en && p->isr &&
        ((p->intr_type == GPIO_INTR_ANYEDGE) ||
         (p->intr_type == GPIO_INTR_POSEDGE  && edge == GPIO_INTR_POSEDGE) ||
         (p->intr_type == GPIO_INTR_NEGEDGE  && edge == GPIO_INTR_NEGEDGE) ||
         (p->intr_type == GPIO_INTR_HIGH_LEVEL && edge == GPIO_INTR_POSEDGE) ||
         (p->intr_type == GPIO_INTR_LOW_LEVEL  && edge == GPIO_INTR_NEGEDGE));
    isr  = fire ? p->isr : NULL;
    arg  = p->isr_arg;
    gisr = s_gisr;         /* global (gpio_isr_register) slot runs on
                            every delivered edge, enabled or not — it
                            is the raw GPIO interrupt source handler  */
    garg = s_gisr_arg;
    pthread_mutex_unlock(&s_lock);

    if (gisr) gisr(garg);
    if (isr)  isr(arg);
}

void gpio_sim_irq(int gpio, int level)
{
    if (!pin_valid(gpio)) return;
    if (level < 0) {                      /* -1 = both edges          */
        deliver_edge(gpio, GPIO_INTR_NEGEDGE);
        deliver_edge(gpio, GPIO_INTR_POSEDGE);
    } else if (level == 0) {
        deliver_edge(gpio, GPIO_INTR_NEGEDGE);
    } else {
        deliver_edge(gpio, GPIO_INTR_POSEDGE);
    }
}

/* ------------------------------------------------------------ *
 *  Wake / hold / sleep-domain state — recorded for the sleep shim.
 * ------------------------------------------------------------ */
esp_err_t gpio_wakeup_enable(gpio_num_t gpio_num, gpio_int_type_t intr_type)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    if (intr_type != GPIO_INTR_LOW_LEVEL && intr_type != GPIO_INTR_HIGH_LEVEL)
        return ESP_ERR_INVALID_ARG;      /* IDF: wake sources are levels */
    pthread_mutex_lock(&s_lock);
    s_pins[g].wake_type = (int)intr_type;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_wakeup_disable(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].wake_type = 0;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_hold_en(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].hold = 1;
    mirror_hold_add(g);        /* sleep shim reads g_vsim_sleep.holds  */
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_hold_dis(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].hold = 0;
    mirror_hold_del(g);
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

void gpio_deep_sleep_hold_en(void)
{
    pthread_mutex_lock(&s_lock);
    s_deep_sleep_hold = true;
    pthread_mutex_unlock(&s_lock);
}

void gpio_deep_sleep_hold_dis(void)
{
    pthread_mutex_lock(&s_lock);
    s_deep_sleep_hold = false;
    pthread_mutex_unlock(&s_lock);
}

esp_err_t gpio_sleep_sel_en(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].sleep_sel = 1;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_sleep_sel_dis(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].sleep_sel = 0;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_sleep_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].sleep_mode = (uint8_t)mode;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_sleep_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].sleep_pull = (uint8_t)pull;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_sleep_hold_en(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].sleep_hold = 1;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_sleep_hold_dis(gpio_num_t gpio_num)
{
    const int g = (int)gpio_num;
    if (!pin_valid(g)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    s_pins[g].sleep_hold = 0;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_force_hold_all(void)
{
    pthread_mutex_lock(&s_lock);
    for (int g = 0; g < N_PINS; g++)
        if (pin_valid(g)) s_pins[g].hold = 1;
    mirror_hold_clear();
    for (int g = 0; g < N_PINS; g++)
        if (pin_valid(g)) mirror_hold_add(g);
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t gpio_force_unhold_all(void)
{
    pthread_mutex_lock(&s_lock);
    for (int g = 0; g < N_PINS; g++)
        if (pin_valid(g)) s_pins[g].hold = 0;
    mirror_hold_clear();
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

/* ------------------------------------------------------------ *
 *  Sim-only introspection (SIM_HOST_BUILD).
 * ------------------------------------------------------------ */
#ifdef SIM_HOST_BUILD
bool gpio_sim_is_held(int gpio)
{
    if (!pin_valid(gpio)) return false;
    pthread_mutex_lock(&s_lock);
    bool h = s_pins[gpio].hold != 0;
    pthread_mutex_unlock(&s_lock);
    return h;
}

int gpio_sim_hold_pins(int *gpios_out, int max)
{
    pthread_mutex_lock(&s_lock);
    int n = 0;
    for (int g = 0; g < N_PINS; g++) {
        if (!s_pins[g].hold) continue;
        if (gpios_out && n < max) gpios_out[n] = g;
        n++;
    }
    pthread_mutex_unlock(&s_lock);
    return n;
}

bool gpio_sim_deep_sleep_hold(void)
{
    pthread_mutex_lock(&s_lock);
    bool h = s_deep_sleep_hold;
    pthread_mutex_unlock(&s_lock);
    return h;
}

int gpio_sim_wakeup_type(int gpio)
{
    if (!pin_valid(gpio)) return -1;
    pthread_mutex_lock(&s_lock);
    int t = s_pins[gpio].wake_type;
    pthread_mutex_unlock(&s_lock);
    return t ? t : -1;
}

int gpio_sim_mode(int gpio)
{
    if (!pin_valid(gpio)) return -1;
    pthread_mutex_lock(&s_lock);
    int m = s_pins[gpio].mode;
    pthread_mutex_unlock(&s_lock);
    return m;
}

int gpio_sim_pull(int gpio)
{
    if (!pin_valid(gpio)) return -1;
    pthread_mutex_lock(&s_lock);
    int p = (s_pins[gpio].pull_up ? 1 : 0) | (s_pins[gpio].pull_down ? 2 : 0);
    pthread_mutex_unlock(&s_lock);
    return p;
}

int gpio_sim_iomux_in(int gpio)
{
    if (!pin_valid(gpio)) return -1;
    pthread_mutex_lock(&s_lock);
    int s = s_pins[gpio].iomux_in_sig;
    pthread_mutex_unlock(&s_lock);
    return s ? s - 1 : -1;
}

int gpio_sim_iomux_out(int gpio)
{
    if (!pin_valid(gpio)) return -1;
    pthread_mutex_lock(&s_lock);
    int s = s_pins[gpio].iomux_out_fn;
    pthread_mutex_unlock(&s_lock);
    return s ? s - 1 : -1;
}

void gpio_sim_reset(void)
{
    pthread_mutex_lock(&s_lock);
    memset(s_pins, 0, sizeof(s_pins));
    for (int g = 0; g < N_PINS; g++) {
        s_pins[g].drive_cap = GPIO_DRIVE_CAP_DEFAULT;
    }
    s_isr_service    = false;
    s_isr_flags      = 0;
    s_gisr           = NULL;
    s_gisr_arg       = NULL;
    s_gisr_flags     = 0;
    s_deep_sleep_hold = false;
    mirror_hold_clear();
    /* g_gpio_forbidden_cfg intentionally survives a reset — it is a
     * run-scoped safety counter like g_vb_stats, not pad state.       */
    pthread_mutex_unlock(&s_lock);
}
#endif /* SIM_HOST_BUILD */
