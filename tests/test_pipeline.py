"""DSP correctness: synth PPG → recovered HR within tolerance; corruption → low SQI."""

import numpy as np

from vitalq.processing import ppg


def _ppg(n=2000, fs=200.0, hr=65.0, noise=25.0, seed=1):
    rng = np.random.default_rng(seed)
    t = np.arange(n) / fs
    ibi = 60.0 / hr
    phase = (t % ibi) / ibi
    pulse = np.exp(-0.5 * ((phase - 0.10) / 0.045) ** 2) + \
        0.35 * np.exp(-0.5 * ((phase - 0.38) / 0.07) ** 2)
    return 40_000 + pulse * 2400 + rng.normal(0, noise, n)


def test_hr_recovery():
    x = _ppg()
    res = ppg.process_ppg_window(x.tolist(), 200.0)
    assert res["hr_bpm"] is not None
    assert abs(res["hr_bpm"] - 65) < 3.0
    assert res["n_beats"] >= 9           # 65 bpm ≈ 11 beats per 10 s window
    assert res["sqi"]["sqi"] > 0.5       # clean synthetic → decent quality


def test_flatline_low_sqi():
    res = ppg.process_ppg_window([0.0] * 2000, 200.0)
    assert res["sqi"]["sqi"] < 0.3       # clipped/flat → flagged


def test_motion_burst_lowers_sqi():
    clean = ppg.process_ppg_window(_ppg(seed=1).tolist(), 200.0)
    rng = np.random.default_rng(2)
    burst = _ppg(seed=1) + rng.normal(0, 2500, 2000)
    dirty = ppg.process_ppg_window(burst.tolist(), 200.0)
    assert dirty["sqi"]["sqi"] < clean["sqi"]["sqi"]


def test_r_ratio_and_unscaled_spo2():
    red, ir = _ppg(seed=3), _ppg(seed=4) * 1.1
    r = ppg.r_ratio(red, ir)
    assert r is not None and r > 0
    s = ppg.spo2_uncalibrated(r)
    assert s is not None


def test_morphology_features():
    res = ppg.process_ppg_window(_ppg().tolist(), 200.0)
    # synth: systolic peak at phase .10, dicrotic at .38 → rise ≈ 0.09 s, refl ≈ 0.26 s
    assert res["rise_time_s"] is not None and 0.05 < res["rise_time_s"] < 0.25
    assert res["reflection_index_s"] is not None and \
        0.15 < res["reflection_index_s"] < 0.4
    assert res["pulse_amp_mean"] is not None and res["pulse_amp_mean"] > 0
    assert res["ibi_cv"] is not None and res["ibi_cv"] < 0.05   # clean synthetic is regular


def test_frequency_prv_and_respiration():
    # 60 s trace with 0.22 Hz amplitude modulation → session-scale features
    fs, hr, dur = 200.0, 65.0, 60
    rng = np.random.default_rng(9)
    t = np.arange(int(fs * dur)) / fs
    ibi = 60.0 / hr
    ph = (t % ibi) / ibi
    pulse = np.exp(-0.5 * ((ph - 0.10) / 0.045) ** 2) + \
        0.35 * np.exp(-0.5 * ((ph - 0.38) / 0.07) ** 2)
    x = 40_000 + pulse * 2400 * (1 + 0.10 * np.sin(2 * np.pi * 0.22 * t)) \
        + rng.normal(0, 25, len(t))
    xf = ppg.bandpass(x, fs)
    peaks = ppg.detect_beats(xf, fs)
    onsets = ppg.refine_onsets(xf, peaks, fs)
    ibi, _ = ppg.clean_ibi(np.diff(onsets) / fs)
    prv = ppg.prv_frequency(onsets, ibi)
    assert prv["prv_hf_ms2"] is not None
    resp = ppg.resp_from_envelope(xf, fs)
    assert resp["resp_hz_env"] is not None
    assert abs(resp["resp_hz_env"] - 0.22) < 0.08


def test_ibi_artefact_rejection():
    ibi = np.array([0.92] * 10 + [0.30])       # one spurious short interval
    clean, n_removed = ppg.clean_ibi(ibi)
    assert n_removed == 1 and len(clean) == 10


def test_fusion_drops_dirty_channel():
    fs, n = 200.0, 2000
    good = _ppg(n, fs)
    flat = np.zeros(n)
    fused = ppg.fuse_windows({"ppg.red": good, "ppg.ir": flat},
                             {"ppg.red": 0.9, "ppg.ir": 0.0})
    res = ppg.process_ppg_window(fused.tolist(), fs)
    assert abs(res["hr_bpm"] - 65) < 3.0      # flat channel contributes nothing


def test_contact_hysteresis():
    from vitalq.processing import scalars as sc
    qs = [(t, q) for t, q in enumerate([1.0, 1.0, 0.1, 1.0, 0.1, 0.1, 0.9, 0.9])]
    skin = sc.contact_hysteresis(qs, enter_off=2, exit_on=2)
    # single dip (idx2) does NOT drop state; two lows (4,5) drop it; two highs restore
    assert skin[:3] == [True, True, True]
    assert not skin[5] and skin[7]
