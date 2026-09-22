"""PPG processing chain (docs/06 §2) — v2.

Chain: bandpass → systolic peaks → foot/onset refinement → artefact-cleaned IBI
→ HR/PRV stats (+frequency-domain) → pulse morphology → respiration estimate →
signal-quality components. PRV, not HRV: wrist PPG inter-beat intervals are not
ECG RR intervals — the naming stays honest. SpO2 stays R-ratio + uncalibrated.
"""

from __future__ import annotations

import numpy as np
from scipy.signal import butter, correlate, find_peaks, hilbert, sosfiltfilt, welch
from scipy.stats import kurtosis, skew

HR_BAND_HZ = (0.5, 4.0)        # 30–240 bpm
MIN_IBI_S, MAX_IBI_S = 0.4, 2.0   # ≤150 bpm floor; shorter refractory double-counts dicrotic
RESP_BAND_HZ = (0.10, 0.60)    # 6–36 breaths/min — plausible breathing range
IBI_MAD_REJECT = 0.20          # drop IBI deviating >20% from local median


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


def refine_onsets(x_filt: np.ndarray, peaks: np.ndarray, fs: float) -> np.ndarray:
    """Pulse-foot (onset) before each systolic peak: local minimum between the
    previous peak and this one. Onset-based IBI is more accurate for PRV than
    peak-to-peak (foot is less morphology-sensitive than the systolic apex)."""
    onsets: list[int] = []
    for i, p in enumerate(peaks):
        start = peaks[i - 1] if i > 0 else max(0, p - int(0.5 * fs))
        seg = x_filt[start:p + 1]
        if len(seg) < 3:
            continue
        onsets.append(start + int(np.argmin(seg)))
    return np.asarray(onsets)


def clean_ibi(ibi: np.ndarray) -> tuple[np.ndarray, int]:
    """Artefact rejection: drop intervals >IBI_MAD_REJECT from the rolling median.
    Motion bursts inject spurious beats; a single bad interval otherwise corrupts
    sdnn/rmssd and the frequency-domain estimate."""
    if len(ibi) < 3:
        return ibi, 0
    med = np.median(ibi)
    keep = np.abs(ibi - med) <= IBI_MAD_REJECT * med
    return ibi[keep], int((~keep).sum())


def ibi_stats(ibi: np.ndarray, n_peaks: int, n_removed: int = 0) -> dict:
    if len(ibi) < 2:
        return {"hr_bpm": None, "rmssd_ms": None, "sdnn_ms": None,
                "n_beats": int(n_peaks), "n_ibi_removed": int(n_removed)}
    return {
        "hr_bpm": round(float(60.0 / np.mean(ibi)), 2),
        "rmssd_ms": (round(float(np.sqrt(np.mean(np.diff(ibi) ** 2)) * 1000), 2)
                     if len(ibi) > 2 else None),
        "sdnn_ms": round(float(np.std(ibi) * 1000), 2),
        "ibi_cv": round(float(np.std(ibi) / np.mean(ibi)), 4),
        "n_beats": int(n_peaks),
        "n_ibi_removed": int(n_removed),
    }


def prv_frequency(onsets: np.ndarray, ibi: np.ndarray,
                  fs_tacho: float = 4.0) -> dict:
    """Frequency-domain PRV: interpolate the IBI tachogram at fs_tacho, Welch PSD,
    integrate VLF/LF/HF. Returns respiration estimate from the HF peak too.
    Needs ≥8 clean beats; otherwise all None (honest abstention)."""
    none = {"prv_vlf_ms2": None, "prv_lf_ms2": None, "prv_hf_ms2": None,
            "prv_lf_hf": None, "resp_hz_prv": None}
    if len(ibi) < 8:
        return none
    t = np.cumsum(ibi) - ibi[0]
    t_u = np.arange(t[0], t[-1], 1.0 / fs_tacho)
    if len(t_u) < 16:
        return none
    tach = np.interp(t_u, t, ibi) * 1000.0          # ms
    tach = tach - np.polyval(np.polyfit(t_u, tach, 1), t_u)  # detrend
    f, p = welch(tach, fs=fs_tacho, nperseg=min(64, len(t_u)))
    def band(lo, hi):
        m = (f >= lo) & (f <= hi)
        return float(np.trapezoid(p[m], f[m])) if m.any() else 0.0
    vlf, lf, hf = band(0.003, 0.04), band(0.04, 0.15), band(0.15, 0.4)
    resp_m = (f >= RESP_BAND_HZ[0]) & (f <= RESP_BAND_HZ[1])
    resp_hz = float(f[resp_m][np.argmax(p[resp_m])]) if resp_m.any() else None
    return {
        "prv_vlf_ms2": round(vlf, 2), "prv_lf_ms2": round(lf, 2),
        "prv_hf_ms2": round(hf, 2),
        "prv_lf_hf": round(lf / hf, 3) if hf > 0 else None,
        "resp_hz_prv": round(resp_hz, 3) if resp_hz is not None else None,
    }


def morphology_features(x: np.ndarray, x_filt: np.ndarray, onsets: np.ndarray,
                        peaks: np.ndarray, fs: float) -> dict:
    """Pulse morphology per window — the vascular-tone family the spec wants for
    downstream modelling: rise time, width at half amplitude, systolic/diastolic
    area ratio, dicrotic-notch reflection index, crest time."""
    out = {"pulse_amp_mean": None, "rise_time_s": None, "pulse_width50_s": None,
           "area_ratio": None, "reflection_index_s": None, "crest_time_s": None}
    if len(onsets) < 3 or len(peaks) < 3:
        return out
    dc = float(np.mean(x))
    amps, rises, widths, area_r, refl, crest = [], [], [], [], [], []
    for p in peaks:
        # this beat's foot = nearest onset ≤ p
        prior = onsets[onsets <= p]
        nxt = onsets[onsets > p]
        if len(prior) == 0 or len(nxt) == 0:
            continue
        o1, o2 = prior[-1], nxt[0]
        if not (o1 <= p < o2):
            continue
        amp = float(x_filt[p] - x_filt[o1])
        if amp <= 0:
            continue
        rises.append((p - o1) / fs)
        amps.append(amp)
        crest.append((p - o1) / max(o2 - o1, 1))
        half = x_filt[o1] + 0.5 * amp
        above = np.flatnonzero(x_filt[o1:o2] >= half)
        if len(above) >= 2:
            widths.append((above[-1] - above[0]) / fs)
        # dicrotic notch = valley between systolic peak and the dicrotic wave.
        # Find the wave first (next significant peak in [0.1, 0.6]·ibi), then the
        # notch is the minimum between them — a bare argmin drifts into the
        # next foot on real waveforms.
        n_lo = p + int(0.10 * (o2 - o1))
        n_hi = min(o2, p + int(0.60 * (o2 - o1)))
        notch = None
        if n_hi - n_lo > 4:
            sub, _ = find_peaks(x_filt[n_lo:n_hi], prominence=0.15 * amp)
            if len(sub):
                dicrotic = n_lo + int(sub[0])
                notch = p + int(np.argmin(x_filt[p:dicrotic]))
            else:
                notch = n_lo + int(np.argmin(x_filt[n_lo:n_hi]))
        if notch is not None:
            refl.append((notch - p) / fs)
            a_sys = float(np.sum(x[o1:notch] - dc))
            a_dia = float(np.sum(x[notch:o2] - dc))
            if a_dia != 0:
                area_r.append(abs(a_sys / a_dia))
    def _m(v):
        return round(float(np.mean(v)), 4) if v else None
    return {"pulse_amp_mean": _m(amps), "rise_time_s": _m(rises),
            "pulse_width50_s": _m(widths), "area_ratio": _m(area_r),
            "reflection_index_s": _m(refl), "crest_time_s": _m(crest)}


def resp_from_envelope(x_filt: np.ndarray, fs: float) -> dict:
    """Respiration rate from PPG amplitude modulation (RIAV): Hilbert envelope →
    lowpass → Welch → dominant frequency in the breathing band."""
    out = {"resp_hz_env": None, "resp_confidence": None}
    n = len(x_filt)
    if n < int(fs * 20):   # need ≥20 s for a 0.1–0.6 Hz peak to be meaningful
        return out
    env = np.abs(hilbert(x_filt - np.mean(x_filt)))
    sos = butter(2, 1.0, btype="low", fs=fs, output="sos")
    env = sosfiltfilt(sos, env, padlen=min(9, n - 1))
    step = max(1, int(fs / 10))
    env = env[::step]
    f, p = welch(env, fs=fs / step, nperseg=min(128, len(env)))
    m = (f >= RESP_BAND_HZ[0]) & (f <= RESP_BAND_HZ[1])
    if not m.any() or p[m].sum() <= 0:
        return out
    pk = int(np.argmax(p[m]))
    conf = float(p[m][pk] / p.sum())          # fraction of total power at peak bin
    return {"resp_hz_env": round(float(f[m][pk]), 3),
            "resp_confidence": round(conf, 3)}


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


def _rail_fraction(x: np.ndarray) -> float:
    """Fraction of samples piled at the ADC rail (saturation/clip detection)."""
    n = len(x)
    if n < 4:
        return 0.0
    rail = float(np.max(np.abs(x - np.median(x))))
    if rail <= 0:
        return 1.0
    hi = float(np.max(x))
    return float(np.mean(x >= hi - 0.005 * rail))


def periodicity(x_filt: np.ndarray, fs: float) -> float:
    """Normalised autocorrelation first-sidelobe height over the HR lag range —
    strong periodicity means a real pulse, not noise."""
    n = len(x_filt)
    if n < int(2 * fs):
        return 0.0
    v = float(np.var(x_filt))
    if v <= 0:
        return 0.0
    ac = np.correlate(x_filt - x_filt.mean(), x_filt - x_filt.mean(),
                      "full")[n - 1:] / (v * n)
    lo, hi = int(fs * MIN_IBI_S), min(int(fs * MAX_IBI_S), n - 1)
    if lo >= hi:
        return 0.0
    return float(np.clip(np.max(ac[lo:hi]), 0.0, 1.0))


def sqi_components(x: np.ndarray, x_filt: np.ndarray, peaks: np.ndarray,
                   fs: float) -> dict:
    n = len(x)
    flat_frac = float(np.mean(np.abs(np.diff(x)) < 1e-9)) if n > 2 else 0.0
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
        "flat_frac": round(flat_frac, 4), "rail_frac": round(_rail_fraction(x), 4),
        "spectral_purity": round(spectral_purity, 4),
        "periodicity": round(periodicity(x_filt, fs), 4),
        "template_r": round(max(template_r, 0.0), 4),
        "perfusion_index": round(perfusion_index(x), 5),
    }


def combine_sqi(c: dict) -> float:
    """Heuristic SQI ∈ [0,1] — multiplicative gates so one catastrophic
    component dominates. Replaced by a learnt model once BUT-PPG-derived
    weights exist (Phase A1)."""
    score = 1.0
    score *= min(1.0, c["spectral_purity"] / 0.55)          # want HR-band dominated
    score *= min(1.0, max(0.0, c["template_r"]) / 0.8)      # beat morphology repeatability
    score *= min(1.0, max(0.05, c["periodicity"] / 0.75))   # autocorrelation sidelobe
    score *= min(1.0, max(0.05, c["perfusion_index"] / 0.01))  # PI < 1% is weak
    if c["flat_frac"] > 0.05:
        score *= 0.3                                        # flatlining
    if c["rail_frac"] > 0.05:
        score *= 0.4                                        # saturating the ADC
    return round(float(np.clip(score, 0.0, 1.0)), 3)


def pulse_template(x_filt: np.ndarray, onsets: np.ndarray, fs: float,
                   half_frac: float = 0.55) -> np.ndarray | None:
    """Median pulse shape aligned on beat onsets — the per-window template a
    matched filter correlates against (recovers beats peak-finding missed)."""
    if len(onsets) < 3:
        return None
    ibi = np.diff(onsets) / fs
    ibi = ibi[(ibi >= MIN_IBI_S) & (ibi <= MAX_IBI_S)]
    if len(ibi) == 0:
        return None
    half = max(3, int(np.median(ibi) * half_frac * fs))
    beats = [x_filt[o - half:o + half] for o in onsets
             if o - half >= 0 and o + half <= len(x_filt)]
    if len(beats) < 2:
        return None
    seg = np.stack(beats)
    seg = (seg - seg.mean(axis=1, keepdims=True))
    template = np.median(seg, axis=0)
    return template - template.mean()


def template_match_peaks(x_filt: np.ndarray, template: np.ndarray,
                         fs: float) -> tuple[np.ndarray, float]:
    """Matched-filter beat detection: correlate the pulse template over the
    signal and find correlation peaks at physiological spacing. Returns
    (peak_indices, mean NCC at detections)."""
    if template is None or len(template) >= len(x_filt):
        return np.array([], dtype=int), 0.0
    t = template[::-1]
    corr = correlate(x_filt, t, mode="same")
    norm = np.sqrt(np.convolve(x_filt ** 2, np.ones(len(t)), "same")
                   * float(np.sum(t ** 2)) + 1e-12)
    ncc = corr / norm                                  # in [-1, 1]
    cand, props = find_peaks(ncc, height=0.5,
                             distance=int(fs * MIN_IBI_S))
    if len(cand) == 0:
        return np.array([], dtype=int), 0.0
    return cand, float(np.mean(props["peak_heights"]))


def process_ppg_window(samples: list[float], fs: float) -> dict:
    """Full v2 chain for one window: peaks → onsets → cleaned IBI stats →
    frequency PRV → morphology → respiration → SQI."""
    x = np.asarray(samples, dtype=float)
    xf = bandpass(x, fs)
    peaks = detect_beats(xf, fs)
    onsets = refine_onsets(xf, peaks, fs)
    ibi_raw = np.diff(onsets) / fs if len(onsets) >= 2 else np.array([])
    ibi_raw = ibi_raw[(ibi_raw >= MIN_IBI_S) & (ibi_raw <= MAX_IBI_S)]
    ibi, n_removed = clean_ibi(ibi_raw)
    feats = ibi_stats(ibi, len(peaks), n_removed)
    # matched-filter detector: counts beats the peak finder missed and gives a
    # correlation-quality read on every detection (tm = template match)
    tmpl = pulse_template(xf, onsets, fs)
    tm_peaks, tm_ncc = template_match_peaks(xf, tmpl, fs)
    feats["tm_n_beats"] = int(len(tm_peaks))
    feats["tm_ncc_mean"] = round(tm_ncc, 3) if len(tm_peaks) else None
    if len(tm_peaks) >= 2:
        tm_ibi = np.diff(tm_peaks) / fs
        tm_ibi = tm_ibi[(tm_ibi >= MIN_IBI_S) & (tm_ibi <= MAX_IBI_S)]
        if len(tm_ibi):
            feats["tm_hr_bpm"] = round(float(60.0 / np.median(tm_ibi)), 2)
            # template recovers >20% more beats → its HR is the better estimate
            if feats.get("hr_bpm") is None or len(tm_peaks) > 1.2 * len(peaks):
                feats["hr_bpm"] = feats["tm_hr_bpm"]
                feats["hr_source"] = "template"
    feats.update(prv_frequency(onsets, ibi))
    feats.update(morphology_features(x, xf, onsets, peaks, fs))
    feats.update(resp_from_envelope(xf, fs))
    sqi = sqi_components(x, xf, peaks, fs)
    sqi["sqi"] = combine_sqi(sqi)
    return {**feats, "sqi": sqi}


def fuse_windows(chans: dict[str, np.ndarray], sqis: dict[str, float]) -> np.ndarray:
    """SQI-weighted z-score fusion of co-windowed PPG channels (docs/06 §5).
    Each channel is z-scored (removes LED-current/wavelength scale), weighted by
    its SQI, and averaged — a dirty channel contributes ~nothing."""
    keys = [k for k in chans if sqis.get(k, 0.0) > 0.05]
    if not keys:
        keys = list(chans)[:1]
    if not keys:
        return np.array([])
    n = min(len(chans[k]) for k in keys)
    acc, wsum = np.zeros(n), 0.0
    for k in keys:
        v = np.asarray(chans[k][:n], dtype=float)
        sd = np.std(v)
        w = max(sqis.get(k, 0.0), 1e-3)
        if sd > 0:
            acc += w * (v - np.mean(v)) / sd
            wsum += w
    return acc / max(wsum, 1e-9)
