/* vq_kernels.c — the full vq_op_t kernel set. Plain portable C99; every
 * hot loop is a clean int32 accumulate over int8 so the ESP32-S3 SIMD
 * unit (EE.QACC/AA.MAC via the compiler) gets most of the win for free.
 * If a profiling pass later shows DENSE/CONV dominating, swap the two
 * innermost loops for esp-dsp dsps_mul_s8/dsps_conv — the requant fixed
 * point (vq_fp_ratio/vq_mulhi) is already the TFLite-style scheme.
 *
 * Conventions (see vq_kernels.h for the blob-level doc):
 *  - q8 tensors are int8 with per-tensor scale/zp; i32 tensors are raw
 *    accumulators (biases); f32 tensors are real values.
 *  - Requant uses round-half-up arithmetic shift; bias adds happen in
 *    int32 accumulator units (exporter pre-scales i32 biases by
 *    in_scale*w_scale).
 *  - GRU keeps its state in f32 across timesteps (documented below);
 *    all other ops stay in the integer domain.
 */
#include "vq_kernels.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ---- small helpers -------------------------------------------------- */

int vq_esize(uint8_t dtype)
{
    switch (dtype) {
    case VQ_DT_I32:
    case VQ_DT_F32: return 4;
    default:        return 1;   /* q8 and anything unknown */
    }
}

uint32_t vq_elems_of(uint8_t ndim, const uint16_t *dims)
{
    if (ndim == 0) {
        return 1;               /* scalar */
    }
    uint32_t e = 1;
    for (int i = 0; i < ndim && i < 4; i++) {
        e *= dims[i];
    }
    return e;
}

uint32_t vq_crc32(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

static int8_t clamp8(int32_t v)
{
    if (v > 127)  return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}

/* ---- fixed-point requant ------------------------------------------- */

void vq_fp_ratio(float ratio, int32_t *mult, int *shift)
{
    if (!(ratio > 0.0f) || !isfinite(ratio)) {
        *mult = 0;              /* degenerate: forces output = zp */
        *shift = 0;
        return;
    }
    int e = 0;
    const float m = frexpf(ratio, &e);          /* m in [0.5,1)    */
    double mq = (double)m * 2147483648.0;       /* Q31             */
    if (mq >= 2147483647.0) {
        mq = 2147483647.0;                      /* m==1.0 edge     */
    }
    *mult  = (int32_t)llrint(mq);
    *shift = 31 - e;
    if (*shift > 62)  *shift = 62;              /* tiny ratios     */
    if (*shift < -30) *shift = -30;             /* absurd upscale  */
}

int32_t vq_mulhi(int32_t acc, int32_t mult, int shift)
{
    int64_t p = (int64_t)acc * (int64_t)mult;   /* |p| < 2^62      */
    int64_t r;
    if (shift > 0) {
        const int64_t rnd = (int64_t)1 << (shift - 1);
        r = (p + rnd) >> shift;                 /* round-half-up   */
    } else if (shift == 0) {
        r = p;
    } else {
        r = p << (-shift);                      /* shift >= -30    */
    }
    if (r > INT32_MAX) return INT32_MAX;
    if (r < INT32_MIN) return INT32_MIN;
    return (int32_t)r;
}

int8_t vq_requant8(int32_t acc, int32_t mult, int shift, int32_t zp)
{
    return clamp8(vq_mulhi(acc, mult, shift) + zp);
}

/* ---- generic element access (slow paths) ---------------------------- */

float vq_rd_f32(const vq_rtensor_t *t, uint32_t i)
{
    switch (t->dtype) {
    case VQ_DT_Q8: {
        int8_t q;
        memcpy(&q, t->data + i, 1);
        return ((float)q - (float)t->zp) * t->scale;
    }
    case VQ_DT_I32: {
        int32_t v;
        memcpy(&v, t->data + (size_t)i * 4u, 4);
        return (float)v * (t->scale != 0.0f ? t->scale : 1.0f);
    }
    default: {
        float v;
        memcpy(&v, t->data + (size_t)i * 4u, 4);
        return v;
    }
    }
}

void vq_wr_f32(vq_rtensor_t *t, uint32_t i, float v)
{
    switch (t->dtype) {
    case VQ_DT_Q8: {
        const float s = (t->scale != 0.0f) ? t->scale : 1.0f;
        const int8_t q = clamp8((int32_t)lrintf(v / s) + t->zp);
        memcpy(t->data + i, &q, 1);
        break;
    }
    case VQ_DT_I32: {
        const float s = (t->scale != 0.0f) ? t->scale : 1.0f;
        const int32_t q = (int32_t)lrintf(v / s);
        memcpy(t->data + (size_t)i * 4u, &q, 4);
        break;
    }
    default: {
        memcpy(t->data + (size_t)i * 4u, &v, 4);
        break;
    }
    }
}

/* i32 element read (biases) — memcpy so unaligned weights are safe.    */
static int32_t rd_i32(const vq_rtensor_t *t, uint32_t i)
{
    int32_t v = 0;
    if (t != NULL && i < t->elems) {
        if (t->dtype == VQ_DT_I32 || t->dtype == VQ_DT_F32) {
            memcpy(&v, t->data + (size_t)i * 4u, 4);
        } else {
            v = (int32_t)(int8_t)t->data[i] - t->zp;
        }
    }
    return v;
}

static int32_t param_i32(const uint8_t *p, uint32_t len, int idx, int32_t dflt)
{
    if (p != NULL && (uint32_t)(idx + 1) * 4u <= len) {
        int32_t v;
        memcpy(&v, p + (size_t)idx * 4u, 4);
        return v;
    }
    return dflt;
}

static float param_f32(const uint8_t *p, uint32_t len, int idx, float dflt)
{
    if (p != NULL && (uint32_t)(idx + 1) * 4u <= len) {
        float v;
        memcpy(&v, p + (size_t)idx * 4u, 4);
        return v;
    }
    return dflt;
}

static float act_f32(vq_op_t op, float x)
{
    switch (op) {
    case VQ_OP_SIGMOID: return 1.0f / (1.0f + expf(-x));
    case VQ_OP_TANH:    return tanhf(x);
    default: /* GELU, tanh approx 0.5x(1+tanh(sqrt(2/pi)(x+0.044715x^3))) */
        return 0.5f * x * (1.0f + tanhf(0.7978845608028654f *
                                        (x + 0.044715f * x * x * x)));
    }
}

/* Lazy 256-entry LUT: one output byte per possible input byte for this
 * node's (in scale,zp) -> (out scale,zp) mapping.                       */
static void lut_build(vq_lut8_t *lut, const vq_rtensor_t *in,
                      const vq_rtensor_t *out, vq_op_t op)
{
    for (int i = 0; i < 256; i++) {
        const int8_t q  = (int8_t)(uint8_t)i;
        const float  x  = ((float)q - (float)in->zp) * in->scale;
        const float  y  = act_f32(op, x);
        const float  os = (out->scale != 0.0f) ? out->scale : 1.0f;
        lut->q[i] = clamp8((int32_t)lrintf(y / os) + out->zp);
    }
    lut->ready = 1u;
}

static vq_err_t lut_run(vq_op_t op, vq_rtensor_t *in, vq_rtensor_t *out,
                        vq_lut8_t *lut)
{
    if (in->dtype != VQ_DT_Q8 || out->dtype != VQ_DT_Q8 ||
        in->elems != out->elems || lut == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lut->ready) {
        lut_build(lut, in, out, op);
    }
    const uint8_t *src = in->data;
    int8_t *dst = (int8_t *)out->data;
    for (uint32_t i = 0; i < in->elems; i++) {
        dst[i] = lut->q[src[i]];
    }
    return ESP_OK;
}

/* ---- DENSE ----------------------------------------------------------
 * x[..,N] q8  w[M,N] q8  b[M] i32 (opt)  -> out[..,M] q8
 * batched over leading dims: rows = x.elems / N, out.elems = rows*M.  */
static vq_err_t k_dense(vq_rtensor_t *const *in, int n_in,
                        vq_rtensor_t *const *out)
{
    const vq_rtensor_t *x = in[0];
    const vq_rtensor_t *w = in[1];
    const vq_rtensor_t *b = (n_in > 2) ? in[2] : NULL;
    if (x->dtype != VQ_DT_Q8 || w->dtype != VQ_DT_Q8 ||
        out[0]->dtype != VQ_DT_Q8) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t N = (w->ndim >= 2) ? w->dims[1] : w->elems;
    if (N == 0 || w->elems % N != 0 || x->elems % N != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t M    = w->elems / N;
    const uint32_t rows = x->elems / N;
    if (out[0]->elems != rows * M) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (b != NULL && b->elems < M) {
        return ESP_ERR_INVALID_SIZE;
    }
    int32_t mult; int shift;
    vq_fp_ratio(x->scale * w->scale / out[0]->scale, &mult, &shift);
    const int32_t zo = out[0]->zp;

    for (uint32_t r = 0; r < rows; r++) {
        const int8_t *xr = (const int8_t *)x->data + (size_t)r * N;
        int8_t *yr = (int8_t *)out[0]->data + (size_t)r * M;
        for (uint32_t m = 0; m < M; m++) {
            const int8_t *wr = (const int8_t *)w->data + (size_t)m * N;
            int32_t acc = rd_i32(b, m);
            for (uint32_t n = 0; n < N; n++) {          /* clean MAC   */
                acc += (int32_t)xr[n] * (int32_t)wr[n];
            }
            yr[m] = vq_requant8(acc, mult, shift, zo);
        }
    }
    return ESP_OK;
}

/* ---- CONV1D ----------------------------------------------------------
 * x[L,Ci] q8, w[Co,k,Ci/groups] q8, b[Co] i32 opt -> y[Lo,Co] q8.      */
static vq_err_t k_conv1d(vq_rtensor_t *const *in, int n_in,
                         vq_rtensor_t *const *out,
                         const uint8_t *p, uint32_t plen)
{
    const vq_rtensor_t *x = in[0];
    const vq_rtensor_t *w = in[1];
    const vq_rtensor_t *b = (n_in > 2) ? in[2] : NULL;
    if (x->dtype != VQ_DT_Q8 || w->dtype != VQ_DT_Q8 ||
        out[0]->dtype != VQ_DT_Q8 || x->ndim < 2 || w->ndim < 3) {
        return ESP_ERR_INVALID_ARG;
    }
    const int32_t k      = param_i32(p, plen, 0, (int32_t)w->dims[1]);
    const int32_t stride = param_i32(p, plen, 1, 1);
    const int32_t pad    = param_i32(p, plen, 2, 0);
    const int32_t dil    = param_i32(p, plen, 3, 1);
    const int32_t groups = param_i32(p, plen, 4, 1);
    const uint32_t L  = x->dims[0], Ci = x->dims[1];
    const uint32_t Co = w->dims[0], Cg = w->dims[2];
    if (k <= 0 || stride <= 0 || dil <= 0 || groups <= 0 ||
        Ci != (uint32_t)groups * Cg || Co % (uint32_t)groups != 0 ||
        w->dims[1] != (uint32_t)k) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int32_t loin = (int32_t)L + 2 * pad - dil * (k - 1) - 1;
    const uint32_t Lo = (loin >= 0) ? (uint32_t)(loin / stride) + 1u : 0u;
    if (out[0]->elems != Lo * Co) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (b != NULL && b->elems < Co) {
        return ESP_ERR_INVALID_SIZE;
    }
    int32_t mult; int shift;
    vq_fp_ratio(x->scale * w->scale / out[0]->scale, &mult, &shift);
    const int32_t zo = out[0]->zp;
    const uint32_t cpg = Co / (uint32_t)groups;   /* out ch per group  */
    const int8_t *xd = (const int8_t *)x->data;
    const int8_t *wd = (const int8_t *)w->data;
    int8_t *yd = (int8_t *)out[0]->data;

    for (uint32_t l = 0; l < Lo; l++) {
        for (uint32_t co = 0; co < Co; co++) {
            const uint32_t g = co / cpg;
            int32_t acc = rd_i32(b, co);
            const int8_t *wr = wd + (size_t)co * (size_t)k * Cg;
            for (int32_t j = 0; j < k; j++) {
                const int32_t li = (int32_t)l * stride + j * dil - pad;
                if (li < 0 || li >= (int32_t)L) {
                    continue;
                }
                const int8_t *xr = xd + (size_t)li * Ci + g * Cg;
                const int8_t *wk = wr + (size_t)j * Cg;
                for (uint32_t c = 0; c < Cg; c++) {
                    acc += (int32_t)xr[c] * (int32_t)wk[c];
                }
            }
            yd[(size_t)l * Co + co] = vq_requant8(acc, mult, shift, zo);
        }
    }
    return ESP_OK;
}

/* ---- DWCONV1D --------------------------------------------------------
 * x[L,C] q8, w[C,k] q8, b[C] i32 opt -> y[Lo,C] q8 (depth mult 1).     */
static vq_err_t k_dwconv1d(vq_rtensor_t *const *in, int n_in,
                           vq_rtensor_t *const *out,
                           const uint8_t *p, uint32_t plen)
{
    const vq_rtensor_t *x = in[0];
    const vq_rtensor_t *w = in[1];
    const vq_rtensor_t *b = (n_in > 2) ? in[2] : NULL;
    if (x->dtype != VQ_DT_Q8 || w->dtype != VQ_DT_Q8 ||
        out[0]->dtype != VQ_DT_Q8 || x->ndim < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t L = x->dims[0], C = x->dims[1];
    const int32_t k      = param_i32(p, plen, 0,
                        (w->ndim >= 2) ? (int32_t)w->dims[1]
                                       : (int32_t)(w->elems / C));
    const int32_t stride = param_i32(p, plen, 1, 1);
    const int32_t pad    = param_i32(p, plen, 2, 0);
    const int32_t dil    = param_i32(p, plen, 3, 1);
    if (k <= 0 || stride <= 0 || dil <= 0 || w->elems != C * (uint32_t)k) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int32_t loin = (int32_t)L + 2 * pad - dil * (k - 1) - 1;
    const uint32_t Lo = (loin >= 0) ? (uint32_t)(loin / stride) + 1u : 0u;
    if (out[0]->elems != Lo * C || (b != NULL && b->elems < C)) {
        return ESP_ERR_INVALID_SIZE;
    }
    int32_t mult; int shift;
    vq_fp_ratio(x->scale * w->scale / out[0]->scale, &mult, &shift);
    const int32_t zo = out[0]->zp;
    const int8_t *xd = (const int8_t *)x->data;
    const int8_t *wd = (const int8_t *)w->data;
    int8_t *yd = (int8_t *)out[0]->data;

    for (uint32_t l = 0; l < Lo; l++) {
        for (uint32_t c = 0; c < C; c++) {
            const int8_t *wc = wd + (size_t)c * (size_t)k;
            int32_t acc = rd_i32(b, c);
            for (int32_t j = 0; j < k; j++) {
                const int32_t li = (int32_t)l * stride + j * dil - pad;
                if (li < 0 || li >= (int32_t)L) {
                    continue;
                }
                acc += (int32_t)xd[(size_t)li * C + c] * (int32_t)wc[j];
            }
            yd[(size_t)l * C + c] = vq_requant8(acc, mult, shift, zo);
        }
    }
    return ESP_OK;
}

/* ---- pools ------------------------------------------------------------
 * x[L,C] q8 -> y[Lo,C] q8. MAX on raw q8 (zp-monotone). AVG sums the
 * dezeroed taps and requants with ratio sa/(cnt*sout).                 */
static vq_err_t k_pool1d(vq_op_t op, vq_rtensor_t *const *in,
                         vq_rtensor_t *const *out,
                         const uint8_t *p, uint32_t plen)
{
    const vq_rtensor_t *x = in[0];
    if (x->dtype != VQ_DT_Q8 || out[0]->dtype != VQ_DT_Q8 ||
        x->ndim < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t L = x->dims[0], C = x->dims[1];
    const int32_t k      = param_i32(p, plen, 0, 2);
    const int32_t stride = param_i32(p, plen, 1, k);
    const int32_t pad    = param_i32(p, plen, 2, 0);
    if (k <= 0 || stride <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int32_t loin = (int32_t)L + 2 * pad - k;
    const uint32_t Lo = (loin >= 0) ? (uint32_t)(loin / stride) + 1u : 0u;
    if (out[0]->elems != Lo * C) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int8_t *xd = (const int8_t *)x->data;
    int8_t *yd = (int8_t *)out[0]->data;

    if (op == VQ_OP_MAXPOOL1D) {
        for (uint32_t l = 0; l < Lo; l++) {
            for (uint32_t c = 0; c < C; c++) {
                int8_t best = -128;
                int32_t cnt = 0;
                for (int32_t j = 0; j < k; j++) {
                    const int32_t li = (int32_t)l * stride + j - pad;
                    if (li < 0 || li >= (int32_t)L) {
                        continue;
                    }
                    const int8_t v = xd[(size_t)li * C + c];
                    if (v > best) {
                        best = v;
                    }
                    cnt++;
                }
                /* empty window (full padding) -> quantised 0.0 = zp   */
                yd[(size_t)l * C + c] = (cnt > 0) ? best : clamp8(x->zp);
            }
        }
        return ESP_OK;
    }

    /* AVGPOOL1D */
    for (uint32_t l = 0; l < Lo; l++) {
        for (uint32_t c = 0; c < C; c++) {
            int32_t sum = 0;
            int32_t cnt = 0;
            for (int32_t j = 0; j < k; j++) {
                const int32_t li = (int32_t)l * stride + j - pad;
                if (li < 0 || li >= (int32_t)L) {
                    continue;
                }
                sum += (int32_t)xd[(size_t)li * C + c] - x->zp;
                cnt++;
            }
            int32_t mult; int shift;
            const float cnt_f = (cnt > 0) ? (float)cnt : 1.0f;
            vq_fp_ratio(x->scale / (cnt_f * out[0]->scale), &mult, &shift);
            yd[(size_t)l * C + c] = vq_requant8(sum, mult, shift,
                                              out[0]->zp);
        }
    }
    return ESP_OK;
}

/* ---- GAP: x[L,C] -> y[C], mean over L --------------------------------- */
static vq_err_t k_gap(vq_rtensor_t *const *in, vq_rtensor_t *const *out)
{
    const vq_rtensor_t *x = in[0];
    if (x->dtype != VQ_DT_Q8 || out[0]->dtype != VQ_DT_Q8 ||
        x->ndim < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t L = x->dims[0], C = x->dims[1];
    if (L == 0 || out[0]->elems != C) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int8_t *xd = (const int8_t *)x->data;
    int8_t *yd = (int8_t *)out[0]->data;
    int32_t mult; int shift;
    vq_fp_ratio(x->scale / ((float)L * out[0]->scale), &mult, &shift);
    for (uint32_t c = 0; c < C; c++) {
        int32_t sum = 0;
        for (uint32_t l = 0; l < L; l++) {
            sum += (int32_t)xd[(size_t)l * C + c] - x->zp;
        }
        yd[c] = vq_requant8(sum, mult, shift, out[0]->zp);
    }
    return ESP_OK;
}

/* ---- RELU / RELU6 -------------------------------------------------------
 * Real-domain clamp: y = max(x,0) (or min(...,6)). d = q - zp keeps the
 * integer domain; the 6.0 bound is pre-rounded to input q-units.        */
static vq_err_t k_relu(vq_op_t op, vq_rtensor_t *const *in,
                       vq_rtensor_t *const *out)
{
    const vq_rtensor_t *x = in[0];
    if (x->dtype != VQ_DT_Q8 || out[0]->dtype != VQ_DT_Q8 ||
        x->elems != out[0]->elems ||
        x->scale <= 0.0f || out[0]->scale <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t mult; int shift;
    vq_fp_ratio(x->scale / out[0]->scale, &mult, &shift);
    const int32_t zi = x->zp, zo = out[0]->zp;
    const int32_t hi = (op == VQ_OP_RELU6)
                     ? (int32_t)lrintf(6.0f / x->scale) : INT32_MAX;
    const int8_t *xd = (const int8_t *)x->data;
    int8_t *yd = (int8_t *)out[0]->data;
    for (uint32_t i = 0; i < x->elems; i++) {
        int32_t d = (int32_t)xd[i] - zi;
        if (d < 0) {
            d = 0;
        }
        if (d > hi) {
            d = hi;
        }
        yd[i] = vq_requant8(d, mult, shift, zo);
    }
    return ESP_OK;
}

/* ---- ADD / MUL with broadcast -------------------------------------------
 * Both take the broadcast fast path when shapes match and the general
 * numpy-style trailing-dims path otherwise. ADD uses two fixed-point
 * ratios (sa/sout, sb/sout); MUL uses one (sa*sb/sout) on the i32
 * product — pure integer, no f32 in either path.                         */
static void bstride(const vq_rtensor_t *t, const vq_rtensor_t *o,
                    uint32_t st[4])
{
    /* element strides of t expressed in out-dim space; 0 for
     * broadcasted (size-1 or implicit leading) dims                    */
    for (int d = 0; d < 4; d++) {
        st[d] = 0;
    }
    const int off = (int)o->ndim - (int)t->ndim;
    uint32_t s = 1;
    for (int d = (int)t->ndim - 1; d >= 0; d--) {
        const int od = d + off;
        if (od >= 0 && od < 4) {
            st[od] = (t->dims[d] == 1) ? 0u : s;
        }
        s *= (t->dims[d] != 0) ? t->dims[d] : 1u;
    }
}

static uint32_t boff(const uint32_t st[4], const uint16_t odims[4],
                     uint8_t ndim, uint32_t idx)
{
    uint32_t off = 0;
    for (int d = (int)ndim - 1; d >= 0; d--) {
        const uint32_t dim = odims[d] ? odims[d] : 1u;
        off += (idx % dim) * st[d];
        idx /= dim;
    }
    return off;
}

static vq_err_t k_addmul(vq_op_t op, vq_rtensor_t *const *in,
                         vq_rtensor_t *const *out)
{
    const vq_rtensor_t *a = in[0];
    const vq_rtensor_t *b = in[1];
    vq_rtensor_t *y = out[0];
    if (a->dtype != VQ_DT_Q8 || b->dtype != VQ_DT_Q8 ||
        y->dtype != VQ_DT_Q8 || a->ndim > y->ndim || b->ndim > y->ndim) {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t ma, mb; int sa, sb;
    vq_fp_ratio(a->scale / y->scale, &ma, &sa);
    if (op == VQ_OP_ADD) {
        vq_fp_ratio(b->scale / y->scale, &mb, &sb);
    } else {
        vq_fp_ratio(a->scale * b->scale / y->scale, &mb, &sb);
    }
    const int32_t zo = y->zp;
    const int8_t *ad = (const int8_t *)a->data;
    const int8_t *bd = (const int8_t *)b->data;
    int8_t *yd = (int8_t *)y->data;

    const int same_shape = (a->elems == y->elems && b->elems == y->elems);
    const int a_scalar = (a->elems == 1), b_scalar = (b->elems == 1);
    uint32_t as[4] = {0, 0, 0, 0}, bs[4] = {0, 0, 0, 0};
    if (!same_shape) {                       /* hoisted out of the loop */
        bstride(a, y, as);
        bstride(b, y, bs);
    }

    for (uint32_t i = 0; i < y->elems; i++) {
        int32_t ai, bi;
        if (same_shape) {
            ai = ad[i]; bi = bd[i];
        } else {
            ai = a_scalar ? ad[0] : ad[boff(as, y->dims, y->ndim, i)];
            bi = b_scalar ? bd[0] : bd[boff(bs, y->dims, y->ndim, i)];
        }
        int32_t acc;
        if (op == VQ_OP_ADD) {
            acc = vq_mulhi(ai - a->zp, ma, sa) + vq_mulhi(bi - b->zp, mb, sb);
        } else {
            acc = (ai - a->zp) * (bi - b->zp);    /* <= 65536            */
        }
        yd[i] = (op == VQ_OP_ADD)
              ? clamp8(acc + zo)
              : vq_requant8(acc, mb, sb, zo);
    }
    return ESP_OK;
}

/* ---- CONCAT ------------------------------------------------------------
 * params {axis} (default = last dim). All inputs same ndim and same
 * dims except along axis. Fast path: same dtype+quant -> block memcpy;
 * otherwise per-element requant through the f32 accessors.              */
static vq_err_t k_concat(vq_rtensor_t *const *in, int n_in,
                         vq_rtensor_t *const *out,
                         const uint8_t *p, uint32_t plen)
{
    vq_rtensor_t *y = out[0];
    if (n_in < 1 || y->ndim == 0 || y->ndim > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t ax = param_i32(p, plen, 0, (int32_t)y->ndim - 1);
    if (ax < 0) {
        ax += y->ndim;
    }
    if (ax < 0 || ax >= y->ndim) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t outer = 1, inner = 1;
    for (int d = 0; d < y->ndim; d++) {
        if (d < ax)      outer *= y->dims[d];
        else if (d > ax) inner *= y->dims[d];
    }

    int same_q = 1;
    uint32_t ax_sum = 0;
    for (int i = 0; i < n_in; i++) {
        const vq_rtensor_t *t = in[i];
        if (t->ndim != y->ndim) {
            return ESP_ERR_INVALID_SIZE;
        }
        for (int d = 0; d < y->ndim; d++) {
            if (d != ax && t->dims[d] != y->dims[d]) {
                return ESP_ERR_INVALID_SIZE;
            }
        }
        ax_sum += t->dims[ax];
        if (t->dtype != y->dtype || t->scale != y->scale ||
            t->zp != y->zp) {
            same_q = 0;
        }
    }
    if (ax_sum != y->dims[ax]) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t ax_off = 0;
    for (int i = 0; i < n_in; i++) {
        vq_rtensor_t *t = in[i];
        const uint32_t alen = t->dims[ax];
        if (same_q) {
            const size_t chunk = (size_t)alen * inner * (size_t)vq_esize(t->dtype);
            for (uint32_t o = 0; o < outer; o++) {
                memcpy(y->data + ((size_t)o * y->dims[ax] + ax_off) * inner *
                       (size_t)vq_esize(t->dtype),
                       t->data + (size_t)o * chunk, chunk);
            }
        } else {
            /* slow path: dequant/requant per element                  */
            for (uint32_t o = 0; o < outer; o++) {
                for (uint32_t a = 0; a < alen; a++) {
                    for (uint32_t r = 0; r < inner; r++) {
                        const uint32_t src = (o * alen + a) * inner + r;
                        const uint32_t dst = (o * y->dims[ax] + ax_off + a)
                                           * inner + r;
                        vq_wr_f32(y, dst, vq_rd_f32(t, src));
                    }
                }
            }
        }
        ax_off += alen;
    }
    return ESP_OK;
}

/* ---- RESHAPE — pure view: out->data aliases in->data ----------------- */
static vq_err_t k_reshape(vq_rtensor_t *const *in, vq_rtensor_t *const *out)
{
    if (in[0]->bytes != out[0]->bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    out[0]->data = in[0]->data;             /* view, no copy            */
    return ESP_OK;
}

/* ---- LAYERNORM over last dim, f32 accumulate --------------------------
 * in: x[..,C] (+ optional gamma[C], beta[C] — any dtype, dequanted).
 * params {float eps} default 1e-5. y = (x-mean)/sqrt(var+eps)*g + b.    */
static vq_err_t k_layernorm(vq_rtensor_t *const *in, int n_in,
                            vq_rtensor_t *const *out,
                            const uint8_t *p, uint32_t plen)
{
    const vq_rtensor_t *x = in[0];
    const vq_rtensor_t *g = (n_in > 1) ? in[1] : NULL;
    const vq_rtensor_t *b = (n_in > 2) ? in[2] : NULL;
    vq_rtensor_t *y = out[0];
    if (x->ndim == 0 || y->elems != x->elems) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t C = x->dims[x->ndim - 1];
    if (C == 0 || (g != NULL && g->elems < C) || (b != NULL && b->elems < C)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const float eps = param_f32(p, plen, 0, 1e-5f);
    const uint32_t rows = x->elems / C;

    for (uint32_t r = 0; r < rows; r++) {
        const uint32_t base = r * C;
        float mean = 0.0f;
        for (uint32_t c = 0; c < C; c++) {
            mean += vq_rd_f32(x, base + c);
        }
        mean /= (float)C;
        float var = 0.0f;
        for (uint32_t c = 0; c < C; c++) {
            const float d = vq_rd_f32(x, base + c) - mean;
            var += d * d;
        }
        var /= (float)C;
        const float inv = 1.0f / sqrtf(var + eps);
        for (uint32_t c = 0; c < C; c++) {
            const float gv = (g != NULL) ? vq_rd_f32(g, c) : 1.0f;
            const float bv = (b != NULL) ? vq_rd_f32(b, c) : 0.0f;
            const float v = (vq_rd_f32(x, base + c) - mean) * inv * gv + bv;
            vq_wr_f32(y, base + c, v);
        }
    }
    return ESP_OK;
}

/* ---- SOFTMAX over last dim ----------------------------------------------
 * dequant -> f32 softmax (max-subtracted) -> requant to out scale/zp.      */
static vq_err_t k_softmax(vq_rtensor_t *const *in, vq_rtensor_t *const *out)
{
    const vq_rtensor_t *x = in[0];
    vq_rtensor_t *y = out[0];
    if (x->ndim == 0 || y->elems != x->elems) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t C = x->dims[x->ndim - 1];
    const uint32_t rows = x->elems / (C ? C : 1u);
    if (C == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (uint32_t r = 0; r < rows; r++) {
        const uint32_t base = r * C;
        float mx = -INFINITY;
        for (uint32_t c = 0; c < C; c++) {
            const float v = vq_rd_f32(x, base + c);
            if (v > mx) {
                mx = v;
            }
        }
        float sum = 0.0f;
        /* two passes to keep the f32 buffer off the stack             */
        for (uint32_t c = 0; c < C; c++) {
            sum += expf(vq_rd_f32(x, base + c) - mx);
        }
        if (sum <= 0.0f) {
            sum = 1.0f;
        }
        for (uint32_t c = 0; c < C; c++) {
            const float p = expf(vq_rd_f32(x, base + c) - mx) / sum;
            vq_wr_f32(y, base + c, p);
        }
    }
    return ESP_OK;
}

/* ---- GRU -------------------------------------------------------------------
 * in: x[T,I] q8, w[3H,I+H] q8 (rows are z|r,n, first I cols = input
 * weights, last H cols = recurrent), b[6H] f32-or-q8 (b_ih then b_hh),
 * optional h0[H]. params {hidden} optional — H = w.dims[0]/3 is trusted
 * more. out: [H] (last hidden) or [T,H] (full sequence), decided by
 * out.elems.
 *
 * PyTorch gate convention:  z = sig(pre_z + rec_z)
 *                           r = sig(pre_r + rec_r)
 *                           n = tanh(pre_n + r * rec_n)
 *                           h = (1 - z)*n + z*h
 * State h stays f32 across timesteps (documented deviation: quantising
 * h per step costs ~1% extra error on a 1-2M-param class model and the
 * cell runs once per decision cycle, so f32 is the right trade-off).
 * Weight dequant is inline in the MAC — no dequant scratch needed.       */
static vq_err_t k_gru(vq_rtensor_t *const *in, int n_in,
                      vq_rtensor_t *const *out)
{
    const vq_rtensor_t *x = in[0];
    const vq_rtensor_t *w = in[1];
    const vq_rtensor_t *b = (n_in > 2) ? in[2] : NULL;
    const vq_rtensor_t *h0 = (n_in > 3) ? in[3] : NULL;
    vq_rtensor_t *y = out[0];
    if (x->dtype != VQ_DT_Q8 || w->dtype != VQ_DT_Q8 ||
        x->ndim != 2 || w->ndim != 2 || w->dims[0] % 3u != 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t T = x->dims[0], I = x->dims[1];
    const uint32_t H = w->dims[0] / 3u;
    if (H == 0 || w->dims[1] != I + H ||
        (b != NULL && b->elems < 6u * H) ||
        (h0 != NULL && h0->elems < H) ||
        (y->elems != H && y->elems != T * H)) {
        return ESP_ERR_INVALID_SIZE;
    }
    float *h  = malloc(H * sizeof(float));
    float *zn = malloc(2u * H * sizeof(float));   /* z then r slots    */
    float *nn = malloc(H * sizeof(float));
    float *rec = malloc(3u * H * sizeof(float));
    float *xr = malloc(I * sizeof(float));        /* dequanted x row   */
    if (h == NULL || zn == NULL || nn == NULL || rec == NULL || xr == NULL) {
        free(h); free(zn); free(nn); free(rec); free(xr);
        return ESP_ERR_NO_MEM;
    }
    for (uint32_t i = 0; i < H; i++) {
        h[i] = (h0 != NULL) ? vq_rd_f32(h0, i) : 0.0f;
    }
    const int8_t *wd = (const int8_t *)w->data;
    const float ws = w->scale;
    const int32_t wzp = w->zp;

    for (uint32_t t = 0; t < T; t++) {
        for (uint32_t i = 0; i < I; i++) {
            xr[i] = vq_rd_f32(x, t * I + i);      /* dequant row once  */
        }
        /* pre[g] = b_ih[g] + sum_i xw ; rec[g] = b_hh[g] + sum_i hw    */
        for (uint32_t g = 0; g < 3u * H; g++) {
            const int8_t *wr = wd + (size_t)g * (I + H);
            float pre = (b != NULL) ? vq_rd_f32(b, g) : 0.0f;
            for (uint32_t i = 0; i < I; i++) {
                pre += xr[i] * ((float)(int8_t)wr[i] - wzp) * ws;
            }
            float rc = (b != NULL) ? vq_rd_f32(b, 3u * H + g) : 0.0f;
            for (uint32_t i = 0; i < H; i++) {
                rc += h[i] * ((float)(int8_t)wr[I + i] - wzp) * ws;
            }
            if (g < H) {
                zn[g]      = pre;       /* z pre-act                   */
            } else if (g < 2u * H) {
                zn[H + g - H] = pre;    /* r pre-act                   */
            } else {
                nn[g - 2u * H] = pre;   /* n pre-act                   */
            }
            rec[g] = rc;
        }
        for (uint32_t i = 0; i < H; i++) {
            const float z = 1.0f / (1.0f + expf(-(zn[i]     + rec[i])));
            const float r = 1.0f / (1.0f + expf(-(zn[H + i] + rec[H + i])));
            const float n = tanhf(nn[i] + r * rec[2u * H + i]);
            h[i] = (1.0f - z) * n + z * h[i];
        }
        if (y->elems == T * H) {
            for (uint32_t i = 0; i < H; i++) {
                vq_wr_f32(y, t * H + i, h[i]);
            }
        }
    }
    if (y->elems == H) {
        for (uint32_t i = 0; i < H; i++) {
            vq_wr_f32(y, i, h[i]);
        }
    }
    free(h); free(zn); free(nn); free(rec); free(xr);
    return ESP_OK;
}

/* ---- QUANT / DEQUANT ----------------------------------------------------
 * Generic dtype bridge through the f32 accessors: QUANT produces the
 * out dtype from any in dtype (f32->q8 the common case; q8->q8 is a
 * rescale), DEQUANT writes f32 out. Both directions use the tensors'
 * own scale/zp.                                                            */
static vq_err_t k_quant(vq_rtensor_t *const *in, vq_rtensor_t *const *out)
{
    if (in[0]->elems != out[0]->elems) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (uint32_t i = 0; i < out[0]->elems; i++) {
        vq_wr_f32(out[0], i, vq_rd_f32(in[0], i));
    }
    return ESP_OK;
}

/* ---- dispatcher --------------------------------------------------------- */

const char *vq_op_name(int op)
{
    static const char *const names[VQ_OP_COUNT] = {
        "DENSE", "CONV1D", "DWCONV1D", "MAXPOOL1D", "AVGPOOL1D", "GAP",
        "RELU", "RELU6", "GELU", "SIGMOID", "TANH",
        "ADD", "MUL", "CONCAT", "RESHAPE", "LAYERNORM", "SOFTMAX",
        "GRU", "QUANT", "DEQUANT",
    };
    if (op < 0 || op >= VQ_OP_COUNT) {
        return "?";
    }
    return names[op];
}

vq_err_t vq_kern_exec(vq_op_t op,
                      vq_rtensor_t *const *ins, int n_in,
                      vq_rtensor_t *const *outs, int n_out,
                      const uint8_t *params, uint32_t param_len,
                      vq_lut8_t *lut)
{
    if (n_out < 1 || outs == NULL || outs[0] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (op) {
    case VQ_OP_DENSE:
        if (n_in < 2) return ESP_ERR_INVALID_ARG;
        return k_dense(ins, n_in, outs);
    case VQ_OP_CONV1D:
        if (n_in < 2) return ESP_ERR_INVALID_ARG;
        return k_conv1d(ins, n_in, outs, params, param_len);
    case VQ_OP_DWCONV1D:
        if (n_in < 2) return ESP_ERR_INVALID_ARG;
        return k_dwconv1d(ins, n_in, outs, params, param_len);
    case VQ_OP_MAXPOOL1D:
    case VQ_OP_AVGPOOL1D:
        if (n_in < 1) return ESP_ERR_INVALID_ARG;
        return k_pool1d(op, ins, outs, params, param_len);
    case VQ_OP_GAP:
        if (n_in < 1) return ESP_ERR_INVALID_ARG;
        return k_gap(ins, outs);
    case VQ_OP_RELU:
    case VQ_OP_RELU6:
        if (n_in < 1) return ESP_ERR_INVALID_ARG;
        return k_relu(op, ins, outs);
    case VQ_OP_GELU:
    case VQ_OP_SIGMOID:
    case VQ_OP_TANH:
        if (n_in < 1) return ESP_ERR_INVALID_ARG;
        return lut_run(op, ins[0], outs[0], lut);
    case VQ_OP_ADD:
    case VQ_OP_MUL:
        if (n_in < 2) return ESP_ERR_INVALID_ARG;
        return k_addmul(op, ins, outs);
    case VQ_OP_CONCAT:
        return k_concat(ins, n_in, outs, params, param_len);
    case VQ_OP_RESHAPE:
        if (n_in < 1) return ESP_ERR_INVALID_ARG;
        return k_reshape(ins, outs);
    case VQ_OP_LAYERNORM:
        if (n_in < 1) return ESP_ERR_INVALID_ARG;
        return k_layernorm(ins, n_in, outs, params, param_len);
    case VQ_OP_SOFTMAX:
        if (n_in < 1) return ESP_ERR_INVALID_ARG;
        return k_softmax(ins, outs);
    case VQ_OP_GRU:
        if (n_in < 2) return ESP_ERR_INVALID_ARG;
        return k_gru(ins, n_in, outs);
    case VQ_OP_QUANT:
    case VQ_OP_DEQUANT:
        if (n_in < 1) return ESP_ERR_INVALID_ARG;
        return k_quant(ins, outs);
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}
