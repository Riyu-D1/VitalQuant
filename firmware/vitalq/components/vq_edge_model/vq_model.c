/* vq_model.c — VQMB blob loader + graph executor. Implements the FIXED
 * contract in components/vq/include/vq_model.h; the blob format details
 * (node record, params/index-list placement, crc coverage) are pinned
 * in vq_kernels.h.
 *
 * Memory discipline:
 *  - weights stay wherever they were found — mmap'd flash for the
 *    'model' partition path, caller-owned memory for load_blob (the
 *    compiled-in stub uses a static buffer). Zero-copy both ways.
 *  - model struct, tensor/node runtime arrays, per-node LUTs and the
 *    activation arena all come from heap_caps_malloc(MALLOC_CAP_SPIRAM)
 *    with an internal-RAM fallback so a PSRAM-less build still runs.
 *  - arena is a static per-model slab: every non-weight tensor gets a
 *    16 B-aligned slot of product(dims)*esize bytes at load; nothing is
 *    freed inside a run, nothing leaks after free.
 *
 * Fallback: vq_model_load() tries the 'model' data partition (subtype
 * 0x40) first; any failure (absent, magic/ver/crc bad, truncated,
 * alloc fail) falls back to the compiled-in stub so the decision
 * pipeline is always end-to-end. The header comment says "returns
 * ESP_ERR_NOT_FOUND" — the in-loader fallback is intentional per the
 * vq_edge_model tasking; is_stub + selftest_report expose which source
 * is live, and callers that need strictness can check it via the
 * report or by calling vq_model_load_blob() themselves.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"

#if defined(__has_include)
#  if __has_include("esp_cpu.h")
#    include "esp_cpu.h"
#    define VQ_HAVE_CCOUNT 1
#  endif
#endif

#include "vq_kernels.h"
#include "stub_model_bin.h"

static const char *TAG = "vq_model";

#define VQ_MODEL_PART_SUBTYPE 0x40     /* 'model' in partitions.csv     */
#define VQ_ELEMS_MAX   (16u * 1024u * 1024u)

/* ------------------------------------------------------------------ */

typedef struct {
    vqmb_node_t      d;        /* parsed disk record               */
    const uint8_t   *params;   /* into params blob (NULL if len 0)  */
    vq_lut8_t       *lut;      /* non-NULL for GELU/SIGMOID/TANH    */
} rt_node_t;

struct vq_model {
    vqmb_header_t  hdr;
    vq_rtensor_t  *ts;         /* [n_tensors]  PSRAM                */
    rt_node_t     *nodes;      /* [n_nodes]    PSRAM                */
    uint32_t      *in_idx;     /* [n_inputs]   PSRAM                */
    uint32_t      *out_idx;    /* [n_outputs]  PSRAM                */
    uint8_t       *arena;      /* activation slab, PSRAM            */
    uint32_t       arena_alloc;
    uint32_t       arena_used;
    esp_partition_mmap_handle_t mmap_h;
    bool           owns_mmap;  /* weights region is mmap'd          */
    bool           is_stub;    /* loaded the compiled-in fallback   */
    uint32_t       param_count;
    int64_t        last_us;
    uint64_t       op_cycles[VQ_OP_COUNT];
    uint32_t       op_calls[VQ_OP_COUNT];
};

static void *vq_alloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) {
        p = heap_caps_malloc(sz, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }
    return p;
}

static void *vq_calloc(size_t n, size_t sz)
{
    void *p = vq_alloc(n * sz);
    if (p != NULL) {
        memset(p, 0, n * sz);
    }
    return p;
}

static inline uint32_t vq_cyc(void)
{
#ifdef VQ_HAVE_CCOUNT
    return esp_cpu_get_cycle_count();
#else
    return (uint32_t)esp_timer_get_time();   /* µs as pseudo-cycles   */
#endif
}

static inline uint32_t align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1u) & ~(a - 1u);
}

static uint32_t blob_fixed_bytes(const vqmb_header_t *h)
{
    return (uint32_t)sizeof(vqmb_header_t)
         + h->n_tensors * (uint32_t)sizeof(vq_tensor_t)
         + h->n_nodes   * (uint32_t)sizeof(vqmb_node_t)
         + (h->n_inputs + h->n_outputs) * 4u;
}

/* ------------------------------------------------------------------ */
/* Parse + build runtime state over a blob (mmap'd or caller memory).
 * m must be zeroed; on failure every partially-allocated field is freed
 * by vq_model_free().                                              */
static vq_err_t parse_blob(vq_model_t *m, const uint8_t *blob, size_t len)
{
    if (len < sizeof(vqmb_header_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(&m->hdr, blob, sizeof(m->hdr));
    const vqmb_header_t *h = &m->hdr;
    if (h->magic != VQMB_MAGIC) {
        return ESP_ERR_INVALID_RESPONSE;      /* bad magic            */
    }
    if (h->ver != VQMB_VER) {
        return ESP_ERR_INVALID_VERSION;
    }
    if (h->n_tensors == 0 || h->n_tensors > VQ_MAX_TENSORS ||
        h->n_nodes > VQ_MAX_NODES ||
        h->n_inputs > VQ_MAX_IO || h->n_outputs > VQ_MAX_IO) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t fixed  = blob_fixed_bytes(h);
    if (fixed > len) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (h->weights_bytes > len) {               /* early absurd-size cut */
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t t_off  = sizeof(vqmb_header_t);
    const size_t n_off  = t_off + h->n_tensors * sizeof(vq_tensor_t);
    const size_t ix_off = n_off + h->n_nodes * sizeof(vqmb_node_t);

    /* params blob length is implicit: max(param_off+param_len)        */
    uint32_t params_bytes = 0;
    for (uint32_t i = 0; i < h->n_nodes; i++) {
        vqmb_node_t nd;
        memcpy(&nd, blob + n_off + i * sizeof(vqmb_node_t), sizeof nd);
        const uint64_t end = (uint64_t)nd.param_off + nd.param_len;
        if (end > (64u << 20)) {                /* params are tiny       */
            return ESP_ERR_INVALID_SIZE;
        }
        if (end > params_bytes) {
            params_bytes = (uint32_t)end;
        }
        if (nd.op >= VQ_OP_COUNT || nd.n_in > 4 || nd.n_out > 4) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    const uint32_t weights_off = align_up(fixed + params_bytes, 4u);
    const uint64_t blob_len = (uint64_t)weights_off + h->weights_bytes;
    if (blob_len > len || blob_len > UINT32_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (vq_crc32(0, blob + sizeof(vqmb_header_t),
                 (size_t)blob_len - sizeof(vqmb_header_t)) != h->crc32) {
        return ESP_ERR_INVALID_CRC;
    }

    /* ---- runtime arrays ------------------------------------------ */
    m->ts    = vq_calloc(h->n_tensors, sizeof(vq_rtensor_t));
    m->nodes = vq_calloc(h->n_nodes ? h->n_nodes : 1, sizeof(rt_node_t));
    m->in_idx  = vq_calloc(h->n_inputs  ? h->n_inputs  : 1, 4);
    m->out_idx = vq_calloc(h->n_outputs ? h->n_outputs : 1, 4);
    if (m->ts == NULL || m->nodes == NULL ||
        m->in_idx == NULL || m->out_idx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (h->n_inputs) {
        memcpy(m->in_idx, blob + ix_off, h->n_inputs * 4u);
    }
    if (h->n_outputs) {
        memcpy(m->out_idx, blob + ix_off + h->n_inputs * 4u,
               h->n_outputs * 4u);
    }
    for (uint32_t i = 0; i < h->n_inputs; i++) {
        if (m->in_idx[i] >= h->n_tensors) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (uint32_t i = 0; i < h->n_outputs; i++) {
        if (m->out_idx[i] >= h->n_tensors) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    /* ---- tensors: pass 1, validate + count weights/arena ---------- */
    const uint8_t *weights = blob + weights_off;
    uint32_t arena_need = 0;
    uint32_t params = 0;
    for (uint32_t i = 0; i < h->n_tensors; i++) {
        vq_tensor_t dt;
        memcpy(&dt, blob + t_off + i * sizeof(vq_tensor_t), sizeof dt);
        if (dt.dtype > VQ_DT_F32 || dt.ndim > 4) {
            return ESP_ERR_INVALID_ARG;
        }
        if (dt.dtype == VQ_DT_Q8 && (dt.zp < -128 || dt.zp > 127)) {
            return ESP_ERR_INVALID_ARG;         /* zp must fit int8     */
        }
        const uint64_t el = vq_elems_of(dt.ndim, dt.dims);
        if (el == 0 || el > VQ_ELEMS_MAX) {
            return ESP_ERR_INVALID_SIZE;
        }
        vq_rtensor_t *t = &m->ts[i];
        t->dtype = dt.dtype;
        t->ndim  = dt.ndim;
        memcpy(t->dims, dt.dims, sizeof t->dims);
        t->scale = dt.scale;
        t->zp    = dt.zp;
        t->elems = (uint32_t)el;
        t->bytes = (uint32_t)el * (uint32_t)vq_esize(dt.dtype);
        memcpy(t->name, dt.name, sizeof t->name);
        t->name[sizeof t->name - 1] = '\0';

        if (dt.size > 0) {                        /* weight/const      */
            if (dt.size != t->bytes ||
                (uint64_t)dt.offset + dt.size > h->weights_bytes) {
                return ESP_ERR_INVALID_SIZE;
            }
            t->data = (uint8_t *)(weights + dt.offset);
            t->is_const = 1;
            params += t->elems;
        } else {                                  /* activation        */
            arena_need = align_up(arena_need, VQ_ARENA_ALIGN) + t->bytes;
        }
    }
    m->param_count = params;
    m->arena_used  = arena_need;
    if (arena_need > VQ_ARENA_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* ---- nodes: resolve params + validate tensor refs ------------- */
    for (uint32_t i = 0; i < h->n_nodes; i++) {
        vqmb_node_t nd;
        memcpy(&nd, blob + n_off + i * sizeof(vqmb_node_t), sizeof nd);
        for (uint32_t j = 0; j < nd.n_in; j++) {
            if (nd.in[j] >= h->n_tensors) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        for (uint32_t j = 0; j < nd.n_out; j++) {
            if (nd.out[j] >= h->n_tensors ||
                m->ts[nd.out[j]].is_const) {   /* can't write weights */
                return ESP_ERR_INVALID_ARG;
            }
        }
        m->nodes[i].d = nd;
        m->nodes[i].params = (nd.param_len > 0)
                           ? blob + fixed + nd.param_off : NULL;
        if (nd.op == VQ_OP_GELU || nd.op == VQ_OP_SIGMOID ||
            nd.op == VQ_OP_TANH) {
            m->nodes[i].lut = vq_alloc(sizeof(vq_lut8_t));
            if (m->nodes[i].lut == NULL) {
                return ESP_ERR_NO_MEM;
            }
            m->nodes[i].lut->ready = 0;
        }
    }

    /* ---- arena ------------------------------------------------------ */
    m->arena_alloc = (arena_need > h->arena_bytes) ? arena_need
                                                   : h->arena_bytes;
    m->arena = vq_alloc(m->arena_alloc ? m->arena_alloc : 16);
    if (m->arena == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(m->arena, 0, m->arena_alloc);
    uint32_t cur = 0;
    for (uint32_t i = 0; i < h->n_tensors; i++) {
        vq_rtensor_t *t = &m->ts[i];
        if (t->is_const) {
            continue;
        }
        cur = align_up(cur, VQ_ARENA_ALIGN);
        if (cur + t->bytes > m->arena_alloc) {
            return ESP_ERR_INVALID_SIZE;      /* impossible by build  */
        }
        t->data = m->arena + cur;
        cur += t->bytes;
    }
    m->arena_used = cur;

    /* Deterministic first-run inputs: quantised 0.0 (zp byte for q8). */
    for (uint32_t i = 0; i < h->n_inputs; i++) {
        vq_rtensor_t *t = &m->ts[m->in_idx[i]];
        memset(t->data,
               (t->dtype == VQ_DT_Q8) ? (int)(int8_t)t->zp : 0,
               t->bytes);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
vq_err_t vq_model_load_blob(vq_model_t **out, const void *blob, size_t len)
{
    if (out == NULL || blob == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    vq_model_t *m = vq_calloc(1, sizeof(vq_model_t));
    if (m == NULL) {
        return ESP_ERR_NO_MEM;
    }
    vq_err_t err = parse_blob(m, (const uint8_t *)blob, len);
    if (err != ESP_OK) {
        vq_model_free(m);
        return err;
    }
    m->owns_mmap = false;
    *out = m;
    return ESP_OK;
}

vq_err_t vq_model_load(vq_model_t **out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    vq_err_t err = ESP_ERR_NOT_FOUND;

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)VQ_MODEL_PART_SUBTYPE,
        "model");
    if (part != NULL) {
        const void *base = NULL;
        esp_partition_mmap_handle_t mh = 0;
        err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                                 &base, &mh);
        if (err == ESP_OK) {
            vq_model_t *m = vq_calloc(1, sizeof(vq_model_t));
            if (m != NULL) {
                err = parse_blob(m, (const uint8_t *)base, part->size);
                if (err == ESP_OK) {
                    m->owns_mmap = true;
                    m->mmap_h = mh;
                    *out = m;
                    ESP_LOGI(TAG, "VQMB v%u from 'model' part: %u tensors, "
                             "%u nodes, %u params, arena %u B",
                             (unsigned)m->hdr.ver, (unsigned)m->hdr.n_tensors,
                             (unsigned)m->hdr.n_nodes,
                             (unsigned)m->param_count,
                             (unsigned)m->arena_used);
                    return ESP_OK;
                }
                vq_model_free(m);
            } else {
                err = ESP_ERR_NO_MEM;
            }
            esp_partition_munmap(mh);
        }
    }

    ESP_LOGW(TAG, "no usable model-partition blob (%s) — using "
             "compiled-in stub", esp_err_to_name(err));
    size_t slen = 0;
    const uint8_t *sblob = vq_stub_model(&slen);
    err = vq_model_load_blob(out, sblob, slen);
    if (err == ESP_OK) {
        (*out)->is_stub = true;
        ESP_LOGI(TAG, "stub model loaded (%u B blob, %u params)",
                 (unsigned)slen, (unsigned)(*out)->param_count);
    }
    return err;                          /* stub failure is fatal     */
}

void vq_model_free(vq_model_t *m)
{
    if (m == NULL) {
        return;
    }
    if (m->nodes != NULL) {
        for (uint32_t i = 0; i < m->hdr.n_nodes; i++) {
            if (m->nodes[i].lut != NULL) {
                heap_caps_free(m->nodes[i].lut);
            }
        }
    }
    if (m->owns_mmap) {
        esp_partition_munmap(m->mmap_h);
    }
    heap_caps_free(m->ts);
    heap_caps_free(m->nodes);
    heap_caps_free(m->in_idx);
    heap_caps_free(m->out_idx);
    heap_caps_free(m->arena);
    heap_caps_free(m);
}

/* ------------------------------------------------------------------ */
size_t vq_model_n_inputs(const vq_model_t *m)
{
    return (m != NULL) ? (size_t)m->hdr.n_inputs : 0u;
}

size_t vq_model_n_outputs(const vq_model_t *m)
{
    return (m != NULL) ? (size_t)m->hdr.n_outputs : 0u;
}

int vq_model_input_index(const vq_model_t *m, const char *name)
{
    if (m == NULL || name == NULL) {
        return -1;
    }
    for (uint32_t i = 0; i < m->hdr.n_inputs; i++) {
        if (strncmp(m->ts[m->in_idx[i]].name, name,
                    sizeof(m->ts[0].name)) == 0) {
            return (int)i;
        }
    }
    return -1;
}

vq_err_t vq_model_set_input_f32(vq_model_t *m, int idx,
                                const float *data, size_t n)
{
    if (m == NULL || data == NULL || idx < 0 ||
        (uint32_t)idx >= m->hdr.n_inputs) {
        return ESP_ERR_INVALID_ARG;
    }
    vq_rtensor_t *t = &m->ts[m->in_idx[idx]];
    /* pre-fill the tail with quantised 0.0 so a short write leaves a
     * deterministic input (mask-free defaults per features.h)        */
    memset(t->data, (t->dtype == VQ_DT_Q8) ? (int)(int8_t)t->zp : 0,
           t->bytes);
    const uint32_t cnt = (n < t->elems) ? (uint32_t)n : t->elems;
    for (uint32_t i = 0; i < cnt; i++) {
        vq_wr_f32(t, i, data[i]);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
vq_err_t vq_model_run(vq_model_t *m, int64_t *latency_us)
{
    if (m == NULL || m->ts == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t t0 = esp_timer_get_time();
    for (uint32_t i = 0; i < m->hdr.n_nodes; i++) {
        const rt_node_t *nd = &m->nodes[i];
        vq_rtensor_t *ins[4]  = { NULL, NULL, NULL, NULL };
        vq_rtensor_t *outs[4] = { NULL, NULL, NULL, NULL };
        for (uint32_t j = 0; j < nd->d.n_in; j++) {
            ins[j] = &m->ts[nd->d.in[j]];
            if (ins[j]->data == NULL) {
                return ESP_ERR_INVALID_STATE;    /* unbound input    */
            }
        }
        for (uint32_t j = 0; j < nd->d.n_out; j++) {
            outs[j] = &m->ts[nd->d.out[j]];
        }
        const uint32_t c0 = vq_cyc();
        const vq_err_t err = vq_kern_exec((vq_op_t)nd->d.op,
                                          ins, (int)nd->d.n_in,
                                          outs, (int)nd->d.n_out,
                                          nd->params, nd->d.param_len,
                                          nd->lut);
        const uint32_t c1 = vq_cyc();
        m->op_cycles[nd->d.op] += (uint64_t)(c1 - c0);
        m->op_calls[nd->d.op]++;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "node %u op %s failed: %s", (unsigned)i,
                     vq_op_name((int)nd->d.op), esp_err_to_name(err));
            return err;
        }
    }
    m->last_us = esp_timer_get_time() - t0;
    if (latency_us != NULL) {
        *latency_us = m->last_us;
    }
    return ESP_OK;
}

vq_err_t vq_model_get_output_f32(vq_model_t *m, int idx,
                                 float *data, size_t n)
{
    if (m == NULL || data == NULL || idx < 0 ||
        (uint32_t)idx >= m->hdr.n_outputs) {
        return ESP_ERR_INVALID_ARG;
    }
    const vq_rtensor_t *t = &m->ts[m->out_idx[idx]];
    const uint32_t cnt = (n < t->elems) ? (uint32_t)n : t->elems;
    for (uint32_t i = 0; i < cnt; i++) {
        data[i] = vq_rd_f32(t, i);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
uint32_t vq_model_param_count(const vq_model_t *m)
{
    return (m != NULL) ? m->param_count : 0u;
}

uint32_t vq_model_arena_used(const vq_model_t *m)
{
    return (m != NULL) ? m->arena_used : 0u;
}

/* emit() appends at *off with clamping — a truncated snprintf would
 * otherwise push *off past len and the next len-*off would underflow. */
static void emit(char *buf, size_t len, size_t *off, const char *fmt, ...)
{
    if (*off >= len) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(buf + *off, len - *off, fmt, ap);
    va_end(ap);
    if (w <= 0) {
        return;
    }
    *off += (size_t)w;
    if (*off >= len) {
        *off = len - 1;              /* cursor parked at the NUL       */
    }
}

/* Report: header info, param count, arena, per-op call/cycle table,
 * one golden inference on all-zero inputs. `const` is cast away for
 * the golden run — it only writes into the model's own arena/LUTs.   */
void vq_model_selftest_report(const vq_model_t *cm, char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }
    buf[0] = '\0';
    if (cm == NULL) {
        snprintf(buf, len, "vq_model: NULL");
        return;
    }
    vq_model_t *m = (vq_model_t *)cm;    /* arena writes are internal */
    size_t off = 0;
    emit(buf, len, &off,
         "VQMB v%u src=%s tensors=%u nodes=%u io=%u/%u "
         "params=%u weights=%uB arena=%u/%uB",
         (unsigned)m->hdr.ver,
         m->is_stub ? "stub" : (m->owns_mmap ? "partition" : "blob"),
         (unsigned)m->hdr.n_tensors, (unsigned)m->hdr.n_nodes,
         (unsigned)m->hdr.n_inputs, (unsigned)m->hdr.n_outputs,
         (unsigned)m->param_count,
         (unsigned)m->hdr.weights_bytes,
         (unsigned)m->arena_used, (unsigned)m->arena_alloc);

    for (int op = 0; op < VQ_OP_COUNT; op++) {
        if (m->op_calls[op] == 0) {
            continue;
        }
        emit(buf, len, &off, "\n  %-9s x%u %llu cy",
             vq_op_name(op), (unsigned)m->op_calls[op],
             (unsigned long long)m->op_cycles[op]);
    }

    /* golden inference on zeros */
    for (uint32_t i = 0; i < m->hdr.n_inputs; i++) {
        vq_rtensor_t *t = &m->ts[m->in_idx[i]];
        memset(t->data,
               (t->dtype == VQ_DT_Q8) ? (int)(int8_t)t->zp : 0,
               t->bytes);
    }
    int64_t lat = 0;
    const vq_err_t err = vq_model_run(m, &lat);
    if (err != ESP_OK) {
        emit(buf, len, &off, "\n  golden[zeros]: FAILED %s",
             esp_err_to_name(err));
        return;
    }
    float tmp[2] = { 0.0f, 0.0f };
    vq_model_get_output_f32(m, 0, tmp, 2);
    emit(buf, len, &off,
         "\n  golden[zeros]: prob_normal=%.3f "
         "prob_problematic=%.3f lat=%lldus",
         (double)tmp[0], (double)tmp[1], (long long)lat);
}
