/* vq_decision.c — the few-minutes policy loop (vq/include/decision.h).
 *
 * Cycle (every cfg.period_s, default 180 s; also on demand for console):
 *
 *   1. Power gate      — vq_power_state() must be NORMAL or CONSERVE,
 *                        else the cycle is skipped and logged. No scoring
 *                        while the board is preparing to sleep.
 *   2. Features        — vq_features_compute(window = 60 s).
 *   3. Motion gate     — if cfg.motion_gate: VQ_F_MOT_RMS > cap or
 *                        VQ_F_STILL_S < min → skip scoring, log. Motion
 *                        makes the window untrustworthy; escalating it
 *                        would just spam the cloud. (Post-compute check:
 *                        the gates are features of the same window.)
 *   4. Channel gate    — cfg.require_ppg: at least one PPG-derived
 *                        feature (PI_RED | PI_IR | SQI) must be valid in
 *                        the mask, else scoring is not attempted and the
 *                        cycle escalates with VQ_ESC_R_QUALITY.
 *   5. SQI gate        — VQ_F_PPG_SQI < VQ_DEC_SQI_MIN → QUALITY reason
 *                        (score still runs; it is just not trusted).
 *   6. Normalise → set model input "features" (fall back to input 0 if
 *      the blob does not declare that name) → run → prob[1].
 *   7. Reasons         — prob >= th → VQ_ESC_R_PROB;
 *                        prob in [th-0.1, th) → uncertain flag only;
 *                        any model/input/output failure → MODEL_ERR;
 *                        forced/console → MANUAL.
 *   8. Hysteresis      — rolling VQ_DEC_HIST_N-prob history; a single
 *                        over-threshold run does not escalate on PROB
 *                        alone: needs VQ_DEC_PERSIST_N consecutive, or a
 *                        high-confidence run (>= th + band). Error and
 *                        quality reasons always escalate immediately —
 *                        see vq_decision_priv.h for the rationale.
 *   9. Escalate        — build blob {hdr + feature vec + int16 channel
 *                        windows} in PSRAM → vq_log_append(VQ_LOG_-
 *                        ESCALATION) FIRST (durability), then
 *                        vq_cloud_escalate() (non-blocking queue).
 *                        Offline: the bundle sits in log_store and the
 *                        cloud layer drains it on reconnect — nothing is
 *                        silently dropped, per the contract.
 *  10. Cloud advisory  — arrives async via vq_decision_on_cloud() →
 *                        stored on the last decision + VQ_EVT_-
 *                        ESCALATION_RESULT event. Advisory only; the
 *                        device never turns it into a claim.
 *
 * Fallback ordering per decision.h: local model → (anomaly|uncertain|
 * error) → cloud model → log_store until uplink returns.
 *
 * Model acquisition: vq_model_load() (partition blob) → on any failure a
 * compiled-in zero-node VQMB stub via vq_model_load_blob() keeps the
 * pipeline end-to-end → if even that is rejected, model == NULL and
 * every cycle takes the MODEL_ERR escalation path.
 */
#include <string.h>
#include <stddef.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "decision.h"
#include "features.h"
#include "vq_model.h"
#include "pipeline.h"
#include "cloud_client.h"
#include "power_mgr.h"
#include "log_store.h"

#include "vq_decision_ext.h"
#include "vq_decision_priv.h"

static const char *TAG = "vq_dec";

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    bool               init;
    vq_decision_cfg_t  cfg;

    vq_model_t        *model;
    bool               model_ok;        /* usable graph loaded         */
    bool               model_stub;      /* ...and it is the stub       */
    int                feat_idx;        /* input slot for "features"   */

    esp_timer_handle_t timer;
    TaskHandle_t       task;
    SemaphoreHandle_t  wake_sem;        /* timer → task (binary)       */
    SemaphoreHandle_t  cycle_mtx;       /* one cycle at a time         */
    SemaphoreHandle_t  lock;            /* last / hist / force_next    */
    volatile bool      running;
    volatile bool      stop_req;
    volatile bool      task_done;       /* task committed to self-exit */

    vq_decision_t      last;
    float              hist[VQ_DEC_HIST_N];   /* ring, newest at pos-1  */
    uint8_t            hist_len;
    uint8_t            hist_pos;
    uint8_t            consec_over;
    bool               force_next;
} dec_state_t;

static dec_state_t s;

/* Persistent buffer for the compiled-in stub: vq_model_load_blob() may
 * keep a pointer into the blob (mmap-style), so it must outlive init.  */
static uint8_t s_stub_blob[256];

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static bool fmask(uint64_t mask, int idx)
{
    return idx >= 0 && idx < 64 && ((mask >> idx) & 1u);
}

static void store_last(const vq_decision_t *d)
{
    /* The async cloud advisory belongs to the previous cycle's
     * escalation — carry it forward so /status keeps showing it until
     * the next advisory lands. */
    xSemaphoreTake(s.lock, portMAX_DELAY);
    vq_decision_t prev = s.last;
    s.last = *d;
    s.last.cloud_prob = prev.cloud_prob;
    memcpy(s.last.cloud_model, prev.cloud_model, sizeof(s.last.cloud_model));
    xSemaphoreGive(s.lock);
}

static void hist_push(float p)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.hist[s.hist_pos] = p;
    s.hist_pos = (s.hist_pos + 1) % VQ_DEC_HIST_N;
    if (s.hist_len < VQ_DEC_HIST_N) s.hist_len++;
    if (p >= s.cfg.threshold) s.consec_over++;
    else                      s.consec_over = 0;
    xSemaphoreGive(s.lock);
}

static bool take_force(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool f = s.force_next;
    s.force_next = false;
    xSemaphoreGive(s.lock);
    return f;
}

/* Append a VQ_LOG_EVENT record: dec_evt_hdr_t + aux. Never fatal — an
 * absent W25Q512 just loses the event, the flow continues.            */
static void log_evt(uint32_t evt, const void *aux, uint32_t aux_len)
{
    uint8_t buf[sizeof(dec_evt_hdr_t) + 160];
    if (!aux) aux_len = 0;
    if (aux_len > sizeof(buf) - sizeof(dec_evt_hdr_t))
        aux_len = sizeof(buf) - sizeof(dec_evt_hdr_t);
    dec_evt_hdr_t h = {
        .evt     = evt,
        .t_us    = esp_timer_get_time(),
        .aux_len = aux_len,
    };
    memcpy(buf, &h, sizeof(h));
    if (aux_len) memcpy(buf + sizeof(h), aux, aux_len);
    vq_log_append(VQ_LOG_EVENT, buf, sizeof(h) + aux_len);
}

static void log_scored(const vq_decision_t *d, uint8_t flags, uint8_t pwr)
{
    dec_score_aux_t a = {
        .prob       = d->prob,
        .threshold  = d->threshold,
        .reasons    = d->reasons,
        .latency_us = d->latency_us,
        .flags      = flags,
        .pwr_state  = pwr,
    };
    log_evt(VQ_EVT_MODEL_SCORED, &a, sizeof(a));
}

/* ------------------------------------------------------------------ */
/* Compiled-in stub blob                                               */
/* ------------------------------------------------------------------ */

/* Table-free CRC32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF). Written
 * locally so the stub path has no dependency beyond vq headers; the
 * blob CRC convention (whole blob, crc32 field zeroed) is documented —
 * if the runtime disagrees it rejects the blob and we degrade to the
 * MODEL_ERR path, which is safe.                                     */
static uint32_t dec_crc32(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

/* Minimal VQMB blob: tensor[0] = input "features" f32[40], tensor[1] =
 * output "prob" f32[2], zero nodes. Keeps the loop end-to-end while the
 * real architecture is TBD — output reads back as zeros → prob ≈ 0.
 * Zero nodes means no assumptions about the on-disk node struct (the
 * header documents its fields but not the packing).                   */
static size_t build_stub_blob(uint8_t *buf, size_t cap)
{
    vqmb_header_t h;
    vq_tensor_t   t[2];
    memset(&h, 0, sizeof(h));
    memset(t, 0, sizeof(t));

    h.magic         = VQMB_MAGIC;
    h.ver           = 1;
    h.n_tensors     = 2;
    h.n_nodes       = 0;
    h.n_inputs      = 1;
    h.n_outputs     = 1;
    h.arena_bytes   = 4096;
    h.weights_bytes = 0;
    h.crc32         = 0;

    strncpy(t[0].name, "features", sizeof(t[0].name) - 1);
    t[0].dtype   = 2;                       /* f32                    */
    t[0].ndim    = 1;
    t[0].dims[0] = VQ_FEATURE_DIM;
    t[0].scale   = 1.0f;

    strncpy(t[1].name, "prob", sizeof(t[1].name) - 1);
    t[1].dtype   = 2;
    t[1].ndim    = 1;
    t[1].dims[0] = 2;                       /* {normal, problematic}  */
    t[1].scale   = 1.0f;

    size_t need = sizeof(h) + sizeof(t);
    if (cap < need) return 0;
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), t, sizeof(t));
    uint32_t crc = dec_crc32(buf, need);
    /* patch crc32 in place (offset within vqmb_header_t) */
    memcpy(buf + offsetof(vqmb_header_t, crc32), &crc, sizeof(crc));
    return need;
}

static void load_model(void)
{
    s.model = NULL;
    s.model_ok = false;
    s.model_stub = false;
    s.feat_idx = -1;

    vq_err_t err = vq_model_load(&s.model);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "model partition unusable (%s) → compiled-in stub",
                 esp_err_to_name(err));
        size_t n = build_stub_blob(s_stub_blob, sizeof(s_stub_blob));
        err = n ? vq_model_load_blob(&s.model, s_stub_blob, n)
                : (vq_err_t)ESP_ERR_NO_MEM;
        if (err == ESP_OK) s.model_stub = true;
    }
    if (err != ESP_OK || !s.model) {
        ESP_LOGE(TAG, "no usable model (%s) — every cycle escalates "
                 "MODEL_ERR until a blob lands", esp_err_to_name(err));
        return;
    }
    if (vq_model_n_inputs(s.model) < 1 || vq_model_n_outputs(s.model) < 1) {
        ESP_LOGE(TAG, "model has %u in / %u out — unusable",
                 (unsigned)vq_model_n_inputs(s.model),
                 (unsigned)vq_model_n_outputs(s.model));
        return;                             /* keep handle, mark bad  */
    }
    /* Contract mapping: named input "features" → feature vector; if the
     * blob does not declare that name, use input 0.                   */
    int idx = vq_model_input_index(s.model, "features");
    s.feat_idx = (idx >= 0) ? idx : 0;
    s.model_ok = true;
    ESP_LOGI(TAG, "model loaded (%s): %u params, arena %u B, feat input #%d",
             s.model_stub ? "stub" : "partition",
             (unsigned)vq_model_param_count(s.model),
             (unsigned)vq_model_arena_used(s.model), s.feat_idx);
}

/* ------------------------------------------------------------------ */
/* Escalation bundle                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *buf;
    uint32_t len;
    bool     heap;                          /* false → static hdr-only */
} bundle_t;

/* Quantize one channel window into the cursor. *pw advances past
 * {ch_hdr + n int16}. n_in is clamped by the caller to what fits.      */
static void pack_channel(uint8_t **pw, const vq_sample_t *smp, size_t n)
{
    vq_esc_ch_hdr_t ch;
    memset(&ch, 0, sizeof(ch));
    ch.ch    = (uint8_t)smp[0].channel;
    ch.codec = 0;
    ch.n     = (uint16_t)n;
    ch.t0_us = smp[0].t_us;
    ch.dt_us = (n >= 2) ? (int32_t)((smp[n - 1].t_us - smp[0].t_us) / (int64_t)(n - 1)) : 0;

    float mn = INFINITY, mx = -INFINITY;
    for (size_t i = 0; i < n; i++) {
        float v = smp[i].value;
        if (!isfinite(v)) continue;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    if (mx > mn) {
        ch.scale  = (mx - mn) / 65534.0f;    /* valid codes ±32767      */
        ch.offset = (mx + mn) * 0.5f;
    } else {
        ch.scale  = 1.0f;
        ch.offset = isfinite(mn) ? mn : 0.0f;
    }

    memcpy(*pw, &ch, sizeof(ch));
    *pw += sizeof(ch);
    for (size_t i = 0; i < n; i++) {
        int16_t q;
        float v = smp[i].value;
        if (!isfinite(v) || ch.scale <= 0.0f) q = VQ_DEC_Q_NAN;
        else {
            long l = lrintf((v - ch.offset) / ch.scale);
            if (l > 32767)  l = 32767;
            if (l < -32767) l = -32767;
            q = (int16_t)l;
        }
        memcpy(*pw, &q, sizeof(q));
        *pw += sizeof(q);
    }
}

/* Build the durable escalation payload: fixed header + feature vector +
 * int16-packed raw windows for cfg.esc_ch[]. PSRAM first; on allocation
 * failure degrade to a header-only static buffer — an escalation is
 * never dropped because a window would not fit.                        */
static bundle_t build_bundle(const vq_decision_t *d,
                             const vq_feature_vec_t *fv, bool feat_valid)
{
    bundle_t b = {0};

    /* Per-channel cap sized to the configured window, hard-capped.     */
    size_t chcap = (size_t)s.cfg.esc_window_s * VQ_DEC_ASSUMED_MAX_SPS + 64;
    if (chcap > VQ_DEC_CH_MAX_SAMPLES) chcap = VQ_DEC_CH_MAX_SAMPLES;
    if (chcap < 16) chcap = 16;

    /* The durable copy goes through vq_log_append — anything larger than
     * VQ_LOG_MAX_PAYLOAD is rejected whole (silent escalation loss).
     * Bound the blob AND the pack cap by the record limit: the pack loop
     * then truncates channel windows instead of dropping the bundle.   */
    size_t blob_cap = sizeof(vq_esc_hdr_t) +
                      VQ_ESC_CHANNELS_MAX * (sizeof(vq_esc_ch_hdr_t) + chcap * 2);
    if (blob_cap > VQ_LOG_MAX_PAYLOAD)
        blob_cap = VQ_LOG_MAX_PAYLOAD;
    size_t stage_sz = chcap * sizeof(vq_sample_t);

    uint8_t *blob = heap_caps_malloc(blob_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!blob) blob = heap_caps_malloc(blob_cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    static uint8_t hdr_only[sizeof(vq_esc_hdr_t)];   /* last-ditch buf  */
    bool heap_blob = (blob != NULL);
    size_t cap;
    if (!blob) { blob = hdr_only; cap = sizeof(hdr_only); }
    else       { cap = blob_cap; }

    /* Staging only pays off when the blob can actually hold samples.   */
    vq_sample_t *stage = NULL;
    if (heap_blob) {
        stage = heap_caps_malloc(stage_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!stage)
            stage = heap_caps_malloc(stage_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!stage) {
            /* Reduced staging: ship truncated windows rather than none. */
            chcap    = 4096;
            stage_sz = chcap * sizeof(vq_sample_t);
            stage    = heap_caps_malloc(stage_sz,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }

    uint8_t *w = blob;
    vq_esc_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.magic      = VQ_ESC_BLOB_MAGIC;
    h.ver        = VQ_ESC_BLOB_VER;
    h.flags      = feat_valid ? 0x01 : 0x00;
    h.t_us       = esp_timer_get_time();
    h.latency_us = d->latency_us;
    h.prob       = d->prob;
    h.threshold  = d->threshold;
    h.reasons    = d->reasons;
    if (feat_valid && fv) {
        h.feat_mask = fv->mask;
        memcpy(h.feat, fv->v, sizeof(h.feat));
    }
    memcpy(w, &h, sizeof(h));
    w += sizeof(h);

    uint64_t seen = 0;                              /* dedupe esc_ch[]  */
    int64_t  win_us = (int64_t)s.cfg.esc_window_s * 1000000;
    if (stage && heap_blob) {
        for (size_t i = 0; i < VQ_ESC_CHANNELS_MAX; i++) {
            uint8_t ch = s.cfg.esc_ch[i];
            if (ch >= VQ_CH_COUNT || (seen & (1ull << ch))) continue;
            seen |= 1ull << ch;

            size_t n = vq_pipeline_window((vq_channel_id_t)ch, win_us,
                                          stage, chcap);
            if (n == 0) continue;
            /* clamp to remaining room (n*2 + ch hdr) */
            size_t room = cap - (size_t)(w - blob);
            if (room <= sizeof(vq_esc_ch_hdr_t) + 2) break;
            size_t nfit = (room - sizeof(vq_esc_ch_hdr_t)) / 2;
            if (n > nfit) n = nfit;                 /* newest-first? keep
                                                     * the FIRST nfit —
                                                     * ring returns from
                                                     * window start.    */
            pack_channel(&w, stage, n);
            h.n_ch++;
        }
    }

    /* patch final n_ch into the written header */
    memcpy(blob, &h, sizeof(h));
    b.buf  = blob;
    b.len  = (uint32_t)(w - blob);
    b.heap = heap_blob;

    if (stage) heap_caps_free(stage);
    if (!heap_blob)
        ESP_LOGW(TAG, "blob alloc failed — feature-only escalation (%u B)",
                 (unsigned)b.len);
    return b;
}

static void free_bundle(bundle_t *b)
{
    if (b->heap && b->buf) heap_caps_free(b->buf);
    b->buf = NULL;
}

/* ------------------------------------------------------------------ */
/* The decision cycle                                                  */
/* ------------------------------------------------------------------ */

static void decision_cycle(bool manual, vq_decision_t *out)
{
    vq_decision_t d;
    memset(&d, 0, sizeof(d));
    d.prob       = NAN;
    d.threshold  = s.cfg.threshold;
    d.latency_us = -1;
    d.cloud_prob = -1;

    vq_pwr_state_t pwr = vq_power_state();
    if (manual) d.reasons |= VQ_ESC_R_MANUAL;   /* console-forced mark   */

    /* -------- gate 0: power ------------------------------------- */
    if (pwr != VQ_PWR_NORMAL && pwr != VQ_PWR_CONSERVE) {
        ESP_LOGW(TAG, "cycle skipped — power state %d", (int)pwr);
        log_scored(&d, DEC_SCORE_F_SKIPPED | DEC_SCORE_F_POWER_GATE,
                   (uint8_t)pwr);
        store_last(&d);
        if (out) *out = d;
        return;
    }

    /* -------- features ------------------------------------------ */
    vq_feature_vec_t fv;
    memset(&fv, 0, sizeof(fv));
    bool feat_ok = (vq_features_compute(&fv, VQ_DEC_FEATURE_WINDOW_S) == ESP_OK);
    if (!feat_ok) {
        d.reasons  |= VQ_ESC_R_MODEL_ERR;
        d.uncertain = true;
    }

    /* -------- gate 1: motion (post-compute) ---------------------- */
    if (feat_ok && s.cfg.motion_gate) {
        bool moving = false;
        if (fmask(fv.mask, VQ_F_MOT_RMS) &&
            fv.v[VQ_F_MOT_RMS] > VQ_DEC_MOT_RMS_MAX) moving = true;
        if (fmask(fv.mask, VQ_F_STILL_S) &&
            fv.v[VQ_F_STILL_S] < VQ_DEC_STILL_S_MIN) moving = true;
        if (moving) {
            ESP_LOGI(TAG, "cycle skipped — motion gate (rms %.3f, still %.1f s)",
                     fmask(fv.mask, VQ_F_MOT_RMS) ? (double)fv.v[VQ_F_MOT_RMS] : -1.0,
                     fmask(fv.mask, VQ_F_STILL_S) ? (double)fv.v[VQ_F_STILL_S] : -1.0);
            log_scored(&d, DEC_SCORE_F_SKIPPED | DEC_SCORE_F_MOTION_GATE,
                       (uint8_t)pwr);
            store_last(&d);
            if (out) *out = d;
            return;
        }
    }

    /* -------- gate 2/3: channel set + SQI (raw features) --------- */
    bool ppg_missing = false;
    if (feat_ok && s.cfg.require_ppg) {
        uint64_t ppg = (1ull << VQ_F_PPG_SQI) |
                       (1ull << VQ_F_PPG_PI_RED) |
                       (1ull << VQ_F_PPG_PI_IR);
        ppg_missing = ((fv.mask & ppg) == 0);
        if (ppg_missing) {
            d.reasons  |= VQ_ESC_R_QUALITY;   /* can't trust local score */
            d.uncertain = true;
        }
    }
    if (feat_ok && fmask(fv.mask, VQ_F_PPG_SQI) &&
        fv.v[VQ_F_PPG_SQI] < VQ_DEC_SQI_MIN) {
        d.reasons  |= VQ_ESC_R_QUALITY;
        d.uncertain = true;
    }

    /* Normalise once, after the raw-value gates above: the model input
     * and the escalation feature vector are then always the same z-
     * scored vector regardless of which gate fired.                  */
    if (feat_ok) vq_features_normalise(&fv);

    /* -------- model ---------------------------------------------- */
    bool scored = false;
    if (feat_ok && !ppg_missing && s.model_ok) {
        if (vq_model_set_input_f32(s.model, s.feat_idx, fv.v,
                                   VQ_FEATURE_DIM) != ESP_OK) {
            d.reasons  |= VQ_ESC_R_MODEL_ERR;
            d.uncertain = true;
        } else {
            int64_t lat = -1;
            if (vq_model_run(s.model, &lat) != ESP_OK) {
                d.reasons  |= VQ_ESC_R_MODEL_ERR;
                d.uncertain = true;
            } else {
                d.latency_us = lat;
                float pr[2] = {NAN, NAN};
                if (vq_model_get_output_f32(s.model, 0, pr, 2) != ESP_OK ||
                    !isfinite(pr[1])) {
                    d.reasons  |= VQ_ESC_R_MODEL_ERR;
                    d.uncertain = true;
                } else {
                    d.prob = pr[1];
                    if (d.prob < 0.0f) d.prob = 0.0f;
                    if (d.prob > 1.0f) d.prob = 1.0f;
                    scored = true;
                    hist_push(d.prob);        /* + consec_over update  */
                    if (d.prob >= s.cfg.threshold) {
                        d.reasons |= VQ_ESC_R_PROB;
                    } else if (d.prob >= s.cfg.threshold - VQ_DEC_UNCERT_BAND) {
                        d.uncertain = true;   /* band: flag only       */
                    }
                }
            }
        }
    } else if (feat_ok && !s.model_ok) {
        d.reasons  |= VQ_ESC_R_MODEL_ERR;
        d.uncertain = true;
    }
    /* ppg_missing && require_ppg → scoring not attempted (contract:
     * PPG is the minimum channel set); QUALITY reason already set.     */

    /* -------- escalation decision -------------------------------- */
    bool force = take_force();
    if (force) d.reasons |= VQ_ESC_R_MANUAL;

    bool escalate = force;
    if (d.reasons & (VQ_ESC_R_MODEL_ERR | VQ_ESC_R_QUALITY))
        escalate = true;                            /* errors: now      */
    if ((d.reasons & VQ_ESC_R_PROB) &&
        (s.consec_over >= VQ_DEC_PERSIST_N ||
         d.prob >= fminf(0.99f, s.cfg.threshold + VQ_DEC_UNCERT_BAND)))
        escalate = true;                            /* persistent/high  */

    uint8_t ev_flags = (scored ? 0 : DEC_SCORE_F_SKIPPED) |
                       ((force || manual) ? DEC_SCORE_F_FORCED : 0);
    log_scored(&d, ev_flags, (uint8_t)pwr);

    if (escalate) {
        bundle_t b = build_bundle(&d, &fv, feat_ok);
        /* Durable FIRST, uplink second: if we are offline the bundle
         * sits in log_store and the cloud layer drains it later.       */
        bool persisted =
            (vq_log_append(VQ_LOG_ESCALATION, b.buf, b.len) == ESP_OK);
        bool queued =
            (vq_cloud_escalate(&d, &fv, b.buf, b.len) == ESP_OK);
        d.escalated = queued || persisted;          /* handed off, not lost */

        dec_esc_aux_t a = {
            .prob      = d.prob,
            .reasons   = d.reasons,
            .blob_len  = b.len,
            .queued    = (uint8_t)queued,
            .persisted = (uint8_t)persisted,
        };
        log_evt(VQ_EVT_ESCALATION_SENT, &a, sizeof(a));
        ESP_LOGW(TAG, "escalation: prob %.3f th %.3f reasons 0x%02x blob %u B "
                 "queued %d persisted %d", (double)d.prob, (double)d.threshold,
                 (unsigned)d.reasons, (unsigned)b.len, queued, persisted);
        free_bundle(&b);
    } else {
        ESP_LOGI(TAG, "score: prob %.3f th %.3f lat %lld us consec %u%s",
                 (double)d.prob, (double)d.threshold, (long long)d.latency_us,
                 (unsigned)s.consec_over, d.uncertain ? " (uncertain)" : "");
    }

    store_last(&d);
    if (out) *out = d;
}

/* ------------------------------------------------------------------ */
/* Timer + task                                                        */
/* ------------------------------------------------------------------ */

static void timer_cb(void *arg)
{
    (void)arg;
    /* esp_timer TASK dispatch (default) — safe to give a semaphore; the
     * binary sem coalesces missed firings: late cycles are not queued. */
    if (s.wake_sem) xSemaphoreGive(s.wake_sem);
}

static void decision_task(void *arg)
{
    (void)arg;
    while (!s.stop_req) {
        if (xSemaphoreTake(s.wake_sem,
                           pdMS_TO_TICKS(VQ_DEC_WAKE_POLL_MS)) != pdTRUE)
            continue;
        if (s.stop_req) break;
        if (xSemaphoreTake(s.cycle_mtx, 0) != pdTRUE) {
            ESP_LOGD(TAG, "tick skipped — console run_once in progress");
            continue;
        }
        decision_cycle(false, NULL);
        xSemaphoreGive(s.cycle_mtx);
    }
    /* Committed to exit: stop() may no longer delete our handle — it
     * could be gone by the time it runs. task_done is the join flag. */
    s.task_done = true;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* Public API (decision.h)                                             */
/* ------------------------------------------------------------------ */

vq_err_t vq_decision_init(const vq_decision_cfg_t *cfg)
{
    if (s.init) return ESP_ERR_INVALID_STATE;

    memset(&s, 0, sizeof(s));
    if (cfg) s.cfg = *cfg;
    /* defaults + sanitation */
    if (s.cfg.period_s == 0)     s.cfg.period_s    = VQ_DEC_DEFAULT_PERIOD_S;
    if (!(s.cfg.threshold > 0.0f && s.cfg.threshold <= 1.0f))
        s.cfg.threshold = VQ_DEC_DEFAULT_THRESH;
    if (s.cfg.esc_window_s == 0) s.cfg.esc_window_s = VQ_DEC_DEFAULT_ESC_WIN_S;
    if (s.cfg.esc_window_s > VQ_DEC_ESC_WIN_MAX_S)
        s.cfg.esc_window_s = VQ_DEC_ESC_WIN_MAX_S;

    s.last.prob       = NAN;
    s.last.threshold  = s.cfg.threshold;
    s.last.latency_us = -1;
    s.last.cloud_prob = -1.0f;                    /* "-1 if none"        */

    s.lock      = xSemaphoreCreateMutex();
    s.cycle_mtx = xSemaphoreCreateMutex();
    if (!s.lock || !s.cycle_mtx) {
        if (s.lock)      vSemaphoreDelete(s.lock);
        if (s.cycle_mtx) vSemaphoreDelete(s.cycle_mtx);
        s.lock = s.cycle_mtx = NULL;
        return ESP_ERR_NO_MEM;
    }

    load_model();

    s.init = true;
    ESP_LOGI(TAG, "decision loop init: period %u s, th %.2f, motion_gate %d, "
             "require_ppg %d, esc_win %u s", (unsigned)s.cfg.period_s,
             (double)s.cfg.threshold, s.cfg.motion_gate, s.cfg.require_ppg,
             (unsigned)s.cfg.esc_window_s);
    return ESP_OK;
}

vq_err_t vq_decision_start(void)
{
    if (!s.init)    return ESP_ERR_INVALID_STATE;
    if (s.running)  return ESP_ERR_INVALID_STATE;

    s.stop_req  = false;
    s.task_done = false;
    if (!s.wake_sem) s.wake_sem = xSemaphoreCreateBinary();
    if (!s.wake_sem) return ESP_ERR_NO_MEM;

    if (xTaskCreate(decision_task, "vq_dec", VQ_DEC_TASK_STACK, NULL,
                    VQ_DEC_TASK_PRIO, &s.task) != pdPASS)
        return ESP_ERR_NO_MEM;

    const esp_timer_create_args_t ta = {
        .callback = timer_cb,
        .name     = "vq_dec",
    };
    bool ok = (esp_timer_create(&ta, &s.timer) == ESP_OK) &&
              (esp_timer_start_periodic(s.timer,
                       (uint64_t)s.cfg.period_s * 1000000ull) == ESP_OK);
    if (!ok) {
        if (s.timer) {
            esp_timer_stop(s.timer);
            esp_timer_delete(s.timer);
            s.timer = NULL;
        }
        s.stop_req = true;
        xSemaphoreGive(s.wake_sem);
        for (int i = 0; i < 100 && !s.task_done; i++)
            vTaskDelay(pdMS_TO_TICKS(10));
        if (!s.task_done) {
            vTaskDelete(s.task);
            vSemaphoreDelete(s.cycle_mtx);   /* may be held → recreate   */
            s.cycle_mtx = xSemaphoreCreateMutex();
        }
        s.task = NULL;
        return ESP_FAIL;
    }
    s.running = true;
    ESP_LOGI(TAG, "decision loop started (%u s cadence)",
             (unsigned)s.cfg.period_s);
    return ESP_OK;
}

vq_err_t vq_decision_stop(void)
{
    if (!s.init)    return ESP_ERR_INVALID_STATE;
    if (!s.running) return ESP_OK;                 /* idempotent         */

    s.running  = false;
    s.stop_req = true;
    if (s.timer) {
        esp_timer_stop(s.timer);
        esp_timer_delete(s.timer);
        s.timer = NULL;
    }
    if (s.wake_sem) xSemaphoreGive(s.wake_sem);    /* unstick the task   */
    for (int i = 0; i < VQ_DEC_STOP_WAIT_MS / 10 && !s.task_done; i++)
        vTaskDelay(pdMS_TO_TICKS(10));
    if (!s.task_done) {
        /* Stuck inside a cycle (flash write / model run). Force-delete
         * is safe here only because task_done is never set mid-cycle.  */
        ESP_LOGW(TAG, "task still in cycle after %d ms — force delete",
                 VQ_DEC_STOP_WAIT_MS);
        vTaskDelete(s.task);
        /* If the task died holding cycle_mtx the mutex would stay taken
         * forever — recreate it so the next start()/run_once() works.  */
        vSemaphoreDelete(s.cycle_mtx);
        s.cycle_mtx = xSemaphoreCreateMutex();
    }
    s.task = NULL;
    return ESP_OK;
}

vq_err_t vq_decision_run_once(vq_decision_t *out)
{
    if (!s.init) return ESP_ERR_INVALID_STATE;
    vq_decision_t d;
    if (xSemaphoreTake(s.cycle_mtx,
                       pdMS_TO_TICKS(VQ_DEC_RUNONCE_WAIT_MS)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    decision_cycle(true, &d);      /* MANUAL provenance bit; same path   */
    xSemaphoreGive(s.cycle_mtx);
    if (out) *out = d;
    return ESP_OK;
}

void vq_decision_last(vq_decision_t *out)
{
    if (!out) return;
    if (!s.init) {
        memset(out, 0, sizeof(*out));
        out->prob       = NAN;
        out->cloud_prob = -1.0f;
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    *out = s.last;
    xSemaphoreGive(s.lock);
}

void vq_decision_on_cloud(float cloud_prob, const char *model_id,
                          const char *advice_json, size_t len)
{
    if (!s.init) return;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.last.cloud_prob = cloud_prob;
    if (model_id) {
        strncpy(s.last.cloud_model, model_id, sizeof(s.last.cloud_model) - 1);
        s.last.cloud_model[sizeof(s.last.cloud_model) - 1] = '\0';
    }
    xSemaphoreGive(s.lock);

    dec_esc_res_aux_t a;
    memset(&a, 0, sizeof(a));
    a.cloud_prob = cloud_prob;
    if (model_id) strncpy(a.model, model_id, sizeof(a.model) - 1);
    if (advice_json && len) {
        a.advice_len = (len > sizeof(a.advice)) ? sizeof(a.advice) : (uint16_t)len;
        memcpy(a.advice, advice_json, a.advice_len);
    }
    log_evt(VQ_EVT_ESCALATION_RESULT, &a, sizeof(a));
    ESP_LOGI(TAG, "cloud advisory: prob %.3f model %s (%u B advice)",
             (double)cloud_prob, model_id ? model_id : "-", (unsigned)len);
}

/* ------------------------------------------------------------------ */
/* Extension hooks (vq_decision_ext.h)                                  */
/* ------------------------------------------------------------------ */

void vq_decision_force_next(void)
{
    if (!s.init) return;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.force_next = true;
    xSemaphoreGive(s.lock);
}

size_t vq_decision_prob_hist(float *out, size_t max)
{
    if (!out || !s.init) return 0;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    size_t n = s.hist_len;
    if (n > max) n = max;
    /* oldest → newest: oldest sits at hist_pos when the ring is full   */
    for (size_t i = 0; i < n; i++) {
        size_t idx = (s.hist_pos + VQ_DEC_HIST_N - s.hist_len + i) % VQ_DEC_HIST_N;
        out[i] = s.hist[idx];
    }
    xSemaphoreGive(s.lock);
    return n;
}

uint8_t vq_decision_consec_over(void)
{
    return s.consec_over;                 /* byte read — atomic on Xtensa */
}

bool vq_decision_is_running(void)
{
    return s.running;
}
