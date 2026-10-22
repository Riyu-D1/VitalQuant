/* decision.h — the on-device policy loop.
 *
 * Every few minutes (config, default 180 s): compute features → run the
 * INT8 model → probability p.  If p >= threshold (or model marks the
 * window uncertain/low-quality), build an ESCALATION bundle (feature
 * vector + raw channel windows + scores + reasons) and queue it for the
 * cloud model via cloud_client. The cloud verdict is ADVISORY: it is
 * logged, surfaced on /status and attached to the next batch — the
 * device never makes or displays a clinical claim (brief §10).
 *
 * Fallback ordering: local model → (anomaly|uncertain|error) → cloud
 * model → if unreachable, escalate bundle sits in log_store until uplink
 * returns. No silent drops.
 */
#pragma once
#include "vq_types.h"
#include "features.h"

typedef struct {
    float    prob;                 /* model output, 0..1               */
    float    threshold;            /* escalation threshold (config)    */
    bool     uncertain;            /* model uncertainty / bad SQI gate */
    bool     escalated;            /* bundle queued to cloud           */
    uint32_t reasons;              /* VQ_ESC_R_* bitmask               */
    int64_t  latency_us;
    float    cloud_prob;           /* advisory result, -1 if none      */
    char     cloud_model[32];      /* responding model id              */
} vq_decision_t;

#define VQ_ESC_R_PROB        (1u<<0)  /* prob >= threshold              */
#define VQ_ESC_R_UNCERTAIN   (1u<<1)  /* model uncertainty flag         */
#define VQ_ESC_R_MODEL_ERR   (1u<<2)  /* runtime error → cloud fallback */
#define VQ_ESC_R_MANUAL      (1u<<3)  /* console-forced                  */
#define VQ_ESC_R_QUALITY     (1u<<4)  /* SQI too low to trust local     */

typedef struct {
    uint32_t period_s;            /* default 180                        */
    float    threshold;           /* default 0.65                       */
    bool     motion_gate;         /* require still-enough window        */
    bool     require_ppg;         /* minimum channel set for scoring    */
    uint8_t  esc_ch[VQ_ESC_CHANNELS_MAX];  /* raw channels to attach   */
    uint32_t esc_window_s;        /* raw window length, default 60      */
} vq_decision_cfg_t;

vq_err_t vq_decision_init(const vq_decision_cfg_t *cfg);
vq_err_t vq_decision_start(void);
vq_err_t vq_decision_stop(void);
vq_err_t vq_decision_run_once(vq_decision_t *out);  /* console/test hook */
void     vq_decision_last(vq_decision_t *out);
/* Cloud advisory arrives async → stored here + event.                  */
void     vq_decision_on_cloud(float cloud_prob, const char *model_id,
                              const char *advice_json, size_t len);
