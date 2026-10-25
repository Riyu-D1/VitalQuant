/* vq_pipeline.c — sensor orchestration per pipeline.h + vq_sensor.h.
 *
 * Flow: driver register fns (fixed order, below) → probe → init → start.
 * Each started sensor gets sink = pipeline hub; its own DRDY/ADC_RDY/INT
 * task drains the device FIFO and pushes contract-stamped samples.
 *
 * The hub (called on driver tasks, mutex-serialised) fans every sample to:
 *   - up to VQ_MAX_SINKS subscriber sinks (features, uploader, console tap)
 *   - a per-channel PSRAM ring window (lazy-allocated, sized by class+rate)
 *   - a double-buffered VQ_LOG_SAMPLES batch drained by a logger task so
 *     W25Q512 writes never stall a 500 Hz drain task
 *   - per-channel edge-interval stats (mean/jitter, §11.7 acceptance)
 *
 * A 30 s health task calls each streaming sensor's healthy(), applies a
 * stall check (no samples for >20 periods), emits VQ_EVT_SENSOR_FAULT into
 * the event ring + flash event log, and retries init→start with
 * exponential backoff. Nothing absent or degraded is ever fatal.
 */
#include <string.h>
#include <math.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "pipeline.h"
#include "log_store.h"
#include "clock_service.h"

static const char *TAG = "vq_pipe";

/* ---------------- driver registration, fixed order --------------------
 * Each driver component's public header declares its register function;
 * externed here so the link fails loudly if a driver component is dropped
 * rather than silently probing fewer sensors.
 */
extern vq_err_t vq_ads1292_register(void);   /* ECG/resp  SPI CS10 DRDY=2   */
extern vq_err_t vq_afe4900_register(void);   /* PPG       SPI CS18 RDY=21   */
extern vq_err_t vq_ad5940_register(void);    /* EDA/bioZ/sweat SPI CS15     */
extern vq_err_t vq_max86141_register(void);  /* satellite PPG SPI CS38      */
extern vq_err_t vq_env_register(void);       /* BME280 env @0x76            */
extern vq_err_t vq_aux_register(void);       /* TMP117 x3 + SHT45 + gauge   */
extern vq_err_t vq_island_register(void);    /* AS7341 + MLX90632 on PCA9306*/
extern vq_err_t vq_imu_register(void);       /* LSM6DSV80X @0x6A            */
extern vq_err_t vq_mic_register(void);       /* IM69D130 PDM (optional)     */
extern vq_err_t vq_fsr_register(void);       /* FSR402 ADC1_CH0             */

typedef vq_err_t (*reg_fn_t)(void);
static const struct { const char *tag; reg_fn_t fn; } s_reg_order[] = {
    { "ads1292",  vq_ads1292_register  },
    { "afe4900",  vq_afe4900_register  },
    { "ad5940",   vq_ad5940_register   },
    { "max86141", vq_max86141_register },
    { "env",      vq_env_register      },
    { "aux",      vq_aux_register      },
    { "island",   vq_island_register   },
    { "imu",      vq_imu_register      },
    { "mic",      vq_mic_register      },
    { "fsr",      vq_fsr_register      },
};

/* ---------------- pipeline-side per-sensor state ---------------------- */
typedef enum {
    SS_UNPROBED = 0,
    SS_ABSENT,          /* probe → ESP_ERR_NOT_FOUND (or probe error) */
    SS_READY,           /* init ok, not streaming                     */
    SS_STREAMING,       /* start ok                                   */
    SS_STOPPED,         /* was streaming, stop() ok                   */
    SS_FAULT,           /* unhealthy/init-fail — health task retries  */
} sensor_state_t;

typedef struct {
    sensor_state_t state;
    uint32_t fail_streak;      /* consecutive fault/restart failures    */
    int64_t  next_retry_us;    /* backoff gate                          */
    int64_t  start_us;         /* last successful start (stall grace)   */
    bool     started_once;     /* safe to call stop() on retry          */
} sensor_pipe_t;

static sensor_pipe_t s_pipe[VQ_SENSOR_MAX];

/* ---------------- channel bookkeeping --------------------------------- */
typedef enum { CLS_ECG = 0, CLS_PPG, CLS_SLOW, CLS_MIC } chan_class_t;

static int8_t       s_chan_owner[VQ_CH_COUNT];   /* registry index or -1  */
static uint32_t     s_chan_samples[VQ_CH_COUNT]; /* total pushed          */

static chan_class_t chan_class(vq_channel_id_t ch)
{
    switch (ch) {
    case VQ_CH_ECG_II:
    case VQ_CH_RESP:        return CLS_ECG;
    case VQ_CH_PPG_RED:
    case VQ_CH_PPG_IR:
    case VQ_CH_PPG_SAT:     return CLS_PPG;
    case VQ_CH_MIC:         return CLS_MIC;
    default:                return CLS_SLOW;
    }
}

/* ---------------- per-channel PSRAM ring windows ---------------------- */
typedef struct {
    vq_sample_t *buf;     /* heap_caps_malloc(MALLOC_CAP_SPIRAM), lazy */
    size_t cap;
    size_t head;          /* next write index                          */
    size_t count;         /* valid entries, <= cap                     */
    bool   alloc_failed;
} chan_ring_t;

static chan_ring_t s_rings[VQ_CH_COUNT];

static float chan_rate_est(vq_channel_id_t ch)
{
    int idx = s_chan_owner[ch];
    size_t count;
    vq_sensor_t *sens = vq_sensors(&count);
    if (idx >= 0 && (size_t)idx < count && sens[idx].ops->rate_hz > 0.0f) {
        return sens[idx].ops->rate_hz;
    }
    switch (chan_class(ch)) {
    case CLS_ECG: return 500.0f;
    case CLS_PPG: return 250.0f;
    case CLS_MIC: return 8000.0f;
    default:      return 1.0f;
    }
}

static size_t ring_capacity_for(vq_channel_id_t ch)
{
    float hz = chan_rate_est(ch);
    size_t cap;
    size_t max;
    switch (chan_class(ch)) {
    case CLS_ECG:
        cap = (size_t)(VQ_WIN_ECG_S * hz * 1.10f);   /* 60 s @ ~500 sps */
        max = 38400;                                  /* hard bound     */
        break;
    case CLS_PPG:
        cap = (size_t)(VQ_WIN_PPG_S * hz * 1.10f);   /* 60 s @ ~200 sps */
        max = 38400;
        break;
    case CLS_MIC:
        return 4096;               /* block channel — a few seconds max */
    default:
        cap = (size_t)(VQ_WIN_SLOW_S * hz * 1.10f);  /* 300 s @ <=~8 sps */
        max = 8192;
        break;
    }
    if (cap < 64)   cap = 64;
    if (cap > max)  cap = max;
    return cap;
}

static void ring_push(vq_channel_id_t ch, const vq_sample_t *s)
{
    chan_ring_t *r = &s_rings[ch];
    if (r->buf == NULL) {
        if (r->alloc_failed) {
            return;
        }
        size_t cap = ring_capacity_for(ch);
        r->buf = heap_caps_malloc(cap * sizeof(vq_sample_t),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (r->buf == NULL) {
            r->alloc_failed = true;
            ESP_LOGE(TAG, "PSRAM alloc failed for %s (%u samples)",
                     vq_channel_name(ch), (unsigned)cap);
            return;
        }
        r->cap = cap;
        r->head = 0;
        r->count = 0;
        ESP_LOGD(TAG, "ring %s: %u samples", vq_channel_name(ch), (unsigned)cap);
    }
    r->buf[r->head] = *s;
    r->head = (r->head + 1) % r->cap;
    if (r->count < r->cap) {
        r->count++;
    }
}

/* ---------------- edge-interval stats (Welford) ------------------------
 * Inter-sample t_us deltas per channel → mean period + rms jitter.
 * Single outliers (>4x mean after warmup, e.g. a FIFO stall + resume) are
 * not period jitter — excluded from stats but still move last_t.
 */
typedef struct {
    int64_t  last_t;
    double   mean;
    double   m2;
    uint32_t n;             /* accepted intervals */
} chan_edge_t;

static chan_edge_t s_edge[VQ_CH_COUNT];
static uint32_t    s_bad_channel;     /* samples with ch >= VQ_CH_COUNT  */
static uint32_t    s_nonmono;         /* non-monotonic t_us sightings    */

static void edge_update(vq_channel_id_t ch, int64_t t_us)
{
    chan_edge_t *e = &s_edge[ch];
    if (e->last_t == 0 || t_us <= e->last_t) {
        if (e->last_t != 0) {
            s_nonmono++;
        }
        e->last_t = t_us;
        return;
    }
    double dt = (double)(t_us - e->last_t);
    e->last_t = t_us;
    if (e->n >= 16 && dt > 4.0 * e->mean) {
        return;                     /* stream stall, not jitter */
    }
    e->n++;
    double d = dt - e->mean;
    e->mean += d / (double)e->n;
    e->m2   += d * (dt - e->mean);
}

/* ---------------- log_store batching (double buffer) ------------------
 * Hub appends into the active buffer; on full, it is handed to the logger
 * task via s_logq and the hub continues into the other buffer. A flash
 * append therefore never runs on a driver drain task.
 */
/* One VQ_LOG_SAMPLES record must fit one flash sector — a batch bigger
 * than VQ_LOG_MAX_PAYLOAD is rejected whole (bug history: 512 x 24 B
 * samples = 12 KiB appended zero records before the cap was derived).  */
#define VQ_LOG_BATCH_CAP   (VQ_LOG_MAX_PAYLOAD / sizeof(vq_sample_t))
_Static_assert(VQ_LOG_BATCH_CAP >= 16, "batch cap implausibly small");
#define VQ_LOGQ_DEPTH      2          /* both buffers may be in flight   */
#define VQ_LOGQ_QUIT       (-1)       /* sentinel idx                    */

typedef struct {
    vq_sample_t *data;                /* PSRAM                           */
    size_t n;
    bool   in_flight;                 /* owned by logger task            */
} batch_buf_t;

static batch_buf_t   s_batch[2];
static int           s_batch_active = -1;   /* -1 → batching disabled    */
static QueueHandle_t s_logq;
static volatile uint32_t s_log_dropped;

/* ---------------- shared state ---------------------------------------- */
static SemaphoreHandle_t s_lock;
static TaskHandle_t      s_health_task;
static TaskHandle_t      s_log_task;
static volatile bool     s_running;
static volatile bool     s_health_alive;   /* task sets false before exit */
static volatile bool     s_log_alive;
static bool              s_inited;
static bool              s_started;
static size_t            s_present;
static size_t            s_absent;
static size_t            s_faulted;

/* ---------------- event ring (sensor faults etc.) ---------------------
 * Status/console readers get the durable copy via VQ_LOG_EVENT flash
 * records; the RAM ring keeps the most recent few for in-process users.
 * Dependencies stay one-way: this component does NOT call vq_console.
 */
#define VQ_EVT_RING 16
typedef struct {
    vq_event_id_t id;
    int64_t       t_us;
    char          tag[16];
} pipe_event_t;

static pipe_event_t s_evts[VQ_EVT_RING];
static size_t       s_evt_head, s_evt_count;

typedef struct {                    /* VQ_LOG_EVENT payload (24 B)       */
    uint32_t id;
    int64_t  t_us;
    char     tag[12];
} evt_payload_t;

static void event_emit(vq_event_id_t id, const char *tag)
{
    int64_t now = vq_clock_us();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    pipe_event_t *e = &s_evts[s_evt_head];
    e->id = id;
    e->t_us = now;
    if (tag) {
        strncpy(e->tag, tag, sizeof(e->tag) - 1);
        e->tag[sizeof(e->tag) - 1] = '\0';
    } else {
        e->tag[0] = '\0';
    }
    s_evt_head = (s_evt_head + 1) % VQ_EVT_RING;
    if (s_evt_count < VQ_EVT_RING) {
        s_evt_count++;
    }
    xSemaphoreGive(s_lock);

    if (vq_log_flash_present()) {
        evt_payload_t p;
        p.id = (uint32_t)id;
        p.t_us = now;
        strncpy(p.tag, tag ? tag : "", sizeof(p.tag) - 1);
        p.tag[sizeof(p.tag) - 1] = '\0';
        vq_log_append(VQ_LOG_EVENT, &p, sizeof(p));
    }
}

/* ---------------- subscriber sinks ------------------------------------ */
#define VQ_MAX_SINKS 6
static vq_sample_sink_t s_sinks[VQ_MAX_SINKS];
static void            *s_sink_ctx[VQ_MAX_SINKS];
static size_t          s_n_sinks;

static void batch_push_locked(const vq_sample_t *s);   /* fwd — defined
                                                        * with log_task  */

/* ---------------- the hub ---------------------------------------------- */
static void hub_sink(void *ctx, const vq_sample_t *samples, size_t n)
{
    if (samples == NULL || n == 0 || s_lock == NULL) {
        return;
    }
    vq_sensor_t *src = (vq_sensor_t *)ctx;

    /* Pass 1 (locked): accounting + windows + stats + batch staging. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (src) {
        src->sample_count += n;
    }
    for (size_t i = 0; i < n; i++) {
        const vq_sample_t *sm = &samples[i];
        int ch = (int)sm->channel;
        if (ch < 0 || ch >= (int)VQ_CH_COUNT) {
            s_bad_channel++;
            if (src) {
                src->err_count++;
            }
            continue;
        }
        if (src && sm->t_us > src->last_t_us) {
            src->last_t_us = sm->t_us;
        }
        s_chan_samples[ch]++;
        edge_update((vq_channel_id_t)ch, sm->t_us);
        ring_push((vq_channel_id_t)ch, sm);
        batch_push_locked(sm);
    }
    /* snapshot subscribers — sink calls run unlocked so a slow consumer
     * cannot serialise driver drain tasks against each other. */
    size_t ns = s_n_sinks;
    vq_sample_sink_t sinks[VQ_MAX_SINKS];
    void *sctx[VQ_MAX_SINKS];
    memcpy(sinks, s_sinks, ns * sizeof(sinks[0]));
    memcpy(sctx,  s_sink_ctx, ns * sizeof(sctx[0]));
    xSemaphoreGive(s_lock);

    /* Pass 2 (unlocked): fan-out. */
    for (size_t i = 0; i < ns; i++) {
        sinks[i](sctx[i], samples, n);
    }
}

/* ---------------- batch helpers ---------------------------------------- */
static void batch_push_locked(const vq_sample_t *s)
{
    if (s_batch_active < 0) {
        return;
    }
    batch_buf_t *b = &s_batch[s_batch_active];
    if (b->n >= VQ_LOG_BATCH_CAP) {
        int o = s_batch_active ^ 1;
        if (s_batch[o].in_flight || s_batch[o].n > 0) {
            s_log_dropped++;            /* logger behind — drop sample   */
            return;
        }
        int idx = s_batch_active;
        b->in_flight = true;
        s_batch_active = o;
        if (xQueueSend(s_logq, &idx, 0) != pdTRUE) {
            /* should never happen (queue depth == #buffers) — recover   */
            b->in_flight = false;
            s_batch_active = idx;
            s_log_dropped += (uint32_t)b->n;
            b->n = 0;
            return;
        }
        b = &s_batch[o];
    }
    b->data[b->n++] = *s;
}

/* detach the active buffer if it holds a partial batch; -1 = nothing    */
static int batch_detach_partial(void)
{
    int detached = -1;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int a = s_batch_active;
    if (a >= 0 && s_batch[a].n > 0) {
        int o = a ^ 1;
        if (!s_batch[o].in_flight && s_batch[o].n == 0) {
            s_batch[a].in_flight = true;
            s_batch_active = o;
            detached = a;
        }
    }
    xSemaphoreGive(s_lock);
    return detached;
}

static void batch_flush(int idx)     /* logger task context only         */
{
    batch_buf_t *b = &s_batch[idx];
    if (b->n > 0 && vq_log_flash_present()) {
        vq_err_t e = vq_log_append(VQ_LOG_SAMPLES, b->data,
                                   (uint32_t)(b->n * sizeof(vq_sample_t)));
        if (e != ESP_OK) {
            s_log_dropped += (uint32_t)b->n;
            ESP_LOGW(TAG, "log append failed (%s), %u samples dropped",
                     esp_err_to_name(e), (unsigned)b->n);
        }
    } else if (b->n > 0) {
        s_log_dropped += (uint32_t)b->n;   /* no flash — counted, dropped */
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    b->n = 0;
    b->in_flight = false;
    xSemaphoreGive(s_lock);
}

static void log_task(void *arg)
{
    (void)arg;
    int idx;
    while (1) {
        if (xQueueReceive(s_logq, &idx, pdMS_TO_TICKS(4000)) == pdTRUE) {
            if (idx == VQ_LOGQ_QUIT) {
                break;
            }
            if (idx >= 0 && idx < 2) {
                batch_flush(idx);
            }
        } else {
            /* idle timeout — bound partial-batch staleness at ~4 s.
             * If the stop() sentinel missed a full queue, exit anyway. */
            if (!s_running) {
                break;
            }
            idx = batch_detach_partial();
            if (idx >= 0) {
                batch_flush(idx);
            }
        }
    }
    /* drain anything still queued — stop() flushes leftovers after us,
     * but in-flight buffers are only cleared here.                    */
    while (xQueueReceive(s_logq, &idx, 0) == pdTRUE) {
        if (idx >= 0 && idx < 2) {
            batch_flush(idx);
        }
    }
    ESP_LOGI(TAG, "logger task exit");
    s_log_alive = false;
    vTaskDelete(NULL);
}

/* ---------------- health monitor --------------------------------------- */
#define HEALTH_PERIOD_MS   30000
#define HEALTH_STACK       4096
#define LOG_TASK_STACK     4096
#define PIPE_TASK_PRIO     5

static int64_t backoff_us(uint32_t streak)
{
    int64_t s = 30;
    while (streak > 1 && s < 300) {
        s *= 2;
        streak--;
    }
    if (s > 300) {
        s = 300;
    }
    return s * 1000000LL;             /* 30 s → 300 s cap */
}

static void sensor_fault(vq_sensor_t *s, sensor_pipe_t *st,
                         const char *why, int64_t now)
{
    st->state = SS_FAULT;
    st->fail_streak++;
    st->next_retry_us = now + backoff_us(st->fail_streak);
    s->err_count++;
    if (st->started_once && s->ops->stop) {
        s->ops->stop(s);              /* quiesce before retry attempts   */
    }
    ESP_LOGW(TAG, "SENSOR_FAULT %s: %s (streak %u, retry in %" PRId64 " s)",
             s->ops->name, why, (unsigned)st->fail_streak,
             (st->next_retry_us - now) / 1000000LL);
    event_emit(VQ_EVT_SENSOR_FAULT, s->ops->name);
}

static void sensor_retry(vq_sensor_t *s, sensor_pipe_t *st, int64_t now)
{
    vq_err_t e = ESP_OK;
    if (s->ops->init) {
        e = s->ops->init(s);
    }
    if (e == ESP_OK) {
        s->sink     = hub_sink;       /* re-attach hub — init may reset */
        s->sink_ctx = s;
        e = s->ops->start ? s->ops->start(s) : ESP_ERR_INVALID_STATE;
    }
    if (e == ESP_OK) {
        st->state        = SS_STREAMING;
        st->fail_streak  = 0;
        st->started_once = true;
        st->start_us     = now;
        ESP_LOGI(TAG, "%s recovered — streaming", s->ops->name);
    } else {
        st->fail_streak++;
        st->next_retry_us = now + backoff_us(st->fail_streak);
        s->err_count++;
        ESP_LOGW(TAG, "%s restart failed (%s), next try in %" PRId64 " s",
                 s->ops->name, esp_err_to_name(e),
                 (st->next_retry_us - now) / 1000000LL);
    }
}

static void health_task(void *arg)
{
    (void)arg;
    uint32_t last_drop = s_log_dropped;
    while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HEALTH_PERIOD_MS)) == 0) {
        if (!s_running) {
            break;
        }
        int64_t now = vq_clock_us();
        size_t count;
        vq_sensor_t *sens = vq_sensors(&count);
        for (size_t i = 0; i < count; i++) {
            vq_sensor_t   *s  = &sens[i];
            sensor_pipe_t *st = &s_pipe[i];

            if (st->state == SS_STREAMING) {
                bool ok = true;
                const char *why = NULL;
                if (s->ops->healthy && !s->ops->healthy(s)) {
                    ok = false;
                    why = "healthy()";
                } else if (s->ops->rate_hz > 0.0f &&
                           (now - st->start_us) > 60000000LL) {
                    /* silent-stall: nothing pushed for >20 periods
                     * (floor 10 s), after a 60 s post-start grace.     */
                    int64_t last;
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    last = s->last_t_us;
                    xSemaphoreGive(s_lock);
                    int64_t lim = (int64_t)(20.0e6 / s->ops->rate_hz);
                    if (lim < 10000000LL) {
                        lim = 10000000LL;
                    }
                    int64_t idle = (last > 0) ? (now - last) : (now - st->start_us);
                    if (idle > lim) {
                        ok = false;
                        why = "stalled";
                    }
                }
                if (!ok) {
                    sensor_fault(s, st, why, now);
                }
            } else if (st->state == SS_FAULT && now >= st->next_retry_us) {
                sensor_retry(s, st, now);
            }
        }
        if (s_log_dropped != last_drop) {
            ESP_LOGW(TAG, "log batch backlog: +%u dropped samples",
                     (unsigned)(s_log_dropped - last_drop));
            last_drop = s_log_dropped;
        }
        if (s_bad_channel || s_nonmono) {
            ESP_LOGW(TAG, "hub anomalies: bad_ch=%u nonmono=%u",
                     (unsigned)s_bad_channel, (unsigned)s_nonmono);
            s_bad_channel = 0;
            s_nonmono = 0;
        }
    }
    ESP_LOGI(TAG, "health task exit");
    s_health_alive = false;
    vTaskDelete(NULL);
}

/* ---------------- public API ------------------------------------------- */
vq_err_t vq_pipeline_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_logq = xQueueCreate(VQ_LOGQ_DEPTH, sizeof(int));
    if (s_logq == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (int c = 0; c < (int)VQ_CH_COUNT; c++) {
        s_chan_owner[c] = -1;
    }

    /* 1. registration — fixed order, failures logged not fatal.         */
    for (size_t i = 0; i < sizeof(s_reg_order) / sizeof(s_reg_order[0]); i++) {
        vq_err_t e = s_reg_order[i].fn();
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "register %s failed: %s",
                     s_reg_order[i].tag, esp_err_to_name(e));
        }
    }

    size_t count;
    vq_sensor_t *sens = vq_sensors(&count);
    ESP_LOGI(TAG, "%u sensor(s) registered", (unsigned)count);

    /* 2. channel → owner map (first claim wins; conflicts logged).      */
    for (size_t i = 0; i < count; i++) {
        vq_sensor_t *s = &sens[i];
        for (size_t k = 0; k < s->ops->n_channels; k++) {
            int ch = (int)s->ops->channels[k];
            if (ch < 0 || ch >= (int)VQ_CH_COUNT) {
                ESP_LOGW(TAG, "%s: channel id %d out of range",
                         s->ops->name, ch);
                continue;
            }
            if (s_chan_owner[ch] >= 0) {
                ESP_LOGW(TAG, "%s: %s already owned by %s — overriding",
                         s->ops->name, vq_channel_name(ch),
                         sens[s_chan_owner[ch]].ops->name);
            }
            s_chan_owner[ch] = (int8_t)i;
        }
    }

    /* 3. probe + init — absent optional devices are marked, never fatal.*/
    for (size_t i = 0; i < count; i++) {
        vq_sensor_t   *s  = &sens[i];
        sensor_pipe_t *st = &s_pipe[i];
        st->state = SS_UNPROBED;

        vq_err_t e = s->ops->probe ? s->ops->probe(s) : ESP_OK;
        if (e == ESP_ERR_NOT_FOUND) {
            st->state = SS_ABSENT;
            s_absent++;
            if (s->ops->required) {
                ESP_LOGE(TAG, "REQUIRED %s absent: %s",
                         s->ops->name, esp_err_to_name(e));
            } else {
                ESP_LOGW(TAG, "%s absent (%s) — optional, continuing",
                         s->ops->name, esp_err_to_name(e));
            }
            continue;
        }
        if (e != ESP_OK) {
            /* answered but probe failed (bus stall, bad id) — degraded,
             * not absent: fault state → health task retries init+start. */
            st->state = SS_FAULT;
            st->fail_streak = 1;
            st->next_retry_us = vq_clock_us() + backoff_us(1);
            s_faulted++;
            ESP_LOGW(TAG, "%s probe failed: %s — will retry",
                     s->ops->name, esp_err_to_name(e));
            continue;
        }
        e = s->ops->init ? s->ops->init(s) : ESP_OK;
        if (e != ESP_OK) {
            st->state = SS_FAULT;
            st->fail_streak = 1;
            st->next_retry_us = vq_clock_us() + backoff_us(1);
            s_faulted++;
            ESP_LOGW(TAG, "%s init failed: %s — health task will retry",
                     s->ops->name, esp_err_to_name(e));
            continue;
        }
        st->state = SS_READY;
        s_present++;
        ESP_LOGI(TAG, "%s ready (%u ch, %.2f Hz%s)",
                 s->ops->name, (unsigned)s->ops->n_channels,
                 (double)s->ops->rate_hz,
                 s->ops->required ? ", required" : "");
    }

    /* 4. PSRAM batch buffers — degrade to no-batching on alloc failure. */
    s_batch[0].data = heap_caps_malloc(VQ_LOG_BATCH_CAP * sizeof(vq_sample_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_batch[1].data = heap_caps_malloc(VQ_LOG_BATCH_CAP * sizeof(vq_sample_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_batch[0].data && s_batch[1].data) {
        s_batch_active = 0;
    } else {
        s_batch_active = -1;
        ESP_LOGE(TAG, "PSRAM batch alloc failed — sample logging disabled");
    }

    s_inited = true;
    ESP_LOGI(TAG, "pipeline init: %u ready, %u absent, %u faulted",
             (unsigned)s_present, (unsigned)s_absent, (unsigned)s_faulted);
    return ESP_OK;
}

vq_err_t vq_pipeline_start(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_started) {
        return ESP_OK;
    }
    s_running = true;

    /* service tasks first so early bursts are logged, not dropped.      */
    if (s_batch_active >= 0 && !s_log_alive) {
        s_log_alive = true;
        if (xTaskCreate(log_task, "vq_plog", LOG_TASK_STACK, NULL,
                        PIPE_TASK_PRIO, &s_log_task) != pdPASS) {
            s_log_alive = false;
            ESP_LOGE(TAG, "logger task create failed");
        }
    }
    if (!s_health_alive) {
        s_health_alive = true;
        if (xTaskCreate(health_task, "vq_health", HEALTH_STACK, NULL,
                        PIPE_TASK_PRIO, &s_health_task) != pdPASS) {
            s_health_alive = false;
            ESP_LOGE(TAG, "health task create failed");
        }
    }

    /* then stream — drivers spawn their own drain tasks inside start(). */
    size_t count;
    vq_sensor_t *sens = vq_sensors(&count);
    size_t started = 0;
    int64_t now = vq_clock_us();
    for (size_t i = 0; i < count; i++) {
        vq_sensor_t   *s  = &sens[i];
        sensor_pipe_t *st = &s_pipe[i];
        if (st->state != SS_READY && st->state != SS_STOPPED) {
            continue;                 /* ABSENT/UNPROBED/FAULT → health  */
        }
        s->sink     = hub_sink;
        s->sink_ctx = s;
        vq_err_t e = s->ops->start ? s->ops->start(s) : ESP_ERR_INVALID_STATE;
        if (e == ESP_OK) {
            st->state        = SS_STREAMING;
            st->started_once = true;
            st->start_us     = now;
            st->fail_streak  = 0;
            started++;
        } else {
            st->state = SS_FAULT;
            st->fail_streak = 1;
            st->next_retry_us = now + backoff_us(1);
            s->err_count++;
            ESP_LOGW(TAG, "%s start failed: %s",
                     s->ops->name, esp_err_to_name(e));
            event_emit(VQ_EVT_SENSOR_FAULT, s->ops->name);
        }
    }
    s_started = true;
    ESP_LOGI(TAG, "pipeline started: %u/%u streaming",
             (unsigned)started, (unsigned)s_present);
    return ESP_OK;
}

vq_err_t vq_pipeline_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }
    s_running = false;

    /* 1. stop streaming — reverse order, no new samples expected after. */
    size_t count;
    vq_sensor_t *sens = vq_sensors(&count);
    for (size_t i = count; i-- > 0;) {
        vq_sensor_t   *s  = &sens[i];
        sensor_pipe_t *st = &s_pipe[i];
        if ((st->state == SS_STREAMING || st->state == SS_FAULT) &&
            st->started_once && s->ops->stop) {
            vq_err_t e = s->ops->stop(s);
            if (e != ESP_OK) {
                ESP_LOGW(TAG, "%s stop: %s", s->ops->name, esp_err_to_name(e));
            }
            st->state = SS_STOPPED;
        }
    }

    /* 2. let service tasks exit (health wakes via notify; logger via
     *    sentinel), wait for their alive-flags, force-delete only a task
     *    that is genuinely still running (a completed handle is stale). */
    if (s_health_alive && s_health_task) {
        xTaskNotifyGive(s_health_task);
    }
    if (s_log_alive && s_logq) {
        int quit = VQ_LOGQ_QUIT;
        xQueueSend(s_logq, &quit, pdMS_TO_TICKS(200));
    }
    for (int i = 0; i < 100 && (s_health_alive || s_log_alive); i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_health_alive && s_health_task) {
        vTaskDelete(s_health_task);
        s_health_alive = false;
    }
    if (s_log_alive && s_log_task) {
        vTaskDelete(s_log_task);
        s_log_alive = false;
    }

    /* 3. final flush — anything still staged goes to flash now.         */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < 2; i++) {
        if (s_batch[i].n > 0 && !s_batch[i].in_flight &&
            vq_log_flash_present()) {
            vq_log_append(VQ_LOG_SAMPLES, s_batch[i].data,
                          (uint32_t)(s_batch[i].n * sizeof(vq_sample_t)));
        }
        s_batch[i].n = 0;
        s_batch[i].in_flight = false;
    }
    xSemaphoreGive(s_lock);

    s_started = false;
    ESP_LOGI(TAG, "pipeline stopped");
    return ESP_OK;
}

vq_err_t vq_pipeline_subscribe(vq_sample_sink_t sink, void *ctx)
{
    if (sink == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;   /* init() first */
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < s_n_sinks; i++) {
        if (s_sinks[i] == sink && s_sink_ctx[i] == ctx) {
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (s_n_sinks >= VQ_MAX_SINKS) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    s_sinks[s_n_sinks] = sink;
    s_sink_ctx[s_n_sinks] = ctx;
    s_n_sinks++;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

size_t vq_pipeline_window(vq_channel_id_t ch, int64_t window_us,
                          vq_sample_t *out, size_t max)
{
    if (out == NULL || max == 0 || window_us <= 0 ||
        ch < 0 || ch >= VQ_CH_COUNT || s_lock == NULL) {
        return 0;
    }
    int64_t t_min = vq_clock_us() - window_us;   /* "last N µs" = vs now */
    size_t n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    chan_ring_t *r = &s_rings[ch];
    if (r->buf && r->count) {
        /* newest→oldest; scan the full ring (intra-batch reordering can
         * violate strict monotonicity, so no early break).              */
        for (size_t i = 0; i < r->count && n < max; i++) {
            size_t idx = (r->head + r->cap - 1 - i) % r->cap;
            if (r->buf[idx].t_us >= t_min) {
                out[n++] = r->buf[idx];
            }
        }
    }
    xSemaphoreGive(s_lock);
    return n;
}

void vq_pipeline_edge_stats(vq_channel_id_t ch, vq_edge_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->mean_us = 0.0f;
    out->jitter_us = 0.0f;
    out->n = 0;
    if (ch < 0 || ch >= VQ_CH_COUNT || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    chan_edge_t *e = &s_edge[ch];
    out->n = e->n;
    if (e->n > 0) {
        out->mean_us = (float)e->mean;
    }
    if (e->n > 1) {
        out->jitter_us = (float)sqrt(e->m2 / (double)(e->n - 1));
    }
    xSemaphoreGive(s_lock);
}
