/* features.h — the feature contract between the sensing pipeline and the
 * on-device model. The model's real architecture arrives later; until
 * then the blob declares its own input shape and the decision layer maps
 * named inputs: tensor 0 = feature vector (VQ_FEATURE_DIM floats, z-
 * normalised per vq_feature_scale), optional extra tensors = raw windows.
 *
 * Feature semantics are engineering proxies, NOT clinical measurements:
 * e.g. "perfusion_index" is a signal-quality/feature value, never SpO2.
 */
#pragma once
#include "vq_types.h"

/* Feature indices — fixed order; unset channels stay NaN and the model's
 * input mask handles them. Add new features at the END only.          */
typedef enum {
    VQ_F_HR_BPM = 0,        /* PPG-derived rate proxy                  */
    VQ_F_HRV_RMSSD,         /* variability proxy                       */
    VQ_F_HR_ECG,            /* ADS1292R rate if lead-on                */
    VQ_F_RESP_RATE,         /* respiration proxy                       */
    VQ_F_PAT_MS,            /* pulse-arrival proxy, sympathetic tone   */
    VQ_F_PPG_PI_RED,        /* perfusion index (engineering proxy)     */
    VQ_F_PPG_PI_IR,
    VQ_F_PPG_SQI,           /* signal-quality index 0..1               */
    VQ_F_SPO2_PROXY,        /* R-ratio proxy — NOT calibrated SpO2     */
    VQ_F_TEMP_SKIN,
    VQ_F_TEMP_DOME_MINUS_SKIN, /* ZHF-style trend delta               */
    VQ_F_TEMP_DISTAL_MINUS_SKIN, /* perfusion-gradient proxy         */
    VQ_F_TEMP_OBJ_MINUS_SKIN,
    VQ_F_SKIN_RH,
    VQ_F_EDA_LEVEL,
    VQ_F_EDA_SLOPE,
    VQ_F_BIOZ_MAG,
    VQ_F_MOT_RMS,           /* motion energy (alarm gating)            */
    VQ_F_STILL_S,           /* seconds since last motion               */
    VQ_F_SPEC_STO2_IDX,     /* NIR730/AS7341 derived index             */
    VQ_F_CONTACT,           /* FSR ordinal                             */
    VQ_F_BATT_SOC,
    VQ_F_TEMP_BOARD,
    /* ... reserved to VQ_FEATURE_DIM                                 */
    VQ_F_COUNT
} vq_feature_idx_t;

typedef struct {
    float    v[VQ_FEATURE_DIM];
    uint64_t mask;          /* bit i = v[i] valid                       */
    int64_t  t_us;          /* window end stamp                         */
    uint32_t window_s;      /* feature window length                    */
} vq_feature_vec_t;

/* Pipeline → features: fill from the last window of samples. Stateless
 * extractors; NaN for missing. */
vq_err_t vq_features_compute(vq_feature_vec_t *out, uint32_t window_s);
/* z-score normalisation table (from training; placeholder = identity).  */
extern const float vq_feature_scale[2][VQ_FEATURE_DIM];  /* [mean][std]  */
void     vq_features_normalise(vq_feature_vec_t *f);
const char *vq_feature_name(int idx);
