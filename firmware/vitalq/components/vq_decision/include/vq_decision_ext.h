/* vq_decision_ext.h — console/selftest hooks for vq_decision.
 *
 * Extension surface, NOT part of the frozen contract in
 * vq/include/decision.h. Exists so `score` (console) and
 * `decision_loop` (selftest "forced escalation dry-run") can drive the
 * full escalation path without touching thresholds or faking sensor data.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "vq_types.h"

/* Arm a one-shot forced escalation: the NEXT decision cycle (periodic
 * or run_once) adds VQ_ESC_R_MANUAL to reasons and escalates regardless
 * of the score. Consumed by that cycle — arm again for another shot. */
void     vq_decision_force_next(void);

/* Rolling probability history, oldest -> newest. Returns entries copied
 * (<= VQ_DEC_HIST_N = 5). Only successfully scored cycles are recorded. */
size_t   vq_decision_prob_hist(float *out, size_t max);

/* Consecutive over-threshold count — the hysteresis counter behind the
 * documented "2 consecutive over threshold to escalate on PROB" rule.  */
uint8_t  vq_decision_consec_over(void);

bool     vq_decision_is_running(void);
