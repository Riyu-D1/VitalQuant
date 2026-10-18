#!/usr/bin/env python3
"""hw.py — fix_v7 analog-chain models (parameters from netlist + ngspice).

ECG:   J5 -> R32/33 51k -> ECG_P/N -> D1/D2 TVS (leakage only) ->
       C50/51 100n AC coupling -> ADS1292R IN1P/N. ngspice: HP 0.467 Hz,
       LP 1.348 kHz (51k x 2.2n C52/53 EMI). PGA4 -> FS +/-605mV, 24-bit,
       ~4 uVrms input noise. Resp channel: RESP_MOD 40.2k carrier inj.
EDA:   AD5940 CE -> R39 1k -> C54 15n -> R76 51k -> J6.1 ; SE -> C55 470n
       -> R77 51k -> J6.3. AC excitation >=500 Hz required (I43).
BioZ:  AD5940 F/S paths via R80-83 51k.
PPG:   AFE4900 TX (SFH7072 660nm red + 940nm IR, ~20mA) + PD TIA 16-18bit ENOB;
       MAX86141 second optical path (backup).
TMP117 skin (+~1C board offset, documented I21); MLX90632 IR; LSM6DSV80X IMU.
"""
import numpy as np
from scipy.signal import butter, filtfilt, lfilter

def _hp(sig, fc, fs):
    b, a = butter(1, fc / (fs / 2), 'high'); return filtfilt(b, a, sig)

def _lp(sig, fc, fs, order=2):
    b, a = butter(order, fc / (fs / 2), 'low'); return filtfilt(b, a, sig)

# ---------------- ECG / ADS1292R channel 1 ----------------
ADS_NOISE_UV = 4.0e-6 * 1e3          # 4 uVrms -> mV
def ecg_front(pat, fs):
    """Return measured ECG (mV, post-ADC) incl. artifacts."""
    t, rng, mot = pat['t'], pat['rng'], pat['motion']
    truth = pat['truth']
    sig = pat['ecg'].copy()
    # respiration baseline wander (electrode motion, ~0.3mV/resp)
    sig += 0.15 * pat['resp']
    # electrode half-cell offset drifts (AC-coupled -> attenuated)
    off = np.sum(rng.uniform(-0.25, 0.25, 2)) * 1e-3 * np.sin(2 * np.pi * 0.01 * t)
    sig += off
    # 50 Hz mains residue after CMRR ~ -80dB diff-mode
    sig += rng.uniform(0.02, 0.1) * np.sin(2 * np.pi * 50 * t + rng.uniform(0, 6))
    # EMG / muscle noise (higher in motion)
    sig += rng.standard_normal(len(t)) * (0.005 + 0.05 * mot)
    # motion artifact at electrodes (lead-off wiggle)
    sig += mot * rng.uniform(0.3, 1.2) * np.sin(2 * np.pi * 0.5 * t)
    # chain: HP 0.467 Hz, LP 1.348 kHz (measurement bandwidth here < fs/2)
    sig = _hp(sig, 0.467, fs)
    sig = _lp(sig, 120, fs)                 # digital BW 120Hz
    # quantize: PGA4, Vref 2.42 -> LSB = 4.84/2^24 V ~ 0.29uV ~ negligible
    sig += rng.standard_normal(len(t)) * ADS_NOISE_UV
    return sig

# ---------------- ADS1292R respiration (impedance pneumography) ----------------
def resp_channel(pat, fs):
    """Thoracic impedance via RESP_MOD carrier -> demodulated Z."""
    rng, mot = pat['rng'], pat['motion']
    z = 400 + 1.5 * pat['resp'] + 0.3 * rng.standard_normal(len(pat['resp']))
    z += mot * 20                                        # motion swamps Z
    dem = _lp(z, 0.6, fs)                                # demod LPF
    return (dem - dem.mean()) / dem.std()                # normalized

def edr_signal(pat, ecg_meas, fs):
    """ECG-derived respiration: R-peak amplitude modulation (axis rotation)."""
    return pat['resp'] * 1.0 + pat['motion'] * 2.0 + \
           pat['rng'].standard_normal(len(pat['resp'])) * 0.15

# ---------------- PPG / AFE4900 + SFH7072 (+ MAX86141 backup) ----------------
def ppg_channels(pat):
    """Return red & IR PPG (AC/DC pairs) measured at PD.
    SpO2 coupling: R_true=(110-SpO2)/25 calibration model.
    AFE4900: ~16-bit ENOB, LED ~20mA, TIA noise floor; PI<0.5% -> SNR collapses."""
    rng, tp = pat['rng'], pat['tp']
    mot = np.interp(tp, pat['t'], pat['motion'])
    pi, spo2 = pat['truth']['pi'], pat['truth']['spo2']
    ac, dc = pat['ppg_ac'], pat['ppg_dc']
    r_true = (110.0 - spo2) / 25.0
    # wavelength dependence
    ac_r = ac * r_true
    ac_i = ac * 1.0
    # perfusion-dependent noise: photon shot + TIA noise floor
    tia_n = 0.0004                                      # AC noise floor
    ac_r += rng.standard_normal(len(tp)) * (tia_n / max(pi, 0.1) * 0.4)
    ac_i += rng.standard_normal(len(tp)) * (tia_n / max(pi, 0.1) * 0.4)
    # motion artifact is large on PPG
    ac_r += mot[:len(tp)] * 0.02 * rng.standard_normal(len(tp))
    ac_i += mot[:len(tp)] * 0.02 * rng.standard_normal(len(tp))
    # respiration modulates baseline
    resp_p = np.interp(tp, pat['t'], pat['resp'])
    ac_r += resp_p * 0.03 * ac_r.std() * 3
    ac_i += resp_p * 0.03 * ac_i.std() * 3
    dc_r = dc * 1.0 + 0.001 * rng.standard_normal(len(tp))
    dc_i = dc * 1.0 + 0.001 * rng.standard_normal(len(tp))
    return ac_r, dc_r, ac_i, dc_i

# ---------------- Temperature ----------------
def temps(pat, rng):
    """TMP117 skin (with documented +1C board offset), MLX90632 IR."""
    tcore = pat['truth']['tcore']
    skin = tcore - rng.uniform(0.3, 0.8)                 # wrist skin < core
    tmp117 = skin + 1.0 + rng.normal(0, 0.05)            # +1C board offset (I21)
    mlx = tcore + rng.normal(0, 0.3)                     # IR field noise/emissivity
    return tmp117, mlx, skin

# ---------------- EDA / AD5940 ----------------
def eda_read(pat, rng):
    """Tonic skin conductance uS via >=500Hz excitation; sweat/motion couple."""
    scl = pat['truth']['eda']
    return scl * (1 + rng.normal(0, 0.03)) + pat['truth']['motion'] * rng.uniform(0, 2)

# ---------------- BioZ ----------------
def bioz_read(pat, rng):
    """Impedance index (trend): sepsis -> fluid shift lowers Z."""
    return pat['truth']['bioz'] * (1 + rng.normal(0, 0.01))
