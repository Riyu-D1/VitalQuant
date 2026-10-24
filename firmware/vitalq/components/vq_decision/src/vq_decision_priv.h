/* vq_decision_priv.h — module-private constants + wire formats.
 *
 * NOT part of the frozen vq contract (vq/include/decision.h): tunables
 * here may change without a contract rev, but the two wire formats below
 * are consumed off-device, so they are versioned:
 *
 *   VQ_LOG_EVENT record payload (log_store, type VQ_LOG_EVENT):
 *     dec_evt_hdr_t { evt, t_us, aux_len } + aux_len bytes of aux.
 *     aux is a C struct per event id, defined below — little-endian,
 *     packed, versioned implicitly by VQ_ESC_BLOB_VER bumps if changed.
 *
 *   Escalation blob (log_store VQ_LOG_ESCALATION + cloud_client upload):
 *     vq_esc_hdr_t { 'VQEB', ver, flags, n_ch, t_us, latency, prob,
 *                    threshold, reasons, feat_mask, feat[40] }
 *     then n_ch chunks of:
 *       vq_esc_ch_hdr_t { ch, codec, n, dt_us, t0_us, scale, offset }
 *       + n int16 samples, decoded as f = s*scale + offset; INT16_MIN
 *       is the NaN/dropped-sample sentinel (valid range is ±32767).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "vq_types.h"
#include "decision.h"

/* ------------------------------------------------------------------ */
/* Tunables                                                            */
/* ------------------------------------------------------------------ */

#define VQ_DEC_FEATURE_WINDOW_S   60u      /* features window, fixed     */
#define VQ_DEC_DEFAULT_PERIOD_S   180u
#define VQ_DEC_DEFAULT_THRESH     0.65f
#define VQ_DEC_DEFAULT_ESC_WIN_S  60u
#define VQ_DEC_ESC_WIN_MAX_S      300u     /* clamp: rings keep <=300 s  */

/* Motion gate — checked AFTER vq_features_compute (motion energy and
 * still-time are properties of the window just computed). Skips scoring
 * entirely: a moving wearer's features are untrustworthy, and scoring
 * anyway would flood the cloud with garbage escalations.               */
#define VQ_DEC_MOT_RMS_MAX        0.50f    /* feature units (as computed)*/
#define VQ_DEC_STILL_S_MIN        10.0f    /* need >=10 s still          */

/* Quality gate: SQI below this → VQ_ESC_R_QUALITY (score still runs so
 * the bundle carries a prob, it is just not trusted locally).          */
#define VQ_DEC_SQI_MIN            0.35f

/* Hysteresis (brief: "rolling history + hysteresis note"): keep the
 * last VQ_DEC_HIST_N scored probabilities. A lone over-threshold sample
 * sets VQ_ESC_R_PROB but does NOT escalate; escalation on PROB requires
 * either VQ_DEC_PERSIST_N consecutive over-threshold runs, or a single
 * high-confidence run (prob >= threshold + VQ_DEC_UNCERT_BAND).
 * VQ_ESC_R_* has no PERSIST bit — documented: persistence is expressed
 * by PROB + the logged history, no new bit is invented. The band below
 * threshold [th-0.1, th) only sets vq_decision_t.uncertain (no reason
 * bit, no escalation by itself).                                      */
#define VQ_DEC_HIST_N             5
#define VQ_DEC_PERSIST_N          2
#define VQ_DEC_UNCERT_BAND        0.10f

/* Task / sync */
#define VQ_DEC_TASK_STACK         8192     /* bytes (xTaskCreate on IDF) */
#define VQ_DEC_TASK_PRIO          5        /* below streaming drivers    */
#define VQ_DEC_WAKE_POLL_MS       1000     /* stop_req poll granularity  */
#define VQ_DEC_RUNONCE_WAIT_MS    30000    /* console waits for a cycle  */
#define VQ_DEC_STOP_WAIT_MS       5000     /* stop() grace before kill   */

/* Escalation packing. Worst-case channel rate assumption for sizing    */
/* only — actual n comes from vq_pipeline_window.                       */
#define VQ_DEC_ASSUMED_MAX_SPS    600u     /* > PPG 500 sps ceiling      */
#define VQ_DEC_CH_MAX_SAMPLES     32000u   /* hard cap per channel       */
#define VQ_DEC_Q_NAN              ((int16_t)-32768)  /* NaN sentinel     */

/* ------------------------------------------------------------------ */
/* VQ_LOG_EVENT payloads                                               */
/* ------------------------------------------------------------------ */

typedef struct __attribute__((packed)) {
    uint32_t evt;                          /* vq_event_id_t            */
    int64_t  t_us;                         /* esp_timer µs             */
    uint32_t aux_len;                      /* bytes following this hdr */
} dec_evt_hdr_t;

/* aux for VQ_EVT_MODEL_SCORED (also used for skipped cycles).         */
typedef struct __attribute__((packed)) {
    float    prob;                         /* NaN when not scored      */
    float    threshold;
    uint32_t reasons;                      /* VQ_ESC_R_*               */
    int64_t  latency_us;                   /* -1 when not run          */
    uint8_t  flags;                        /* DEC_SCORE_F_*            */
    uint8_t  pwr_state;                    /* vq_pwr_state_t at cycle  */
} dec_score_aux_t;
#define DEC_SCORE_F_SKIPPED     0x01       /* no scoring this cycle    */
#define DEC_SCORE_F_POWER_GATE  0x02       /* skipped: low-power state */
#define DEC_SCORE_F_MOTION_GATE 0x04       /* skipped: window moving   */
#define DEC_SCORE_F_FORCED      0x08       /* force_next / manual      */

/* aux for VQ_EVT_ESCALATION_SENT.                                     */
typedef struct __attribute__((packed)) {
    float    prob;
    uint32_t reasons;
    uint32_t blob_len;                     /* bytes handed off         */
    uint8_t  queued;                       /* cloud queue accepted     */
    uint8_t  persisted;                    /* log_store accepted       */
} dec_esc_aux_t;

/* aux for VQ_EVT_ESCALATION_RESULT (advice truncated — full JSON lives
 * on the server; the record only needs enough for logtail triage).    */
typedef struct __attribute__((packed)) {
    float    cloud_prob;
    char     model[32];
    uint16_t advice_len;
    char     advice[96];
} dec_esc_res_aux_t;

/* ------------------------------------------------------------------ */
/* Escalation blob (durable + uploaded verbatim)                        */
/* ------------------------------------------------------------------ */

#define VQ_ESC_BLOB_MAGIC  0x42455156u     /* "VQEB" little-endian     */
#define VQ_ESC_BLOB_VER    1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t ver;
    uint8_t  flags;                        /* bit0: feat[] is valid    */
    uint8_t  n_ch;                         /* channel chunks appended  */
    int64_t  t_us;                         /* decision stamp           */
    int64_t  latency_us;                   /* model latency, -1 none   */
    float    prob;                         /* prob[1], NaN if unscored */
    float    threshold;
    uint32_t reasons;
    uint64_t feat_mask;                    /* valid bits for feat[]    */
    float    feat[VQ_FEATURE_DIM];         /* z-normalised vector      */
} vq_esc_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t  ch;                           /* vq_channel_id_t          */
    uint8_t  codec;                        /* 0 = int16 + scale/offset */
    uint16_t n;
    int32_t  dt_us;                        /* mean inter-sample period */
    int64_t  t0_us;                        /* first sample stamp       */
    float    scale;
    float    offset;                       /* f = s*scale + offset     */
    /* int16_t data[n] follows */
} vq_esc_ch_hdr_t;
