/* vq_led.c — hard-interlock LED slot scheduler (implements led_sched.h).
 *
 * fix_v22 budget (brief §4.3): every LED chain together wants ~900 mA but
 * the TPS61240 boost supplies ~400 mA, so this module enforces IN CODE:
 *
 *   (1) at most ONE chain ON, asserted in the slot-start path — if a chain
 *       is somehow found on when a slot fires it is force-ended and the
 *       violation is counted + logged;
 *   (2) global duty of >=300 mA-class pulses <= 1% over a rolling 60 s
 *       window (600 ms on-time per minute) — a request that would breach
 *       is deferred to the next legal start, never fired;
 *   (3) TX_5V (expander P3) is ON only around high-power LED work and is
 *       OFF whenever no LED work needs it;
 *   (4) vq_power_led_ok(mA) is checked before EVERY slot, and below
 *       VQ_BATT_BOOST_MIN_MV (3.3 V) every slot is refused.
 *
 * EXECUTION MODEL
 *   All slot edges are driven by one-shot esp_timers whose callbacks run in
 *   THE esp_timer task (ESP_TIMER_TASK dispatch — the only method enabled
 *   in this build). That single firing context is deliberate: cb_on/cb_off,
 *   the non-overlap assert, TX_5V writes and the kill sweep are serialized
 *   on one task, so two chains physically cannot be switched
 *   simultaneously. Driver callbacks MUST be SHORT (flip LED registers and
 *   return) — they run inside the esp_timer task and every other esp_timer
 *   in the system waits on them. An expander-based driver cb costs ~400 us
 *   of timer-task time; that is the accepted price of single-task
 *   serialization.
 *
 *   s_lock is a recursive mutex protecting the slot table against API
 *   calls from other tasks. It is NEVER held across an out-of-component
 *   call (expander I2C, power-manager status, user cb_on/cb_off) — that
 *   keeps the lock order sched→(none) and prevents inversion against
 *   callers that might submit while holding an I2C bus lock. After every
 *   drop/re-take the firing path re-validates slot state.
 *
 * TX_5V OWNERSHIP
 *   vq_exp_set(VQ_EXP_TX5_EN, …) is written ONLY by this module, and every
 *   write goes through the refcounted path: s_tx5_holds (engine, held from
 *   slot warm-up to slot end) + s_tx5_user_holds (vq_led_tx5_acquire()).
 *   P3 transitions on the holds 0→1/1→0 edges inside the timer task, so the
 *   boost is never toggled per pulse — a keep-alive horizon
 *   (VQ_LED_TX5_KEEP_US) extends it across near-term follow-up slots and a
 *   short tail (VQ_LED_TX5_TAIL_US) absorbs back-to-back edges. There is no
 *   global way to stop another component from poking P3 via vq_exp_set():
 *   the RULE is that only vq_led_tx5_* may touch TX5_EN — it is the public
 *   API for the rail, and violations are caught in review/selftest
 *   (vq_led_tx5_on() shadow can be compared against the expander shadow).
 *
 * KILL PATH
 *   vq_led_kill_all() only sets a flag and arms the deferred kill timer —
 *   legal from ISR/any context (esp_timer_start_* are ISR-safe in this
 *   IDF). The sweep itself runs in the timer task: stops timers, calls
 *   cb_off on any ACTIVE chain (hardware truth), frees everything, forces
 *   P3 off. It is a FLUSH, not a latch: afterwards new submits work again
 *   (the power/duty gates still refuse illegal work), because the callers
 *   that kill (watchdog, low battery) also drive the power state that keeps
 *   refusing slots while the hazard persists.
 */
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "led_sched.h"
#include "expander.h"
#include "power_mgr.h"
#include "board_pins.h"
#include "vq_led_priv.h"

static const char *TAG = "vq_led";

/* ------------------------------------------------------------------ *
 *  slot states                                                        *
 *  FREE → PEND (queued) → ARMED (next, timer armed) → FIRING (commit   *
 *  point passed, warm-up in progress) → ACTIVE (cb_on issued).         *
 *  PEND/ARMED are cancellable for free; FIRING aborts before cb_on;    *
 *  ACTIVE always completes through cb_off.                            *
 * ------------------------------------------------------------------ */
typedef enum {
    SLOT_FREE = 0,
    SLOT_PEND,
    SLOT_ARMED,
    SLOT_FIRING,
    SLOT_ACTIVE,
} slot_state_t;

typedef struct {
    vq_led_request_t req;
    vq_led_cb_t      cb_on;
    vq_led_cb_t      cb_off;
    void            *ctx;
    slot_state_t     state;
    int64_t          start_us;    /* committed earliest start            */
    int64_t          fired_us;    /* actual cb_on issue time             */
    uint32_t         seq;         /* FIFO tie-break on equal starts      */
    bool             holds_tx5;   /* engine TX5 hold taken for this slot */
    bool             cancel_req;  /* cancel() landed while FIRING/ACTIVE */
} led_slot_t;

/* Which chains draw their emitters from the TX_5V rail. Conservative:
 * every chain on the boost-fed anode side is mapped true; a false positive
 * only costs the boost's idle current. AS7341's on-chip LDR (~91 mA) runs
 * off the 1.8 V island — no boost needed.                               */
static const bool s_chain_tx5[VQ_LED_CHAIN_COUNT] = {
    [VQ_LED_AFE4900]  = true,   /* U16 SFH7072 anodes on TX_5V          */
    [VQ_LED_MAX86141] = true,   /* U22 LED supply on TX_5V              */
    [VQ_LED_IR_GATE]  = true,   /* Q1 low-side switch, IR anode boosted  */
    [VQ_LED_NIR730]   = true,   /* D12 via AD5940 GPIO2/Q4, boosted      */
    [VQ_LED_AS7341]   = false,  /* on-chip driver, 1.8 V island          */
};

static const char *const s_chain_names[VQ_LED_CHAIN_COUNT] = {
    [VQ_LED_AFE4900]  = "afe4900",
    [VQ_LED_MAX86141] = "max86141",
    [VQ_LED_IR_GATE]  = "ir_gate",
    [VQ_LED_NIR730]   = "nir730",
    [VQ_LED_AS7341]   = "as7341",
};

/* ------------------------------- state ------------------------------ */
static SemaphoreHandle_t s_lock;            /* recursive mutex           */
static led_slot_t  s_pool[VQ_LED_POOL];
static led_slot_t *s_next;                  /* ARMED/FIRING slot         */
static led_slot_t *s_active;                /* chain currently ON        */
static uint32_t    s_seq;

static esp_timer_handle_t s_tmr_start;      /* slot begin / warm-up      */
static esp_timer_handle_t s_tmr_off;        /* slot end                  */
static esp_timer_handle_t s_tmr_tx5;        /* TX5 service: on-req + tail */
static esp_timer_handle_t s_tmr_kill;       /* deferred kill sweep       */

static volatile bool s_ready;
static volatile bool s_kill_pending;        /* set by kill_all (ISR ok)  */
static uint32_t    s_violations;            /* rule-1 assert count       */

static int         s_tx5_holds;             /* engine holds (firing hp)  */
static int         s_tx5_user_holds;        /* vq_led_tx5_acquire holds  */
static volatile bool s_tx5_on;              /* P3 shadow                 */
static int64_t     s_tx5_on_us;             /* last P3-rising write time */

/* duty accounting: absolute-second buckets, lazily validated            */
static uint32_t    s_duty_us[VQ_LED_DUTY_BUCKETS];
static uint64_t    s_duty_sec[VQ_LED_DUTY_BUCKETS];   /* which second     */

/* trace ring — acceptance §11.4 monotonicity evidence                   */
static vq_led_trace_t s_trace[VQ_LED_TRACE_N];
static size_t      s_tr_head;               /* next write index          */
static size_t      s_tr_cnt;                /* valid entries             */

/* ------------------------------------------------------------------ */
static void sched_lock(void)   { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
static void sched_unlock(void) { xSemaphoreGiveRecursive(s_lock); }

static bool chain_tx5(vq_led_chain_t c)
{
    return (unsigned)c < VQ_LED_CHAIN_COUNT && s_chain_tx5[c];
}

static bool slot_hp_class(const led_slot_t *s)
{
    return s->req.pulse_mA >= (uint32_t)VQ_LED_DUTY_CLASS_MA;
}

/* (Re)arm a one-shot timer; safe from task or ISR (esp_timer_* are). */
static void tmr_arm(esp_timer_handle_t t, int64_t delay_us)
{
    esp_timer_stop(t);                      /* INVALID_STATE if unarmed   */
    esp_timer_start_once(t, delay_us < 0 ? 0 : (uint64_t)delay_us);
}

/* ------------------------- duty accounting -------------------------- */
/* Attribute a finished hp-class pulse to its start-time second bucket.
 * Buckets are absolute-second indexed and lazily reset, so pulse count is
 * unbounded; reads count whole intersecting buckets (conservative).      */
static void duty_add(int64_t on_us, uint32_t dur_us)
{
    uint64_t sec = (uint64_t)(on_us / 1000000LL);
    unsigned  i  = (unsigned)(sec % VQ_LED_DUTY_BUCKETS);
    if (s_duty_sec[i] != sec) {             /* bucket describes an old s  */
        s_duty_sec[i] = sec;
        s_duty_us[i]  = 0;
    }
    if (UINT32_MAX - s_duty_us[i] < dur_us) {
        s_duty_us[i] = UINT32_MAX;
    } else {
        s_duty_us[i] += dur_us;
    }
}

/* Sum of hp-class on-time overlapping [wb,we): committed past (buckets)
 * plus reserved future intervals of OTHER hp slots and the ACTIVE slot's
 * planned interval. `self` (the slot being placed) is skipped.           */
static uint64_t duty_window_sum(int64_t wb, int64_t we, const led_slot_t *self)
{
    uint64_t sum = 0;
    for (int i = 0; i < VQ_LED_DUTY_BUCKETS; i++) {
        if (!s_duty_us[i]) continue;
        int64_t b0 = (int64_t)s_duty_sec[i] * 1000000LL;
        int64_t b1 = b0 + 1000000LL;
        if (b0 < we && b1 > wb) sum += s_duty_us[i];  /* whole bucket     */
    }
    for (int i = 0; i < VQ_LED_POOL; i++) {
        const led_slot_t *s = &s_pool[i];
        if (s == self || !slot_hp_class(s)) continue;
        if (s->state == SLOT_FREE) continue;
        int64_t st = (s->state == SLOT_ACTIVE) ? s->fired_us : s->start_us;
        int64_t en = st + (int64_t)s->req.on_us;
        int64_t c0 = st > wb ? st : wb;
        int64_t c1 = en < we ? en : we;
        if (c1 > c0) sum += (uint64_t)(c1 - c0);
    }
    return sum;
}

/* Measured (not reserved) on-time in [wb,we): buckets + ACTIVE accrued. */
static uint64_t duty_measured_sum(int64_t wb, int64_t we, int64_t now)
{
    uint64_t sum = 0;
    for (int i = 0; i < VQ_LED_DUTY_BUCKETS; i++) {
        if (!s_duty_us[i]) continue;
        int64_t b0 = (int64_t)s_duty_sec[i] * 1000000LL;
        int64_t b1 = b0 + 1000000LL;
        if (b0 < we && b1 > wb) sum += s_duty_us[i];
    }
    if (s_active && slot_hp_class(s_active)) {
        int64_t c0 = s_active->fired_us > wb ? s_active->fired_us : wb;
        int64_t c1 = now < we ? now : we;
        if (c1 > c0) sum += (uint64_t)(c1 - c0);
    }
    return sum;
}

static bool duty_fits(int64_t t, uint32_t on_us, const led_slot_t *self)
{
    int64_t we = t + (int64_t)on_us;
    int64_t wb = we - VQ_LED_DUTY_WINDOW_US;
    return duty_window_sum(wb, we, self) + (uint64_t)on_us
           <= (uint64_t)VQ_LED_DUTY_LIMIT_US;
}

/* Earliest legal start for an hp-class pulse. A start t is legal when the
 * 60 s window ending at t+on holds <= 600 ms of hp on-time. Feasibility
 * can only change at points where a contribution enters or leaves the
 * window, so the answer is the minimum feasible candidate among:
 *   - t0 itself,
 *   - each contribution's left-edge exit: c.end + WINDOW - on,
 *   - each pending reservation's "squeeze in before it": c.start - on.
 * The largest c.end candidate always fits (empty window), so best exists. */
static int64_t duty_earliest(int64_t t0, uint32_t on_us, const led_slot_t *self)
{
    if (duty_fits(t0, on_us, self)) return t0;

    int64_t best = INT64_MAX;
    int64_t last = t0;              /* guaranteed-feasible fallback      */
    for (int i = 0; i < VQ_LED_DUTY_BUCKETS; i++) {
        if (!s_duty_us[i]) continue;
        int64_t c = ((int64_t)s_duty_sec[i] + 1) * 1000000LL
                    + VQ_LED_DUTY_WINDOW_US - (int64_t)on_us;
        if (c > last) last = c;
        if (c >= t0 && c < best && duty_fits(c, on_us, self)) best = c;
    }
    for (int i = 0; i < VQ_LED_POOL; i++) {
        const led_slot_t *s = &s_pool[i];
        if (s == self || !slot_hp_class(s) || s->state == SLOT_FREE) continue;
        int64_t st = (s->state == SLOT_ACTIVE) ? s->fired_us : s->start_us;
        int64_t en = st + (int64_t)s->req.on_us;
        int64_t c  = en + VQ_LED_DUTY_WINDOW_US - (int64_t)on_us;
        if (c > last) last = c;
        if (c >= t0 && c < best && duty_fits(c, on_us, self)) best = c;
        if (s->state != SLOT_ACTIVE) {
            int64_t b4 = st - (int64_t)on_us;   /* end before it starts */
            if (b4 >= t0 && b4 < best && duty_fits(b4, on_us, self)) best = b4;
        }
    }
    return (best != INT64_MAX) ? best : last;
}

/* Earliest legal start for a pending slot, honouring (b) duty cap for the
 * mA class and (c) the TX_5V warm-up margin for boost-fed chains.         */
static int64_t earliest_legal(led_slot_t *s, int64_t now)
{
    int64_t t = now + VQ_LED_SLOT_GUARD_US;
    if (s->start_us > t) t = s->start_us;   /* keep phase intent          */
    if (slot_hp_class(s)) {
        t = duty_earliest(t, s->req.on_us, s);
    }
    if (chain_tx5(s->req.chain) && !s_tx5_on) {
        /* cold boost: bake the I2C+settle margin into the reported start */
        int64_t pre = now + VQ_LED_TX5_PREARM_US;
        if (t < pre) t = pre;
    }
    return t;
}

/* ------------------------- trace ring ------------------------------- */
static void trace_push(vq_led_chain_t chain, int64_t on_us, int64_t off_us,
                       uint32_t mA)
{
    s_trace[s_tr_head].chain  = chain;
    s_trace[s_tr_head].on_us  = on_us;
    s_trace[s_tr_head].off_us = off_us;
    s_trace[s_tr_head].mA     = mA;
    s_tr_head = (s_tr_head + 1) % VQ_LED_TRACE_N;
    if (s_tr_cnt < VQ_LED_TRACE_N) s_tr_cnt++;
}

/* ------------------------- power gate ------------------------------- */
/* Rule 4. Called with the lock DROPPED — out-of-component call. Below
 * VQ_BATT_BOOST_MIN_MV (or unreadable/NaN cell voltage) everything is
 * refused; >= VQ_PWR_LOW_CUTOFF refuses outright; else the budget gate
 * vq_power_led_ok() decides per mA class.                                */
static bool power_allows(uint32_t pulse_mA)
{
    vq_power_status_t ps;
    vq_power_status(&ps);
    if (ps.state >= VQ_PWR_LOW_CUTOFF) return false;
    if (!(ps.vcell_v * 1000.0f >= (float)VQ_BATT_BOOST_MIN_MV)) return false;
    return vq_power_led_ok(pulse_mA);
}

/* ------------------------- TX_5V control ---------------------------- */
/* THE ONLY P3 writer besides the kill sweep (also here). Callers hold the
 * lock; the I2C write happens with the lock DROPPED (deadlock ordering),
 * and because every writer runs in the esp_timer task the writes
 * themselves cannot interleave.                                          */
static void tx5_write(bool on)
{
    sched_unlock();
    vq_exp_set(VQ_EXP_TX5_EN, on);
    sched_lock();
    s_tx5_on = on;
    if (on) s_tx5_on_us = esp_timer_get_time();
}

/* Any queued hp-chain slot whose start is inside the keep-alive horizon? */
static bool hp_imminent(int64_t now)
{
    for (int i = 0; i < VQ_LED_POOL; i++) {
        const led_slot_t *s = &s_pool[i];
        if ((s->state == SLOT_PEND || s->state == SLOT_ARMED) &&
            chain_tx5(s->req.chain) &&
            s->start_us - now <= VQ_LED_TX5_KEEP_US) {
            return true;
        }
    }
    return false;
}

/* TX5 bookkeeping — run from reschedule_locked() after every queue or
 * hold change. Never STOPS the service timer: it also carries pending
 * turn-on requests, and tmr_tx5_cb re-derives intent at fire time, so a
 * stale tail-off harmlessly no-ops when work arrived meanwhile.          */
static void tx5_housekeeping(int64_t now)
{
    bool want = (s_tx5_holds + s_tx5_user_holds) > 0 || hp_imminent(now);
    if (!want && s_tx5_on && !esp_timer_is_active(s_tmr_tx5)) {
        tmr_arm(s_tmr_tx5, VQ_LED_TX5_TAIL_US);   /* off after the tail   */
    }
}

/* TX5 service cb: runs in the timer task, reconciles P3 with the holds.
 * Armed with 0 delay by vq_led_tx5_acquire() for the on transition and by
 * tx5_housekeeping() with TAIL for the off transition.                   */
static void tmr_tx5_cb(void *arg)
{
    (void)arg;
    sched_lock();
    if (s_kill_pending) { sched_unlock(); return; }
    if ((s_tx5_holds + s_tx5_user_holds) > 0 && !s_tx5_on) {
        tx5_write(true);
    } else if ((s_tx5_holds + s_tx5_user_holds) == 0 &&
               !hp_imminent(esp_timer_get_time()) && s_tx5_on) {
        tx5_write(false);
    }
    sched_unlock();
}

/* ------------------------- slot bookkeeping ------------------------- */
static void slot_release_tx5(led_slot_t *s)
{
    if (s->holds_tx5) {
        s->holds_tx5 = false;
        if (s_tx5_holds > 0) s_tx5_holds--;
    }
}

/* Abort an ARMED/FIRING slot before cb_on — LED never lit, no cb_off.   */
static void slot_abort(led_slot_t *s)
{
    slot_release_tx5(s);
    if (s_next == s) s_next = NULL;
    s->state      = SLOT_FREE;
    s->cancel_req = false;
}

/* Complete an ACTIVE slot: emit cb_off (hardware truth), record the real
 * edges in trace + duty buckets, release its TX5 hold, then re-pend it
 * (periodic, phase-locked to its fired start) or free it. Lock held on
 * entry AND on exit; the driver callback runs with the lock dropped.     */
static void slot_complete(led_slot_t *s, int64_t off_us)
{
    int64_t     on_us = s->fired_us;
    vq_led_cb_t cb    = s->cb_off;
    void       *ctx   = s->ctx;

    s_active = NULL;
    sched_unlock();
    if (cb) cb(ctx);                        /* driver turns its chain off */
    sched_lock();

    trace_push(s->req.chain, on_us, off_us, s->req.pulse_mA);
    if (slot_hp_class(s)) {
        duty_add(on_us, (uint32_t)(off_us - on_us));
    }
    slot_release_tx5(s);

    if (s->req.period_us > 0 && !s->cancel_req && !s_kill_pending) {
        s->state    = SLOT_PEND;
        int64_t ph  = on_us + (int64_t)s->req.period_us;   /* phase-locked */
        int64_t grd = off_us + VQ_LED_SLOT_GUARD_US;
        s->start_us = ph > grd ? ph : grd;
    } else {
        s->state = SLOT_FREE;
    }
    s->cancel_req = false;
}

/* ------------------------- the engine ------------------------------- */
static bool slot_better(const led_slot_t *a, const led_slot_t *b)
{
    if (a->start_us != b->start_us) {
        return a->start_us < b->start_us;          /* earliest slot first */
    }
    if (a->req.priority != b->req.priority) {
        return a->req.priority > b->req.priority;  /* then higher prio    */
    }
    return a->seq < b->seq;                        /* then FIFO           */
}

/* Recompute every pending slot's earliest start, pick the winner, arm the
 * start timer (early enough for the cold-boost warm-up when needed), and
 * run TX5 housekeeping. Lock held.                                       */
static void reschedule_locked(void)
{
    int64_t now = esp_timer_get_time();

    /* Winner selection only while nothing is committed/executing — while
     * a slot is FIRING or ACTIVE the engine is single-threaded on it and
     * no other start may be armed (this IS the non-overlap mechanism).   */
    if (!s_active && !(s_next && s_next->state == SLOT_FIRING)) {
        led_slot_t *win = NULL;
        for (int i = 0; i < VQ_LED_POOL; i++) {
            led_slot_t *s = &s_pool[i];
            if (s->state != SLOT_PEND && s != s_next) continue;
            s->start_us = earliest_legal(s, now);
            if (!win || slot_better(s, win)) win = s;
        }
        if (win != s_next) {
            if (s_next) s_next->state = SLOT_PEND;     /* demoted         */
            s_next = win;
            if (win)  win->state = SLOT_ARMED;
        }
        if (s_next) {
            int64_t at = s_next->start_us;
            if (chain_tx5(s_next->req.chain) && !s_tx5_on) {
                at -= VQ_LED_TX5_PREARM_US;   /* fire early for warm-up  */
            }
            tmr_arm(s_tmr_start, at - now);
        } else {
            esp_timer_stop(s_tmr_start);
        }
    }
    tx5_housekeeping(now);
}

/* Slot-start callback — esp_timer task context. Runs the assert, power
 * gate, TX5 warm-up, then issues cb_on and arms the off timer.           */
static void tmr_start_cb(void *arg)
{
    (void)arg;
    sched_lock();
    led_slot_t *S = s_next;
    if (!S || s_kill_pending) goto out;
    if (S->state != SLOT_ARMED && S->state != SLOT_FIRING) goto out;

    /* ---- hard rule 1: at most one chain ON — asserted HERE ---------- */
    if (s_active) {
        s_violations++;
        ESP_LOGE(TAG, "VIOLATION #%lu: chain %s ON at slot start — kill it",
                 (unsigned long)s_violations,
                 s_chain_names[s_active->req.chain]);
        slot_complete(s_active, esp_timer_get_time());
        /* slot_complete dropped the lock around cb_off — S may have been
         * demoted, cancelled or even freed+reused meanwhile. Revalidate. */
        if (s_next != S ||
            (S->state != SLOT_ARMED && S->state != SLOT_FIRING)) goto out;
    }

    if (S->cancel_req) { slot_abort(S); goto resched; }

    if (S->state == SLOT_ARMED) {
        /* ---- hard rule 4: power gate before every slot (off-lock) --- */
        sched_unlock();
        bool ok = power_allows(S->req.pulse_mA);
        sched_lock();
        if (s_kill_pending) goto out;
        if (s_next != S || S->state != SLOT_ARMED) goto out;
        if (S->cancel_req) { slot_abort(S); goto resched; }
        if (!ok) {
            S->state    = SLOT_PEND;
            S->start_us = esp_timer_get_time() + VQ_LED_POWER_RETRY_US;
            s_next      = NULL;
            goto resched;
        }
        S->state = SLOT_FIRING;             /* commit point              */
    }

    /* ---- TX_5V warm-up for boost-fed chains ------------------------- */
    if (chain_tx5(S->req.chain) && !S->holds_tx5) {
        S->holds_tx5 = true;
        s_tx5_holds++;
        if (!s_tx5_on) {
            tx5_write(true);                /* lock dropped inside       */
            if (s_kill_pending) goto out;   /* sweep owns the corpse     */
            if (s_next != S || S->state != SLOT_FIRING) goto out;
            if (S->cancel_req) { slot_abort(S); goto resched; }
        }
    }

    /* ---- fire at the committed start ------------------------------- */
    {
        int64_t now    = esp_timer_get_time();
        int64_t target = S->start_us;
        if (S->holds_tx5 && s_tx5_on) {
            int64_t ready = s_tx5_on_us + VQ_LED_TX5_WARMUP_US;
            if (ready > target) target = ready;
        }
        if (now < target) {                 /* warm-up still running      */
            tmr_arm(s_tmr_start, target - now);
            goto out;
        }
        if (s_kill_pending) goto out;       /* sweep handles the corpse   */
        S->state    = SLOT_ACTIVE;
        S->fired_us = now;
        s_active    = S;
        s_next      = NULL;
        tmr_arm(s_tmr_off, (int64_t)S->req.on_us);
        {
            vq_led_cb_t cb = S->cb_on;
            void       *cx = S->ctx;
            sched_unlock();
            if (cb) cb(cx);                 /* driver turns its chain on  */
            /* NOTE: returns with the lock released — balanced.           */
            return;
        }
    }

resched:
    reschedule_locked();
out:
    sched_unlock();
}

/* Slot-end callback — esp_timer task context. */
static void tmr_off_cb(void *arg)
{
    (void)arg;
    sched_lock();
    led_slot_t *S = s_active;
    if (!S) { sched_unlock(); return; }
    slot_complete(S, esp_timer_get_time());
    reschedule_locked();
    sched_unlock();
}

/* Deferred kill sweep — esp_timer task context (see design notes).      */
static void tmr_kill_cb(void *arg)
{
    (void)arg;
    sched_lock();
    esp_timer_stop(s_tmr_start);
    esp_timer_stop(s_tmr_off);
    esp_timer_stop(s_tmr_tx5);

    int64_t now = esp_timer_get_time();
    if (s_active) {
        slot_complete(s_active, now);       /* cb_off for hardware truth  */
    }
    if (s_next) {
        slot_release_tx5(s_next);           /* FIRING may hold the boost  */
        s_next->state = SLOT_FREE;
        s_next        = NULL;
    }
    for (int i = 0; i < VQ_LED_POOL; i++) {
        if (s_pool[i].state != SLOT_FREE) {
            s_pool[i].state      = SLOT_FREE;
            s_pool[i].cancel_req = false;
            s_pool[i].holds_tx5  = false;
        }
    }
    s_tx5_holds      = 0;
    s_tx5_user_holds = 0;
    if (s_tx5_on) tx5_write(false);
    s_kill_pending = false;
    sched_unlock();
    ESP_LOGW(TAG, "kill_all: all slots cancelled, TX_5V off");
}

/* ============================ public API ============================= */
vq_err_t vq_led_init(void)
{
    if (s_ready) return ESP_ERR_INVALID_STATE;

    memset(s_pool, 0, sizeof(s_pool));
    s_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    const esp_timer_create_args_t a_start = {
        .callback = tmr_start_cb, .name = "vqled_start",
        .dispatch_method = ESP_TIMER_TASK };
    const esp_timer_create_args_t a_off = {
        .callback = tmr_off_cb, .name = "vqled_off",
        .dispatch_method = ESP_TIMER_TASK };
    const esp_timer_create_args_t a_tx5 = {
        .callback = tmr_tx5_cb, .name = "vqled_tx5",
        .dispatch_method = ESP_TIMER_TASK };
    const esp_timer_create_args_t a_kill = {
        .callback = tmr_kill_cb, .name = "vqled_kill",
        .dispatch_method = ESP_TIMER_TASK };

    if (esp_timer_create(&a_start, &s_tmr_start) != ESP_OK) return ESP_FAIL;
    if (esp_timer_create(&a_off,   &s_tmr_off)   != ESP_OK) return ESP_FAIL;
    if (esp_timer_create(&a_tx5,   &s_tmr_tx5)   != ESP_OK) return ESP_FAIL;
    if (esp_timer_create(&a_kill,  &s_tmr_kill)  != ESP_OK) return ESP_FAIL;

    /* Rule 3 from the first instruction: no work pending → boost off.
     * expander init already drives the safe pattern; re-assert anyway.   */
    vq_exp_set(VQ_EXP_TX5_EN, false);
    s_tx5_on = false;

    s_ready = true;
    ESP_LOGI(TAG, "led scheduler up — %d slots, 1%%/%llds duty cap >=%dmA",
             VQ_LED_POOL, (long long)(VQ_LED_DUTY_WINDOW_US / 1000000),
             VQ_LED_DUTY_CLASS_MA);
    return ESP_OK;
}

vq_err_t vq_led_submit(const vq_led_request_t *req,
                       vq_led_cb_t cb_on, vq_led_cb_t cb_off,
                       void *ctx, int64_t *slot_start_us)
{
    if (!s_ready)                    return ESP_ERR_INVALID_STATE;
    if (!req || !cb_on)              return ESP_ERR_INVALID_ARG;
    if (req->chain >= VQ_LED_CHAIN_COUNT) return ESP_ERR_INVALID_ARG;
    if (req->on_us == 0 || req->on_us > VQ_LED_MAX_ON_US) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->period_us) {
        uint32_t pmin = req->on_us + VQ_LED_SLOT_GUARD_US;
        if (pmin < VQ_LED_MIN_PERIOD_US) pmin = VQ_LED_MIN_PERIOD_US;
        if (req->period_us < pmin) return ESP_ERR_INVALID_ARG;
    }
    /* An hp-class pulse wider than the whole legal budget can never fire */
    if (req->pulse_mA >= VQ_LED_DUTY_CLASS_MA &&
        req->on_us > (uint32_t)VQ_LED_DUTY_LIMIT_US) {
        return ESP_ERR_INVALID_ARG;
    }

    sched_lock();
    if (s_kill_pending) { sched_unlock(); return ESP_ERR_INVALID_STATE; }

    led_slot_t *s = NULL;
    for (int i = 0; i < VQ_LED_POOL; i++) {
        if (s_pool[i].state == SLOT_FREE) { s = &s_pool[i]; break; }
    }
    if (!s) { sched_unlock(); return ESP_ERR_NO_MEM; }

    s->req        = *req;
    s->cb_on      = cb_on;
    s->cb_off     = cb_off;
    s->ctx        = ctx;
    s->state      = SLOT_PEND;
    s->seq        = ++s_seq;
    s->fired_us   = 0;
    s->start_us   = 0;              /* base = now+guard in earliest_legal */
    s->holds_tx5  = false;
    s->cancel_req = false;

    s->start_us = earliest_legal(s, esp_timer_get_time());
    if (slot_start_us) *slot_start_us = s->start_us;

    reschedule_locked();
    sched_unlock();
    return ESP_OK;
}

vq_err_t vq_led_cancel(vq_led_chain_t chain)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (chain >= VQ_LED_CHAIN_COUNT) return ESP_ERR_INVALID_ARG;

    int n = 0;
    sched_lock();
    for (int i = 0; i < VQ_LED_POOL; i++) {
        led_slot_t *s = &s_pool[i];
        if (s->state == SLOT_FREE || s->req.chain != chain) continue;
        switch (s->state) {
        case SLOT_PEND:
            s->state = SLOT_FREE;
            break;
        case SLOT_ARMED:
            slot_abort(s);                  /* frees + clears s_next     */
            break;
        case SLOT_FIRING:
            s->cancel_req = true;           /* start_cb aborts before on */
            break;
        case SLOT_ACTIVE:
            s->cancel_req = true;
            tmr_arm(s_tmr_off, 0);          /* off_cb runs next in task  */
            break;
        default:
            break;
        }
        n++;
    }
    reschedule_locked();
    sched_unlock();
    return n ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/* ---------------------- TX_5V window ownership ---------------------- */
vq_err_t vq_led_tx5_acquire(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    /* The boost must never run under VQ_BATT_BOOST_MIN_MV — gate here
     * too, before touching the holds. Status read stays off-lock.        */
    vq_power_status_t ps;
    vq_power_status(&ps);
    if (ps.state >= VQ_PWR_LOW_CUTOFF ||
        !(ps.vcell_v * 1000.0f >= (float)VQ_BATT_BOOST_MIN_MV)) {
        return ESP_ERR_INVALID_STATE;
    }

    sched_lock();
    if (s_kill_pending) { sched_unlock(); return ESP_ERR_INVALID_STATE; }
    s_tx5_user_holds++;
    /* The P3 write itself is deferred to the timer task so every write
     * stays serialized on one context. ~us later tmr_tx5_cb drives it.   */
    tmr_arm(s_tmr_tx5, 0);
    sched_unlock();
    return ESP_OK;
}

vq_err_t vq_led_tx5_release(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    sched_lock();
    if (s_tx5_user_holds <= 0) {
        sched_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_tx5_user_holds--;
    reschedule_locked();            /* tail-off armed in housekeeping    */
    sched_unlock();
    return ESP_OK;
}

bool vq_led_tx5_on(void)
{
    return s_tx5_on;
}

/* Hard kill — legal from ISR and any task. esp_timer_start_once() is
 * ISR-safe in this IDF (spinlock + IRAM), so no context branch is needed:
 * the flag alone makes in-flight firing paths bail, and the sweep runs
 * ~us later in the timer task where cb_off/I2C are legal.               */
void vq_led_kill_all(void)
{
    s_kill_pending = true;
    if (s_tmr_kill) {
        esp_timer_stop(s_tmr_kill);
        esp_timer_start_once(s_tmr_kill, 1);
    }
}

/* --------------------------- telemetry ------------------------------ */
size_t vq_led_trace(vq_led_trace_t *out, size_t max)
{
    if (!s_ready || !out || max == 0) return 0;
    sched_lock();
    size_t n = s_tr_cnt < max ? s_tr_cnt : max;
    for (size_t i = 0; i < n; i++) {       /* oldest→newest of the last n */
        size_t idx = (s_tr_head + VQ_LED_TRACE_N - n + i) % VQ_LED_TRACE_N;
        out[i] = s_trace[idx];
    }
    sched_unlock();
    return n;
}

float vq_led_duty_pct(void)
{
    if (!s_ready) return 0.0f;
    sched_lock();
    int64_t now = esp_timer_get_time();
    uint64_t sum = duty_measured_sum(now - VQ_LED_DUTY_WINDOW_US, now, now);
    sched_unlock();
    return (float)sum * (100.0f / (float)VQ_LED_DUTY_WINDOW_US);
}
