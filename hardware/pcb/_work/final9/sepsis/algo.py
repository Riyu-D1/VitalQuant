#!/usr/bin/env python3
"""algo.py — reference measurement algorithms (standard, simple)."""
import numpy as np
from scipy.signal import butter, filtfilt, find_peaks, welch

def _bp(x, lo, hi, fs):
    b, a = butter(2, [lo / (fs / 2), hi / (fs / 2)], 'band'); return filtfilt(b, a, x)

def _lp(x, fc, fs):
    b, a = butter(2, fc / (fs / 2), 'low'); return filtfilt(b, a, x)

# ---------- motion gating ----------
def clean_mask(mot, thr=0.8):
    return mot < thr

# ---------- HR / HRV from ECG ----------
def qrs_detect(ecg, fs):
    """Pan-Tompkins-lite: BP 5-18Hz -> derivative -> square -> integ -> peaks."""
    x = _bp(ecg, 5, 18, fs)
    d = np.diff(x) ** 2
    w = int(0.08 * fs)
    integ = np.convolve(d, np.ones(w) / w, 'same')
    thr = 0.3 * np.max(integ)
    pk, _ = find_peaks(integ, height=thr, distance=int(0.28 * fs))
    # map back to R peaks
    rpk = []
    for p in pk:
        lo, hi = max(0, p - int(0.1 * fs)), min(len(ecg), p + int(0.05 * fs))
        rpk.append(lo + int(np.argmax(np.abs(ecg[lo:hi]))))
    return np.array(sorted(set(rpk)))

def hr_metrics(ecg, fs, mask):
    rpk = qrs_detect(ecg * (mask if mask is not None else 1), fs)
    if len(rpk) < 6: return dict(hr=np.nan, rmssd=np.nan, sdnn=np.nan, nbeats=len(rpk))
    rrs = np.diff(rpk) / fs
    rrs = rrs[(rrs > 0.3) & (rrs < 2.0)]
    if len(rrs) < 5: return dict(hr=np.nan, rmssd=np.nan, sdnn=np.nan, nbeats=len(rpk))
    return dict(hr=60.0 / np.mean(rrs),
                rmssd=np.sqrt(np.mean(np.diff(rrs) ** 2)) * 1000,
                sdnn=np.std(rrs) * 1000, nbeats=len(rrs))

# ---------- RR: fuse impedance + EDR + PPG ----------
def rr_from(x, fs, lo=0.12, hi=0.7, mask=None):
    if mask is not None:
        x = x[mask]
    if len(x) < 20 * fs: return np.nan
    f, P = welch(x, fs, nperseg=min(len(x), 30 * fs))
    m = (f >= lo) & (f <= hi)
    if not m.any() or P[m].max() <= 0: return np.nan
    return f[m][np.argmax(P[m])] * 60

# ---------- SpO2 ratio-of-ratios ----------
def spo2_ror(ac_r, dc_r, ac_i, dc_i, fs, mask=None):
    """RoR with stated calibration SpO2 = 110 - 25*R (empirical linear)."""
    if mask is not None:
        sel = mask; ac_r, dc_r, ac_i, dc_i = ac_r[sel], dc_r[sel], ac_i[sel], dc_i[sel]
    if len(ac_r) < fs: return np.nan
    pr = np.sqrt(np.mean(ac_r ** 2)) / np.mean(dc_r)
    pi_ = np.sqrt(np.mean(ac_i ** 2)) / np.mean(dc_i)
    if pi_ <= 0: return np.nan
    R = pr / pi_
    return 110 - 25 * R, R

# ---------- temp ----------
def core_temp(tmp117, mlx):
    """Firmware comp: TMP117 minus documented +1C board offset; fuse w/ MLX."""
    return 0.6 * (tmp117 - 1.0) + 0.4 * mlx

def perfusion_index(ac_i, dc_i):
    return 100.0 * np.sqrt(np.mean(ac_i ** 2)) / np.mean(dc_i)
