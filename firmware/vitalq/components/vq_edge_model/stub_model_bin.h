/* stub_model_bin.h — compiled-in fallback model for vq_model_load().
 *
 * Stand-in for `tools/export_model.py stub --c-header` output: that tool
 * emits a `static const uint8_t vq_stub_blob[]` byte array. The exporter
 * doesn't exist yet, so instead of a frozen byte array this header
 * builds the SAME VQMB blob at first call into a static buffer, using
 * the exact on-disk struct layouts (vqmb_header_t / vq_tensor_t /
 * vqmb_node_t) — identical result, zero risk of a stale array, and the
 * loader exercises the same parse path as a real partition blob.
 *
 * Model: trivial MLP head over the 40-float feature vector
 *   features[40] -DENSE(w1[24,40],b1)-> h1[24] -RELU-> h1r[24]
 *                -DENSE(w2[24,24],b2)-> h2[24] -RELU-> h2r[24]
 *                -DENSE(w3[2,24],b3)-> logits[2] -SOFTMAX-> out[2]
 * out = {prob_normal, prob_problematic} as q8 (scale 1/255, zp -128).
 * Weights are a deterministic LCG pattern — enough to drive every
 * pipeline stage end-to-end, NOT a trained model.
 */
#pragma once
#include <string.h>
#include "vq_kernels.h"      /* vqmb_node_t, VQMB_VER, param structs    */

/* ---- stub geometry --------------------------------------------------- */
#define STUB_NT        13u                     /* tensor count          */
#define STUB_NN        6u                      /* node count            */
#define STUB_ARENA     1024u                   /* header hint           */

/* weights-blob offsets (4-aligned), bytes:
 * w1 24*40 i8 @0, b1 24 i32 @960, w2 24*24 @1056, b2 @1632,
 * w3 2*24 @1728, b3 2 i32 @1776                                        */
#define STUB_W1_OFF    0u
#define STUB_B1_OFF    960u
#define STUB_W2_OFF    1056u
#define STUB_B2_OFF    1632u
#define STUB_W3_OFF    1728u
#define STUB_B3_OFF    1776u
#define STUB_WBYTES    1784u                   /* end of b3            */

#define STUB_FIXED     (sizeof(vqmb_header_t) + STUB_NT * sizeof(vq_tensor_t) + \
                        STUB_NN * sizeof(vqmb_node_t) + 2u * 4u)
/* params blob empty -> weights at align4(STUB_FIXED) = STUB_FIXED      */
#define STUB_LEN       (STUB_FIXED + STUB_WBYTES)

static const uint8_t *vq_stub_model(size_t *len)
{
    static uint8_t blob[STUB_LEN];             /* built once, .bss      */
    static int     built;

    if (built) {
        *len = STUB_LEN;
        return blob;
    }

    /* tensor ids */
    enum { T_FEAT, T_W1, T_B1, T_H1, T_H1R, T_W2, T_B2, T_H2, T_H2R,
           T_W3, T_B3, T_LOGITS, T_OUT };

    vqmb_header_t hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.magic         = VQMB_MAGIC;
    hdr.ver           = VQMB_VER;
    hdr.flags         = 0;
    hdr.n_tensors     = STUB_NT;
    hdr.n_nodes       = STUB_NN;
    hdr.n_inputs      = 1;
    hdr.n_outputs     = 1;
    hdr.arena_bytes   = STUB_ARENA;
    hdr.weights_bytes = STUB_WBYTES;
    hdr.crc32         = 0;                     /* patched below         */
    memcpy(blob, &hdr, sizeof hdr);

    vq_tensor_t t[STUB_NT];
    memset(t, 0, sizeof t);                    /* deterministic padding */
#define STUB_T(i, nm, dt, nd, d0, d1, sc, zp_, off, sz)            \
    do {                                                         \
        vq_tensor_t *tp = &t[i];                                 \
        strncpy(tp->name, nm, sizeof(tp->name) - 1);             \
        tp->dtype = (dt); tp->ndim = (nd);                       \
        tp->dims[0] = (d0); tp->dims[1] = (d1);                  \
        tp->scale = (sc); tp->zp = (zp_);                        \
        tp->offset = (off); tp->size = (sz);                     \
    } while (0)

    /* Scale ladder tuned so z-scored inputs (|x|<~4) land mid-quant and
     * saturated inputs reach the rails without degenerate all-zero
     * activations: LCG weights span [-8,7]*0.02 = ±0.16 real.         */
    STUB_T(T_FEAT,   "features", VQ_DT_Q8,  1, 40, 0, 0.05f,    0,   0,        0);
    STUB_T(T_W1,     "w1",       VQ_DT_Q8,  2, 24, 40, 0.02f,   0,   STUB_W1_OFF, 24u * 40u);
    STUB_T(T_B1,     "b1",       VQ_DT_I32, 1, 24, 0, 0.001f,   0,   STUB_B1_OFF, 24u * 4u);
    STUB_T(T_H1,     "h1",       VQ_DT_Q8,  1, 24, 0, 0.01f,    0,   0,        0);
    STUB_T(T_H1R,    "h1r",      VQ_DT_Q8,  1, 24, 0, 0.01f,    0,   0,        0);
    STUB_T(T_W2,     "w2",       VQ_DT_Q8,  2, 24, 24, 0.02f,   0,   STUB_W2_OFF, 24u * 24u);
    STUB_T(T_B2,     "b2",       VQ_DT_I32, 1, 24, 0, 2.0e-4f,  0,   STUB_B2_OFF, 24u * 4u);
    STUB_T(T_H2,     "h2",       VQ_DT_Q8,  1, 24, 0, 0.02f,    0,   0,        0);
    STUB_T(T_H2R,    "h2r",      VQ_DT_Q8,  1, 24, 0, 0.02f,    0,   0,        0);
    STUB_T(T_W3,     "w3",       VQ_DT_Q8,  2, 2,  24, 0.02f,   0,   STUB_W3_OFF, 2u * 24u);
    STUB_T(T_B3,     "b3",       VQ_DT_I32, 1, 2,  0, 4.0e-4f,  0,   STUB_B3_OFF, 2u * 4u);
    STUB_T(T_LOGITS, "logits",   VQ_DT_Q8,  1, 2,  0, 0.05f,    0,   0,        0);
    STUB_T(T_OUT,    "out",      VQ_DT_Q8,  1, 2,  0, 1.0f / 255.0f, -128, 0,  0);
#undef STUB_T
    memcpy(blob + sizeof(vqmb_header_t), t, sizeof t);

    vqmb_node_t n[STUB_NN];
    memset(n, 0, sizeof n);
#define STUB_N(i, op_, nin, i0, i1, i2, o0)                        \
    do {                                                         \
        vqmb_node_t *np = &n[i];                                 \
        np->op = (op_); np->n_in = (nin);                        \
        np->in[0] = (i0); np->in[1] = (i1); np->in[2] = (i2);    \
        np->n_out = 1; np->out[0] = (o0);                        \
    } while (0)

    STUB_N(0, VQ_OP_DENSE,   3, T_FEAT, T_W1, T_B1, T_H1);
    STUB_N(1, VQ_OP_RELU,    1, T_H1,   0,    0,    T_H1R);
    STUB_N(2, VQ_OP_DENSE,   3, T_H1R,  T_W2, T_B2, T_H2);
    STUB_N(3, VQ_OP_RELU,    1, T_H2,   0,    0,    T_H2R);
    STUB_N(4, VQ_OP_DENSE,   3, T_H2R,  T_W3, T_B3, T_LOGITS);
    STUB_N(5, VQ_OP_SOFTMAX, 1, T_LOGITS, 0,  0,    T_OUT);
#undef STUB_N
    memcpy(blob + sizeof(vqmb_header_t) + sizeof t, n, sizeof n);

    /* index lists right after the node table                        */
    {
        const size_t idxoff = sizeof(vqmb_header_t) + sizeof t + sizeof n;
        const uint32_t idx[2] = { T_FEAT, T_OUT };   /* {in, out}     */
        memcpy(blob + idxoff, idx, sizeof idx);
    }

    /* weights blob at STUB_FIXED (params region is empty).
     * Deterministic LCG pattern: wq in [-8,7] keeps activations in
     * range so the zero-input golden run lands mid-scale.             */
    {
        uint8_t *wd = blob + STUB_FIXED;
        uint32_t st = 0xC0FFEEu;
        for (uint32_t i = 0; i < 24u * 40u + 24u * 24u + 2u * 24u; i++) {
            /* w1 then w2 then w3 are contiguous in the same order     */
            st = st * 1664525u + 1013904223u;
            const int8_t wq = (int8_t)(((st >> 13) & 0xFu) - 8);
            uint32_t off;
            if (i < 24u * 40u)                       off = STUB_W1_OFF + i;
            else if (i < 24u * 40u + 24u * 24u)      off = STUB_W2_OFF + i - 24u * 40u;
            else                                     off = STUB_W3_OFF + i - 24u * 40u - 24u * 24u;
            wd[off] = (uint8_t)wq;
        }
        for (uint32_t i = 0; i < 24u; i++) {         /* i32 biases      */
            const int32_t bv = ((int32_t)(i * 7u) % 9 - 4) * 8;
            memcpy(wd + STUB_B1_OFF + i * 4u, &bv, 4);
            memcpy(wd + STUB_B2_OFF + i * 4u, &bv, 4);
        }
        const int32_t b3v[2] = { 8, -8 };
        memcpy(wd + STUB_B3_OFF, b3v, sizeof b3v);
    }

    /* crc32 over everything after the header                        */
    const uint32_t crc = vq_crc32(0, blob + sizeof(vqmb_header_t),
                                  STUB_LEN - sizeof(vqmb_header_t));
    memcpy(blob + offsetof(vqmb_header_t, crc32), &crc, 4);

    built = 1;
    *len = STUB_LEN;
    return blob;
}
