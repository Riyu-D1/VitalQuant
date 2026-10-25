/* cloud_client.c — VitalQ cloud uplink (component vq_cloud).
 *
 * Implements components/vq/include/cloud_client.h (frozen contract — do not
 * modify that header). Three flows, all serviced by the dedicated "vq_net"
 * FreeRTOS task so callers (pipeline/decision/console) never block on
 * Wi-Fi/TLS:
 *
 *   1. Batch upload   POST {base}/v1/ingest/batch      — vq_cloud_flush()
 *   2. Escalation     POST {base}/v1/edge/escalation   — vq_cloud_escalate()
 *   3. Heartbeat      POST {base}/v1/device/heartbeat  — vq_cloud_heartbeat()
 *
 * Auth (device key from NVS vitalq/cloud_key, `cloud set <url> <key>`):
 *   Authorization: Bearer <key> + X-Device-Key: <key> + X-Device-Id: <id>.
 *   (Bearer + X-Device-Id per brief; X-Device-Key is what docs/05-api-spec.md
 *    and software/src/vitalq/ingest/auth.py actually verify — send all three.)
 *
 * TLS: esp_http_client + esp_crt_bundle_attach when the URL scheme is https;
 * plain http is accepted for the lab. Timeout 10 s per request.
 *
 * ── Drained record payloads (what the batch is assembled FROM) ──────────
 * vq_log_iterate() feeds us log records oldest→newest; each record's payload
 * is auto-detected per record (self-describing, msgpack-free):
 *
 *   a) VQ_LOG_BATCH_OUT — "JSON fragment" records. Canonical contract for
 *      the pipeline batch writer (documented HERE so the writer can match
 *      it byte-for-byte): payload is a JSON array; elements are either
 *        sample:  {"ch":<u8 chan>,"t_us":<i64>,"v":<num>}  or [<ch>,<t_us>,<v>]
 *        event:   {"ev":1,"t_us":<i64>,"kind":"<s>","detail":{…}}
 *                 or ["e",<t_us>,"<kind>",{…}]
 *      Key aliases tolerated on read: ch|c|chan, t_us|t|ts, v|val.
 *
 *   b) VQ_LOG_SAMPLES — raw binary vq_sample_t[] dumps (what vq_pipeline's
 *      logger task actually writes: VQ_LOG_BATCH_CAP samples per record).
 *      Decoded field-by-field via memcpy — payload alignment is not assumed.
 *      Non-zero sample flags are carried as "f".
 *
 *   c) VQ_LOG_EVENT — journaled events (bounded drain, see VQ_DRAIN_EVENTS).
 *      Two producer layouts recognised by shape:
 *        {u32 evt, i64 t_us, u32 aux_len} + aux bytes      (vq_decision)
 *        {u32 id,  i64 t_us, char tag[12]}                 (vq_pipeline)
 *      Emitted as {"t_us":…,"kind":"<evt-name|tag>","detail":{…}}.
 *
 *   d) VQ_LOG_ESCALATION — persisted escalation bundles. vq_decision writes
 *      the VQEB/v1 blob durably BEFORE calling vq_cloud_escalate() — so an
 *      offline bundle "sits in log_store until uplink returns" (decision.h).
 *      The flush drain POSTs each as its own /v1/edge/escalation request and
 *      only counts a record drained on 2xx. (Trade-off, documented: a bundle
 *      that succeeded via the RAM path is still in the ring and will be
 *      posted once more — harmless advisory duplicate, dedup is server-side.)
 *
 * ── Drop protocol (safe under drop_oldest = global FIFO) ────────────────
 * vq_log_drop_oldest(n) frees the n GLOBALLY-oldest records — it cannot
 * select by type. Dropping n = #drained could destroy un-uploaded records
 * that interleave with the drained span. Instead we drop only the longest
 * provably-uploaded prefix: drop_n = #{drained ids < B} where
 *   B = min( per-type bound ),  bound_T = (last drained id of T)+1 if the
 *   T-drain stopped early (cap / byte limit / POST failure), or +inf if it
 *   ran dry. Since every type is drained (or fully consumed), B stays ahead
 *   of real data: nothing un-uploaded is ever dropped; the worst case is a
 *   re-uploaded tail (new batch_id ⇒ duplicate raw rows — tolerable).
 * batch_id = deterministic hash(device|first_t|last_t): identical
 * retransmissions dedup via the server's 409 replay path, which we also
 * treat as delivered.
 *
 * ── Wire schema note ──────────────────────────────────────────────────
 * build_batch_json() is THE single place the envelope is rendered. Emission
 * is a superset of the brief shape:
 *   {device_id, batch_id, session_id, firmware_version, profile:"hw_v3",
 *    profile_hash, clock:{wall_time,monotonic_us,reliable},
 *    clock_anchor:{wall_us,mono_us,reliable}, n_*, samples:[{ch,t_us,v[,f]}],
 *    events:[{t_us,kind,detail}], disclaimer}
 * The repo's ingest service (docs/05 + software/src/vitalq/core/types.py,
 * BatchIngest) wants scalars[]/windows[]/frames[] split by channel role with
 * channel NAMES — if the backend is enforced strictly, only this function
 * needs the remap (vq_channel_name() maps ids→registry strings); everything
 * upstream of it is schema-agnostic.
 *
 * Research prototype: payloads are engineering data; every envelope carries
 * an explicit disclaimer. No clinical content in or out.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "mbedtls/base64.h"
#include "cJSON.h"

#include "vq_types.h"
#include "cloud_client.h"      /* the frozen contract                     */
#include "log_store.h"
#include "wifi_mgr.h"
#include "power_mgr.h"
#include "clock_service.h"
#include "features.h"

static const char *TAG = "vq_cloud";

/* ------------------------------------------------------------------ */
/* Tunables                                                            */
/* ------------------------------------------------------------------ */
#define VQ_NVS_NS            "vitalq"
#define VQ_NVS_URL           "cloud_url"
#define VQ_NVS_KEY           "cloud_key"
#define VQ_NVS_SESS          "cloud_sess"   /* optional session uuid      */

#define VQ_NET_TASK_STACK    8192           /* bytes (xTaskCreate)        */
#define VQ_NET_TASK_PRIO     5
#define VQ_REQ_QUEUE_LEN     8
#define VQ_PEND_N            4              /* deferred-request slots     */
#define VQ_MAX_ATTEMPTS      4

#define VQ_HTTP_TIMEOUT_MS   10000
#define VQ_BURST_GATE_MS     5000           /* vq_wifi_burst_wait arg     */
#define VQ_BURST_BYTES       2048           /* ≥ → power burst markers    */
#define VQ_BURST_EST_MA      400

#define VQ_BATCH_MAX_RECORDS 256            /* hard cap per flush         */
#define VQ_BATCH_MAX_BYTES   (160 * 1024)   /* emitted JSON cap           */
#define VQ_BATCH_MAX_SAMPLES 4000
#define VQ_DRAIN_EVENTS      1              /* 0 → keep journal local     */
#define VQ_EVENT_DRAIN_MAX   32             /* per flush                  */
#define VQ_ESC_DRAIN_MAX     8              /* persisted bundles / flush  */

#define VQ_RESP_CAP          (16 * 1024)
#define VQ_AUTO_FLUSH_MS     (5 * 60 * 1000)/* idle drain cadence         */

#define VQ_PROFILE_STR       "hw_v3"
#define VQ_FW_STR            "fix_v22-0.2.0-dev"
#define VQ_DISCLAIMER        "research prototype - engineering data only, no clinical claims"

#define VQ_URL_MAX           128
#define VQ_KEY_MAX           96
#define VQ_SESS_MAX          48
#define VQ_DEVID_MAX         32

/* ------------------------------------------------------------------ */
/* VQEB/v1 escalation blob — mirrors the versioned wire format in       */
/* vq_decision_priv.h. Parsed defensively: any mismatch → raw b64 only. */
/* ------------------------------------------------------------------ */
#define VQEB_MAGIC           0x42455156u    /* "VQEB" little-endian       */
#define VQEB_VER             1u
#define VQEB_HDR_LEN         204            /* u32 magic; u16 ver; u8 flags;
                                               u8 n_ch; i64 t_us; i64 lat;
                                               f32 prob; f32 thr; u32 reas;
                                               u64 feat_mask; f32 feat[40] */
#define VQEB_CH_HDR_LEN      24             /* u8 ch; u8 codec; u16 n;
                                               i32 dt_us; i64 t0_us;
                                               f32 scale; f32 offset      */

/* ------------------------------------------------------------------ */
/* Types + statics                                                      */
/* ------------------------------------------------------------------ */
typedef enum { RQ_FLUSH = 0, RQ_ESCALATION, RQ_HEARTBEAT } rq_kind_t;

typedef struct {
    rq_kind_t        kind;
    uint8_t          attempts;
    bool             has_dec, has_feat;
    int64_t          not_before_ms;    /* defer-until (esp_timer ms)      */
    size_t           max_records;      /* FLUSH                           */
    vq_decision_t    dec;              /* ESCALATION                      */
    vq_feature_vec_t feat;             /* ESCALATION                      */
    uint8_t         *blob;             /* PSRAM copy taken at call time   */
    size_t           blob_len;
} net_req_t;

typedef struct {                       /* growable emit buffer           */
    char    *p;
    size_t   len, cap;
    bool     oom;
} gbuf_t;

typedef struct {                       /* HTTP response accumulator      */
    char    *buf;
    size_t   len;
    bool     overflow;
} resp_acc_t;

typedef struct {
    int       status;                  /* HTTP status or -1              */
    esp_err_t err;
    char     *body;                    /* malloc'd, may be NULL          */
    size_t    body_len;
    uint32_t  rtt_ms;
} http_result_t;

typedef struct {                       /* drained-record id ledger       */
    uint32_t *ids;
    uint32_t  n, cap;
} idlist_t;

static QueueHandle_t     s_q;
static TaskHandle_t      s_task;
static SemaphoreHandle_t s_cfg_mtx;
static volatile bool     s_cfg_ok;
static char              s_base_url[VQ_URL_MAX]; /* trailing '/' stripped*/
static char              s_dev_key[VQ_KEY_MAX];
static char              s_sess_id[VQ_SESS_MAX];
static char              s_dev_id[VQ_DEVID_MAX]; /* "vitalq-<mac12>"      */

static net_req_t         s_pend[VQ_PEND_N];
static uint8_t           s_pend_n;

static volatile bool     s_flush_qd, s_hb_qd;
static int64_t           s_last_auto_ms;

static struct {                          /* stats surface               */
    uint64_t ok, fail, bytes_up;
    uint32_t rtt_ms;
    uint32_t auth_fail, frag_bad;
    int64_t  clk_off_us;                 /* last clock_correction        */
    float    clk_drift_ppm;
    bool     clk_corr_valid;
} s_st;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static uint64_t fnv1a64(const void *data, size_t len, uint64_t seed)
{
    const uint8_t *d = data;
    uint64_t h = 1469598103934665603ull ^ seed;
    for (size_t i = 0; i < len; i++) { h ^= d[i]; h *= 1099511628211ull; }
    return h;
}

static bool gb_init(gbuf_t *b, size_t cap)
{
    b->p = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b->p) b->p = malloc(cap);
    b->len = 0; b->cap = b->p ? cap : 0; b->oom = (b->p == NULL);
    return !b->oom;
}
static void gb_free(gbuf_t *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

static bool gb_reserve(gbuf_t *b, size_t n)
{
    if (b->len + n + 1 <= b->cap) return true;
    size_t nc = b->cap ? b->cap * 2 : 1024;
    while (b->len + n + 1 > nc) nc *= 2;
    char *np = heap_caps_realloc(b->p, nc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!np) np = realloc(b->p, nc);
    if (!np) { b->oom = true; return false; }
    b->p = np; b->cap = nc;
    return true;
}

static bool gb_putn(gbuf_t *b, const void *src, size_t n)
{
    if (!gb_reserve(b, n)) return false;
    memcpy(b->p + b->len, src, n);
    b->len += n; b->p[b->len] = '\0';
    return true;
}
static bool gb_puts(gbuf_t *b, const char *s) { return gb_putn(b, s, strlen(s)); }

static bool gb_printf(gbuf_t *b, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) { va_end(ap2); b->oom = true; return false; }
    if (!gb_reserve(b, (size_t)need)) { va_end(ap2); return false; }
    vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)need;
    return true;
}

/* "2026-09-21T10:50:00.123Z" */
static void iso8601_us(int64_t wall_us, char *out, size_t n)
{
    time_t sec = (time_t)(wall_us / 1000000);
    int    ms  = (int)((wall_us % 1000000) / 1000);
    struct tm tmv;
    gmtime_r(&sec, &tmv);
    char tmp[24] = {0};
    strftime(tmp, sizeof(tmp), "%Y-%m-%dT%H:%M:%S", &tmv);
    snprintf(out, n, "%s.%03dZ", tmp, ms);
}

static void idl_add(idlist_t *l, uint32_t id) { if (l->n < l->cap) l->ids[l->n++] = id; }

/* ------------------------------------------------------------------ */
/* Config (NVS)                                                        */
/* ------------------------------------------------------------------ */
static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(VQ_NVS_NS, NVS_READONLY, &h) != ESP_OK) { s_cfg_ok = false; return; }
    size_t n = sizeof(s_base_url);
    if (nvs_get_str(h, VQ_NVS_URL, s_base_url, &n) != ESP_OK) s_base_url[0] = 0;
    n = sizeof(s_dev_key);
    if (nvs_get_str(h, VQ_NVS_KEY, s_dev_key, &n) != ESP_OK) s_dev_key[0] = 0;
    n = sizeof(s_sess_id);
    if (nvs_get_str(h, VQ_NVS_SESS, s_sess_id, &n) != ESP_OK) s_sess_id[0] = 0;
    nvs_close(h);
    size_t u = strlen(s_base_url);
    while (u && s_base_url[u - 1] == '/') s_base_url[--u] = 0;
    /* Both halves are required — the ingest service (auth.py) 401s
     * without X-Device-Key, so a URL-only config is NOT usable.       */
    s_cfg_ok = (s_base_url[0] != 0 && s_dev_key[0] != 0);
}

static void derive_dev_id(void)
{
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_dev_id, sizeof(s_dev_id), "vitalq-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ------------------------------------------------------------------ */
/* HTTP plumbing (net-task context only)                                */
/* ------------------------------------------------------------------ */
static esp_err_t http_evt(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        resp_acc_t *a = evt->user_data;
        size_t room = (a->len < VQ_RESP_CAP) ? VQ_RESP_CAP - a->len : 0;
        size_t take = (size_t)evt->data_len < room ? (size_t)evt->data_len : room;
        if (take) {
            char *np = a->buf ? realloc(a->buf, a->len + take + 1)
                              : malloc(take + 1);
            if (!np) { a->overflow = true; return ESP_OK; }
            a->buf = np;
            memcpy(a->buf + a->len, evt->data, take);
            a->len += take; a->buf[a->len] = 0;
        }
        if (take < (size_t)evt->data_len) a->overflow = true;
    }
    return ESP_OK;
}

/* One POST. Caller has already passed the wifi/power gates.            */
static http_result_t http_post(const char *path, const char *body, size_t blen)
{
    http_result_t r = {.status = -1, .err = ESP_FAIL, .body = NULL,
                       .body_len = 0, .rtt_ms = 0};
    resp_acc_t acc = {0};

    /* snapshot config under the mutex — config writes are rare and the
     * copy is small, but we must not hold it across perform()          */
    char url[VQ_URL_MAX + 32];
    char key[VQ_KEY_MAX];
    if (s_cfg_mtx) xSemaphoreTake(s_cfg_mtx, portMAX_DELAY);
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);
    strncpy(key, s_dev_key, sizeof(key) - 1);
    key[sizeof(key) - 1] = 0;
    if (s_cfg_mtx) xSemaphoreGive(s_cfg_mtx);

    esp_http_client_config_t cfg = {
        .url                   = url,
        .method                = HTTP_METHOD_POST,
        .timeout_ms            = VQ_HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .buffer_size           = 2048,
        .buffer_size_tx        = 1024,
        .event_handler         = http_evt,
        .user_data             = &acc,
    };
    if (strncmp(url, "https://", 8) == 0)              /* TLS → mbedTLS  */
        cfg.crt_bundle_attach = esp_crt_bundle_attach; /* cert bundle    */

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return r;

    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "X-Device-Id", s_dev_id);
    if (key[0]) {
        char auth[VQ_KEY_MAX + 8];
        snprintf(auth, sizeof(auth), "Bearer %s", key);
        esp_http_client_set_header(c, "Authorization", auth);
        esp_http_client_set_header(c, "X-Device-Key", key);
    }
    esp_http_client_set_post_field(c, body, (int)blen);

    bool burst = (blen > VQ_BURST_BYTES);
    if (burst) vq_power_burst_begin(VQ_BURST_EST_MA);   /* multi-KB mark  */
    int64_t t0 = esp_timer_get_time();
    r.err = esp_http_client_perform(c);
    r.rtt_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (burst) vq_power_burst_end();

    if (r.err == ESP_OK) r.status = esp_http_client_get_status_code(c);
    else ESP_LOGW(TAG, "POST %s transport: %s", path, esp_err_to_name(r.err));
    esp_http_client_cleanup(c);

    r.body = acc.buf; r.body_len = acc.len;
    if (acc.overflow)
        ESP_LOGW(TAG, "POST %s response truncated @%u B", path, (unsigned)VQ_RESP_CAP);
    return r;
}

/* 0 delivered / <0 retryable / >0 permanent — batch also accepts 409.  */
static int classify(const http_result_t *r, bool replay_ok)
{
    int s = r->status;
    if (s >= 200 && s < 300) return 0;
    if (replay_ok && s == 409) return 0;      /* idempotent replay = have it */
    if (s == 401 || s == 403) { s_st.auth_fail++; return -1; }
    if (s == 429 || s >= 500 || s < 0) return -1;
    return 1;                                  /* other 4xx: schema etc.  */
}

/* ------------------------------------------------------------------ */
/* VQEB/v1 defensive parse                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    bool     ok;
    uint8_t  n_ch, flags;
    int64_t  t_us, latency_us;
    float    prob, threshold;
    uint32_t reasons;
    uint64_t feat_mask;
    float    feat[VQ_FEATURE_DIM];
    uint8_t  ch_ids[VQ_ESC_CHANNELS_MAX];
    uint16_t ch_n[VQ_ESC_CHANNELS_MAX];
    int32_t  ch_dt_us[VQ_ESC_CHANNELS_MAX];
    int64_t  ch_t0_us[VQ_ESC_CHANNELS_MAX];
    float    ch_scale[VQ_ESC_CHANNELS_MAX], ch_off[VQ_ESC_CHANNELS_MAX];
} vqeb_t;

static uint16_t rd_u16(const uint8_t *p){ uint16_t v; memcpy(&v,p,2); return v; }
static uint32_t rd_u32(const uint8_t *p){ uint32_t v; memcpy(&v,p,4); return v; }
static uint64_t rd_u64(const uint8_t *p){ uint64_t v; memcpy(&v,p,8); return v; }
static int64_t  rd_i64(const uint8_t *p){ int64_t  v; memcpy(&v,p,8); return v; }
static float    rd_f32(const uint8_t *p){ float    v; memcpy(&v,p,4); return v; }

static bool vqeb_parse(const uint8_t *b, size_t len, vqeb_t *o)
{
    memset(o, 0, sizeof(*o));
    if (!b || len < VQEB_HDR_LEN) return false;
    if (rd_u32(b) != VQEB_MAGIC || rd_u16(b + 4) != VQEB_VER) return false;
    o->flags      = b[6];
    o->n_ch       = b[7];
    o->t_us       = rd_i64(b + 8);
    o->latency_us = rd_i64(b + 16);
    o->prob       = rd_f32(b + 24);
    o->threshold  = rd_f32(b + 28);
    o->reasons    = rd_u32(b + 32);
    o->feat_mask  = rd_u64(b + 36);
    memcpy(o->feat, b + 44, sizeof(o->feat));
    size_t off = 44 + 4 * VQ_FEATURE_DIM;
    for (uint8_t i = 0; i < o->n_ch && i < VQ_ESC_CHANNELS_MAX; i++) {
        if (off + VQEB_CH_HDR_LEN > len) { o->n_ch = i; break; }
        const uint8_t *h = b + off;
        uint16_t n = rd_u16(h + 2);
        if (off + VQEB_CH_HDR_LEN + (size_t)n * 2 > len) { o->n_ch = i; break; }
        o->ch_ids[i]   = h[0];
        o->ch_n[i]     = n;
        o->ch_dt_us[i] = (int32_t)rd_u32(h + 4);
        o->ch_t0_us[i] = rd_i64(h + 8);
        o->ch_scale[i] = rd_f32(h + 16);
        o->ch_off[i]   = rd_f32(h + 20);
        off += VQEB_CH_HDR_LEN + (size_t)n * 2;
    }
    o->ok = true;
    return true;
}

/* ------------------------------------------------------------------ */
/* Record drain                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    gbuf_t  *samp, *ev;
    idlist_t *ids;
    uint32_t n_records, n_samples, n_events, n_bad;
    int64_t  first_t, last_t;
    bool     hit_cap;
    /* per-type boundary tracking for the prefix-drop protocol           */
    uint8_t  cur_type;                  /* VQ_LOG_* of active iterate    */
    uint8_t  stopped_type;              /* type the drain stopped in (0) */
    uint8_t  visited;                   /* bitmask 1<<type               */
    uint32_t last_id[5];                /* last FULLY drained id / type  */
} drain_ctx_t;

static void emit_sample(drain_ctx_t *c, int ch, int64_t t_us, double v,
                        unsigned flags)
{
    if (!isfinite(v)) { c->n_bad++; return; }
    if (flags)
        gb_printf(c->samp, "%s{\"ch\":%d,\"t_us\":%lld,\"v\":%.9g,\"f\":%u}",
                  c->n_samples ? "," : "", ch, (long long)t_us, v, flags);
    else
        gb_printf(c->samp, "%s{\"ch\":%d,\"t_us\":%lld,\"v\":%.9g}",
                  c->n_samples ? "," : "", ch, (long long)t_us, v);
    c->n_samples++;
    if (t_us < c->first_t) c->first_t = t_us;
    if (t_us > c->last_t)  c->last_t  = t_us;
}

static void emit_event(drain_ctx_t *c, int64_t t_us, const char *kind_json,
                       const char *detail_json)
{
    gb_printf(c->ev, "%s{\"t_us\":%lld,\"kind\":%s,\"detail\":%s}",
              c->n_events ? "," : "", (long long)t_us,
              kind_json ? kind_json : "\"evt\"",
              detail_json ? detail_json : "{}");
    c->n_events++;
    if (t_us < c->first_t) c->first_t = t_us;
    if (t_us > c->last_t)  c->last_t  = t_us;
}

static double json_num(const cJSON *o, const char *k1, const char *k2,
                       const char *k3, bool *found)
{
    const char *ks[3] = {k1, k2, k3};
    for (int i = 0; i < 3 && ks[i]; i++) {
        cJSON *it = cJSON_GetObjectItem(o, ks[i]);
        if (cJSON_IsNumber(it)) { *found = true; return it->valuedouble; }
    }
    *found = false;
    return 0.0;
}

/* BATCH_OUT record: JSON fragment array (contract at file header)      */
static void drain_json_record(drain_ctx_t *c, const uint8_t *p, uint32_t len)
{
    cJSON *arr = cJSON_ParseWithLength((const char *)p, len);
    if (!arr || !cJSON_IsArray(arr)) { c->n_bad++; if (arr) cJSON_Delete(arr); return; }

    cJSON *el;
    cJSON_ArrayForEach(el, arr) {
        if (c->samp->len + c->ev->len > VQ_BATCH_MAX_BYTES ||
            c->n_samples >= VQ_BATCH_MAX_SAMPLES) { c->hit_cap = true; break; }
        if (cJSON_IsArray(el)) {
            cJSON *e0 = cJSON_GetArrayItem(el, 0);
            cJSON *e1 = cJSON_GetArrayItem(el, 1);
            cJSON *e2 = cJSON_GetArrayItem(el, 2);
            if (cJSON_IsString(e0)) {
                /* ["e",t_us,"kind",{detail}] */
                cJSON *e3 = cJSON_GetArrayItem(el, 3);
                char *kd = (e2 && cJSON_IsString(e2)) ? cJSON_PrintUnformatted(e2) : NULL;
                char *dd = (e3 && !cJSON_IsNull(e3)) ? cJSON_PrintUnformatted(e3) : NULL;
                if (cJSON_IsNumber(e1))
                    emit_event(c, (int64_t)e1->valuedouble, kd, dd);
                else c->n_bad++;
                if (kd) cJSON_free(kd);
                if (dd) cJSON_free(dd);
            } else if (cJSON_IsNumber(e0) && cJSON_IsNumber(e1) && cJSON_IsNumber(e2)) {
                emit_sample(c, (int)e0->valuedouble, (int64_t)e1->valuedouble,
                            e2->valuedouble, 0);
            } else c->n_bad++;
        } else if (cJSON_IsObject(el)) {
            if (cJSON_GetObjectItem(el, "ev")) {
                bool ft; double t = json_num(el, "t_us", "t", "ts", &ft);
                cJSON *k  = cJSON_GetObjectItem(el, "kind");
                cJSON *dt = cJSON_GetObjectItem(el, "detail");
                char *kd = (k && cJSON_IsString(k)) ? cJSON_PrintUnformatted(k) : NULL;
                char *dd = (dt && !cJSON_IsNull(dt)) ? cJSON_PrintUnformatted(dt) : NULL;
                emit_event(c, (int64_t)(ft ? t : 0), kd, dd);
                if (kd) cJSON_free(kd);
                if (dd) cJSON_free(dd);
            } else {
                bool f1, f2, f3;
                double ch = json_num(el, "ch", "c", "chan", &f1);
                double tt = json_num(el, "t_us", "t", "ts", &f2);
                double vv = json_num(el, "v", "val", NULL, &f3);
                if (f1 && f2 && f3)
                    emit_sample(c, (int)ch, (int64_t)tt, vv, 0);
                else c->n_bad++;
            }
        } else c->n_bad++;
    }
    cJSON_Delete(arr);
}

/* SAMPLES record: binary vq_sample_t[] (pipeline logger format)        */
static void drain_binary_record(drain_ctx_t *c, const uint8_t *p, uint32_t len)
{
    size_t n = len / sizeof(vq_sample_t);
    if (!n) { c->n_bad++; return; }
    for (size_t i = 0; i < n; i++) {
        if (c->samp->len + c->ev->len > VQ_BATCH_MAX_BYTES ||
            c->n_samples >= VQ_BATCH_MAX_SAMPLES) { c->hit_cap = true; return; }
        vq_sample_t s;
        memcpy(&s, p + i * sizeof(s), sizeof(s));
        if (s.channel >= VQ_CH_COUNT) { c->n_bad++; continue; }
        emit_sample(c, (int)s.channel, s.t_us, (double)s.value, s.flags);
    }
}

/* EVENT record producers (payload layouts at file header)              */
static const char *const s_evt_names[] = {
    "boot","sensor_fault","i2c_err","spi_err","low_battery","emergency_shutdown",
    "vbus_in","vbus_out","charge_state","model_scored","escalation_sent",
    "escalation_result","upload_ok","upload_fail","deep_sleep_enter","watchdog_warn"};

static void drain_event_record(drain_ctx_t *c, const uint8_t *p, uint32_t len)
{
    uint32_t id = 0; int64_t t = 0;
    const uint8_t *aux = NULL; uint32_t alen = 0;
    char kindbuf[20]; const char *kind = NULL;

    /* pipeline evt_payload_t — NOT packed upstream: id@0, pad, t_us@8,
     * tag[12]@16, sizeof == 32. Tag must be printable to claim the shape. */
    if (len == 32) {
        const uint8_t *tag = p + 16;
        bool printable = true;
        for (int i = 0; i < 12 && tag[i]; i++)
            if (tag[i] < 0x20 || tag[i] > 0x7e) printable = false;
        if (printable) {
            memcpy(&id, p, 4); memcpy(&t, p + 8, 8);
            char tg[13]; memcpy(tg, tag, 12); tg[12] = 0;
            snprintf(kindbuf, sizeof(kindbuf), "%s", tg[0] ? tg : "evt");
            kind = kindbuf;
        }
    }
    /* vq_decision dec_evt_hdr_t (packed): evt@0, t_us@4, aux_len@12, aux@16 */
    if (!kind && len >= 16) {
        uint32_t al; memcpy(&al, p + 12, 4);
        if (len == 16 + al) {
            memcpy(&id, p, 4); memcpy(&t, p + 4, 8); aux = p + 16; alen = al;
        }
    }
    if (!kind && !aux) { c->n_bad++; return; }
    if (!kind) {
        if (id < sizeof(s_evt_names) / sizeof(s_evt_names[0]))
            snprintf(kindbuf, sizeof(kindbuf), "%s", s_evt_names[id]);
        else
            snprintf(kindbuf, sizeof(kindbuf), "evt_%lu", (unsigned long)id);
        kind = kindbuf;
    }
    /* JSON safety: tag/aux-name bytes are producer-controlled — strip
     * characters that would break the emitted object                  */
    for (char *q = kindbuf; *q; q++)
        if (*q == '"' || *q == '\\') *q = '_';
    if (aux && alen) {
        /* opaque aux → hex detail (bounded); restore on mid-emit OOM so
         * the events array never carries a truncated tail element       */
        size_t m = c->ev->len; uint32_t ne = c->n_events;
        gb_printf(c->ev, "%s{\"t_us\":%lld,\"kind\":\"%s\",\"detail\":{\"aux_len\":%lu,\"aux\":\"",
                  c->n_events ? "," : "", (long long)t, kind, (unsigned long)alen);
        for (uint32_t i = 0; i < alen && i < 64; i++) gb_printf(c->ev, "%02x", aux[i]);
        if (!gb_puts(c->ev, "\"}}")) { c->ev->len = m; if (c->ev->p) c->ev->p[m] = 0; c->n_events = ne; return; }
        c->n_events++;
        if (t < c->first_t) c->first_t = t;
        if (t > c->last_t)  c->last_t  = t;
    } else {
        char kj[32]; snprintf(kj, sizeof(kj), "\"%s\"", kind);
        emit_event(c, t, kj, NULL);
    }
}

/* iterate cb for BATCH_OUT + SAMPLES (dc->cur_type set by caller).
 * A record that is cut mid-emit (byte cap) is NOT counted drained — its id
 * is pulled back out so it re-uploads whole later. A fully-parsed record
 * that emitted zero elements stays in the ledger: it is unparseable and can
 * never be delivered — dropping it with the batch prevents head-of-line
 * poisoning (counted via frag_bad, logged).                             */
static bool drain_cb(const vq_log_hdr_t *h, const uint8_t *payload, void *ctx_)
{
    drain_ctx_t *c = ctx_;
    if (c->hit_cap || c->ids->n >= c->ids->cap ||
        c->samp->len + c->ev->len > VQ_BATCH_MAX_BYTES) {
        c->hit_cap = true;
        if (!c->stopped_type) c->stopped_type = c->cur_type;
        return false;
    }
    /* "visited" = ≥1 record actually drained. A type whose FIRST cb call
     * already hits the shared cap stays unvisited → drain_bound() falls
     * back to oldest_id(T) instead of falsely treating it as drained.   */
    c->visited |= (uint8_t)(1u << c->cur_type);
    idl_add(c->ids, h->id);
    c->n_records++;
    if (h->t_us < c->first_t) c->first_t = h->t_us;
    if (h->t_us > c->last_t)  c->last_t  = h->t_us;

    const uint8_t *q = payload;
    size_t off = 0;
    while (off < h->len && (q[off] == ' ' || q[off] == '\t' ||
           q[off] == '\r' || q[off] == '\n')) off++;
    if (off < h->len && q[off] == '[')
        drain_json_record(c, payload, h->len);
    else if (h->len >= sizeof(vq_sample_t) && h->len % sizeof(vq_sample_t) == 0)
        drain_binary_record(c, payload, h->len);
    else
        c->n_bad++;                       /* unknown payload → drop-with-batch */

    if (c->hit_cap) {
        /* record cut mid-emit → only part of it went on the wire; pull
         * the id back so it re-uploads WHOLE in a later batch          */
        c->ids->n--;
        if (!c->stopped_type) c->stopped_type = c->cur_type;
        return false;
    }
    /* fully handled (incl. zero-element/corrupt records — kept in the
     * ledger on purpose: they can never be delivered, and dropping them
     * with the batch prevents head-of-line poisoning; counted frag_bad) */
    c->last_id[c->cur_type] = h->id;
    if (c->ids->n >= c->ids->cap) {
        if (!c->stopped_type) c->stopped_type = c->cur_type;
        return false;
    }
    return true;
}

/* iterate cb for EVENT (bounded drain) */
static bool drain_evt_cb(const vq_log_hdr_t *h, const uint8_t *payload, void *ctx_)
{
    drain_ctx_t *c = ctx_;
    if (c->hit_cap || c->n_events >= VQ_EVENT_DRAIN_MAX ||
        c->ids->n >= c->ids->cap ||
        c->samp->len + c->ev->len > VQ_BATCH_MAX_BYTES) {
        c->hit_cap = true;
        if (!c->stopped_type) c->stopped_type = c->cur_type;
        return false;
    }
    c->visited |= (uint8_t)(1u << c->cur_type);
    idl_add(c->ids, h->id);
    c->n_records++;
    drain_event_record(c, payload, h->len);   /* tiny — always complete */
    c->last_id[c->cur_type] = h->id;
    if (c->n_events >= VQ_EVENT_DRAIN_MAX || c->ids->n >= c->ids->cap) {
        if (!c->stopped_type) c->stopped_type = c->cur_type;
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* batch_id — deterministic hash(device|first_t|last_t), uuid-shaped    */
/* ------------------------------------------------------------------ */
static void make_batch_id(int64_t first_t, int64_t last_t, char *out, size_t n)
{
    uint64_t h1 = fnv1a64(s_dev_id, strlen(s_dev_id), 0x1);
    h1 = fnv1a64(&first_t, sizeof(first_t), h1);
    h1 = fnv1a64(&last_t,  sizeof(last_t),  h1);
    uint64_t h2 = fnv1a64(s_dev_id, strlen(s_dev_id), 0x2);
    h2 = fnv1a64(&last_t,  sizeof(last_t),  h2);
    h2 = fnv1a64(&first_t, sizeof(first_t), h2);
    snprintf(out, n, "%08lx-%04lx-%04lx-%04lx-%012llx",
             (unsigned long)(h1 >> 32),
             (unsigned long)((h1 >> 16) & 0xffff),
             (unsigned long)(h1 & 0xffff),
             (unsigned long)(h2 >> 48),
             (unsigned long long)(h2 & 0xffffffffffffULL));
}

/* ------------------------------------------------------------------ */
/* THE schema function — the envelope lives HERE and only here.         */
/* ------------------------------------------------------------------ */
static bool build_batch_json(gbuf_t *out, drain_ctx_t *dc)
{
    gbuf_t samp = {0}, ev = {0};
    if (!gb_init(&samp, 16 * 1024)) return false;
    gb_init(&ev, 4 * 1024);

    dc->samp = &samp; dc->ev = &ev;
    dc->first_t = INT64_MAX; dc->last_t = 0;
    dc->n_records = dc->n_samples = dc->n_events = dc->n_bad = 0;
    dc->hit_cap = false; dc->visited = 0; dc->stopped_type = 0;
    memset(dc->last_id, 0, sizeof(dc->last_id));

    dc->cur_type = VQ_LOG_BATCH_OUT;                     /* JSON fragments */
    vq_log_iterate(VQ_LOG_BATCH_OUT, drain_cb, dc);
    if (!dc->hit_cap) {
        dc->cur_type = VQ_LOG_SAMPLES;                   /* binary dumps   */
        vq_log_iterate(VQ_LOG_SAMPLES, drain_cb, dc);
    }
#if VQ_DRAIN_EVENTS
    if (!dc->hit_cap) {
        dc->cur_type = VQ_LOG_EVENT;
        vq_log_iterate(VQ_LOG_EVENT, drain_evt_cb, dc);
    }
#endif

    if (dc->n_samples == 0 && dc->n_events == 0) {
        gb_free(&samp); gb_free(&ev); return false;
    }
    if (dc->first_t == INT64_MAX) dc->first_t = dc->last_t = 0;

    char bid[40];
    make_batch_id(dc->first_t, dc->last_t, bid, sizeof(bid));

    vq_clock_anchor_t an = vq_clock_anchor();
    char wall[40] = "1970-01-01T00:00:00.000Z";
    if (an.reliable && an.wall_us > 0) iso8601_us(an.wall_us, wall, sizeof(wall));

    /* ── Envelope — schema changes happen HERE (see header note) ── */
    if (!gb_init(out, 64 * 1024)) { gb_free(&samp); gb_free(&ev); return false; }
    gb_printf(out,
        "{\"device_id\":\"%s\",\"batch_id\":\"%s\",\"session_id\":\"%s\","
        "\"firmware_version\":\"%s (idf %s)\",\"profile\":\"%s\",\"profile_hash\":\"%s\","
        "\"clock\":{\"wall_time\":\"%s\",\"monotonic_us\":%lld,\"reliable\":%s},"
        "\"clock_anchor\":{\"wall_us\":%lld,\"mono_us\":%lld,\"reliable\":%s},"
        "\"n_samples\":%lu,\"n_events\":%lu,\"n_records\":%lu,"
        "\"disclaimer\":\"%s\",\"samples\":[",
        s_dev_id, bid,
        s_sess_id[0] ? s_sess_id : "00000000-0000-0000-0000-000000000000",
        VQ_FW_STR, esp_get_idf_version(), VQ_PROFILE_STR, VQ_PROFILE_STR,
        wall, (long long)an.mono_us, an.reliable ? "true" : "false",
        (long long)an.wall_us, (long long)an.mono_us, an.reliable ? "true" : "false",
        (unsigned long)dc->n_samples, (unsigned long)dc->n_events,
        (unsigned long)dc->n_records, VQ_DISCLAIMER);
    gb_putn(out, samp.p ? samp.p : "", samp.len);
    gb_puts(out, "],\"events\":[");
    gb_putn(out, ev.p ? ev.p : "", ev.len);
    gb_puts(out, "]}");
    gb_free(&samp); gb_free(&ev);
    if (dc->n_bad) s_st.frag_bad += dc->n_bad;
    return !out->oom;
}

/* Parse ingest response; honour "clock_correction" (log + store).      */
static void batch_resp(const http_result_t *r)
{
    if (!r->body) return;
    cJSON *j = cJSON_Parse(r->body);
    if (!j) return;
    const cJSON *cc = cJSON_GetObjectItem(j, "clock_correction");
    if (cc) {
        const cJSON *o = cJSON_GetObjectItem(cc, "offset_us");
        const cJSON *d = cJSON_GetObjectItem(cc, "drift_ppm");
        if (cJSON_IsNumber(o)) { s_st.clk_off_us = (int64_t)o->valuedouble; s_st.clk_corr_valid = true; }
        if (cJSON_IsNumber(d)) s_st.clk_drift_ppm = (float)d->valuedouble;
        if (s_st.clk_corr_valid)
            ESP_LOGI(TAG, "clock_correction: offset %lld us (%.3f ms), drift %.2f ppm",
                     (long long)s_st.clk_off_us,
                     (double)(s_st.clk_off_us / 1000.0), (double)s_st.clk_drift_ppm);
    }
    const cJSON *acc = cJSON_GetObjectItem(j, "accepted");
    const cJSON *st  = cJSON_GetObjectItem(j, "server_time");
    ESP_LOGI(TAG, "batch ack: accepted=%lld server_time=%s",
             cJSON_IsNumber(acc) ? (long long)acc->valuedouble : -1LL,
             cJSON_IsString(st) ? st->valuestring : "?");
    cJSON_Delete(j);
}

/* ------------------------------------------------------------------ */
/* Escalation                                                          */
/* ------------------------------------------------------------------ */
static void emit_reasons(gbuf_t *o, uint32_t r)
{
    static const struct { uint32_t bit; const char *n; } rn[] = {
        {VQ_ESC_R_PROB, "prob"}, {VQ_ESC_R_UNCERTAIN, "uncertain"},
        {VQ_ESC_R_MODEL_ERR, "model_err"}, {VQ_ESC_R_MANUAL, "manual"},
        {VQ_ESC_R_QUALITY, "quality"},
    };
    bool first = true;
    for (size_t i = 0; i < sizeof(rn) / sizeof(rn[0]); i++)
        if (r & rn[i].bit) {
            gb_printf(o, "%s\"%s\"", first ? "" : ",", rn[i].n);
            first = false;
        }
}

static void emit_features(gbuf_t *o, uint64_t mask, const float *v)
{
    bool first = true;
    for (int i = 0; i < VQ_FEATURE_DIM; i++) {
        if (!(mask & (1ull << i)) || !isfinite(v[i])) continue;
        const char *nm = vq_feature_name(i);
        char fb[16];
        if (!nm || !nm[0]) { snprintf(fb, sizeof(fb), "f%d", i); nm = fb; }
        gb_printf(o, "%s\"%s\":%.6g", first ? "" : ",", nm, (double)v[i]);
        first = false;
    }
}

/* Escalation envelope — shared by the RAM path (dec+feat live) and the
 * durable-drain path (fields recovered from the VQEB header).          */
static char *build_escalation_json(const vq_decision_t *dec,
                                   const vq_feature_vec_t *feat,
                                   const uint8_t *blob, size_t blob_len,
                                   size_t *out_len)
{
    vqeb_t e;
    bool eb = vqeb_parse(blob, blob_len, &e);

    float    prob  = dec  ? dec->prob      : (eb ? e.prob      : NAN);
    float    thr   = dec  ? dec->threshold : (eb ? e.threshold : NAN);
    uint32_t rsn   = dec  ? dec->reasons   : (eb ? e.reasons   : 0);
    bool     unc   = dec  ? dec->uncertain : (rsn & (VQ_ESC_R_UNCERTAIN | VQ_ESC_R_QUALITY)) != 0;
    uint64_t fmask = feat ? feat->mask     : (eb && (e.flags & 1) ? e.feat_mask : 0);
    const float *fv = feat ? feat->v       : (eb && (e.flags & 1) ? e.feat : NULL);

    /* windows payload: base64 of the verbatim VQEB blob — self-describing
     * (header + per-ch {ch,codec,n,dt_us,t0_us,scale,offset} + s16le).  */
    size_t   b64cap = blob_len ? ((blob_len + 2) / 3) * 4 + 1 : 1;
    uint8_t *b64 = heap_caps_malloc(b64cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b64) b64 = malloc(b64cap);
    size_t b64len = 0;
    if (b64 && blob_len &&
        mbedtls_base64_encode(b64, b64cap, &b64len, blob, blob_len) != 0)
        b64len = 0;

    bool wok = false;
    int64_t wall = vq_clock_wall_us(&wok);
    char wbuf[40];
    if (wok && wall > 0) iso8601_us(wall, wbuf, sizeof(wbuf));

    gbuf_t o = {0};
    if (!gb_init(&o, 8 * 1024)) { if (b64) free(b64); return NULL; }
    gb_printf(&o, "{\"device_id\":\"%s\",\"t_wall\":", s_dev_id);
    if (wok && wall > 0) gb_printf(&o, "\"%s\"", wbuf); else gb_puts(&o, "null");
    gb_printf(&o, ",\"t_wall_ok\":%s,\"t_us\":%lld,",
              (wok && wall > 0) ? "true" : "false",
              (long long)(eb ? e.t_us : esp_timer_get_time()));
    if (isfinite(prob)) gb_printf(&o, "\"prob\":%.4f,", (double)prob);
    else                gb_puts(&o, "\"prob\":null,");
    if (isfinite(thr))  gb_printf(&o, "\"threshold\":%.4f,", (double)thr);
    else                gb_puts(&o, "\"threshold\":null,");
    gb_printf(&o, "\"reasons\":%lu,\"reason_names\":[", (unsigned long)rsn);
    emit_reasons(&o, rsn);
    gb_printf(&o, "],\"uncertain\":%s,\"features\":{", unc ? "true" : "false");
    if (fv) emit_features(&o, fmask, fv);
    gb_printf(&o, "},\"windows\":{\"format\":\"s16le-b64\",\"container\":\"vqeb/%u\","
              "\"n_bytes\":%lu,\"n_samples\":%lu,\"channel_ids\":[",
              eb ? VQEB_VER : 0u,
              (unsigned long)blob_len, (unsigned long)(blob_len / 2));
    if (eb)
        for (uint8_t i = 0; i < e.n_ch; i++) {
            const char *nm = (e.ch_ids[i] < VQ_CH_COUNT) ? vq_channel_name(e.ch_ids[i]) : NULL;
            gb_printf(&o, "%s\"%s\"", i ? "," : "", nm ? nm : "?");
        }
    gb_puts(&o, "],\"channels\":[");
    if (eb)
        for (uint8_t i = 0; i < e.n_ch; i++)
            gb_printf(&o, "%s{\"ch\":%u,\"n\":%u,\"dt_us\":%ld,\"t0_us\":%lld,"
                      "\"scale\":%.6g,\"offset\":%.6g}", i ? "," : "",
                      (unsigned)e.ch_ids[i], (unsigned)e.ch_n[i],
                      (long)e.ch_dt_us[i], (long long)e.ch_t0_us[i],
                      (double)e.ch_scale[i], (double)e.ch_off[i]);
    gb_puts(&o, "],\"data\":\"");
    if (b64len) gb_putn(&o, b64, b64len);
    gb_printf(&o, "\"},\"disclaimer\":\"%s\"}", VQ_DISCLAIMER);
    if (b64) free(b64);
    if (o.oom) { gb_free(&o); return NULL; }
    *out_len = o.len;
    return o.p;
}

static void esc_resp(const http_result_t *r)
{
    if (!r->body) return;
    cJSON *j = cJSON_Parse(r->body);
    if (!j) return;
    float prob = -1.0f;
    const cJSON *p = cJSON_GetObjectItem(j, "prob");
    if (cJSON_IsNumber(p)) prob = (float)p->valuedouble;
    const cJSON *m = cJSON_GetObjectItem(j, "model_id");
    const char *mid = (m && cJSON_IsString(m)) ? m->valuestring : "";
    const cJSON *a = cJSON_GetObjectItem(j, "advice");
    /* strdup → free(); cJSON_PrintUnformatted → cJSON_free()           */
    char *adv = NULL; bool adv_cjson = false;
    if (a) {
        if (cJSON_IsString(a)) adv = strdup(a->valuestring);
        else { adv = cJSON_PrintUnformatted(a); adv_cjson = true; }
    }
    /* advisory → decision layer (vq_decision component implements it) */
    vq_decision_on_cloud(prob, mid, adv ? adv : "", adv ? strlen(adv) : 0);
    ESP_LOGI(TAG, "cloud advisory: prob %.3f model \"%s\"", (double)prob, mid);
    if (adv) { if (adv_cjson) cJSON_free(adv); else free(adv); }
    cJSON_Delete(j);
}

/* ------------------------------------------------------------------ */
/* Heartbeat                                                           */
/* ------------------------------------------------------------------ */
static const char *pwr_state_name(vq_pwr_state_t s)
{
    switch (s) {
    case VQ_PWR_NORMAL:     return "normal";
    case VQ_PWR_CONSERVE:   return "conserve";
    case VQ_PWR_LOW_CUTOFF: return "low_cutoff";
    case VQ_PWR_EMERGENCY:  return "emergency";
    }
    return "unknown";
}

static char *build_heartbeat_json(size_t *out_len)
{
    vq_power_status_t ps; vq_power_status(&ps);
    vq_decision_t d;    vq_decision_last(&d);
    gbuf_t o = {0};
    if (!gb_init(&o, 1024)) return NULL;
    gb_printf(&o,
        "{\"device_id\":\"%s\",\"uptime_s\":%lld,"
        "\"power\":{\"vcell\":%.3f,\"soc\":%.1f,\"state\":\"%s\",\"charging\":%s,\"vbus\":%s},"
        "\"wifi_rssi\":%d,\"clock_synced\":%s,\"last_prob\":",
        s_dev_id, (long long)(esp_timer_get_time() / 1000000),
        (double)ps.vcell_v, (double)ps.soc_pct, pwr_state_name(ps.state),
        ps.charging ? "true" : "false", ps.vbus ? "true" : "false",
        (int)vq_wifi_rssi(), vq_clock_synced() ? "true" : "false");
    if (isfinite(d.prob)) gb_printf(&o, "%.4f", (double)d.prob);
    else                  gb_puts(&o, "null");
    gb_printf(&o, ",\"disclaimer\":\"%s\"}", VQ_DISCLAIMER);
    if (o.oom) { gb_free(&o); return NULL; }
    *out_len = o.len;
    return o.p;
}

/* ------------------------------------------------------------------ */
/* Prefix-drop helpers (protocol at file header)                        */
/* ------------------------------------------------------------------ */
static bool first_id_cb(const vq_log_hdr_t *h, const uint8_t *p, void *c)
{ (void)p; *(uint32_t *)c = h->id; return false; }

static uint32_t oldest_id(uint32_t type)
{
    uint32_t id = UINT32_MAX;
    vq_log_iterate(type, first_id_cb, &id);
    return id;
}

/* count drained ids below bound — they form the provably-uploaded prefix */
static uint32_t count_below(const uint32_t *ids, uint32_t n, uint32_t bound)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) if (ids[i] < bound) k++;
    return k;
}

/* bound B = min over all log types of the first NOT-delivered id:
 *   ESCALATION       → last_posted+1 when the drain stopped, else +inf
 *   batch types      → last_drained+1 when drain stopped early in T
 *                      (+inf when T ran dry), or oldest_id(T) when T was
 *                      never reached. batch_ok=false treats every batch
 *                      type as undrained (their records stay for retry).
 * drop_n = #{delivered ids < B} — a global prefix of delivered records,
 * so drop_oldest() can never take an un-uploaded record.               */
static uint32_t drain_bound(const drain_ctx_t *dc, uint32_t esc_bound,
                            bool batch_ok)
{
    static const uint32_t btypes[3] = {VQ_LOG_BATCH_OUT, VQ_LOG_SAMPLES, VQ_LOG_EVENT};
    uint32_t b = esc_bound;
    for (int i = 0; i < 3; i++) {
        uint32_t t = btypes[i], cand = UINT32_MAX;
        if (!batch_ok || !(dc->visited & (uint8_t)(1u << t)))
            cand = oldest_id(t);
        else if (dc->stopped_type == t)
            cand = dc->last_id[t] + 1;
        /* else: fully drained → +inf */
        if (cand < b) b = cand;
    }
    return b;
}

/* ------------------------------------------------------------------ */
/* Send paths — each returns 0 delivered / <0 retryable / >0 permanent  */
/* ------------------------------------------------------------------ */
static void stat_ok(size_t bytes, uint32_t rtt)
{ s_st.ok++; s_st.bytes_up += bytes; s_st.rtt_ms = rtt; }
static void stat_fail(void) { s_st.fail++; }

/* Persisted-escalation drain: POST each VQ_LOG_ESCALATION record,
 * oldest→newest, inside the iterate cb (payload lifetime bound to cb).
 * Only 2xx'd ids join `drops`; first failure stops the pass.           */
typedef struct {
    idlist_t *drops;
    uint32_t  posted;
    uint32_t  last_posted_id;
    bool      stop;
    bool      gate_fail;               /* burst gate timed out mid-drain */
} esc_drain_t;

static bool esc_post_cb(const vq_log_hdr_t *h, const uint8_t *p, void *ctx_)
{
    esc_drain_t *c = ctx_;
    if (c->stop || c->posted >= VQ_ESC_DRAIN_MAX) { c->stop = true; return false; }
    /* the gate is a poll, not a lease — re-check it before EVERY POST,
     * including each drained bundle (a long drain can outlive the
     * power window that admitted the request).                          */
    if (vq_wifi_burst_wait(VQ_BURST_GATE_MS) != ESP_OK) {
        c->stop = true; c->gate_fail = true; return false;
    }

    size_t blen = 0;
    char *body = build_escalation_json(NULL, NULL, p, h->len, &blen);
    if (!body) { c->stop = true; return false; }
    http_result_t r = http_post("/v1/edge/escalation", body, blen);
    free(body);
    int cls = classify(&r, false);
    if (cls == 0) {
        idl_add(c->drops, h->id);
        c->posted++; c->last_posted_id = h->id;
        esc_resp(&r);                          /* advisory → decision    */
        stat_ok(blen, r.rtt_ms);
    } else {
        c->stop = true;
        if (cls > 0) ESP_LOGW(TAG, "escalation drain: HTTP %d — kept", r.status);
    }
    if (r.body) free(r.body);
    return !c->stop;
}

/* FLUSH: persisted-escalation drain → batch envelope → prefix drop.    */
static int send_flush(size_t max_records)
{
    uint32_t *ids = malloc(VQ_BATCH_MAX_RECORDS * sizeof(uint32_t));
    if (!ids) return -1;
    idlist_t drops = {.ids = ids, .n = 0, .cap = VQ_BATCH_MAX_RECORDS};

    /* 1. persisted escalations first (time-critical advisories)         */
    esc_drain_t ed = {.drops = &drops, .posted = 0, .last_posted_id = 0,
                      .stop = false};
    vq_log_iterate(VQ_LOG_ESCALATION, esc_post_cb, &ed);
    uint32_t esc_bound = ed.stop ? ed.last_posted_id + 1 : UINT32_MAX;
    uint32_t base_n = drops.n;

    /* 2. batch envelope — drained ids continue in the same ledger       */
    uint32_t bcap = VQ_BATCH_MAX_RECORDS - drops.n;
    if (max_records && max_records < bcap) bcap = (uint32_t)max_records;
    idlist_t bids = {.ids = ids + drops.n, .n = 0, .cap = bcap};
    drain_ctx_t dc = {0};
    dc.ids = &bids;

    gbuf_t body = {0};
    bool have = build_batch_json(&body, &dc);
    drops.n = base_n + bids.n;

    int cls = 0;
    if (ed.gate_fail) {
        drops.n = base_n;                      /* power gate closed     */
        cls = -1;                              /* retryable             */
    } else if (have) {
        /* re-check the gate before the batch POST itself — the esc drain
         * above may have taken longer than the power window             */
        if (vq_wifi_burst_wait(VQ_BURST_GATE_MS) != ESP_OK) {
            drops.n = base_n;
            cls = -1;
        } else {
            http_result_t r = http_post("/v1/ingest/batch", body.p, body.len);
            cls = classify(&r, true);          /* 409 replay = delivered */
            if (cls == 0) { batch_resp(&r); stat_ok(body.len, r.rtt_ms); }
            else          { drops.n = base_n; }/* not delivered → keep   */
            if (r.body) free(r.body);
        }
    } else {
        drops.n = base_n;                      /* nothing sent → keep    */
        ESP_LOGD(TAG, "flush: nothing to send");
    }
    if (body.p) gb_free(&body);

    /* 3. drop only the provably-delivered prefix (see header proof)     */
    uint32_t bound = drain_bound(&dc, esc_bound, have && cls == 0);
    uint32_t k = count_below(drops.ids, drops.n, bound);
    if (k) vq_log_drop_oldest(k);
    if (drops.n > k)
        ESP_LOGI(TAG, "flush: dropped %lu, kept %lu (cap/interleave — tail "
                 "re-uploads; server dedups by batch_id)",
                 (unsigned long)k, (unsigned long)(drops.n - k));
    free(ids);
    return cls;
}

static int send_escalation(const net_req_t *r)
{
    size_t blen = 0;
    char *body = build_escalation_json(r->has_dec ? &r->dec : NULL,
                                     r->has_feat ? &r->feat : NULL,
                                     r->blob, r->blob_len, &blen);
    if (!body) return -1;
    http_result_t res = http_post("/v1/edge/escalation", body, blen);
    free(body);
    int cls = classify(&res, false);
    if (cls == 0) { esc_resp(&res); stat_ok(blen, res.rtt_ms); }
    if (res.body) free(res.body);
    return cls;
}

static int send_heartbeat(void)
{
    size_t blen = 0;
    char *body = build_heartbeat_json(&blen);
    if (!body) return -1;
    http_result_t res = http_post("/v1/device/heartbeat", body, blen);
    free(body);
    int cls = classify(&res, false);
    if (cls == 0) stat_ok(blen, res.rtt_ms);
    if (res.body) free(res.body);
    return cls;
}

/* ------------------------------------------------------------------ */
/* Net task machinery                                                  */
/* ------------------------------------------------------------------ */
static void req_free(net_req_t *r) { if (r->blob) { free(r->blob); r->blob = NULL; } }

static const uint32_t s_backoff_ms[VQ_MAX_ATTEMPTS] = {2000, 8000, 30000, 90000};

static void pend_add(net_req_t *r, uint32_t delay_ms)
{
    if (s_pend_n >= VQ_PEND_N) {         /* slots full → terminal drop   */
        ESP_LOGW(TAG, "pending slots full — dropping req kind %d", (int)r->kind);
        if (r->kind == RQ_ESCALATION)
            ESP_LOGW(TAG, "escalation bundle remains in log_store (durable copy)");
        req_free(r);
        stat_fail();
        return;
    }
    r->not_before_ms = now_ms() + delay_ms;
    s_pend[s_pend_n++] = *r;             /* move — caller must not reuse */
}

static void process_req(net_req_t *r)
{
    /* gates — cheap, before any build work                             */
    if (!s_cfg_ok) {
        ESP_LOGD(TAG, "not configured — req kind %d dropped (records persist)",
                 (int)r->kind);
        req_free(r);                     /* BATCH_OUT/ESCALATION records persist */
        return;
    }
    if (!vq_wifi_up()) { pend_add(r, 15000); return; }
    if (vq_wifi_burst_wait(VQ_BURST_GATE_MS) != ESP_OK) {
        ESP_LOGI(TAG, "burst gate timeout — deferring req kind %d", (int)r->kind);
        pend_add(r, 30000); return;
    }

    int cls;
    switch (r->kind) {
    case RQ_FLUSH:      cls = send_flush(r->max_records); break;
    case RQ_ESCALATION: cls = send_escalation(r); break;
    default:            cls = send_heartbeat(); break;
    }

    if (cls == 0) { req_free(r); return; }          /* delivered          */
    if (cls > 0) {                                  /* permanent refusal */
        ESP_LOGE(TAG, "req kind %d permanently refused (HTTP) — dropping req",
                 (int)r->kind);
        req_free(r);                                /* records stay in ring */
        stat_fail();
        return;
    }
    /* retryable */
    if (++r->attempts >= VQ_MAX_ATTEMPTS) {
        ESP_LOGW(TAG, "req kind %d gave up after %d attempts",
                 (int)r->kind, VQ_MAX_ATTEMPTS);
        /* escalation bundles persist in log_store (decision wrote them
         * first) — nothing is silently lost; the flush drain will pick
         * the record up on a later uplink.                              */
        req_free(r);
        stat_fail();
        return;
    }
    pend_add(r, s_backoff_ms[r->attempts - 1]);
}

static bool enqueue_req(const net_req_t *r)
{
    if (!s_q || xQueueSend(s_q, r, 0) != pdTRUE) return false;
    return true;
}

static void wifi_cb(bool up, void *ctx)
{
    (void)ctx;
    if (up && !s_flush_qd) {
        net_req_t r = {0};
        r.kind = RQ_FLUSH; r.max_records = 0;     /* 0 → default cap    */
        if (enqueue_req(&r)) { s_flush_qd = true; s_last_auto_ms = now_ms(); }
    }
}

static void maybe_auto_flush(void)
{
    int64_t t = now_ms();
    if (!s_cfg_ok || !vq_wifi_up() || s_flush_qd) { s_last_auto_ms = t; return; }
    if (t - s_last_auto_ms >= VQ_AUTO_FLUSH_MS) {
        net_req_t r = {0};
        r.kind = RQ_FLUSH; r.max_records = 0;
        if (enqueue_req(&r)) s_flush_qd = true;
        s_last_auto_ms = t;
    }
}

static void net_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* 1. service due pending requests first */
        int64_t t = now_ms();
        int due = -1;
        int64_t next_due = INT64_MAX;
        for (int i = 0; i < s_pend_n; i++) {
            if (s_pend[i].not_before_ms <= t) { due = i; break; }
            if (s_pend[i].not_before_ms < next_due) next_due = s_pend[i].not_before_ms;
        }
        if (due >= 0) {
            net_req_t r = s_pend[due];
            s_pend[due] = s_pend[--s_pend_n];
            process_req(&r);
            continue;
        }
        /* 2. wait for a queue item until the next pending is due (or 1 s
         *    so the auto-flush cadence ticks even when idle)            */
        TickType_t wait = pdMS_TO_TICKS(1000);
        if (next_due != INT64_MAX) {
            int64_t d = next_due - t;
            if (d < 0) d = 0;
            if (d < 1000) wait = pdMS_TO_TICKS((uint32_t)d) + 1;
        }
        net_req_t r;
        if (xQueueReceive(s_q, &r, wait) == pdTRUE) {
            if (r.kind == RQ_FLUSH)     s_flush_qd = false;
            if (r.kind == RQ_HEARTBEAT) s_hb_qd = false;
            process_req(&r);
        }
        /* 3. opportunistic drain cadence */
        maybe_auto_flush();
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
vq_err_t vq_cloud_init(void)
{
    if (s_q) return ESP_OK;
    s_cfg_mtx = xSemaphoreCreateMutex();
    derive_dev_id();
    nvs_load();
    s_q = xQueueCreate(VQ_REQ_QUEUE_LEN, sizeof(net_req_t));
    if (!s_q) return ESP_ERR_NO_MEM;
    if (xTaskCreate(net_task, "vq_net", VQ_NET_TASK_STACK, NULL,
                    VQ_NET_TASK_PRIO, &s_task) != pdPASS) {
        vQueueDelete(s_q); s_q = NULL;
        return ESP_ERR_NO_MEM;
    }
    vq_wifi_on_change(wifi_cb, NULL);           /* best-effort hookup    */
    ESP_LOGI(TAG, "cloud client up — dev %s, %s", s_dev_id,
             s_cfg_ok ? s_base_url : "unconfigured");
    return ESP_OK;
}

vq_err_t vq_cloud_config(const char *base_url, const char *device_key)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(VQ_NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    if (base_url)   nvs_set_str(h, VQ_NVS_URL, base_url);
    if (device_key) nvs_set_str(h, VQ_NVS_KEY, device_key);
    e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) return e;
    if (s_cfg_mtx) xSemaphoreTake(s_cfg_mtx, portMAX_DELAY);
    nvs_load();                                 /* re-read + normalise  */
    if (s_cfg_mtx) xSemaphoreGive(s_cfg_mtx);
    /* nudge a drain — config often arrives after data buffered offline */
    if (s_cfg_ok && !s_flush_qd) {
        net_req_t r = {0};
        r.kind = RQ_FLUSH;
        if (enqueue_req(&r)) s_flush_qd = true;
    }
    return ESP_OK;
}

bool vq_cloud_configured(void) { return s_cfg_ok; }

vq_err_t vq_cloud_flush(size_t max_records)
{
    if (!s_cfg_ok) return ESP_ERR_INVALID_STATE;    /* records persist    */
    if (s_flush_qd) return ESP_OK;                  /* coalesce           */
    net_req_t r = {0};
    r.kind = RQ_FLUSH;
    r.max_records = (max_records && max_records < VQ_BATCH_MAX_RECORDS)
                    ? max_records : VQ_BATCH_MAX_RECORDS;
    if (!enqueue_req(&r)) return ESP_ERR_NO_MEM;
    s_flush_qd = true;
    return ESP_OK;
}

vq_err_t vq_cloud_escalate(const vq_decision_t *dec,
                           const vq_feature_vec_t *features,
                           const uint8_t *windows_blob, size_t blob_len)
{
    if (!s_cfg_ok) return ESP_ERR_INVALID_STATE;    /* durable copy exists */
    if (!s_q) return ESP_ERR_INVALID_STATE;

    net_req_t r = {0};
    r.kind = RQ_ESCALATION;
    if (dec)      { r.dec = *dec;         r.has_dec  = true; }
    if (features) { r.feat = *features;   r.has_feat = true; }
    if (windows_blob && blob_len) {
        /* copy → PSRAM now; base64-encoded at send time in net task    */
        r.blob = heap_caps_malloc(blob_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!r.blob) r.blob = malloc(blob_len);
        if (!r.blob) return ESP_ERR_NO_MEM;
        memcpy(r.blob, windows_blob, blob_len);
        r.blob_len = blob_len;
    }
    if (!enqueue_req(&r)) { req_free(&r); return ESP_ERR_NO_MEM; }
    return ESP_OK;
}

vq_err_t vq_cloud_heartbeat(void)
{
    if (!s_cfg_ok) return ESP_ERR_INVALID_STATE;
    if (s_hb_qd) return ESP_OK;                     /* coalesce           */
    net_req_t r = {0};
    r.kind = RQ_HEARTBEAT;
    if (!enqueue_req(&r)) return ESP_ERR_NO_MEM;
    s_hb_qd = true;
    return ESP_OK;
}

void vq_cloud_stats(uint64_t *ok, uint64_t *fail, uint32_t *rtt_ms)
{
    if (ok)     *ok     = s_st.ok;
    if (fail)   *fail   = s_st.fail;
    if (rtt_ms) *rtt_ms = s_st.rtt_ms;
}
