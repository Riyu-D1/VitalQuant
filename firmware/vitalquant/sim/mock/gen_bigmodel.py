#!/usr/bin/env python3
"""Generate a ~1.5M-parameter INT8 VQMB test model + graph JSON.

Architecture (deterministic, stdlib only):
  features[40] -> fc_in DENSE(40->128) RELU
               -> fc_mid1 DENSE(128->512) RELU
               -> fc_mid2 DENSE(512->1024) RELU
               -> fc_mid3 DENSE(1024->384) RELU
               -> fc_out DENSE(384->2) SOFTMAX
               = 5120 + 65536 + 524288 + 393216 + 770 (+biases ~2026 int32)
               ≈ 1.0M weights int8 (+ i32 biases)  -> ~1.1MB blob < 4MB.
Use --scale to grow/shrink the hidden widths.
"""
import json, os, random, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXPORTER = os.path.join(HERE, "..", "..", "tools", "export_model.py")


def gen(outdir, scale=1.0):
    os.makedirs(outdir, exist_ok=True)
    rng = random.Random(0xCAFE)
    widths = [40, 128, 512, 1024, int(768*scale), 2]
    tensors, nodes, files, n_params = [], [], {}, 0
    prev = "features"
    for i in range(1, 6):
        wo, wi = widths[i], widths[i-1]
        w, b = f"fc{i}_w", f"fc{i}_b"
        wb = bytes(rng.randint(-127, 127) & 0xFF for _ in range(wo*wi))
        bb = b"".join(struct.pack("<i", rng.randint(-512, 512)) for _ in range(wo))
        fn_w, fn_b = w + ".bin", b + ".bin"
        open(os.path.join(outdir, fn_w), "wb").write(wb)
        open(os.path.join(outdir, fn_b), "wb").write(bb)
        tensors += [{"name": w, "dtype": "q8", "dims": [wo, wi],
                     "scale": 0.02, "zp": 0, "weights_file": fn_w},
                    {"name": b, "dtype": "i32", "dims": [wo],
                     "weights_file": fn_b}]
        h = f"h{i}"
        tensors.append({"name": h, "dtype": "q8", "dims": [wo],
                        "scale": 0.02, "zp": 0})
        nodes.append({"op": "DENSE", "inputs": [prev, w, b], "outputs": [h]})
        if i < 5:
            a = f"a{i}"
            tensors.append({"name": a, "dtype": "q8", "dims": [wo],
                            "scale": 0.02, "zp": 0})
            nodes.append({"op": "RELU", "inputs": [h], "outputs": [a]})
            prev = a
        else:
            prev = h
        n_params += wo*wi + wo
    nodes.append({"op": "SOFTMAX", "inputs": [prev], "outputs": ["prob"]})
    graph = {"name": "vq_big15m", "inputs": [
                {"name": "features", "shape": [40], "dtype": "q8",
                 "scale": 0.02, "zp": 0}],
             "outputs": [{"name": "prob", "shape": [2], "dtype": "q8",
                          "scale": 0.00390625, "zp": -128}],
             "tensors": tensors, "nodes": nodes}
    gp = os.path.join(outdir, "big15m.json")
    json.dump(graph, open(gp, "w"), indent=1)
    print(f"graph: {gp}  params={n_params}")
    return gp, n_params


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "big")
    scale = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
    gp, np_ = gen(out, scale)
    blob = os.path.join(out, "big15m.bin")
    subprocess.run([sys.executable, EXPORTER, "export", "--graph", gp,
                    "--weights", out, "--out", blob], check=True)
    subprocess.run([sys.executable, EXPORTER, "info", blob], check=False)
    print("blob:", blob, os.path.getsize(blob), "bytes")
