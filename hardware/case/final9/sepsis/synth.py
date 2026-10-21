#!/usr/bin/env python3
"""synth.py — synthetic physiology for VitalQ sepsis study.

Published ranges cited:
- Sepsis-3 / qSOFA (Singer 2016 JAMA 315(8)): RR>=22, SBP<=100, mentation
- NEWS2 (RCP 2017): RR/SpO2/HR/temp/SBP thresholds
- Surviving Sepsis Campaign 2021: lactate>2, MAP<65, vasopressors
- Septic shock vitals: HR>100-120, RR>=22-28, T>38.3 or <36.0
- HRV in sepsis: Chen & Kuo 2007; de Castilho 2018 (RMSSD/SDNN drop)
- Perfusion index PI: Lima 2009 — <1.4% abnormal; ~0.2% shock
- EDA/EDA-in-sepsis literature: elevated sympathetic tone
"""
import numpy as np

FS = 500          # ADC sample rate (ADS1292R 500 sps)
FS_PPG = 200      # AFE4900 frame rate
FS_AUX = 26       # temp/EDA/BioZ/IMU-ish reporting rate

# ---------------- physiology models ----------------

def mcsharry_ecg(t, rr_series, morph_scale=1.0):
    """McSharry/Clifford dynamical model -> ECG in mV.
    rr_series: array of beat times."""
    x = np.zeros_like(t); z = np.zeros_like(t)
    th = np.zeros_like(t)
    # standard ecgsyn params
    ai = np.array([1.2, -5.0, 30.0, -7.5, 0.75]) * morph_scale
    bi = np.array([0.25, 0.1, 0.1, 0.1, 0.4])
    ti0 = np.array([-70, -15, 0, 15, 100]) * np.pi / 180.0
    # build phase from beat times
    beats = np.asarray(rr_series)
    for i, tb in enumerate(beats):
        if i == 0: continue
        t0 = beats[i - 1]
        mask = (t >= t0) & (t < tb)
        seg = t[mask] - t0
        th[mask] = 2 * np.pi * seg / (tb - t0) - np.pi
    th[t >= beats[-1]] = np.pi
    for a, b, tc in zip(ai, bi, ti0):
        dth = (th - tc + np.pi) % (2 * np.pi) - np.pi
        z += a * dth * np.exp(-dth ** 2 / (2 * b ** 2))
    return z * 0.6  # ~1.2mV QRS amplitude -> ~0.7mV peak

def rr_times(duration, hr_bpm, rmssd_ms, rng):
    """Beat times with HRV: RSA (resp-freq modulation) + 1/f-ish noise.
    rmssd_ms controls variability amplitude."""
    times = [0.0]
    mean_rr = 60.0 / hr_bpm
    while times[-1] < duration + 2:
        t = times[-1]
        # variability as fraction of mean RR
        frac = rmssd_ms / 1000.0 / mean_rr
        dev = mean_rr * frac * (rng.standard_normal() * 0.7 +
                                0.6 * np.sin(2 * np.pi * 0.25 * t) +
                                0.4 * np.sin(2 * np.pi * 0.1 * t + 1.0))
        times.append(t + mean_rr + dev)
    return np.array(times)

def ppg_wave(t, rr_series, pi_frac, rng):
    """PPG AC component at PD. pi_frac = AC/DC perfusion index (0.002=0.2%).
    Returns AC and DC."""
    sig = np.zeros_like(t)
    beats = np.asarray(rr_series)
    for i in range(1, len(beats) - 1):
        t0, t1 = beats[i - 1], beats[i + 1]
        rr = (t1 - t0) / 2
        tb = beats[i]
        dicro = tb + 0.3 * rr
        sig += np.exp(-0.5 * ((t - tb) / (0.07 * rr)) ** 2)
        sig += 0.4 * np.exp(-0.5 * ((t - dicro) / (0.1 * rr)) ** 2)
    sig = np.clip(sig, 0, 1.2)
    dc = 1.0
    rms = np.sqrt(np.mean(sig ** 2))
    ac = sig / max(rms, 1e-9) * pi_frac * dc * (1 + rng.standard_normal(len(t)) * 0.002)
    return ac, dc

def motion_signal(t, level, rng, fs):
    """IMU accel rms profile. level 0=still,1=rest micro,2=walk,3=burst."""
    base = np.abs(rng.standard_normal(len(t))) * 0.01 * level
    bursts = np.zeros(len(t))
    nburst = int(level * duration_of(t) / 8)
    for _ in range(nburst):
        c = rng.integers(0, len(t)); w = rng.integers(fs // 2, 2 * fs)
        s = rng.uniform(0.3, 1.0) * level
        w0, w1 = max(0, c - w // 2), min(len(t), c + w // 2)
        bursts[w0:w1] += s * np.hanning(w1 - w0)
    return base + bursts

def duration_of(t): return t[-1] - t[0]

# ---------------- case definitions (published ranges) ----------------

CASES = {
 'healthy_rest':  dict(hr=(60,80),  rr=(12,16), spo2=(97,99),  tcore=(36.5,37.2),
                      rmssd=(30,60), pi=(2.0,5.0), eda=(2,8),   bioz=(0.9,1.0),
                      motion=0.3, sbp=(115,125), avpu=0),
 'healthy_exercise': dict(hr=(130,160), rr=(24,38), spo2=(97,99), tcore=(37.0,38.0),
                      rmssd=(5,15),  pi=(4.0,8.0), eda=(10,25), bioz=(0.85,0.95),
                      motion=2.0, sbp=(140,170), avpu=0),
 'early_sepsis':  dict(hr=(95,110), rr=(22,26), spo2=(94,95),  tcore=(38.3,38.9),
                      rmssd=(8,18),  pi=(0.5,1.4), eda=(8,20),  bioz=(0.8,0.9),
                      motion=0.4, sbp=(100,115), avpu=0),
 'septic_shock':  dict(hr=(120,150), rr=(28,40), spo2=(88,92), tcore=(38.9,40.2),
                      rmssd=(3,8),   pi=(0.15,0.4), eda=(15,35), bioz=(0.7,0.85),
                      motion=0.2, sbp=(70,99),  avpu=1),
 'septic_shock_hypothermic': dict(hr=(110,140), rr=(26,36), spo2=(88,92),
                      tcore=(35.0,35.8), rmssd=(3,8), pi=(0.15,0.4), eda=(15,35),
                      bioz=(0.7,0.85), motion=0.2, sbp=(70,99), avpu=1),
 'fever_no_sepsis': dict(hr=(88,108), rr=(15,20), spo2=(96,99), tcore=(38.0,38.9),
                      rmssd=(15,35), pi=(1.5,4.0), eda=(5,15),  bioz=(0.9,1.0),
                      motion=0.4, sbp=(110,135), avpu=0),
 'motion_artifact': dict(hr=(65,85), rr=(12,18), spo2=(96,99), tcore=(36.5,37.3),
                      rmssd=(25,55), pi=(1.5,4.0), eda=(3,12),  bioz=(0.9,1.0),
                      motion=3.0, sbp=(110,130), avpu=0),
}
SEPTIC = {'early_sepsis', 'septic_shock', 'septic_shock_hypothermic'}

def make_patient(case, rng, duration=120.0):
    """Ground-truth physiology dict for one patient."""
    p = CASES[case]
    u = lambda k: rng.uniform(*p[k])
    hr, rr_f = u('hr'), u('rr') / 60.0
    spo2, tcore = u('spo2'), u('tcore')
    rmssd, pi = u('rmssd'), u('pi')
    t = np.arange(0, duration, 1 / FS)
    beats = rr_times(duration, hr, rmssd, rng)
    ecg = mcsharry_ecg(t, beats)
    # respiration: thoracic impedance + EDR coupling
    resp = np.sin(2 * np.pi * rr_f * t + rng.uniform(0, 6))
    mot = motion_signal(t, p['motion'], rng, FS)
    tp = np.arange(0, duration, 1 / FS_PPG)
    beats_p = beats[(beats >= 0) & (beats < duration)]
    ac, dc = ppg_wave(tp, beats_p, pi / 100.0, rng)
    # realized ground truth (what the generator actually produced)
    rrs = np.diff(beats)
    rrs = rrs[(rrs > 0.3) & (rrs < 2.0)]
    hr_r = 60.0 / np.mean(rrs)
    rmssd_r = np.sqrt(np.mean(np.diff(rrs) ** 2)) * 1000
    return dict(case=case, t=t, beats=beats, ecg=ecg, resp=resp,
                resp_hz=rr_f, tp=tp, ppg_ac=ac, ppg_dc=dc,
                truth=dict(hr=hr_r, rr=rr_f * 60, spo2=spo2, tcore=tcore,
                           rmssd=rmssd_r, pi=pi, eda=u('eda'), bioz=u('bioz'),
                           sbp=u('sbp'), avpu=p['avpu'], motion=p['motion']),
                motion=mot, rng=rng)
