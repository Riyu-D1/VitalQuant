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
