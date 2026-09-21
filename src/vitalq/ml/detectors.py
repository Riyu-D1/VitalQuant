"""Anomaly detectors + ensemble (Phase-C, docs/07).

Three complementary detectors on the same scaled feature matrix:
- IsolationForest (in baselines.py) — density isolation
- PCARecon — reconstruction error: catches anomalies in the *correlation
  structure* that pointwise isolation misses
- LagForecaster — per-feature ridge on the last `lags` windows: catches
  temporal breaks (sudden shifts) even when the value itself is in-range

`AnomalyEnsemble` normalises each detector's score by its train median/IQR and
takes the max — a window is anomalous if ANY detector fires, which is the
honest behaviour for 'experimental anomaly score' flagging.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from sklearn.decomposition import PCA
from sklearn.ensemble import IsolationForest


class PCARecon:
    """Reconstruction-error detector; keeps components covering `var` variance."""
    def __init__(self, var: float = 0.95, max_components: int = 10):
        self.var = var
        self.max_components = max_components

    def fit(self, X: np.ndarray) -> PCARecon:
        n = min(self.max_components, X.shape[1], X.shape[0])
        probe = PCA(n_components=n, random_state=0).fit(X)
        k = int(np.searchsorted(np.cumsum(probe.explained_variance_ratio_),
                                self.var) + 1)
        self.pca_ = PCA(n_components=max(1, min(k, n)), random_state=0).fit(X)
        return self

    def score(self, X: np.ndarray) -> np.ndarray:
        rec = self.pca_.inverse_transform(self.pca_.transform(X))
        return np.sum((X - rec) ** 2, axis=1)


class LagForecaster:
    """Ridge forecaster: predict each feature from its own previous `lags`
    values plus the other features' current values. Score = residual norm."""
    def __init__(self, lags: int = 3, alpha: float = 1.0):
        self.lags = lags
        self.alpha = alpha

    def _design(self, X: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        """Rows t >= lags: lagged history [x_{t-1}..x_{t-lags}] → target x_t."""
        D, Y = [], []
        for t in range(self.lags, len(X)):
            D.append(X[t - self.lags:t].ravel())
            Y.append(X[t])
        return np.asarray(D), np.asarray(Y)

    def fit(self, X: np.ndarray) -> LagForecaster:
        D, Y = self._design(X)
        G = D.T @ D + self.alpha * np.eye(D.shape[1])
        self.coef_ = np.linalg.solve(G, D.T @ Y)   # (lags·nf, nf) multi-target
        return self

    def score(self, X: np.ndarray) -> np.ndarray:
        D, Y = self._design(X)
        if len(D) == 0:
            return np.zeros(0)
        pred = D @ self.coef_
        return np.sum((Y - pred) ** 2, axis=1)


@dataclass
class AnomalyEnsemble:
    """Max of median/IQR-normalised detector scores."""
    detectors: dict = field(default_factory=dict)
    scales: dict = field(default_factory=dict)   # name -> (median, iqr)

    def fit(self, X: np.ndarray, contamination: float = 0.05) -> AnomalyEnsemble:
        self.detectors = {
            "iforest": IsolationForest(n_estimators=100,
                                       contamination=contamination,
                                       random_state=0).fit(X),
            "pca_recon": PCARecon().fit(X),
            "lag_fcast": LagForecaster(lags=min(3, max(1, len(X) // 4))).fit(X),
        }
        for name in self.detectors:
            s = self._raw(name, X)
            med, q1, q3 = np.median(s), *np.percentile(s, [25, 75])
            self.scales[name] = (float(med), float(max(q3 - q1, 1e-9)))
        return self

    def _raw(self, name: str, X: np.ndarray) -> np.ndarray:
        det = self.detectors[name]
        if name == "iforest":
            return -det.score_samples(X)
        return det.score(X)

    def score_parts(self, X: np.ndarray) -> dict[str, np.ndarray]:
        """Normalised score per detector + 'ensemble' = max."""
        parts = {}
        for name in self.detectors:
            s = self._raw(name, X)
            med, iqr = self.scales[name]
            if len(s) < len(X):     # lag_fcast can't score the first `lags` rows
                s = np.concatenate([np.full(len(X) - len(s), np.median(s)), s])
            parts[name] = (s - med) / iqr
        parts["ensemble"] = np.max(np.stack(list(parts.values())), axis=0)
        return parts
