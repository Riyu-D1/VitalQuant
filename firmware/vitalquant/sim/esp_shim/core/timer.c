/* Host-sim esp_timer — all esp_timer callbacks run on ONE dedicated
 * thread, serialized exactly like the real esp_timer task.
 *
 * Timing model
 * ------------
 * Expiries are tracked on the VIRTUAL vboard clock (vboard_now_us()).
 * The thread waits on a condition variable; the wait duration is the
 * virtual time to the next expiry converted to wall time with a running
 * estimate of the current time scale (the contract exposes no scale
 * getter, so the thread measures it from wall-vs-virtual deltas and
 * re-converges within one bounded wait after any change — i.e. when a
 * scenario bumps VSIM_TIME_SCALE the pending waits re-arm, bounded by
 * MAX_WAIT_WALL_US).  Waits are also interrupted by a broadcast from
 * create/start/stop/delete so a newly armed earlier deadline takes
 * effect immediately.
 *
 * Reentrancy
 * ----------
 * Callbacks run with no lock held, so a callback may legally call
 * esp_timer_start_once/stop/delete on itself or any other timer.  A timer
 * deleted while its callback is executing is freed only after the
 * callback returns.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

#include "esp_timer.h"
#include "vboard.h"

struct esp_timer {
    esp_timer_cb_t       cb;
    void                *arg;
    esp_timer_dispatch_t dispatch;
    char                 name[24];
    bool                 skip_unhandled;
    uint64_t             period;    /* µs; 0 = one-shot */
    int64_t              expiry;    /* virtual µs       */
    bool                 armed;     /* in the armed set (counts in scans) */
    bool                 firing;    /* callback currently running         */
    bool                 dead;      /* deleted while firing -> deferred   */
    bool                 stop_req;  /* stop()/delete() seen during firing */
    struct esp_timer    *next;
};

static pthread_mutex_t s_mtx       = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_cv        = PTHREAD_COND_INITIALIZER;
static pthread_once_t  s_once      = PTHREAD_ONCE_INIT;
static struct esp_timer *s_timers;            /* all live timers (armed or not) */
static double          s_wall_per_virt = 1.0; /* est. wall µs per virtual µs    */

/* Wait clamps: keep the thread responsive to scale changes without
 * spinning when a deadline is imminent. */
#define MIN_WAIT_WALL_US 40
#define MAX_WAIT_WALL_US 5000
/* Max catch-up firings per wake for a periodic that fell far behind. */
#define MAX_CATCHUP_FIRES 32

static uint64_t wall_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static void timer_insert_locked(struct esp_timer *t)
{
    t->next  = s_timers;
    s_timers = t;
}

static void timer_remove_locked(struct esp_timer *t)
{
    struct esp_timer **pp = &s_timers;
    while (*pp) {
        if (*pp == t) { *pp = t->next; return; }
        pp = &(*pp)->next;
    }
}

/* Earliest armed expiry <= limit, or the minimum armed expiry overall
 * when limit == INT64_MAX semantics are wanted via `any` flag. */
static struct esp_timer *find_expired_locked(int64_t now)
{
    struct esp_timer *best = NULL;
    for (struct esp_timer *t = s_timers; t; t = t->next) {
        if (!t->armed || t->expiry > now) continue;
        if (!best || t->expiry < best->expiry) best = t;
    }
    return best;
}

static int64_t next_armed_expiry_locked(void)
{
    int64_t best = INT64_MAX;
    for (struct esp_timer *t = s_timers; t; t = t->next) {
        if (t->armed && t->expiry < best) best = t->expiry;
    }
    return best;
}

/* Compute the next expiry for a periodic timer that just fired at
 * `now`.  Honours skip_unhandled_events; bounds catch-up so a timer can
 * never livelock the thread. */
static void schedule_periodic(struct esp_timer *t, int64_t now)
{
    int64_t next = t->expiry + (int64_t)t->period;
    int     steps = 0;
    while (next <= now) {
        if (t->skip_unhandled || ++steps > MAX_CATCHUP_FIRES) {
            next = now + (int64_t)t->period;
            break;
        }
        next += (int64_t)t->period;
    }
    t->expiry = next;
}

static void *timer_thread_main(void *arg)
{
    int64_t  last_virt = vboard_now_us();
    uint64_t last_wall = wall_now_us();
    (void)arg;

    for (;;) {
        const int64_t  now     = vboard_now_us();
        const uint64_t wnow    = wall_now_us();
        const int64_t  d_virt  = now - last_virt;
        const int64_t  d_wall  = (int64_t)(wnow - last_wall);
        last_virt = now;
        last_wall = wnow;

        /* Refine the wall-per-virtual-µs estimate (only when both clocks
         * measurably advanced).  This is how the thread notices a scale
         * change: the estimate re-converges after at most one bounded
         * wait, then deadlines are re-armed at the new scale. */
        if (d_virt > 0 && d_wall > 0) {
            double sample = (double)d_wall / (double)d_virt;
            if (sample < 1e-5)  sample = 1e-5;
            if (sample > 100.0) sample = 100.0;
            s_wall_per_virt = 0.7 * s_wall_per_virt + 0.3 * sample;
        }

        pthread_mutex_lock(&s_mtx);
        struct esp_timer *t = find_expired_locked(now);
        if (!t) {
            /* Nothing due — wait for the next deadline, a wake-up
             * signal from the public API, or the bounded poll that
             * re-checks the time scale. */
            const int64_t next = next_armed_expiry_locked();
            if (next == INT64_MAX) {
                pthread_cond_wait(&s_cv, &s_mtx);
            } else {
                int64_t rem = next - now;
                if (rem < 0) rem = 0;
                double wait = (double)rem * s_wall_per_virt;
                if (wait < MIN_WAIT_WALL_US) wait = MIN_WAIT_WALL_US;
                if (wait > MAX_WAIT_WALL_US) wait = MAX_WAIT_WALL_US;
                const uint64_t ab = wnow + (uint64_t)wait;
                struct timespec ts = {
                    .tv_sec  = (time_t)(ab / 1000000ull),
                    .tv_nsec = (long)((ab % 1000000ull) * 1000ull),
                };
                pthread_cond_timedwait(&s_cv, &s_mtx, &ts);
            }
            pthread_mutex_unlock(&s_mtx);
            continue;
        }

        /* Pop the expired timer out of the armed set while its callback
         * runs; pre-schedule the next period for periodics. */
        t->armed    = false;
        t->firing   = true;
        t->stop_req = false;
        const bool was_periodic = (t->period != 0);
        if (was_periodic) schedule_periodic(t, now);
        pthread_mutex_unlock(&s_mtx);

        t->cb(t->arg);

        pthread_mutex_lock(&s_mtx);
        t->firing = false;
        if (t->dead) {
            timer_remove_locked(t);
            free(t);
        } else if (t->stop_req) {
            /* callback (or another thread) stopped it */
            t->armed = false;
        } else if (was_periodic && !t->armed) {
            /* auto-rearm at the pre-computed boundary */
            t->armed = true;
        }
        pthread_cond_broadcast(&s_cv);
        pthread_mutex_unlock(&s_mtx);
    }
    return NULL;
}

static void ensure_thread(void)
{
    pthread_attr_t attr;
    pthread_t      th;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    if (pthread_create(&th, &attr, timer_thread_main, NULL) != 0) {
        /* Failing to spawn the timer thread is fatal for the sim. */
        fprintf(stderr, "esp_timer: failed to spawn timer thread\n");
        abort();
    }
    pthread_attr_destroy(&attr);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args,
                           esp_timer_handle_t *out_handle)
{
    if (!create_args || !create_args->callback || !out_handle)
        return ESP_ERR_INVALID_ARG;

    pthread_once(&s_once, ensure_thread);

    struct esp_timer *t = calloc(1, sizeof(*t));
    if (!t) return ESP_ERR_NO_MEM;

    t->cb             = create_args->callback;
    t->arg            = create_args->arg;
    t->dispatch       = create_args->dispatch_method;
    t->skip_unhandled = create_args->skip_unhandled_events;
    if (create_args->name) {
        snprintf(t->name, sizeof(t->name), "%s", create_args->name);
    }

    pthread_mutex_lock(&s_mtx);
    timer_insert_locked(t);
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);

    *out_handle = t;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    if (!timer) return ESP_ERR_INVALID_ARG;
    pthread_once(&s_once, ensure_thread);

    pthread_mutex_lock(&s_mtx);
    if (timer->dead) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_INVALID_STATE; }
    timer->expiry   = vboard_now_us() + (int64_t)timeout_us;
    timer->period   = 0;
    timer->armed    = true;
    timer->stop_req = false;
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
    if (!timer) return ESP_ERR_INVALID_ARG;
    if (period == 0) return ESP_ERR_INVALID_ARG;
    pthread_once(&s_once, ensure_thread);

    pthread_mutex_lock(&s_mtx);
    if (timer->dead) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_INVALID_STATE; }
    timer->period   = period;
    timer->expiry   = vboard_now_us() + (int64_t)period;
    timer->armed    = true;
    timer->stop_req = false;
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    if (!timer) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    timer->armed    = false;
    timer->stop_req = true;
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    if (!timer) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    timer->armed    = false;
    timer->stop_req = true;
    if (timer->firing) {
        /* Deleted from its own callback or while firing elsewhere —
         * the timer thread frees it after the callback returns. */
        timer->dead = true;
    } else {
        timer_remove_locked(timer);
        free(timer);
    }
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_timer_restart(esp_timer_handle_t timer, uint64_t timeout_us)
{
    if (!timer) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (timer->period != 0 || (!timer->armed && !timer->firing)) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    timer->expiry   = vboard_now_us() + (int64_t)timeout_us;
    timer->armed    = true;
    timer->stop_req = false;
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t timer)
{
    if (!timer) return false;
    pthread_mutex_lock(&s_mtx);
    const bool active = timer->armed || timer->firing;
    pthread_mutex_unlock(&s_mtx);
    return active;
}

int64_t esp_timer_get_time(void)
{
    return vboard_now_us();
}

int64_t esp_timer_get_next_alarm(void)
{
    pthread_once(&s_once, ensure_thread);
    pthread_mutex_lock(&s_mtx);
    const int64_t next = next_armed_expiry_locked();
    pthread_mutex_unlock(&s_mtx);
    return (next == INT64_MAX) ? -1 : next;
}

esp_err_t esp_timer_get_next_alarm_for_wake_up(int64_t *out_timestamp_us)
{
    if (!out_timestamp_us) return ESP_ERR_INVALID_ARG;
    pthread_once(&s_once, ensure_thread);
    pthread_mutex_lock(&s_mtx);
    const int64_t next = next_armed_expiry_locked();
    pthread_mutex_unlock(&s_mtx);
    if (next == INT64_MAX) return ESP_ERR_NOT_FOUND;
    *out_timestamp_us = next;
    return ESP_OK;
}

esp_err_t esp_timer_init(void)
{
    pthread_once(&s_once, ensure_thread);
    return ESP_OK;
}

esp_err_t esp_timer_deinit(void)
{
    /* The shim's single timer thread lives for the process; there is no
     * safe teardown while timers may still be referenced. */
    return ESP_ERR_NOT_SUPPORTED;
}

void esp_shim_timer_reset_all(void)
{
    pthread_once(&s_once, ensure_thread);
    pthread_mutex_lock(&s_mtx);
    struct esp_timer **pp = &s_timers;
    while (*pp) {
        struct esp_timer *t = *pp;
        t->armed    = false;
        t->stop_req = true;
        if (t->firing) {
            t->dead = true;          /* freed when its callback returns */
            pp = &t->next;
        } else {
            *pp = t->next;
            free(t);
        }
    }
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mtx);
}

esp_err_t esp_timer_dump(FILE *stream)
{
    FILE *out = stream ? stream : stderr;
    pthread_mutex_lock(&s_mtx);
    const int64_t now = vboard_now_us();
    for (struct esp_timer *t = s_timers; t; t = t->next) {
        fprintf(out, "esp_timer %-16s %-8s %-8s expiry=%lld us (in %lld us) period=%llu us\n",
                t->name[0] ? t->name : "(anon)",
                t->period ? "periodic" : "once",
                (t->armed || t->firing) ? "active" : "stopped",
                (long long)t->expiry,
                t->armed ? (long long)(t->expiry - now) : 0ll,
                (unsigned long long)t->period);
    }
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}
