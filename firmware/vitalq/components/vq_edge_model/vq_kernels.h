/* vq_kernels.h — internal contract between the VQMB graph executor
 * (vq_model.c) and the INT8 kernel set (vq_kernels.c). NOT public API —
 * the public contract is components/vq/include/vq_model.h and is FIXED.
 *
 * This header also pins down the on-disk VQMB pieces that vq_model.h
 * leaves unnamed (the node record and the per-op param blobs), plus the
 * tensor-layout conventions the kernels expect. Everything is
 * little-endian; the tensor record is vq_tensor_t verbatim
 * (sizeof(vq_tensor_t) == 60, natural C layout — records are memcpy'd,
 * never cast, so blob base alignment does not matter).
 *
 * Full blob layout (offsets from blob base):
 *   [0]                vqmb_header_t            (40 B, 10x u32)
 *   [40]               vq_tensor_t[n_tensors]   (60 B each)
 *   [..]               vqmb_node_t [n_nodes]    (52 B each)
 *   [..]               uint32_t in_idx [n_inputs]
 *   [..]               uint32_t out_idx[n_outputs]
 *   [fixed]            params blob              (params_bytes, see below)
 *   [align4(fixed+..)] weights blob             (weights_bytes)
 *
 * fixed = 40 + 60*n_tensors + 52*n_nodes + 4*(n_inputs+n_outputs).
 * params_bytes is IMPLICIT: = max(param_off+param_len) over all nodes
 * (0 when no node carries params); the params region is padded up to a
 * 4-byte boundary before weights start. crc32 covers [40, blob_len),
 * blob_len = weights_off + weights_bytes. The Python exporter
 * (tools/export_model.py) and stub_model_bin.h both emit this layout.
 *
 * Tensor shape conventions (channel-last, TFLite-flavoured):
 *   DENSE    x[..,N] * w[M,N](+i32 b[M])        -> [..,M]   (batched)
 *   CONV1D   x[L,Ci] * w[Co,k,Ci/g](+i32 b[Co]) -> [Lo,Co]
 *   DWCONV1D x[L,C]  * w[C,k]  (+i32 b[C])      -> [Lo,C]
 *   *POOL1D  x[L,C]                             -> [Lo,C]
 *   GAP      x[L,C]                             -> [C]
 *   CONCAT   axis param, all in dtypes equal    -> same ndim
 *   LAYERNORM x[..,C] (+gamma[C],beta[C])       -> [..,C]
 *   SOFTMAX  over last dim                      -> same shape
 *   GRU      x[T,I] w[3H,I+H] b[6H] (h0[H] opt) -> [H] or [T,H]
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "vq_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VQMB_VER         1u
#define VQ_MAX_TENSORS   256u
#define VQ_MAX_NODES     256u
#define VQ_MAX_IO        8u
#define VQ_ARENA_ALIGN   16u
#define VQ_ARENA_MAX     (6u * 1024u * 1024u)   /* sanity cap, 8MB PSRAM */

/* dtype ids (match vq_model.h comment: 0=q8, 1=i32, 2=f32) */
#define VQ_DT_Q8         0u
#define VQ_DT_I32        1u
#define VQ_DT_F32        2u

/* Node record — 52 B, all-u32, no padding. param_off indexes the
 * params blob (between the index lists and the aligned weights blob). */
typedef struct {
    uint32_t op;          /* vq_op_t                            */
    uint32_t n_in;        /* <= 4                               */
    uint32_t in[4];
    uint32_t n_out;       /* <= 4                               */
    uint32_t out[4];
    uint32_t param_off, param_len;
} vqmb_node_t;

/* Per-op param blobs (read via memcpy — alignment not guaranteed).   */
typedef struct { int32_t k, stride, pad, dil, groups; } vq_p_conv_t;    /* CONV1D    */
typedef struct { int32_t k, stride, pad, dil; }        vq_p_dwconv_t;   /* DWCONV1D  */
typedef struct { int32_t k, stride, pad; }             vq_p_pool_t;     /* MAX/AVG   */
typedef struct { int32_t axis; }                       vq_p_axis_t;     /* CONCAT    */
typedef struct { float   eps; }                        vq_p_ln_t;       /* LAYERNORM */
typedef struct { int32_t hidden; }                     vq_p_gru_t;      /* GRU (opt) */

/* Runtime tensor: disk form + resolved data pointer. data points into
 * the PSRAM arena for activations (size==0 on disk) or into the
 * mmap'd/blob weights region for constants (size>0 on disk).          */
typedef struct {
    uint8_t  dtype;      /* VQ_DT_*                              */
    uint8_t  ndim;       /* 0..4 (0 = scalar, elems 1)           */
    uint16_t dims[4];
    float    scale;      /* q8 quant scale; 0 → kernel default   */
    int32_t  zp;
    uint8_t *data;
    uint32_t elems;      /* product(dims), 1 for scalar           */
    uint32_t bytes;      /* elems * esize                         */
    uint8_t  is_const;   /* data lives in weights blob            */
    char     name[32];
} vq_rtensor_t;

/* Per-node lazy LUT for GELU/SIGMOID/TANH: 256 output bytes indexed by
 * the raw input byte, built once on first run for that node's
 * (in-scale,zp → out-scale,zp) pair.                                   */
typedef struct {
    int8_t  q[256];
    uint8_t ready;
} vq_lut8_t;

/* ---- shared helpers ------------------------------------------------ */
int      vq_esize(uint8_t dtype);              /* 1/4/4                  */
uint32_t vq_elems_of(uint8_t ndim, const uint16_t *dims);

/* Requantisation: ratio -> (mult Q31, shift) once per node, then
 * mulhi+round per element — the standard TFLite-style fixed-point path.
 *   vq_fp_ratio(r)   : decompose real multiplier r = m*2^e, m∈[0.5,1)
 *                      into mult = round(m*2^31), shift = 31-e
 *   vq_mulhi(acc,M,s): round( acc*M * 2^-s ) with half-up rounding,
 *                      clamped to int32
 *   vq_requant8(...) : vq_mulhi + zp, clamped to int8                  */
void    vq_fp_ratio(float ratio, int32_t *mult, int *shift);
int32_t vq_mulhi(int32_t acc, int32_t mult, int shift);
int8_t  vq_requant8(int32_t acc, int32_t mult, int shift, int32_t zp);

/* Generic element access — handles q8/i32/f32 for the slow paths
 * (broadcast ADD/MUL, CONCAT with mixed scales, QUANT/DEQUANT, GRU).
 * i32 tensors read as (float)v * scale (scale 0 → raw v); f32 raw.    */
float   vq_rd_f32(const vq_rtensor_t *t, uint32_t i);
void    vq_wr_f32(vq_rtensor_t *t, uint32_t i, float v);

const char *vq_op_name(int op);                /* for the selftest report */

/* CRC32 (IEEE 802.3, reflected, poly 0xEDB88320 — same as zlib's).
 * Shared by the loader's blob check and the stub builder.              */
uint32_t vq_crc32(uint32_t crc, const void *data, size_t len);

/* Execute one node. ins/outs are resolved runtime tensors (already
 * bounds-checked by the executor). lut is non-NULL only for nodes the
 * loader flagged as LUT-backed (GELU/SIGMOID/TANH). params may be NULL
 * when param_len == 0.                                                 */
vq_err_t vq_kern_exec(vq_op_t op,
                      vq_rtensor_t *const *ins, int n_in,
                      vq_rtensor_t *const *outs, int n_out,
                      const uint8_t *params, uint32_t param_len,
                      vq_lut8_t *lut);

#ifdef __cplusplus
}
#endif
