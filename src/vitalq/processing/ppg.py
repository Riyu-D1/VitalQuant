"""PPG processing chain (docs/06 §2).

filter → peaks → IBI → HR/PRV + perfusion index + ratio-of-ratios, and the
signal-quality components. Conventions from the SQI literature (Appl. Sci. 2022
survey); peaks via scipy.find_peaks with refractory + adaptive prominence.

PRV, not HRV: wrist PPG inter-beat intervals are not ECG RR intervals — the
naming stays honest.
"""

from __future__ import annotations

import numpy as np
from scipy.signal import butter, find_peaks, sosfiltfilt
from scipy.stats import kurtosis, skew

HR_BAND_HZ = (0.5, 4.0)        # 30–240 bpm
MIN_IBI_S, MAX_IBI_S = 0.4, 2.0   # ≤150 bpm floor; shorter refractory double-counts dicrotic


def bandpass(x: np.ndarray, fs: float, lo: float = HR_BAND_HZ[0],
             hi: float = HR_BAND_HZ[1], order: int = 4) -> np.ndarray:
    if len(x) < order * 10:
        return x - np.mean(x)
    sos = butter(order, [lo, hi], btype="band", fs=fs, output="sos")
    padlen = min(3 * (2 * order), len(x) - 1)
    return sosfiltfilt(sos, x, padlen=padlen)


def detect_beats(x_filt: np.ndarray, fs: float) -> np.ndarray:
    """Systolic peak indices. Adaptive prominence, refractory = MIN_IBI_S."""
    prom = max(np.std(x_filt) * 0.5, 1e-6)
    peaks, _ = find_peaks(x_filt, distance=int(MIN_IBI_S * fs), prominence=prom)
    return peaks


def ibi_stats(peaks: np.ndarray, fs: float) -> dict:
    if len(peaks) < 3:
        return {"hr_bpm": None, "rmssd_ms": None, "sdnn_ms": None, "n_beats": int(len(peaks))}
    ibi = np.diff(peaks) / fs
    ibi = ibi[(ibi >= MIN_IBI_S) & (ibi <= MAX_IBI_S)]
    if len(ibi) < 2:
        return {"hr_bpm": None, "rmssd_ms": None, "sdnn_ms": None, "n_beats": int(len(peaks))}
    return {
        "hr_bpm": round(float(60.0 / np.mean(ibi)), 2),
        "rmssd_ms": (round(float(np.sqrt(np.mean(np.diff(ibi) ** 2)) * 1000), 2)
                 if len(ibi) > 2 else None),
        "sdnn_ms": round(float(np.std(ibi) * 1000), 2),
        "n_beats": int(len(peaks)),
    }


def perfusion_index(x: np.ndarray) -> float:
    """AC rms / DC mean — the honest perfusion proxy behind SpO2's R."""
    dc = float(np.mean(x))
    if dc <= 0:
        return 0.0
    ac = float(np.sqrt(np.mean((x - dc) ** 2)))
    return ac / dc


def r_ratio(red: np.ndarray, ir: np.ndarray) -> float | None:
    """Ratio of ratios R = (AC/DC)_red / (AC/DC)_ir. Uncalibrated — see doc 02."""
    pr, pi = perfusion_index(red), perfusion_index(ir)
    if pi <= 0:
        return None
    return pr / pi


def spo2_uncalibrated(r: float | None) -> float | None:
    """110 − 25R population approximation — NEVER calibrated; flagged in name."""
    if r is None:
        return None
    return round(110.0 - 25.0 * r, 2)


def sqi_components(x: np.ndarray, x_filt: np.ndarray, peaks: np.ndarray,
                   fs: float) -> dict:
    n = len(x)
    clipped = float(np.mean(np.abs(np.diff(x)) < 1e-9)) if n > 2 else 0.0  # flatline/ADC rail
    sk, ku = float(skew(x_filt)), float(kurtosis(x_filt))
    # spectral purity = 1 − out-of-band residual fraction (raw detrended vs filtered)
    resid = x - x.mean() - (x_filt - x_filt.mean())
    total = float(np.var(x - x.mean()))
    spectral_purity = (max(0.0, 1.0 - float(np.var(resid)) / total) if total > 0 else 0.0)
    # beat-template correlation: mean corr of each beat vs ensemble average
    template_r = 0.0
    if len(peaks) >= 4:
        segs = []
        for a, b in zip(peaks[:-1], peaks[1:], strict=False):
            seg = x_filt[a:b]
            if 0.3 * fs < len(seg) < 2.0 * fs:
                segs.append(seg)
        if len(segs) >= 3:
            m = min(len(s) for s in segs)
            arr = np.array([s[:m] for s in segs])
            tmpl = arr.mean(axis=0)
            rs = [float(np.corrcoef(s, tmpl)[0, 1]) for s in arr]
            template_r = float(np.nanmean(rs))
    return {
        "skew": round(sk, 3), "kurtosis": round(ku, 3),
        "clipped_frac": round(clipped, 4), "spectral_purity": round(spectral_purity, 4),
        "template_r": round(max(template_r, 0.0), 4),
        "perfusion_index": round(perfusion_index(x), 5),
    }


def combine_sqi(c: dict) -> float:
    """Heuristic SQI ∈ [0,1] from components — replaced by a learnt model once
    BUT-PPG-derived weights exist (Phase A1)."""
    score = 1.0
    score *= min(1.0, c["spectral_purity"] / 0.55)          # want HR-band dominated
    score *= min(1.0, max(0.0, c["template_r"]) / 0.8)      # beat morphology repeatability
    score *= min(1.0, max(0.05, c["perfusion_index"] / 0.01))  # PI < 1% is weak
    if c["clipped_frac"] > 0.05:
        score *= 0.3                                        # flatlining/railing
    return round(float(np.clip(score, 0.0, 1.0)), 3)


def process_ppg_window(samples: list[float], fs: float) -> dict:
    """Full chain for one window. Returns features + SQI detail."""
    x = np.asarray(samples, dtype=float)
    xf = bandpass(x, fs)
    peaks = detect_beats(xf, fs)
    feats = ibi_stats(peaks, fs)
    sqi = sqi_components(x, xf, peaks, fs)
    sqi["sqi"] = combine_sqi(sqi)
    return {**feats, "sqi": sqi}
