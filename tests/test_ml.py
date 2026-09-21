"""ML detectors + evaluation tests."""
import numpy as np

from vitalq.ml.detectors import AnomalyEnsemble, LagForecaster, PCARecon
from vitalq.ml.experiments import auprc, auroc


def _features(n=200, seed=0):
    rng = np.random.default_rng(seed)
    t = np.arange(n)
    return np.stack([np.sin(t / 20) + rng.normal(0, 0.05, n),      # smooth
                     65 + np.cumsum(rng.normal(0, 0.1, n)),         # drifting HR
                     rng.normal(0.7, 0.05, n)], axis=1)


def test_pca_recon_flags_off_manifold():
    X = _features()
    X[:, 2] = X[:, 0] + X[:, 1]        # exact 2D manifold inside 3 features
    det = PCARecon(var=0.95).fit(X)
    normal = det.score(X)
    Xb = X.copy()
    Xb[100:, 2] += 10                  # break the correlation structure
    broken = det.score(Xb)
    assert broken[100:].mean() > normal[:100].mean() * 100


def test_lag_forecaster_flags_temporal_break():
    X = _features()
    det = LagForecaster(lags=3).fit(X)
    normal = det.score(X)
    Xb = X.copy()
    Xb[100] += 8                                      # sudden jump in all feats
    broken = det.score(Xb)
    assert broken[97] > np.median(normal) + 10        # window t=100 scored at idx 97


def test_ensemble_score_parts_and_flag():
    X = _features()
    ens = AnomalyEnsemble().fit(X)
    parts = ens.score_parts(X)
    assert set(parts) == {"iforest", "pca_recon", "lag_fcast", "ensemble"}
    Xb = np.vstack([X, [[0.0, 200.0, 5.0]]])
    p = ens.score_parts(Xb)
    assert p["ensemble"][-1] > np.median(p["ensemble"][:-1]) + 3


def test_auroc_auprc_perfect_and_edge():
    y = np.array([False] * 90 + [True] * 10)
    s = np.concatenate([np.zeros(90), np.ones(10)])
    assert auroc(y, s) == 1.0
    assert auprc(y, s) == 1.0
    assert auroc(np.zeros(10, dtype=bool), np.arange(10)) is None   # one class
