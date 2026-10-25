/* vq_model.h — on-device INT8 inference runtime (1-2 M params).
 *
 * Model lives in the dedicated 'model' flash partition (4 MB) as a VQMB
 * blob; weights are mmap'd from flash (zero-copy), the activation arena
 * lives in the 8 MB octal PSRAM. Custom architecture TBD → the runtime
 * is a small graph executor over fixed operators, so ANY architecture
 * that compiles to the op set runs unmodified.
 *
 * VQMB layout (little-endian):
 *   header { magic 'VQMB', ver, flags, n_tensors, n_nodes, n_inputs,
 *            n_outputs, arena_bytes, weights_bytes, crc32 }
 *   tensor[n_tensors] { name[32], dtype, ndim, dims[4], scale, zp,
 *                       offset, size }    dtype: 0=q8,1=i32,2=f32
 *   node[n_nodes]     { op, n_in, in[4], n_out, out[4],
 *                       param_off, param_len }
 *   params blob, weights blob (int8)
 *
 * Op set is deliberately tiny but sufficient for CNN/MLP/tiny-recurrent
 * custom heads: see vq_op_t. The Python exporter (tools/export_model.py)
 * lowers a JSON/ONNX graph into this blob.
 */
#pragma once
#include "vq_types.h"

typedef enum {
    VQ_OP_DENSE = 0,       /* per-tensor int8 GEMM + bias i32 → requant */
    VQ_OP_CONV1D,          /* int8, params: stride,pad,dil,groups       */
    VQ_OP_DWCONV1D,        /* depthwise int8                            */
    VQ_OP_MAXPOOL1D,
    VQ_OP_AVGPOOL1D,
    VQ_OP_GAP,             /* global average pool → [C]                 */
    VQ_OP_RELU, VQ_OP_RELU6, VQ_OP_GELU, VQ_OP_SIGMOID, VQ_OP_TANH,
    VQ_OP_ADD, VQ_OP_MUL,  /* broadcast, requant                        */
    VQ_OP_CONCAT,
    VQ_OP_RESHAPE,
    VQ_OP_LAYERNORM,       /* int16 accumulate                          */
    VQ_OP_SOFTMAX,
    VQ_OP_GRU,             /* int8 cell, seq-major; for tiny recurrent  */
    VQ_OP_QUANT, VQ_OP_DEQUANT,
    VQ_OP_COUNT
} vq_op_t;

typedef struct {
    char     name[32];
    uint8_t  dtype;        /* 0=q8 int8, 1=i32, 2=f32                   */
    uint8_t  ndim;         /* <=4                                       */
    uint16_t dims[4];
    float    scale;        /* quant scale (q8)                          */
    int32_t  zp;           /* zero point                                */
    uint32_t offset, size; /* into weights blob; activations if size=0  */
} vq_tensor_t;             /* on-disk form; runtime wraps with ptrs      */

typedef struct {
    uint32_t magic, ver, flags;
    uint32_t n_tensors, n_nodes, n_inputs, n_outputs;
    uint32_t arena_bytes, weights_bytes, crc32;
} vqmb_header_t;
#define VQMB_MAGIC 0x424D5156  /* 'VQMB' */

typedef struct vq_model vq_model_t;

/* Load from 'model' partition (mmap). Validates header+crc, builds the
 * arena in PSRAM. If absent/corrupt → returns ESP_ERR_NOT_FOUND and the
 * decision layer falls back to the compiled-in stub model.             */
vq_err_t  vq_model_load(vq_model_t **out);
/* Load a compiled-in blob (built-in stub so pipeline is end-to-end
 * before the real architecture lands).                                 */
vq_err_t  vq_model_load_blob(vq_model_t **out, const void *blob, size_t len);
void      vq_model_free(vq_model_t *m);

size_t    vq_model_n_inputs(const vq_model_t *m);
size_t    vq_model_n_outputs(const vq_model_t *m);
/* Input slots by name: "features" (float32[40]), optional "ppg_win" …  */
int       vq_model_input_index(const vq_model_t *m, const char *name);
vq_err_t  vq_model_set_input_f32(vq_model_t *m, int idx, const float *data, size_t n);

/* Run inference. Returns inference latency. Output 0 convention:
 * float[2] {prob_normal, prob_problematic} after softmax.              */
vq_err_t  vq_model_run(vq_model_t *m, int64_t *latency_us);
vq_err_t  vq_model_get_output_f32(vq_model_t *m, int idx, float *data, size_t n);

/* Diagnostics: param count, arena bytes used, per-op cycles (selftest). */
uint32_t  vq_model_param_count(const vq_model_t *m);
uint32_t  vq_model_arena_used(const vq_model_t *m);
void      vq_model_selftest_report(const vq_model_t *m, char *buf, size_t len);
