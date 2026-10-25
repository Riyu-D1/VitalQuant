/* led_sched.h — optical LED scheduler with HARD non-overlap guarantee.
 *
 * fix_v22 budget (brief §4.3): all chains together ≈900 mA but TPS61240
 * supplies ~400 mA → at most ONE LED chain active per time slot, and the
 * global duty for 400 mA-class pulses <= 1%. TX_5V (expander P3) is on
 * only around LED windows. Slots are scheduled on esp_timer.
 *
 * Chains: AFE4900 TX1-4 (SFH7072 ~200 mA), MAX86141 LED1-3 (~200 mA),
 * IR via IR_GATE/Q1, NIR730 via AD5940 GPIO2, AS7341 on-chip LDR (~91 mA).
 */
#pragma once
#include "vq_types.h"

typedef enum {
    VQ_LED_AFE4900 = 0,
    VQ_LED_MAX86141,
    VQ_LED_IR_GATE,
    VQ_LED_NIR730,      /* via AD5940 GPIO2 — still a scheduled slot     */
    VQ_LED_AS7341,      /* on-chip driver                                */
    VQ_LED_CHAIN_COUNT
} vq_led_chain_t;

typedef struct {
    vq_led_chain_t chain;
    uint32_t       pulse_mA;     /* class: >~300 mA → 1% duty rules      */
    uint32_t       on_us;        /* pulse width                          */
    uint32_t       period_us;    /* repetition; 0 = one-shot             */
    uint8_t        priority;     /* higher preempts scheduling order     */
} vq_led_request_t;

vq_err_t vq_led_init(void);

/* Submit a pulse/window request. The scheduler returns the earliest slot
 * honouring: non-overlap, 1% duty for >=300 mA class, TX_5V on-time
 * margin. cb fires at slot start (turn your chain on) and cb_end at
 * slot end. Never blocks a slot across sleep.                          */
typedef void (*vq_led_cb_t)(void *ctx);
vq_err_t vq_led_submit(const vq_led_request_t *req,
                       vq_led_cb_t cb_on, vq_led_cb_t cb_off,
                       void *ctx, int64_t *slot_start_us);
vq_err_t vq_led_cancel(vq_led_chain_t chain);

/* TX_5V window ownership — the only legal way to enable the boost.     */
vq_err_t vq_led_tx5_acquire(void);   /* scheduler-internal + selftest   */
vq_err_t vq_led_tx5_release(void);
bool     vq_led_tx5_on(void);

/* Hard kill — watchdog, brown-out risk, low battery. Async-safe.       */
void     vq_led_kill_all(void);

/* Monotonicity evidence for acceptance §11.4: rolling log of last N
 * slot boundaries (chain, start_us, end_us) for trace/scope check.     */
#define VQ_LED_TRACE_N 32
typedef struct { vq_led_chain_t chain; int64_t on_us, off_us; uint32_t mA; } vq_led_trace_t;
size_t   vq_led_trace(vq_led_trace_t *out, size_t max);
float    vq_led_duty_pct(void);      /* measured duty, last 60 s         */
