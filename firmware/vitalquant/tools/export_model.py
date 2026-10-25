#!/usr/bin/env python3
"""export_model.py -- VQMB model tooling for vitalquant firmware v2.

VQMB is the on-flash model blob consumed by vq_model_load() /
vq_model_load_blob().  The format is defined by
components/vq/include/vq_model.h; this tool is the producer side.
All multi-byte fields are little-endian.  Stdlib only -- no numpy,
no onnx, no pip deps.

BLOB LAYOUT
-----------
  [0]   vqmb_header_t, 40 B, 10 x u32:
          magic         = 0x424D5156 ('VQMB')
          ver           = 1
          flags         = 0 (reserved; exporter always writes 0)
          n_tensors, n_nodes, n_inputs, n_outputs
          arena_bytes   = activation-arena estimate: peak live set of
                          activation tensors, each buffer 16 B aligned.
                          PSRAM budget hint for the runtime; the loader
                          may clamp it.
          weights_bytes = size of the weights blob
          crc32         = zlib.crc32() of *everything after this field*,
                          i.e. bytes [40, EOF) = tensor table + node
                          table + params blob + weights blob.
                          (CRC-32/ISO-HDLC; the runtime must apply the
                          same pre/post conditioning as zlib.crc32.)

  [40]  tensor table, n_tensors x 60 B.  On-disk vq_tensor_t is the
        NATURAL C layout of the struct in vq_model.h (so the loader can
        memcpy it straight out of the mmap'd partition):
            +0  char name[32]   NUL-padded ASCII, <=31 chars
            +32 u8   dtype      0=q8(int8), 1=i32, 2=f32
            +33 u8   ndim       1..4
            +34 u16  dims[4]    unused slots = 0
            +42 --   pad[2]     (natural alignment of f32; written 0)
            +44 f32  scale      quant scale (q8 tensors)
            +48 i32  zp         zero point
            +52 u32  offset     byte offset into the weights blob
            +56 u32  size       payload bytes; 0 => activation tensor
        ORDERING CONVENTION -- the header carries counts only, so the
        loader identifies model I/O by position:
            tensor[0 .. n_inputs)                  = graph inputs
            tensor[n_inputs .. n_inputs+n_outputs) = graph outputs
            tensor[n_inputs+n_outputs ..)          = weights + activations

  ...   node table, n_nodes x 52 B.  Layout matches vqmb_node_t in
        components/vq_edge_model/vq_kernels.h — all u32 fields:
            +0  u32  op          vq_op_t value (see OPS below)
            +4  u32  n_in        1..4
            +8  u32  in[4]       tensor indices; unused slots = 0xFFFFFFFF
            +24 u32  n_out       1..4
            +28 u32  out[4]      tensor indices; unused slots = 0xFFFFFFFF
            +44 u32  param_off   byte offset into the params blob
            +48 u32  param_len   byte length (binary op-params; may be 0)

  ...   u32 in_idx [n_inputs]   graph input tensor indices.  ORDERING
        CONVENTION: inputs are tensors[0..n_inputs), so this is
        [0..n_inputs); then u32 out_idx [n_outputs] = the following
        n_outputs tensor indices.

  ...   params blob   per-node binary parameter records concatenated
                      back to back (param_off/param_len slice out one
                      node's record).  Encoding is a flat scalar array,
                      NOT JSON — the runtime reads them via param_i32 /
                      param_f32 element lookups:
            CONV1D      i32[5]  {k, stride, pad, dil, groups}
            DWCONV1D    i32[4]  {k, stride, pad, dil}
            MAXPOOL1D / AVGPOOL1D  i32[3]  {k, stride, pad}
            CONCAT      i32[1]  {axis}
            LAYERNORM   f32[1]  {eps}
            all other ops        empty (runtime uses defaults)

  ...   weights blob  raw little-endian payloads back to back, each
                      payload starting 4-byte aligned (zero-padded).
                      A tensor's offset/size address this blob.
                      q8 = int8, i32 = int32 LE, f32 = float32 LE.

EXECUTION MODEL: nodes run in node-table order (the JSON must list them
in topological order).  Activations live in the PSRAM arena; weights
stay mmap'd in flash.  All compute is INT8 unless a tensor's dtype
says otherwise.

JSON GRAPH SCHEMA (export --graph)
----------------------------------
{
  "name":    "net_name",                          // optional
  "inputs":  [ {"name":"features","shape":[40],
                "dtype":"q8","scale":0.02,"zp":0}, ... ],
  "outputs": [ ...same fields... ],
  "tensors": [                                    // weights + activations
    {"name":"fc1_w","dtype":"q8","dims":[24,40],
     "scale":0.021,"zp":0,"weights_file":"fc1_w.bin"},
    {"name":"fc1_b","dtype":"i32","dims":[24],
     "weights_inline":[3,-1, ... ]},              // prod(dims) values
    {"name":"h1","dtype":"q8","dims":[24]}        // no weights -> activation
  ],
  "nodes": [
    {"op":"DENSE","inputs":["features","fc1_w","fc1_b"],
     "outputs":["h1"],"params":{}}, ...
  ]
}
  * "shape" and "dims" are accepted synonyms everywhere.
  * "dtype" on inputs/outputs is optional, default "q8".  Give f32
    inputs/outputs only with QUANT/DEQUANT nodes to bridge to the
    int8 interior (vq_model_set_input_f32 writes through the declared
    dtype + scale/zp).
  * "scale"/"zp" optional, defaults 1.0 / 0.
  * weights_file resolves inside --weights (default: the graph file's
    directory).  *.bin = raw little-endian payload, *.json = JSON
    number array (identical to weights_inline).  Exactly one of
    weights_file / weights_inline per weight tensor.
  * Every tensor name a node references must be declared in
    inputs/outputs/tensors; intermediates go in tensors[] with no
    weights.  Node inputs may also name weight tensors.

OP TABLE (value = vq_op_t; params -> params blob)
-------------------------------------------------
  DENSE       in:[x,w(,b)] out:[y]   w q8 dims [out,in], b i32 [out],
              x,y q8; y = requant(x @ w^T + b).  Bias may be omitted.
  CONV1D      in:[x,w,b]   params:{stride,pad,dil,groups}   x=[C,L]
  DWCONV1D    in:[x,w,b]   params:{stride,pad,dil}
  MAXPOOL1D / AVGPOOL1D    params:{kernel,stride,pad}
  GAP         in:[x[C,L]]  out:[y[C]]
  RELU RELU6 GELU SIGMOID TANH   unary, dims preserved
  ADD MUL     in:[a,b] broadcast, requant
  CONCAT      in:[2..4]    params:{axis}
  RESHAPE     in:[x] numel preserved; params:{shape} optional
  LAYERNORM   in:[x,gamma,beta] params:{eps}
  SOFTMAX     in:[x]       params:{axis} (default -1)
  GRU         in:[x,h0(,w,b...)] params:{hidden,...} seq-major
  QUANT / DEQUANT  in:[x] out:[y]  q8<->f32 via tensor scale/zp

LIMITS ENFORCED: blob <= 4 MiB ('model' partition), ndim <= 4,
dims <= 65535, name <= 31 ASCII chars, <= 65535 tensors, <= 4
inputs/outputs per node, weight payload == prod(dims)*elem_size.

SUBCOMMANDS
-----------
  export  JSON graph + weight blobs -> vqmodel.bin (+ optional C header)
  stub    deterministic 40->24->24->2 softmax MLP for pipeline
          bring-up; emits stub_model.bin + stub_model_bin.h
  info    parse a blob: header/tensor/node dump, crc check,
          param count, arena estimate
  flash   print the esptool/parttool command for the 'model'
          partition (offset 0x820000 per partitions.csv)
"""
import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

# ---------------------------------------------------------------- format
VQMB_MAGIC = 0x424D5156          # 'VQMB' (vq_model.h)
VQMB_VER = 1
VQMB_MAX_BLOB = 4 * 1024 * 1024  # 'model' partition size, partitions.csv
MODEL_PART_OFFSET = 0x820000     # 'model' partition offset
MODEL_PART_SIZE = 0x400000

HEADER_FMT = "<10I"              # 10 x u32
HEADER_SIZE = struct.calcsize(HEADER_FMT)            # 40
TENSOR_FMT = "<32sBB4H2xfiII"    # natural-layout vq_tensor_t
TENSOR_SIZE = struct.calcsize(TENSOR_FMT)            # 60
NODE_FMT = "<13I"                # vqmb_node_t: 13 x u32 (52 B)
NODE_SIZE = struct.calcsize(NODE_FMT)                # 52
UNUSED_IDX = 0xFFFFFFFF          # unused node in/out slot sentinel

DTYPES = {"q8": 0, "i32": 1, "f32": 2}
DTYPE_NAME = {v: k for k, v in DTYPES.items()}
DTYPE_ELEMSZ = {0: 1, 1: 4, 2: 4}

# Index order MUST match vq_op_t in vq_model.h.
OPS = ["DENSE", "CONV1D", "DWCONV1D", "MAXPOOL1D", "AVGPOOL1D", "GAP",
       "RELU", "RELU6", "GELU", "SIGMOID", "TANH", "ADD", "MUL",
       "CONCAT", "RESHAPE", "LAYERNORM", "SOFTMAX", "GRU",
       "QUANT", "DEQUANT"]
OP_ID = {name: i for i, name in enumerate(OPS)}

# op -> (min_in, max_in, min_out, max_out)
ARITY = {
    "DENSE": (2, 3, 1, 1), "CONV1D": (3, 3, 1, 1), "DWCONV1D": (3, 3, 1, 1),
    "MAXPOOL1D": (1, 1, 1, 1), "AVGPOOL1D": (1, 1, 1, 1), "GAP": (1, 1, 1, 1),
    "RELU": (1, 1, 1, 1), "RELU6": (1, 1, 1, 1), "GELU": (1, 1, 1, 1),
    "SIGMOID": (1, 1, 1, 1), "TANH": (1, 1, 1, 1),
    "ADD": (2, 2, 1, 1), "MUL": (2, 2, 1, 1), "CONCAT": (2, 4, 1, 1),
    "RESHAPE": (1, 1, 1, 1), "LAYERNORM": (3, 3, 1, 1),
    "SOFTMAX": (1, 1, 1, 1), "GRU": (2, 4, 1, 2),
    "QUANT": (1, 1, 1, 1), "DEQUANT": (1, 1, 1, 1),
}

MAX_NAME = 31        # fits char name[32] with NUL
MAX_DIM = 65535      # dims are u16
MAX_TENSORS = 65535  # node in/out indices are u16
MAX_IO = 4           # in[4]/out[4]


class ModelError(Exception):
    """User-facing error: malformed graph/blob or bad input files."""


def _numel(dims):
    n = 1
    for d in dims:
        n *= d
    return n


def _align(n, a):
    return (n + a - 1) // a * a


def _flatten(v):
    out = []
    if isinstance(v, (list, tuple)):
        for x in v:
            out += _flatten(x)
    else:
        out.append(v)
    return out


# ------------------------------------------------------------- graph norm
def _norm_dims(decl, ctx):
    dims = decl.get("dims", decl.get("shape"))
    if not isinstance(dims, list) or not (1 <= len(dims) <= 4):
        raise ModelError("%s: 'dims'/'shape' must be a list of 1..4 ints" % ctx)
    out = []
    for d in dims:
        if not isinstance(d, int) or isinstance(d, bool) or not (1 <= d <= MAX_DIM):
            raise ModelError("%s: bad dim %r (int 1..%d required)" % (ctx, d, MAX_DIM))
        out.append(d)
    return out


def _norm_name(decl, ctx):
    name = decl.get("name")
    if not isinstance(name, str) or not name:
        raise ModelError("%s: missing 'name'" % ctx)
    try:
        name.encode("ascii")
    except UnicodeEncodeError:
        raise ModelError("%s: name %r must be ASCII" % (ctx, name))
    if len(name) > MAX_NAME:
        raise ModelError("%s: name %r too long (max %d chars)" % (ctx, name, MAX_NAME))
    return name


def _norm_dtype(decl, ctx, default):
    dt = decl.get("dtype", default)
    if not isinstance(dt, str) or dt.lower() not in DTYPES:
        raise ModelError("%s: bad dtype %r (one of %s)"
                         % (ctx, dt, "/".join(DTYPES)))
    return dt.lower()


def _weight_payload(decl, td, weights_dir, ctx):
    """Return raw little-endian payload bytes for a weight decl."""
    dims, dt = td["dims"], td["dtype_name"]
    n, esz = _numel(dims), DTYPE_ELEMSZ[td["dtype"]]
    wfile, winline = decl.get("weights_file"), decl.get("weights_inline")
    if wfile is not None and winline is not None:
        raise ModelError("%s: give only one of weights_file/weights_inline" % ctx)
    if wfile is not None:
        p = (weights_dir / wfile) if weights_dir else Path(wfile)
        try:
            raw = p.read_bytes()
        except OSError as e:
            raise ModelError("%s: cannot read weights_file %s: %s" % (ctx, p, e))
        if str(wfile).lower().endswith(".json"):
            try:
                vals = _flatten(json.loads(raw.decode("utf-8")))
            except (ValueError, UnicodeDecodeError) as e:
                raise ModelError("%s: bad JSON in %s: %s" % (ctx, p, e))
            return _pack_vals(vals, dt, n, "%s weights_file %s" % (ctx, p))
        if len(raw) != n * esz:
            raise ModelError("%s: %s has %d bytes, expected %d "
                             "(%d elems x %d B)" % (ctx, p, len(raw), n * esz, n, esz))
        return raw
    if winline is not None:
        return _pack_vals(_flatten(winline), dt, n, "%s weights_inline" % ctx)
    return None


def _pack_vals(vals, dtype_name, n, ctx):
    if len(vals) != n:
        raise ModelError("%s: %d values, expected %d" % (ctx, len(vals), n))
    if dtype_name == "q8":
        for v in vals:
            if not isinstance(v, int) or isinstance(v, bool) or not (-128 <= v <= 127):
                raise ModelError("%s: q8 value %r not an int in [-128,127]" % (ctx, v))
        return struct.pack("<%db" % n, *vals)
    if dtype_name == "i32":
        for v in vals:
            if not isinstance(v, int) or isinstance(v, bool) \
                    or not (-0x80000000 <= v <= 0x7FFFFFFF):
                raise ModelError("%s: i32 value %r out of range" % (ctx, v))
        return struct.pack("<%di" % n, *vals)
    # f32
    try:
        fvals = [float(v) for v in vals]
    except (TypeError, ValueError):
        raise ModelError("%s: f32 values must be numbers" % ctx)
    return struct.pack("<%df" % n, *fvals)


def normalize_graph(g, weights_dir):
    """Validate the parsed JSON graph; return (tensors, nodes, warnings).

    tensors: [{name,dtype,dtype_name,ndim,dims,scale,zp,payload|None}]
        ordered inputs ++ outputs ++ tensors[]
    nodes:   [{op_name,op,in_idx[],out_idx[],params(dict)}]
    """
    if not isinstance(g, dict):
        raise ModelError("graph root must be a JSON object")
    for key in ("inputs", "outputs", "tensors", "nodes"):
        if key in g and not isinstance(g[key], list):
            raise ModelError("'%s' must be a list" % key)
    nodes_json = g.get("nodes", [])
    if not nodes_json:
        raise ModelError("graph has no nodes")

    decls = []   # (kind, decl)  kind in input/output/tensor
    for kind, key in (("input", "inputs"), ("output", "outputs"),
                      ("tensor", "tensors")):
        for i, d in enumerate(g.get(key, [])):
            if not isinstance(d, dict):
                raise ModelError("%s[%d] must be an object" % (key, i))
            decls.append((kind, d, "%s[%d]" % (key, i)))

    if not decls:
        raise ModelError("graph declares no tensors")

    tensors, name_idx = [], {}
    for kind, d, ctx in decls:
        name = _norm_name(d, ctx)
        if name in name_idx:
            raise ModelError("%s: duplicate tensor name %r" % (ctx, name))
        td = {
            "name": name,
            "dtype_name": _norm_dtype(d, ctx, "q8"),
            "dims": _norm_dims(d, ctx),
            "scale": float(d.get("scale", 1.0)),
            "zp": int(d.get("zp", 0)),
            "kind": kind,
        }
        td["dtype"] = DTYPES[td["dtype_name"]]
        td["ndim"] = len(td["dims"])
        td["payload"] = _weight_payload(d, td, weights_dir, ctx)
        if td["kind"] in ("input", "output") and td["payload"] is not None:
            raise ModelError("%s: graph %s %r cannot carry weights"
                             % (ctx, kind, name))
        td["is_weight"] = td["payload"] is not None
        name_idx[name] = len(tensors)
        tensors.append(td)

    n_inputs = sum(1 for k, _, _ in decls if k == "input")
    n_outputs = sum(1 for k, _, _ in decls if k == "output")
    if n_inputs == 0 or n_outputs == 0:
        raise ModelError("graph needs at least one input and one output")
    if len(tensors) > MAX_TENSORS:
        raise ModelError("%d tensors exceeds limit %d" % (len(tensors), MAX_TENSORS))

    nodes, producer, warns = [], {}, []
    for ni, nd in enumerate(nodes_json):
        ctx = "nodes[%d]" % ni
        if not isinstance(nd, dict):
            raise ModelError("%s must be an object" % ctx)
        op_name = nd.get("op")
        if not isinstance(op_name, str) or op_name.upper() not in OP_ID:
            raise ModelError("%s: bad op %r (one of %s)"
                             % (ctx, op_name, "|".join(OPS)))
        op_name = op_name.upper()
        ins, outs = nd.get("inputs"), nd.get("outputs")
        if not isinstance(ins, list) or not isinstance(outs, list):
            raise ModelError("%s: 'inputs'/'outputs' must be name lists" % ctx)
        lo_i, hi_i, lo_o, hi_o = ARITY[op_name]
        if not (lo_i <= len(ins) <= hi_i):
            raise ModelError("%s: %s wants %d..%d inputs, got %d"
                             % (ctx, op_name, lo_i, hi_i, len(ins)))
        if not (lo_o <= len(outs) <= hi_o):
            raise ModelError("%s: %s wants %d..%d outputs, got %d"
                             % (ctx, op_name, lo_o, hi_o, len(outs)))
        in_idx = []
        for nm in ins:
            if nm not in name_idx:
                raise ModelError("%s: undeclared input tensor %r" % (ctx, nm))
            in_idx.append(name_idx[nm])
        out_idx = []
        for nm in outs:
            if nm not in name_idx:
                raise ModelError("%s: undeclared output tensor %r" % (ctx, nm))
            t = tensors[name_idx[nm]]
            if t["is_weight"]:
                raise ModelError("%s: %s writes weight tensor %r" % (ctx, op_name, nm))
            if t["kind"] == "input":
                raise ModelError("%s: %s writes graph input %r" % (ctx, op_name, nm))
            if nm in producer:
                raise ModelError("%s: tensor %r already produced by nodes[%d]"
                                 % (ctx, nm, producer[nm]))
            producer[nm] = ni
            out_idx.append(name_idx[nm])
        params = nd.get("params", {})
        if not isinstance(params, dict):
            raise ModelError("%s: 'params' must be an object" % ctx)
        params_bytes = pack_params(op_name, params, ctx)
        if len(params_bytes) > 64 * 1024:
            raise ModelError("%s: params blob too large (%d B)" % (ctx, len(params_bytes)))
        node = {"op_name": op_name, "op": OP_ID[op_name],
                "in_idx": in_idx, "out_idx": out_idx,
                "params": params, "params_bytes": params_bytes}
        _check_shapes(node, tensors, ctx)
        nodes.append(node)

    # wiring warnings: activations never produced, produced but unused
    for t in tensors:
        if t["is_weight"] or t["kind"] == "input":
            continue
        if t["kind"] != "output" and t["name"] not in producer:
            warns.append("activation tensor %r is never produced" % t["name"])
    used = {i for nd in nodes for i in nd["in_idx"]}
    for t in tensors:
        if t["is_weight"]:
            continue
        i = name_idx[t["name"]]
        if t["kind"] == "output" and t["name"] not in producer:
            warns.append("graph output %r is never produced by any node" % t["name"])
        elif t["kind"] == "input" and i not in used:
            warns.append("graph input %r is never consumed by any node" % t["name"])
        elif t["kind"] == "tensor" and i not in used \
                and t["name"] in producer:
            warns.append("intermediate %r is produced but never consumed" % t["name"])
    return tensors, nodes, warns, n_inputs, n_outputs


def _pi32(params, key, default, ctx):
    v = params.get(key, default)
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        raise ModelError("%s: params.%s must be a number" % (ctx, key))
    return int(v)


def pack_params(op_name, params, ctx):
    """Binary per-op param record (param_i32/param_f32 element order).

    Ops the runtime never reads params for get an empty record; their
    JSON params are still validated/delivered for shape checks.
    """
    if op_name == "CONV1D":
        return struct.pack("<5i",
                           _pi32(params, "kernel", _pi32(params, "k", 1, ctx), ctx),
                           _pi32(params, "stride", 1, ctx),
                           _pi32(params, "pad", 0, ctx),
                           _pi32(params, "dil", 1, ctx),
                           _pi32(params, "groups", 1, ctx))
    if op_name == "DWCONV1D":
        return struct.pack("<4i",
                           _pi32(params, "kernel", _pi32(params, "k", 1, ctx), ctx),
                           _pi32(params, "stride", 1, ctx),
                           _pi32(params, "pad", 0, ctx),
                           _pi32(params, "dil", 1, ctx))
    if op_name in ("MAXPOOL1D", "AVGPOOL1D"):
        k = _pi32(params, "kernel", _pi32(params, "k", 2, ctx), ctx)
        return struct.pack("<3i", k,
                           _pi32(params, "stride", k, ctx),
                           _pi32(params, "pad", 0, ctx))
    if op_name == "CONCAT":
        return struct.pack("<i", _pi32(params, "axis", -1, ctx))
    if op_name == "LAYERNORM":
        eps = params.get("eps", 1e-5)
        if not isinstance(eps, (int, float)):
            raise ModelError("%s: params.eps must be a number" % ctx)
        return struct.pack("<f", float(eps))
    # DENSE, GAP, RELU*, ADD, MUL, RESHAPE, SOFTMAX, GRU, QUANT, DEQUANT:
    # the runtime reads no params for these (GRU shape comes from the
    # weight tensor; SOFTMAX/RESHAPE use tensor dims).
    return b""


_PARAM_KEYS = {
    "CONV1D": ("<5i", ["k", "stride", "pad", "dil", "groups"]),
    "DWCONV1D": ("<4i", ["k", "stride", "pad", "dil"]),
    "MAXPOOL1D": ("<3i", ["k", "stride", "pad"]),
    "AVGPOOL1D": ("<3i", ["k", "stride", "pad"]),
    "CONCAT": ("<i", ["axis"]),
    "LAYERNORM": ("<f", ["eps"]),
}


def _params_disp(op_id, raw):
    """Human-readable decode of a binary param record for `info`."""
    if not raw:
        return "{}"
    op_name = OPS[op_id] if op_id < len(OPS) else None
    spec = _PARAM_KEYS.get(op_name)
    if spec is not None:
        fmt, keys = spec
        want = struct.calcsize(fmt)
        if len(raw) >= want:
            vals = struct.unpack_from(fmt, raw, 0)
            return json.dumps(dict(zip(keys, vals)),
                              separators=(",", ":"))
    return "<%d B: %s>" % (len(raw), raw[:24].hex())


def _check_shapes(node, tensors, ctx):
    """Light sanity checks; cheap to run, catches most bad graphs."""
    T = [tensors[i] for i in node["in_idx"]]
    O = [tensors[i] for i in node["out_idx"]]
    op = node["op_name"]
    if op == "DENSE":
        x, w = T[0], T[1]
        b = T[2] if len(T) > 2 else None
        for t, role in ((x, "input"), (w, "weight")):
            if t["dtype"] != 0:
                raise ModelError("%s: DENSE %s %r must be q8, got %s"
                                 % (ctx, role, t["name"], t["dtype_name"]))
        if w["ndim"] != 2:
            raise ModelError("%s: DENSE weight %r must be 2-D [out,in]"
                             % (ctx, w["name"]))
        if x["dims"][-1] != w["dims"][1]:
            raise ModelError("%s: DENSE in-features %d != weight in-dim %d"
                             % (ctx, x["dims"][-1], w["dims"][1]))
        if b is not None and (b["dtype"] != 1 or b["ndim"] != 1
                              or b["dims"][0] != w["dims"][0]):
            raise ModelError("%s: DENSE bias %r must be i32 [%d]"
                             % (ctx, b["name"], w["dims"][0]))
        if O[0]["dims"][-1] != w["dims"][0]:
            raise ModelError("%s: DENSE out last dim %d != weight out-dim %d"
                             % (ctx, O[0]["dims"][-1], w["dims"][0]))
        if _numel(O[0]["dims"]) * x["dims"][-1] != _numel(x["dims"]) * w["dims"][0]:
            raise ModelError("%s: DENSE out %s not batch-compatible with in %s"
                             % (ctx, O[0]["dims"], x["dims"]))
    elif op in ("RELU", "RELU6", "GELU", "SIGMOID", "TANH", "SOFTMAX"):
        if T[0]["dims"] != O[0]["dims"]:
            raise ModelError("%s: %s dims in %s != out %s"
                             % (ctx, op, T[0]["dims"], O[0]["dims"]))
    elif op == "RESHAPE":
        want = node["params"].get("shape")
        if want is not None and want != O[0]["dims"]:
            raise ModelError("%s: RESHAPE params.shape %s != out dims %s"
                             % (ctx, want, O[0]["dims"]))
        if _numel(T[0]["dims"]) != _numel(O[0]["dims"]):
            raise ModelError("%s: RESHAPE numel %d != %d"
                             % (ctx, _numel(T[0]["dims"]), _numel(O[0]["dims"])))
    elif op == "GAP":
        if _numel(O[0]["dims"]) != T[0]["dims"][0]:
            raise ModelError("%s: GAP out %s != [C=%d] of in %s"
                             % (ctx, O[0]["dims"], T[0]["dims"][0], T[0]["dims"]))
    elif op == "CONCAT":
        axis = node["params"].get("axis")
        if axis is not None:
            ax = axis % T[0]["ndim"]
            for t in T[1:]:
                if t["ndim"] != T[0]["ndim"]:
                    raise ModelError("%s: CONCAT ndim mismatch %s vs %s"
                                     % (ctx, t["name"], T[0]["name"]))
                for k in range(T[0]["ndim"]):
                    if k != ax and t["dims"][k] != T[0]["dims"][k]:
                        raise ModelError("%s: CONCAT dim %d mismatch: %s vs %s"
                                         % (ctx, k, t["dims"], T[0]["dims"]))
            if O[0]["dims"][ax] != sum(t["dims"][ax] for t in T):
                raise ModelError("%s: CONCAT out axis dim %d != sum %d"
                                 % (ctx, O[0]["dims"][ax],
                                    sum(t["dims"][ax] for t in T)))


# ------------------------------------------------------------- blob build
def estimate_arena(tensors, nodes, n_inputs, n_outputs):
    """Peak live activation bytes (each buffer 16 B aligned).

    A non-weight tensor lives from its producer node (inputs: before
    node 0) to its last consumer (outputs / never-consumed: end).
    """
    n_nodes = len(nodes)
    producer, last_use = {}, {}
    for k, nd in enumerate(nodes):
        for i in nd["out_idx"]:
            producer[i] = k
        for i in nd["in_idx"]:
            last_use[i] = k
    out_ids = set(range(n_inputs, n_inputs + n_outputs))
    ivals = []
    for i, t in enumerate(tensors):
        if t["is_weight"]:
            continue
        start = producer.get(i, -1)
        end = last_use.get(i, start)
        if i in out_ids or i not in last_use:
            end = n_nodes - 1 if n_nodes else 0
            if i in out_ids:
                end = n_nodes  # graph outputs must survive the whole run
        b = _align(_numel(t["dims"]) * DTYPE_ELEMSZ[t["dtype"]], 16)
        ivals.append((start, end, b))
    peak = 0
    for tstep in range(-1, n_nodes + 1):
        live = sum(b for s, e, b in ivals if s <= tstep <= e)
        peak = max(peak, live)
    return _align(peak, 16)


def param_count(tensors):
    return sum(_numel(t["dims"]) for t in tensors if t["is_weight"])


def build_blob(tensors, nodes, n_inputs, n_outputs):
    """Serialize the normalized graph; return (blob_bytes, stats)."""
    params_blob = bytearray()
    poffs = []
    for nd in nodes:
        poffs.append((len(params_blob), len(nd["params_bytes"])))
        params_blob += nd["params_bytes"]

    weights_blob = bytearray()
    woffs = {}
    for i, t in enumerate(tensors):
        if not t["is_weight"]:
            woffs[i] = (0, 0)
            continue
        while len(weights_blob) % 4:
            weights_blob.append(0)
        woffs[i] = (len(weights_blob), len(t["payload"]))
        weights_blob += t["payload"]

    arena = estimate_arena(tensors, nodes, n_inputs, n_outputs)

    tt = bytearray()
    for i, t in enumerate(tensors):
        dims4 = t["dims"] + [0] * (4 - t["ndim"])
        off, size = woffs[i]
        tt += struct.pack(TENSOR_FMT, t["name"].encode("ascii"),
                          t["dtype"], t["ndim"], *dims4,
                          t["scale"], t["zp"], off, size)

    nt = bytearray()
    for k, nd in enumerate(nodes):
        ins = nd["in_idx"] + [UNUSED_IDX] * (4 - len(nd["in_idx"]))
        outs = nd["out_idx"] + [UNUSED_IDX] * (4 - len(nd["out_idx"]))
        poff, plen = poffs[k]
        nt += struct.pack(NODE_FMT, nd["op"], len(nd["in_idx"]), *ins,
                          len(nd["out_idx"]), *outs, poff, plen)

    # u32 index lists between node table and params blob (runtime layout)
    idx_blob = struct.pack("<%dI" % n_inputs, *range(n_inputs)) \
             + struct.pack("<%dI" % n_outputs,
                           *range(n_inputs, n_inputs + n_outputs))

    tail = bytes(tt + nt + idx_blob + params_blob + weights_blob)
    crc = zlib.crc32(tail) & 0xFFFFFFFF
    header = struct.pack(HEADER_FMT, VQMB_MAGIC, VQMB_VER, 0,
                         len(tensors), len(nodes), n_inputs, n_outputs,
                         arena, len(weights_blob), crc)
    blob = header + tail
    if len(blob) > VQMB_MAX_BLOB:
        raise ModelError("blob is %d B, exceeds %d B partition limit"
                         % (len(blob), VQMB_MAX_BLOB))
    stats = {"params": param_count(tensors), "arena": arena,
             "weights_bytes": len(weights_blob), "params_bytes": len(params_blob),
             "crc": crc, "size": len(blob)}
    return blob, stats


# ------------------------------------------------------------- C header
def write_c_header(blob, path, array_name, comment):
    lines = ["/* %s" % comment,
             " * Generated by export_model.py -- do not edit. */",
             "#pragma once",
             "#include <stdint.h>",
             "",
             "static const uint8_t %s[] = {" % array_name]
    for i in range(0, len(blob), 12):
        lines.append("    " + ", ".join("0x%02x" % b for b in blob[i:i + 12]) + ",")
    lines += ["};",
              "static const uint32_t %s_len = %d;" % (array_name, len(blob)),
              ""]
    try:
        path.write_text("\n".join(lines))
    except OSError as e:
        raise ModelError("cannot write %s: %s" % (path, e))


# ------------------------------------------------------------- stub model
def _stub_stream(seed, n):
    """Deterministic int8-ish byte stream via sha256(seed || counter)."""
    out, ctr = bytearray(), 0
    while len(out) < n:
        out += hashlib.sha256(b"vq-stub"
                              + (seed & 0xFFFFFFFF).to_bytes(4, "little")
                              + ctr.to_bytes(4, "little")).digest()
        ctr += 1
    return bytes(out[:n])


def _stub_q8(seed_key, n):
    vals = [b - 256 if b > 127 else b for b in _stub_stream(seed_key, n)]
    return struct.pack("<%db" % n, *vals)


def _stub_i32(seed_key, n):
    """Small-magnitude i32 biases, one stream byte per element."""
    vals = [b - 256 if b > 127 else b for b in _stub_stream(seed_key, n)]
    return struct.pack("<%di" % n, *vals)


def stub_graph(seed):
    """features[40] -> DENSE->RELU->DENSE->RELU->DENSE->SOFTMAX -> prob[2]."""
    def T(name, dt, dims, scale, zp=0, payload=None, kind="tensor"):
        return {"name": name, "dtype_name": dt, "dtype": DTYPES[dt],
                "ndim": len(dims), "dims": dims, "scale": scale, "zp": zp,
                "payload": payload, "is_weight": payload is not None,
                "kind": kind}

    def W(name, dims, scale, key):
        return T(name, "q8", dims, scale, payload=_stub_q8(key, _numel(dims)))

    def B(name, n, key):
        return T(name, "i32", [n], 1.0, payload=_stub_i32(key, n))

    tensors = [
        T("features", "q8", [40], 0.02, kind="input"),
        T("prob", "q8", [2], 0.004, kind="output"),
        W("fc1_w", [24, 40], 0.021, seed + 0), B("fc1_b", 24, seed + 1),
        T("d1", "q8", [24], 0.02), T("a1", "q8", [24], 0.02),
        W("fc2_w", [24, 24], 0.023, seed + 2), B("fc2_b", 24, seed + 3),
        T("d2", "q8", [24], 0.02), T("a2", "q8", [24], 0.02),
        W("fc3_w", [2, 24], 0.017, seed + 4), B("fc3_b", 2, seed + 5),
        T("logits", "q8", [2], 0.02),
    ]
    idx = {t["name"]: i for i, t in enumerate(tensors)}

    def N(op, ins, outs, params=None):
        return {"op_name": op, "op": OP_ID[op],
                "in_idx": [idx[i] for i in ins],
                "out_idx": [idx[o] for o in outs],
                "params": params or {},
                "params_bytes": pack_params(op, params or {}, "stub")}

    nodes = [
        N("DENSE", ["features", "fc1_w", "fc1_b"], ["d1"]),
        N("RELU", ["d1"], ["a1"]),
        N("DENSE", ["a1", "fc2_w", "fc2_b"], ["d2"]),
        N("RELU", ["d2"], ["a2"]),
        N("DENSE", ["a2", "fc3_w", "fc3_b"], ["logits"]),
        N("SOFTMAX", ["logits"], ["prob"], {"axis": -1}),
    ]
    return tensors, nodes


# ------------------------------------------------------------- subcommands
def cmd_export(args):
    try:
        g = json.loads(args.graph.read_text())
    except OSError as e:
        raise ModelError("cannot read graph %s: %s" % (args.graph, e))
    except ValueError as e:
        raise ModelError("bad JSON in %s: %s" % (args.graph, e))
    wdir = args.weights if args.weights is not None else args.graph.parent
    tensors, nodes, warns, ni, no = normalize_graph(g, wdir)
    blob, st = build_blob(tensors, nodes, ni, no)
    try:
        args.out.write_bytes(blob)
    except OSError as e:
        raise ModelError("cannot write %s: %s" % (args.out, e))
    for w in warns:
        print("warning: %s" % w, file=sys.stderr)
    print("wrote %s: %d B, %d tensors, %d nodes, %d params, "
          "weights %d B, arena ~%d B, crc32 0x%08x"
          % (args.out, st["size"], len(tensors), len(nodes), st["params"],
             st["weights_bytes"], st["arena"], st["crc"]))
    if args.c_header:
        write_c_header(blob, args.c_header, "vq_model_bin",
                       "Compiled-in VQMB model blob (%s)." % args.graph.name)
        print("wrote %s" % args.c_header)
    return 0


def cmd_stub(args):
    tensors, nodes = stub_graph(args.seed & 0xFFFFFFFF)
    blob, st = build_blob(tensors, nodes, 1, 1)
    try:
        args.out.write_bytes(blob)
    except OSError as e:
        raise ModelError("cannot write %s: %s" % (args.out, e))
    print("wrote %s: %d B, %d tensors, %d nodes, %d params, "
          "weights %d B, arena ~%d B, crc32 0x%08x (seed %d)"
          % (args.out, st["size"], len(tensors), len(nodes), st["params"],
             st["weights_bytes"], st["arena"], st["crc"], args.seed))
    if args.c_header:
        write_c_header(blob, args.c_header, "vq_stub_model_bin",
                       "Compiled-in fallback model for pipeline bring-up.")
        print("wrote %s" % args.c_header)
    return 0


def cmd_info(args):
    try:
        blob = args.model.read_bytes()
    except OSError as e:
        raise ModelError("cannot read %s: %s" % (args.model, e))
    if len(blob) < HEADER_SIZE:
        raise ModelError("%s: %d B < %d B header" % (args.model, len(blob), HEADER_SIZE))
    (magic, ver, flags, n_t, n_n, n_in, n_out,
     arena, wbytes, crc) = struct.unpack_from(HEADER_FMT, blob, 0)
    if magic != VQMB_MAGIC:
        raise ModelError("%s: bad magic 0x%08x (not a VQMB blob)"
                         % (args.model, magic))
    need = (HEADER_SIZE + TENSOR_SIZE * n_t + NODE_SIZE * n_n
            + 4 * (n_in + n_out))
    if len(blob) < need:
        raise ModelError("%s: truncated: %d B < %d B expected by header"
                         % (args.model, len(blob), need))
    tail = blob[HEADER_SIZE:]
    crc_ok = (zlib.crc32(tail) & 0xFFFFFFFF) == crc

    off = HEADER_SIZE
    tensors = []
    for _ in range(n_t):
        (name, dt, ndim, d0, d1, d2, d3, scale, zp,
         woff, wsize) = struct.unpack_from(TENSOR_FMT, blob, off)
        off += TENSOR_SIZE
        dims = [d for d in (d0, d1, d2, d3)][:ndim]
        tensors.append({"name": name.split(b"\0", 1)[0].decode("ascii", "replace"),
                        "dtype": dt, "ndim": ndim, "dims": dims,
                        "scale": scale, "zp": zp, "off": woff, "size": wsize})
    nodes = []
    idx_bytes = 4 * (n_in + n_out)
    params_base = (HEADER_SIZE + TENSOR_SIZE * n_t + NODE_SIZE * n_n
                   + idx_bytes)
    for _ in range(n_n):
        fields = struct.unpack_from(NODE_FMT, blob, off)
        (op, nin) = fields[0], fields[1]
        ins = [x for x in fields[2:6][:nin] if x != UNUSED_IDX]
        outs = [x for x in fields[7:11][:fields[6]] if x != UNUSED_IDX]
        poff, plen = fields[11], fields[12]
        off += NODE_SIZE
        praw = blob[params_base + poff:params_base + poff + plen]
        pdisp = _params_disp(op, praw)
        nodes.append({"op": op, "ins": ins, "outs": outs, "params": pdisp})

    print("VQMB model: %s" % args.model)
    print("  size          : %d B (%.1f KiB)%s"
          % (len(blob), len(blob) / 1024.0,
             "" if len(blob) <= VQMB_MAX_BLOB else "  ** EXCEEDS 4 MiB **"))
    print("  version       : %d   flags: 0x%08x" % (ver, flags))
    print("  tensors       : %d   nodes: %d   inputs: %d   outputs: %d"
          % (n_t, n_n, n_in, n_out))
    print("  weights_bytes : %d   arena_bytes: %d" % (wbytes, arena))
    print("  crc32         : 0x%08x  %s" % (crc, "OK" if crc_ok else "** MISMATCH **"))
    # weights blob is the last section; whatever sits between the node
    # table and it is the params blob.
    wstart = len(blob) - wbytes
    params_len = wstart - need   # params blob between idx lists + weights
    if wstart < need:
        print("  warning       : weights blob overlaps tables "
              "(weights_bytes corrupt?)")
    else:
        print("  params blob   : %d B" % params_len)
    bad = [t["name"] for t in tensors
           if t["size"] and t["off"] + t["size"] > wbytes]
    if bad:
        print("  warning       : weights out of bounds: %s" % ", ".join(bad))

    print("\ntensors:")
    print("  %-4s %-20s %-4s %-4s %-14s %-9s %-6s %s"
          % ("idx", "name", "type", "ndim", "dims", "scale", "zp", "woff/wsize"))
    for i, t in enumerate(tensors):
        role = ("input" if i < n_in else
                "output" if i < n_in + n_out else
                ("weight" if t["size"] else "act"))
        print("  %-4d %-20s %-4s %-4d %-14s %-9.5g %-6d %s  %s"
              % (i, t["name"], DTYPE_NAME.get(t["dtype"], "?%d" % t["dtype"]),
                 t["ndim"], str(t["dims"]), t["scale"], t["zp"],
                 ("%d/%d" % (t["off"], t["size"]) if t["size"] else "-"), role))

    print("\nnodes:")
    for k, nd in enumerate(nodes):
        opname = OPS[nd["op"]] if nd["op"] < len(OPS) else "?%d" % nd["op"]
        ins = ",".join(tensors[i]["name"] if i < len(tensors) else "?%d" % i
                       for i in nd["ins"])
        outs = ",".join(tensors[i]["name"] if i < len(tensors) else "?%d" % i
                        for i in nd["outs"])
        print("  %-3d %-10s %-42s %s"
              % (k, opname, "%s -> %s" % (ins, outs), nd["params"]))

    npar = sum(_numel(t["dims"]) for t in tensors if t["size"])
    print("\nparam count   : %d" % npar)
    # arena re-estimate needs producer/use info -- rebuild from nodes
    pseudo = [{"in_idx": nd["ins"], "out_idx": nd["outs"]} for nd in nodes]
    ptensors = [{"is_weight": bool(t["size"]), "dims": t["dims"],
                 "dtype": t["dtype"] if t["dtype"] in DTYPE_ELEMSZ else 0}
                for t in tensors]
    est = estimate_arena(ptensors, pseudo, n_in, n_out)
    print("arena estimate: %d B (header says %d B)" % (est, arena))
    if not crc_ok:
        print("export_model.py: error: crc32 mismatch", file=sys.stderr)
        return 1
    return 0


def cmd_flash(args):
    print("# 'model' partition: offset 0x%x, size 0x%x (partitions.csv)"
          % (MODEL_PART_OFFSET, MODEL_PART_SIZE))
    try:
        sz = args.model.stat().st_size
        if sz > MODEL_PART_SIZE:
            print("# WARNING: %s is %d B > partition %d B"
                  % (args.model, sz, MODEL_PART_SIZE))
    except OSError:
        print("# WARNING: %s does not exist yet" % args.model)
    print("esptool.py --chip esp32s3 --port %s --baud 460800 "
          "write_flash 0x%x %s" % (args.port, MODEL_PART_OFFSET, args.model))
    print("# or via the ESP-IDF partition tool (resolves offset itself):")
    print("parttool.py --port %s write_partition --partition-name=model "
          "--input %s" % (args.port, args.model))
    return 0


# ------------------------------------------------------------------- main
def main(argv=None):
    p = argparse.ArgumentParser(
        prog="export_model.py",
        description="VQMB model exporter/inspector for vitalquant firmware v2 "
                    "(see module docstring for the blob format + JSON schema).",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True, metavar="cmd")

    pe = sub.add_parser("export", help="JSON graph + int8 weights -> vqmodel.bin",
                        epilog="JSON schema: {name?, inputs:[{name,shape|dims,"
                               "dtype?,scale?,zp?}], outputs:[...], "
                               "tensors:[{name,dtype,dims,scale?,zp?,"
                               "weights_file|weights_inline?}], "
                               "nodes:[{op,inputs,outputs,params?}]}.  "
                               "ops: " + "|".join(OPS) + ".  "
                               "Tensors without weights are activations.  "
                               "See the module docstring for the full spec.",
                        formatter_class=argparse.RawDescriptionHelpFormatter)
    pe.add_argument("--graph", required=True, type=Path,
                    help="JSON graph description (see schema above)")
    pe.add_argument("--weights", type=Path, default=None,
                    help="dir holding weights_file blobs "
                         "(default: the graph file's directory)")
    pe.add_argument("--out", type=Path, default=Path("vqmodel.bin"),
                    help="output blob path (default: vqmodel.bin)")
    pe.add_argument("--c-header", type=Path, default=None,
                    help="optional: also emit a compilable C header "
                         "(static const uint8_t vq_model_bin[])")
    pe.set_defaults(func=cmd_export)

    ps = sub.add_parser("stub", help="emit a tiny valid VQMB stub model "
                                     "(features[40] -> MLP -> prob[2])")
    ps.add_argument("--out", type=Path, default=Path("stub_model.bin"),
                    help="output blob path (default: stub_model.bin)")
    ps.add_argument("--c-header", type=Path, default=Path("stub_model_bin.h"),
                    help="C header path (default: stub_model_bin.h; "
                         "use '' to skip)")
    ps.add_argument("--seed", type=int, default=1,
                    help="PRNG seed for deterministic int8 weights "
                         "(default: 1)")
    ps.set_defaults(func=cmd_stub)

    pi = sub.add_parser("info", help="parse + dump a VQMB blob")
    pi.add_argument("model", type=Path, help="vqmodel.bin to inspect")
    pi.set_defaults(func=cmd_info)

    pf = sub.add_parser("flash", help="print the esptool/parttool command "
                                      "for the 'model' partition")
    pf.add_argument("model", type=Path, nargs="?", default=Path("vqmodel.bin"),
                    help="blob to flash (default: vqmodel.bin)")
    pf.add_argument("--port", default="/dev/ttyUSB0",
                    help="serial port (default: /dev/ttyUSB0)")
    pf.set_defaults(func=cmd_flash)

    args = p.parse_args(argv)
    if args.cmd == "stub" and args.c_header == Path(""):
        args.c_header = None
    try:
        return args.func(args)
    except ModelError as e:
        print("export_model.py: error: %s" % e, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
