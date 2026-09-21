"""Scalar/channel chains: temperature, motion, contact, spectral (docs/06 §2)."""

from __future__ import annotations

import numpy as np


def temperature_features(vals: np.ndarray, times_s: np.ndarray,
                         baseline_med: float | None = None,
                         ambient_vals: np.ndarray | None = None) -> dict:
    """temp_c mean, slope (°C/min), std, slew rate, ambient delta,
    deviation from personal baseline."""
    if len(vals) == 0:
        return {}
    out = {
        "temp_c": round(float(np.mean(vals)), 3),
        "temp_std": round(float(np.std(vals)), 4),
    }
    if len(vals) > 2 and times_s[-1] > times_s[0]:
        out["temp_slope_cpm"] = round(
            float(np.polyfit(times_s, vals, 1)[0] * 60), 5)
        # slew: max absolute dT/dt between consecutive samples (°C/min)
        dt = np.diff(times_s)
        dt[dt == 0] = np.nan
        out["temp_slew_cpm"] = round(
            float(np.nanmax(np.abs(np.diff(vals) / dt)) * 60), 4)
    if ambient_vals is not None and len(ambient_vals):
        # skin-site vs ambient: physiologically meaningful gradient
        out["temp_minus_ambient"] = round(
            float(np.mean(vals) - np.mean(ambient_vals)), 3)
    if baseline_med is not None:
        out["temp_baseline_dev"] = round(float(np.mean(vals) - baseline_med), 3)
    return out


def env_features(vals_map: dict[str, np.ndarray]) -> dict:
    """Environment-channel extras: humidity/pressure drift per window."""
    out: dict[str, float] = {}
    rh = vals_map.get("env.humidity")
    if rh is not None and len(rh) > 1:
        out["env_rh_std"] = round(float(np.std(rh)), 3)
    p = vals_map.get("env.pressure")
    if p is not None and len(p) > 2:
        out["env_pressure_slope_hpa_min"] = round(
            float(np.polyfit(np.arange(len(p)), p, 1)[0]), 5)
    return out


def contact_quality(levels: np.ndarray) -> float:
    """FSR level → ordinal contact quality. Bands are per-enclosure — these are
    prototype defaults from config; seated ~1500 counts ≈ good optical contact."""
    if len(levels) == 0:
        return 0.0
    m = float(np.mean(levels))
    seated_frac = float(np.mean((levels > 800) & (levels < 3000)))
    if m < 50:
        return 0.0                       # device off-skin
    return round(min(1.0, seated_frac * (m / 1500.0)), 3)


def contact_hysteresis(per_window_q: list[tuple[float, float]],
                       enter_off: int = 2, exit_on: int = 2) -> list[bool]:
    """Temporal hysteresis on per-window contact quality: a window is 'on-skin'
    unless <enter_off> consecutive sub-threshold windows demote it, and needs
    <exit_on> consecutive above-threshold windows to promote back. Kills the
    flicker a raw per-window threshold produces at band edges."""
    state = True
    low_run = high_run = 0
    out: list[bool] = []
    for _wkey, q in per_window_q:
        if q < 0.3:
            low_run, high_run = low_run + 1, 0
        elif q > 0.6:
            high_run, low_run = high_run + 1, 0
        else:
            low_run = high_run = 0
        if state and low_run >= enter_off:
            state = False
        elif not state and high_run >= exit_on:
            state = True
        out.append(state)
    return out


def motion_score(accel_xyz: np.ndarray, fs: float) -> float:
    """Fraction of accel band-energy inside the PPG band 0.5–4 Hz + jerk level.
    accel_xyz shape (n, 3)."""
    if len(accel_xyz) < 8:
        return 0.0
    mag = np.linalg.norm(accel_xyz, axis=1)
    f = np.fft.rfft(mag - mag.mean())
    power = np.abs(f) ** 2
    freqs = np.fft.rfftfreq(len(mag), 1 / fs)
    band_frac = float(power[(freqs >= 0.5) & (freqs <= 4.0)].sum()
                      / max(power.sum(), 1e-9))
    jerk = float(np.mean(np.abs(np.diff(mag))))
    return round(float(np.clip(0.6 * band_frac + 4.0 * jerk, 0.0, 1.0)), 4)


def spectral_features(frame_channels: list[dict], dark_channels: dict | None) -> dict:
    """Dark-subtracted, CLEAR-normalised reflectance + ratios (docs/06 §2)."""
    if not frame_channels:
        return {}
    keys = [k for k in frame_channels[0] if k.startswith("f") or k in ("nir",)]
    dark = dark_channels or {}
    means = {k: float(np.mean([fr.get(k, np.nan) for fr in frame_channels]))
             for k in keys}
    dark_sub = {k: max(means[k] - float(dark.get(k, 0.0)), 0.0) for k in keys}
    ref = float(np.mean([fr.get("clear", np.nan) for fr in frame_channels])
                - float(dark.get("clear", 0.0)))
    ref = ref if ref > 0 else np.nan
    out = {f"spec_{k}": round(dark_sub[k] / ref, 5) for k in keys}
    if ref and not np.isnan(ref):
        out["spec_ratio_f5_f3"] = round(
            (dark_sub.get("f5", 0) / max(dark_sub.get("f3", 1e-9), 1e-9)), 4)
        out["spec_ratio_nir_vis"] = round(
            dark_sub.get("nir", 0) / max(np.mean(list(dark_sub.values())[:8]), 1e-9), 4)
    return out
