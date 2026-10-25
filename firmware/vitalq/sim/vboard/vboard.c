/* vboard.c — virtual fix_v22 board core (contract: vboard.h).
 *
 * Implements the virtual clock, the tick/IRQ engine, the GPIO level
 * table, the power-rail model with hardware couplings, the device
 * registry, and the fault/stat globals.
 *
 * Threading model: every table has its own mutex; model code and
 * registered callbacks are ALWAYS invoked with vboard locks released
 * (callbacks may re-enter vboard — e.g. TCA6408's reset() looks up
 * siblings via vboard_find_name).  The single spawned thread is the
 * tick thread; it both calls dev->tick() and drains the IRQ queue so
 * firmware ISRs execute with real concurrent FromISR semantics.
 */
#include "vboard.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Tunables                                                            */
#define VB_GPIO_N           64      /* ESP32-S3 gpio range             */
#define VB_MAX_CBS          8       /* callbacks per gpio / per rail   */
#define VB_IRQ_QSIZE        256     /* pending IRQ ring depth          */
#define VB_TICK_DEFAULT_US  500     /* default tick period, virtual µs */
#define VB_MIN_SLEEP_US     50      /* real-sleep granularity floor    */
#define VB_TICK_MAXREAL_US  20000   /* tick sleep cap — keep stop live */
#define VB_VBAT_CUTOFF_MV   3000    /* ~VQ_BATT_EMERG_MV: cell cutoff  */
#define VB_VBAT_DEFAULT_MV  3900    /* charged LiPo at boot            */

/* ------------------------------------------------------------------ */
/* Contract globals (zero-init; benign defaults set in core_init_once) */
vboard_faults_t g_vb_faults;
vboard_stats_t  g_vb_stats;

void vboard_stats_reset(void) { memset(&g_vb_stats, 0, sizeof g_vb_stats); }

/* ------------------------------------------------------------------ */
/* One-time core init (lazy — every public entry point is safe to call
 * even if a scenario forgets vboard_init()).                          */
static void core_init_once(void);
static pthread_once_t s_core_once = PTHREAD_ONCE_INIT;
#define CORE_INIT() ((void)pthread_once(&s_core_once, core_init_once))
void vboard__init_core(void) { CORE_INIT(); }   /* used by vboard_init.c */

/* forward decls for helpers defined further down                       */
static void irq_flush_pending(void);

/* ------------------------------------------------------------------ */
/* Virtual clock:  virt_now = base + (mono - origin) * scale.
 * set_scale() rebases (origin, base) so now() is continuous — no jumps,
 * strictly monotonic across scale changes.                            */
static int64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static int64_t mono_us(void) { return mono_ns() / 1000; }

static pthread_mutex_t s_clk_mtx = PTHREAD_MUTEX_INITIALIZER;
static double  s_scale     = 1.0;   /* virtual µs per real µs          */
static int64_t s_origin_ns = 0;     /* real ns at last rebase          */
static int64_t s_base_us   = 0;     /* virtual µs at s_origin_ns       */

/* ns-precision origin: at 500x the virtual stamp resolution is ~0.5 µs,
 * so 16 kHz-class streams don't alias onto identical t_us values (which
 * the hub's non-monotonic counter would flag).                        */
static int64_t now_from_locked(int64_t m_ns)
{
    return s_base_us + (int64_t)((double)(m_ns - s_origin_ns) * s_scale / 1000.0);
}

int64_t vboard_now_us(void)
{
    CORE_INIT();
    int64_t m = mono_ns(), v;
    pthread_mutex_lock(&s_clk_mtx);
    v = now_from_locked(m);
    pthread_mutex_unlock(&s_clk_mtx);
    return v;
}

void vboard_clock_set_scale(double x)
{
    CORE_INIT();
    if (!(x > 0.0))
        return;                     /* reject zero/negative/NaN        */
    int64_t m = mono_ns();
    pthread_mutex_lock(&s_clk_mtx);
    int64_t v   = now_from_locked(m);   /* continuity point            */
    s_origin_ns = m;
    s_base_us   = v;
    s_scale     = x;
    pthread_mutex_unlock(&s_clk_mtx);
}

static double clock_scale(void)
{
    double x;
    pthread_mutex_lock(&s_clk_mtx);
    x = s_scale;
    pthread_mutex_unlock(&s_clk_mtx);
    return x;
}

void vboard_clock_sleep_us(int64_t virt_us)
{
    CORE_INIT();
    if (virt_us <= 0) {
        sched_yield();
        return;
    }
    int64_t real_us = (int64_t)(virt_us / clock_scale());
    if (real_us < VB_MIN_SLEEP_US)
        real_us = VB_MIN_SLEEP_US;      /* granularity floor is fine   */
    struct timespec ts = { real_us / 1000000,
                           (real_us % 1000000) * 1000 };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

/* ------------------------------------------------------------------ */
/* GPIO level table.
 * s_pull[] is the released (idle) level the pin returns to when a
 * writer passes level<0 — that is the open-drain "let go".  Board-side
 * devices drive lines with vboard_gpio_set(); firmware outputs arrive
 * through vboard_gpio_from_firmware() and additionally fire the
 * registered vboard_on_gpio_out callbacks (models hook e.g. MIC_CLK). */
static pthread_mutex_t s_gpio_mtx = PTHREAD_MUTEX_INITIALIZER;
static int8_t s_lvl [VB_GPIO_N];
static int8_t s_pull[VB_GPIO_N];
typedef struct { vboard_gpio_out_cb_t cb; void *ctx; } gcb_t;
static gcb_t s_gcb  [VB_GPIO_N][VB_MAX_CBS];
static int   s_gcb_n[VB_GPIO_N];

static int gpio_ok(int g) { return g >= 0 && g < VB_GPIO_N; }

static void gpio_drive(int gpio, int level)     /* level<0 = release   */
{
    if (!gpio_ok(gpio))
        return;
    pthread_mutex_lock(&s_gpio_mtx);
    s_lvl[gpio] = (int8_t)((level < 0) ? s_pull[gpio] : (level != 0));
    pthread_mutex_unlock(&s_gpio_mtx);
}

void vboard_gpio_set(int gpio, int level)       /* board -> line       */
{
    gpio_drive(gpio, level);
}

/* Firmware-side internal pull (weak bias): updates the pin's released
 * level AND the current level — on a floating input the pull wins;
 * anything actively driven by a model/firmware output overrides on its
 * next write.                                                          */
void vboard_gpio_pull(int gpio, int level)
{
    if (!gpio_ok(gpio))
        return;
    pthread_mutex_lock(&s_gpio_mtx);
    s_pull[gpio] = (int8_t)(level != 0);
    s_lvl[gpio]  = (int8_t)(level != 0);
    pthread_mutex_unlock(&s_gpio_mtx);
}

int vboard_gpio_get(int gpio)
{
    CORE_INIT();
    if (!gpio_ok(gpio))
        return -1;
    int v;
    pthread_mutex_lock(&s_gpio_mtx);
    v = s_lvl[gpio];
    pthread_mutex_unlock(&s_gpio_mtx);
    return v;
}

void vboard_gpio_from_firmware(int gpio, int level) /* ESP -> board    */
{
    CORE_INIT();
    if (!gpio_ok(gpio))
        return;
    gcb_t snap[VB_MAX_CBS];
    int n, v;
    pthread_mutex_lock(&s_gpio_mtx);
    s_lvl[gpio] = (int8_t)((level < 0) ? s_pull[gpio] : (level != 0));
    v = s_lvl[gpio];
    n = s_gcb_n[gpio];
    memcpy(snap, s_gcb[gpio], sizeof snap);
    pthread_mutex_unlock(&s_gpio_mtx);
    for (int i = 0; i < n; i++)
        snap[i].cb(gpio, v, snap[i].ctx);
}

void vboard_on_gpio_out(int gpio, vboard_gpio_out_cb_t cb, void *ctx)
{
    CORE_INIT();
    if (!gpio_ok(gpio) || !cb)
        return;
    pthread_mutex_lock(&s_gpio_mtx);
    int n = s_gcb_n[gpio];
    if (n < VB_MAX_CBS) {
        s_gcb[gpio][n].cb  = cb;
        s_gcb[gpio][n].ctx = ctx;
        s_gcb_n[gpio]      = n + 1;
    }
    pthread_mutex_unlock(&s_gpio_mtx);
}

/* ------------------------------------------------------------------ */
/* Power rails.  s_req[] = last requested state, s_eff[] = effective
 * state after the hardware couplings applied HERE (never in models):
 *   VBAT dies at the cell cutoff (VB_VBAT_CUTOFF_MV, set via mv);
 *   3V3 (TPS63802) dies with VBAT;  3V3_ANA tracks 3V3 (R95 tie);
 *   1V8 (TPS7A2018) is fed from 3V3; TX5 enable logic is 3V3-powered.
 * Models/tests read state with vboard_rail(); changes fire cbs.       */
static pthread_mutex_t s_rail_mtx = PTHREAD_MUTEX_INITIALIZER;
static bool s_req[VB_RAIL__N], s_eff[VB_RAIL__N];
static int  s_vbat_mv;
static bool s_vbat_ok;
typedef struct { vboard_rail_cb_t cb; void *ctx; } rcb_t;
static rcb_t s_rcb  [VB_RAIL__N][VB_MAX_CBS];
static int   s_rcb_n[VB_RAIL__N];

static bool eff_locked(vboard_rail_t r)
{
    switch (r) {
    case VB_RAIL_VBUS:    return s_req[VB_RAIL_VBUS];
    case VB_RAIL_VBAT:    return s_req[VB_RAIL_VBAT] && s_vbat_ok;
    case VB_RAIL_3V3:     return s_req[VB_RAIL_3V3]  && eff_locked(VB_RAIL_VBAT);
    case VB_RAIL_3V3_ANA: return eff_locked(VB_RAIL_3V3);
    case VB_RAIL_1V8:     return s_req[VB_RAIL_1V8]  && eff_locked(VB_RAIL_3V3);
    case VB_RAIL_TX5:     return s_req[VB_RAIL_TX5]  && eff_locked(VB_RAIL_3V3);
    default:              return false;
    }
}

/* recompute s_eff under the mutex; reports which rails moved          */
static int rails_sync_locked(vboard_rail_t changed[VB_RAIL__N],
                             bool          on[VB_RAIL__N])
{
    int n = 0;
    for (int r = 0; r < VB_RAIL__N; r++) {
        bool e = eff_locked((vboard_rail_t)r);
        if (e != s_eff[r]) {
            s_eff[r]   = e;
            changed[n] = (vboard_rail_t)r;
            on[n]      = e;
            n++;
        }
    }
    return n;
}

static void rails_fire(const vboard_rail_t *rs, const bool *on, int n)
{
    for (int i = 0; i < n; i++) {
        vboard_rail_t r = rs[i];
        rcb_t snap[VB_MAX_CBS];
        int m;
        pthread_mutex_lock(&s_rail_mtx);
        m = s_rcb_n[r];
        memcpy(snap, s_rcb[r], sizeof snap);
        pthread_mutex_unlock(&s_rail_mtx);
        for (int j = 0; j < m; j++)
            snap[j].cb(r, on[i], snap[j].ctx);
    }
}

void vboard_rail_set(vboard_rail_t r, bool on)
{
    CORE_INIT();
    if (r < 0 || r >= VB_RAIL__N)
        return;
    vboard_rail_t ch[VB_RAIL__N];
    bool          st[VB_RAIL__N];
    pthread_mutex_lock(&s_rail_mtx);
    s_req[r] = on;
    int n = rails_sync_locked(ch, st);
    pthread_mutex_unlock(&s_rail_mtx);
    rails_fire(ch, st, n);
}

bool vboard_rail(vboard_rail_t r)
{
    CORE_INIT();
    if (r < 0 || r >= VB_RAIL__N)
        return false;
    bool e;
    pthread_mutex_lock(&s_rail_mtx);
    e = s_eff[r];
    pthread_mutex_unlock(&s_rail_mtx);
    return e;
}

void vboard_vbat_set_mv(int mv)
{
    CORE_INIT();
    vboard_rail_t ch[VB_RAIL__N];
    bool          st[VB_RAIL__N];
    int           n;
    bool          vbat_fired = false;
    pthread_mutex_lock(&s_rail_mtx);
    s_vbat_mv = mv;
    s_vbat_ok = (mv >= VB_VBAT_CUTOFF_MV);
    n = rails_sync_locked(ch, st);
    /* "stores + notifies": even when the rail state doesn't move, VBAT
     * subscribers (e.g. the gauge model) want the mv update — fire its
     * cbs once so they can re-read vboard_vbat_mv().                  */
    for (int i = 0; i < n; i++)
        if (ch[i] == VB_RAIL_VBAT)
            vbat_fired = true;
    if (!vbat_fired) {
        ch[n] = VB_RAIL_VBAT;
        st[n] = s_eff[VB_RAIL_VBAT];
        n++;
    }
    pthread_mutex_unlock(&s_rail_mtx);
    rails_fire(ch, st, n);
}

int vboard_vbat_mv(void)
{
    CORE_INIT();
    int mv;
    pthread_mutex_lock(&s_rail_mtx);
    mv = s_vbat_mv;
    pthread_mutex_unlock(&s_rail_mtx);
    return mv;
}

void vboard_on_rail(vboard_rail_t r, vboard_rail_cb_t cb, void *ctx)
{
    CORE_INIT();
    if (r < 0 || r >= VB_RAIL__N || !cb)
        return;
    pthread_mutex_lock(&s_rail_mtx);
    int n = s_rcb_n[r];
    if (n < VB_MAX_CBS) {
        s_rcb[r][n].cb  = cb;
        s_rcb[r][n].ctx = ctx;
        s_rcb_n[r]      = n + 1;
    }
    pthread_mutex_unlock(&s_rail_mtx);
}

/* ------------------------------------------------------------------ */
/* Device registry — singly linked list of vboard_dev_t*.
 * Nodes are never freed; register() dedups/replaces by dev->name so
 * tests may re-register models.  Lookups hold the mutex for the scan;
 * iteration for tick/reset releases it around each callback.          */
typedef struct regnode { vboard_dev_t *dev; struct regnode *next; } regnode_t;
static regnode_t       *s_devs, *s_devs_tail;
static pthread_mutex_t  s_reg_mtx = PTHREAD_MUTEX_INITIALIZER;

void vboard_register(vboard_dev_t *dev)
{
    CORE_INIT();
    if (!dev)
        return;
    pthread_mutex_lock(&s_reg_mtx);
    if (dev->name) {
        for (regnode_t *n = s_devs; n; n = n->next) {
            if (n->dev->name && strcmp(n->dev->name, dev->name) == 0) {
                n->dev = dev;               /* re-register = replace   */
                pthread_mutex_unlock(&s_reg_mtx);
                return;
            }
        }
    }
    regnode_t *n = malloc(sizeof *n);
    if (n) {
        n->dev  = dev;
        n->next = NULL;
        if (s_devs_tail)
            s_devs_tail->next = n;
        else
            s_devs = n;
        s_devs_tail = n;
    }
    pthread_mutex_unlock(&s_reg_mtx);
}

vboard_dev_t *vboard_find_i2c(uint8_t addr)
{
    CORE_INIT();
    vboard_dev_t *hit = NULL;
    pthread_mutex_lock(&s_reg_mtx);
    for (regnode_t *n = s_devs; n; n = n->next) {
        /* i2c_addr==0 means "not on I2C" — never matches (so a missing
         * optional device such as 0x4A simply yields NULL).           */
        if (n->dev->i2c_addr && n->dev->i2c_addr == addr) {
            hit = n->dev;
            break;
        }
    }
    pthread_mutex_unlock(&s_reg_mtx);
    return hit;
}

vboard_dev_t *vboard_find_spi(int cs_gpio)
{
    CORE_INIT();
    if (cs_gpio < 0)
        return NULL;
    vboard_dev_t *hit = NULL;
    pthread_mutex_lock(&s_reg_mtx);
    for (regnode_t *n = s_devs; n; n = n->next) {
        if (n->dev->spi_cs == cs_gpio) {
            hit = n->dev;
            break;
        }
    }
    pthread_mutex_unlock(&s_reg_mtx);
    return hit;
}

vboard_dev_t *vboard_find_name(const char *name)
{
    CORE_INIT();
    if (!name)
        return NULL;
    vboard_dev_t *hit = NULL;
    pthread_mutex_lock(&s_reg_mtx);
    for (regnode_t *n = s_devs; n; n = n->next) {
        if (n->dev->name && strcmp(n->dev->name, name) == 0) {
            hit = n->dev;
            break;
        }
    }
    pthread_mutex_unlock(&s_reg_mtx);
    return hit;
}

/* Walk the list running cb(dev,arg) with the registry mutex RELEASED
 * around each callback (callbacks may re-enter vboard).  Nodes are
 * never freed and appends only touch the tail, so re-reading n->next
 * under the lock is race-safe and even picks up mid-pass additions.   */
static void devs_for_each(void (*cb)(vboard_dev_t *, void *), void *arg)
{
    pthread_mutex_lock(&s_reg_mtx);
    regnode_t *n = s_devs;
    while (n) {
        vboard_dev_t *d = n->dev;
        pthread_mutex_unlock(&s_reg_mtx);
        cb(d, arg);
        pthread_mutex_lock(&s_reg_mtx);
        n = n->next;
    }
    pthread_mutex_unlock(&s_reg_mtx);
}

static void reset_one(vboard_dev_t *d, void *arg)
{
    (void)arg;
    if (d->reset)
        d->reset(d);
}

void vboard_reset_all(void)
{
    CORE_INIT();
    /* ESP_EN pulse: undelivered IRQ edges die with the old run...     */
    irq_flush_pending();
    /* ...every ESP output goes Hi-Z, so all lines fall back to pulls. */
    pthread_mutex_lock(&s_gpio_mtx);
    memcpy(s_lvl, s_pull, sizeof s_lvl);
    pthread_mutex_unlock(&s_gpio_mtx);
    /* ...and each model returns to power-on state.                    */
    devs_for_each(reset_one, NULL);
}

/* ------------------------------------------------------------------ */
/* IRQ engine.  Models enqueue edges with vboard_irq_pulse(); the tick
 * thread drains the ring and calls the ISR sink so FromISR code runs
 * on a real concurrent thread.  Sink resolution order:
 *   1) explicit sink registered via vboard_set_isr_sink()
 *   2) weak symbol gpio_sim_irq() provided by the gpio shim
 *   3) none → edges are dropped (counted in stats either way).
 * level -1 = toggle pulse: the line is driven to its opposite level,
 * the ISR edge fires, and the line is restored (plus restore edge)
 * after that tick iteration's dev ticks — "set then restore after
 * tick".                                                              */
__attribute__((weak)) extern void gpio_sim_irq(int gpio, int level);
typedef void (*vboard_isr_sink_t)(int gpio, int level);
static vboard_isr_sink_t s_isr_sink;
void vboard_set_isr_sink(vboard_isr_sink_t fn) { s_isr_sink = fn; }

static struct { int gpio, level; } s_irq_q[VB_IRQ_QSIZE];
static int             s_irq_head, s_irq_count;
static pthread_mutex_t s_irq_mtx = PTHREAD_MUTEX_INITIALIZER;
/* release edges collected during a drain — tick-thread private        */
static int s_rest[VB_IRQ_QSIZE][2];
static int s_nrest;

void vboard_irq_pulse(int gpio, int level)
{
    CORE_INIT();
    if (!gpio_ok(gpio))
        return;
    pthread_mutex_lock(&s_irq_mtx);
    if (s_irq_count < VB_IRQ_QSIZE) {
        int tail = (s_irq_head + s_irq_count) % VB_IRQ_QSIZE;
        s_irq_q[tail].gpio  = gpio;
        s_irq_q[tail].level = level;
        s_irq_count++;
    }                                       /* full queue: drop edge   */
    pthread_mutex_unlock(&s_irq_mtx);
}

static void irq_flush_pending(void)
{
    pthread_mutex_lock(&s_irq_mtx);
    s_irq_head  = 0;
    s_irq_count = 0;
    pthread_mutex_unlock(&s_irq_mtx);
}

static void fire_isr(int gpio, int level)
{
    vboard_isr_sink_t s = s_isr_sink;
    if (!s && &gpio_sim_irq)
        s = gpio_sim_irq;
    if (s)
        s(gpio, level);
    g_vb_stats.irq_pulses++;
}

static void irq_drain(void)                     /* tick thread only    */
{
    for (;;) {
        int g, l;
        pthread_mutex_lock(&s_irq_mtx);
        if (!s_irq_count) {
            pthread_mutex_unlock(&s_irq_mtx);
            break;
        }
        g = s_irq_q[s_irq_head].gpio;
        l = s_irq_q[s_irq_head].level;
        s_irq_head = (s_irq_head + 1) % VB_IRQ_QSIZE;
        s_irq_count--;
        pthread_mutex_unlock(&s_irq_mtx);

        if (l < 0) {                            /* toggle pulse        */
            int cur = vboard_gpio_get(g);
            int a   = !cur;
            gpio_drive(g, a);
            fire_isr(g, a);
            if (s_nrest < VB_IRQ_QSIZE) {
                s_rest[s_nrest][0] = g;
                s_rest[s_nrest][1] = cur;
                s_nrest++;
            }
        } else {
            gpio_drive(g, l & 1);
            fire_isr(g, l & 1);
        }
    }
}

static void irq_restore(void)                   /* tick thread only    */
{
    for (int i = 0; i < s_nrest; i++) {
        gpio_drive(s_rest[i][0], s_rest[i][1]);
        fire_isr(s_rest[i][0], s_rest[i][1]);
    }
    s_nrest = 0;
}

/* ------------------------------------------------------------------ */
/* Tick thread: each period -> drain IRQ queue, call every dev->tick
 * with one shared virtual timestamp, apply pulse restores, then sleep
 * period/scale real time (recomputed every loop — adaptive to live
 * scale changes).  period_us <= 0 selects VB_TICK_DEFAULT_US.         */
static pthread_mutex_t s_tick_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_tick_cond;
static pthread_t       s_tick_thr;
static int             s_tick_running, s_tick_started;
static int64_t         s_tick_period_us = VB_TICK_DEFAULT_US;

static void tick_one(vboard_dev_t *d, void *arg)
{
    if (d->tick)
        d->tick(d, *(const int64_t *)arg);
}

static void *tick_main(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&s_tick_mtx);
        if (!s_tick_running) {
            pthread_mutex_unlock(&s_tick_mtx);
            break;
        }
        int64_t period_v = s_tick_period_us;
        pthread_mutex_unlock(&s_tick_mtx);

        int64_t now = vboard_now_us();
        irq_drain();                        /* deliver assert edges     */
        devs_for_each(tick_one, &now);      /* async device behaviour   */
        irq_restore();                      /* pulse release edges      */

        int64_t real_us = (int64_t)(period_v / clock_scale());
        if (real_us < VB_MIN_SLEEP_US)
            real_us = VB_MIN_SLEEP_US;
        if (real_us > VB_TICK_MAXREAL_US)
            real_us = VB_TICK_MAXREAL_US;   /* stay responsive          */

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec  += real_us / 1000000;
        ts.tv_nsec += (real_us % 1000000) * 1000;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_mutex_lock(&s_tick_mtx);
        if (s_tick_running)
            pthread_cond_timedwait(&s_tick_cond, &s_tick_mtx, &ts);
        pthread_mutex_unlock(&s_tick_mtx);
    }
    return NULL;
}

void vboard_tick_start(int64_t period_us)
{
    CORE_INIT();
    pthread_mutex_lock(&s_tick_mtx);
    if (period_us > 0)
        s_tick_period_us = period_us;
    if (!s_tick_running) {
        s_tick_running = 1;
        if (pthread_create(&s_tick_thr, NULL, tick_main, NULL) == 0)
            s_tick_started = 1;
        else
            s_tick_running = 0;
    }
    /* wake the loop so a period update takes effect immediately        */
    pthread_cond_broadcast(&s_tick_cond);
    pthread_mutex_unlock(&s_tick_mtx);
}

void vboard_tick_stop(void)
{
    pthread_mutex_lock(&s_tick_mtx);
    s_tick_running = 0;
    pthread_cond_broadcast(&s_tick_cond);
    int join = s_tick_started;
    pthread_mutex_unlock(&s_tick_mtx);
    if (join) {
        pthread_join(s_tick_thr, NULL);
        pthread_mutex_lock(&s_tick_mtx);
        s_tick_started = 0;
        pthread_mutex_unlock(&s_tick_mtx);
    }
    irq_flush_pending();
    s_nrest = 0;
}

/* ------------------------------------------------------------------ */
/* TCA6408 expander bridge — optional weak hooks provided by the
 * device model.  Absent model -> vboard_exp_pin returns -1.           */
__attribute__((weak)) extern int  vbdev_tca6408_get_pin(int p);
__attribute__((weak)) extern void vbdev_tca6408_set_input(int p, int level);

int vboard_exp_pin(int p)
{
    CORE_INIT();
    if (&vbdev_tca6408_get_pin)
        return vbdev_tca6408_get_pin(p);
    return -1;
}

void vboard_exp_set_input(int p, int level)
{
    CORE_INIT();
    if (&vbdev_tca6408_set_input)
        vbdev_tca6408_set_input(p, level);
}

/* ------------------------------------------------------------------ */
/* One-time core defaults: RNG seed, env clock scale, GPIO idle levels,
 * rail defaults, benign fault defaults, tick condvar (CLOCK_MONOTONIC).*/
static void core_init_once(void)
{
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&s_tick_cond, &ca);
    pthread_condattr_destroy(&ca);

    srand((unsigned)(time(NULL) ^ ((long)getpid() << 16) ^ mono_us()));

    const char *e = getenv("VSIM_TIME_SCALE");
    double x = e ? atof(e) : 1.0;
    if (!(x > 0.0))
        x = 1.0;
    s_scale     = x;
    s_origin_ns = mono_ns();
    s_base_us   = 0;

    /* GPIO idle levels (board pulls, fix_v22):
     *  HIGH: IO0 BOOT strap, IO3 JTAG strap/EXP_INT, open-drain INT
     *       lines released (TMP117 5, gauge 6, ~PG 7, MAX86141 39),
     *       ADS1292 ~DRDY 2, AD5940 GPIO0 14 released, I2C 8/9,
     *       every SPI CS pull-up {10,15,16,18,38}.
     *  LOW (zero-init — listed for the record): IO45/IO46 straps,
     *       LSM6 INT1 4 and AFE4900 RDY 21 idle low, spare 42.        */
    static const int8_t pull_hi[] = {
        0, 2, 3, 5, 6, 7, 8, 9, 10, 14, 15, 16, 18, 38, 39
    };
    for (size_t i = 0; i < sizeof pull_hi; i++) {
        s_pull[pull_hi[i]] = 1;
        s_lvl[pull_hi[i]]  = 1;
    }

    s_req[VB_RAIL_VBUS]    = false;
    s_req[VB_RAIL_VBAT]    = true;
    s_req[VB_RAIL_3V3]     = true;
    s_req[VB_RAIL_3V3_ANA] = true;
    s_req[VB_RAIL_1V8]     = true;
    s_req[VB_RAIL_TX5]     = false;
    s_vbat_mv = VB_VBAT_DEFAULT_MV;
    s_vbat_ok = true;
    for (int r = 0; r < VB_RAIL__N; r++)
        s_eff[r] = eff_locked((vboard_rail_t)r);

    memset(&g_vb_faults, 0, sizeof g_vb_faults);
    g_vb_faults.heap_fail_after = -1;         /* -1 = off              */
    g_vb_faults.spi_stuck_cs    = -1;         /* -1 = off              */
    memset(&g_vb_stats, 0, sizeof g_vb_stats);
}
