#!/usr/bin/env python3
"""detect.py — NEWS2-partial, qSOFA-proxy, trend-EWS scoring."""

def news2_partial(hr, rr, spo2, temp):
    """NEWS2 subtotal from available vitals (SBP/AVPU/O2-supp missing).
    Published thresholds: RCP NEWS2 (2017). Max available 11 of 17."""
    s = 0
    if rr <= 8: s += 3
    elif rr <= 11: s += 1
    elif rr <= 20: s += 0
    elif rr <= 24: s += 2
    else: s += 3
    if spo2 <= 91: s += 3
    elif spo2 <= 93: s += 2
    elif spo2 <= 95: s += 1
    if temp <= 35.0: s += 3
    elif temp <= 36.0: s += 1
    elif temp <= 38.0: s += 0
    elif temp <= 39.0: s += 1
    else: s += 2
    if hr <= 40: s += 3
    elif hr <= 50: s += 1
    elif hr <= 90: s += 0
    elif hr <= 110: s += 1
    elif hr <= 130: s += 2
    else: s += 3
    return s

def qsofa_proxy(rr):
    """qSOFA = RR>=22 | SBP<=100 | mentation. Only RR measurable -> 0..1 of 3."""
    return int(rr >= 22)

def trend_ews(m):
    """Composite z-score EWS vs healthy-rest baseline (weights from sepsis physiology).
    m: dict hr, rr, spo2, tcore, rmssd, pi, eda."""
    import math
    z = 0.0
    z += 0.8 * max(0, (m['hr'] - 90) / 15)
    z += 1.2 * max(0, (m['rr'] - 20) / 4)
    z += 1.0 * max(0, (abs(m['tcore'] - 37.0) - 0.5) / 0.8)
    z += 1.5 * max(0, (96 - m['spo2']) / 2)
    if m.get('rmssd') == m.get('rmssd'):
        z += 1.0 * max(0, (25 - m['rmssd']) / 12)
    z += 1.0 * max(0, (1.5 - m['pi']) / 1.0)
    z += 0.4 * max(0, (m['eda'] - 12) / 10)
    return z
