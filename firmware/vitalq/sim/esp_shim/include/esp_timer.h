/* Host-sim esp_timer.h — one-shot and periodic timers whose expiries
 * are scheduled on the virtual vboard clock.
 *
 * All callbacks (ESP_TIMER_TASK and, in the sim, ESP_TIMER_ISR too) run
 * on a single dedicated shim timer thread — mirroring the real
 * esp_timer task's serialized-callback semantics.
 */
#ifndef ESP_TIMER_H
#define ESP_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_timer *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);

typedef enum {
    ESP_TIMER_TASK,     /* callback dispatched from the timer task (sim: timer thread) */
    ESP_TIMER_ISR,      /* callback dispatched from ISR context (sim: same thread)     */
} esp_timer_dispatch_t;

typedef struct {
    esp_timer_cb_t        callback;             /* function to call on expiry        */
    void                 *arg;                  /* argument passed to the callback   */
    esp_timer_dispatch_t  dispatch_method;      /* ESP_TIMER_TASK or ESP_TIMER_ISR   */
    const char           *name;                 /* timer name (diagnostics)          */
    bool                  skip_unhandled_events;/* skip events missed while busy     */
} esp_timer_create_args_t;

/** Create a timer (the shim's timer thread starts lazily on first use). */
esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args,
                           esp_timer_handle_t *out_handle);

/** Start a one-shot timer firing timeout_us (virtual) from now. */
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);

/** Start/restart a periodic timer with the given period (virtual µs). */
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period);

/** Stop a timer; may be called from the timer's own callback. */
esp_err_t esp_timer_stop(esp_timer_handle_t timer);

/** Stop (if armed) and delete a timer.  Safe to call on the timer's own
 *  callback — destruction is deferred until the callback returns. */
esp_err_t esp_timer_delete(esp_timer_handle_t timer);

/** Restart an armed one-shot timer to fire after timeout_us. */
esp_err_t esp_timer_restart(esp_timer_handle_t timer, uint64_t timeout_us);

/** True while the timer is armed (or currently executing its callback). */
bool esp_timer_is_active(esp_timer_handle_t timer);

/** Time since sim start in virtual µs — the vboard clock. */
int64_t esp_timer_get_time(void);

/** Virtual-µs timestamp of the next armed alarm, or -1 when none. */
int64_t esp_timer_get_next_alarm(void);

/** Next armed alarm suitable as a light-sleep wake source.
 *  Returns ESP_ERR_NOT_FOUND when no timer is armed. */
esp_err_t esp_timer_get_next_alarm_for_wake_up(int64_t *out_timestamp_us);

/** Legacy IDF entry points kept for source compatibility. */
esp_err_t esp_timer_init(void);
esp_err_t esp_timer_deinit(void);

/** Dump armed timers to a FILE* (real IDF provides esp_timer_dump). */
esp_err_t esp_timer_dump(FILE *stream);

/* ---- Sim-only extension (not real IDF): stop and free every timer.
 *  Intended for the harness's reset path (vsim_reset) so timers armed
 *  by a previous boot cannot fire callbacks into torn-down firmware. */
void esp_shim_timer_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_TIMER_H */
