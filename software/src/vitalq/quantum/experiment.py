"""Experiment runner (docs/08 §3–4).

Sweeps the full matrix — squeezing r, detection efficiency η, tissue noise,
ambient leakage, amplitude, thresholds — never cherry-picked cells. Per cell:
peak SNR, Monte-Carlo shift-detection rate, false-positive rate.

Headline output: `advantage_boundary` — ΔSNR between the best squeezed config
and the classical (r=0) baseline as non-quantum noise grows. Expected physics:
the advantage collapses once tissue/ambient noise exceeds shot noise.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np

from vitalq.quantum import DATA_CLASS
from vitalq.quantum.noise_models import (
    AmbientLeakage,
    LossySqueezedNoise,
    ShotNoise,
    TissueNoise,
)
from vitalq.quantum.signal_models import RamanToyModel


@dataclass
class ExperimentMatrix:
    x_min: float = 1600.0
    x_max: float = 1700.0
    n_x: int = 256
    r_vals: tuple = (0.0, 0.25, 0.5, 0.8, 1.0)
    eta_vals: tuple = (0.5, 0.75, 0.9, 1.0)
    tissue_vals: tuple = (0.0, 0.001, 0.01, 0.05)   # sigma_rel; 0.05 = dominant
    ambient_vals: tuple = (0.0, 10.0)
    amps: tuple = (1000.0,)
    shift_frac: float = 0.05        # positive-case agg_frac → ~1 cm⁻¹ shift
    n_trials: int = 200
    seed: int = 7


def _model(noise_r: float, eta: float, tissue: float, ambient: float):
    m = LossySqueezedNoise(r=noise_r, eta=eta) if noise_r > 0 else ShotNoise()
    if ambient > 0:
        m = AmbientLeakage(m, ambient)
    if tissue > 0:
        m = TissueNoise(m, tissue)
    return m


def _estimate_center(x: np.ndarray, y: np.ndarray) -> float:
    """Peak position by quadratic fit around the max — the honest 'measurement'."""
    i = int(np.argmax(y))
    if i == 0 or i == len(y) - 1:
        return float(x[i])
    x0, x1, x2 = x[i - 1:i + 2]
    y0, y1, y2 = y[i - 1:i + 2]
    denom = (x0 - x1) * (x0 - x2) * (x1 - x2)
    if denom == 0:
        return float(x1)
    a = (x2 * (y1 - y0) + x1 * (y0 - y2) + x0 * (y2 - y1)) / denom
    b = (x2**2 * (y1 - y0) + x1**2 * (y0 - y2) + x0**2 * (y2 - y1)) / denom
    v = -b / (2 * a) if a != 0 else x1
    return float(np.clip(v, x[0], x[-1]))


def run(matrix: ExperimentMatrix) -> dict:
    x = np.linspace(matrix.x_min, matrix.x_max, matrix.n_x)
    cells = []

    for amp in matrix.amps:
        sig_null = RamanToyModel(agg_frac=0.0, amp=amp)
        sig_pos = RamanToyModel(agg_frac=matrix.shift_frac, amp=amp)
        y0 = sig_null.eval(x)
        y1 = sig_pos.eval(x)
        cell_idx = 0
        for r in matrix.r_vals:
            for eta in matrix.eta_vals:
                if r == 0 and eta != matrix.eta_vals[-1]:
                    continue   # classical baseline is r=0 once; η is meaningless without squeezing
                for tissue in matrix.tissue_vals:
                    for ambient in matrix.ambient_vals:
                        cell_idx += 1
                        rng = np.random.default_rng(matrix.seed * 1_000_003
                                                    + cell_idx)
                        noise = _model(r, eta, tissue, ambient)
                        peak_i = int(np.argmax(y0))
                        snr = float(y0[peak_i] / max(noise.std(y0)[peak_i], 1e-12))
                        est_null, est_pos = [], []
                        for _ in range(matrix.n_trials):
                            n0 = rng.normal(0, noise.std(y0))
                            n1 = rng.normal(0, noise.std(y1))
                            est_null.append(_estimate_center(x, y0 + n0))
                            est_pos.append(_estimate_center(x, y1 + n1))
                        est_null = np.asarray(est_null)
                        est_pos = np.asarray(est_pos)
                        # threshold: null-mean + 3·null-std of the estimator
                        mu, sd = float(est_null.mean()), float(est_null.std())
                        thr = mu + 3 * max(sd, 1e-9)
                        cells.append({
                            "r": r, "eta": eta, "tissue_sigma": tissue,
                            "ambient": ambient, "amp": amp,
                            "snr_at_peak": round(snr, 3),
                            "shift_true_cm": float(sig_pos.peak().center -
                                                    sig_null.peak().center),
                            "detect_rate": float((est_pos > thr).mean()),
                            "fpr": float((est_null > thr).mean()),
                            "noise_label": noise.label(),
                        })

    # advantage boundary: for each (tissue, ambient) the best-achievable ΔSNR
    # of any squeezed cell vs the r=0 classical cell
    boundary = []
    for tissue in matrix.tissue_vals:
        for ambient in matrix.ambient_vals:
            cls = [c for c in cells if c["r"] == 0 and c["tissue_sigma"] == tissue
                   and c["ambient"] == ambient]
            sq = [c for c in cells if c["r"] > 0 and c["tissue_sigma"] == tissue
                  and c["ambient"] == ambient]
            if cls and sq:
                best = max(sq, key=lambda c: c["snr_at_peak"])
                boundary.append({
                    "tissue_sigma": tissue, "ambient": ambient,
                    "classical_snr": cls[0]["snr_at_peak"],
                    "best_squeezed_snr": best["snr_at_peak"],
                    "advantage_ratio": round(
                        best["snr_at_peak"] / cls[0]["snr_at_peak"], 4),
                })
    return {
        "data_class": DATA_CLASS,
        "regime": "raman_illustrative",
        "matrix": {k: list(v) if isinstance(v, tuple) else v
                   for k, v in asdict(matrix).items()},
        "cells": cells,
        "advantage_boundary": boundary,
        "separation_notice": "SIMULATED — no quantum hardware; raman_illustrative "
                             "regime, not VitalQ's reflectance measurement.",
            }


def main() -> None:
    p = argparse.ArgumentParser(description="VitalQ quantum-readout simulation "
                                            "(data_class=simulated)")
    p.add_argument("--out", default="experiments/quantum/latest.json")
    p.add_argument("--trials", type=int, default=200)
    args = p.parse_args()
    result = run(ExperimentMatrix(n_trials=args.trials))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2))
    b = result["advantage_boundary"]
    for row in b:
        print(f"tissue={row['tissue_sigma']:>5} ambient={row['ambient']:>5} "
              f"classical={row['classical_snr']:.1f} squeezed={row['best_squeezed_snr']:.1f} "
              f"advantage={row['advantage_ratio']:.3f}x")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
