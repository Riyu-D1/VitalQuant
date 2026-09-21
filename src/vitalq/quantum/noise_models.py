"""Composable noise models (docs/08 §2).

Every model maps a signal amplitude array (photon counts) to a per-sample noise
std. All parameters are explicit and named in the spec — no hidden constants.

Physics:
- shot noise: std = k·√N (Poisson).
- ideal squeezing r: variance × e^{-2r} → std × e^{-r}. Only valid shot-noise-
  limited AND lossless.
- detection loss η: measured variance = η·e^{-2r}·k²N + (1−η)·k²N (vacuum mixes
  back in) → the squeezing advantage dies linearly in η.
- non-quantum floors (electronics/dark, tissue/perfusion, ambient leakage) are
  additive in quadrature and are exactly the terms that decide whether the
  quantum argument matters for a wearable.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol

import numpy as np


class NoiseModel(Protocol):
    """Signal → per-sample noise std. `signal` is photon counts (>=0)."""

    def std(self, signal: np.ndarray) -> np.ndarray: ...

    def label(self) -> str: ...


@dataclass
class ShotNoise:
    """Classical coherent-state limit: std = k·√N."""
    k: float = 1.0

    def std(self, signal: np.ndarray) -> np.ndarray:
        return self.k * np.sqrt(np.maximum(signal, 0.0))

    def label(self) -> str:
        return "shot"


@dataclass
class SqueezedShotNoise:
    """Ideal lossless squeezing: std = k·√N·e^{-r}. r=0 reduces to ShotNoise."""
    r: float
    k: float = 1.0

    def std(self, signal: np.ndarray) -> np.ndarray:
        return self.k * np.sqrt(np.maximum(signal, 0.0)) * np.exp(-self.r)

    def label(self) -> str:
        return f"squeezed(r={self.r})"


@dataclass
class LossySqueezedNoise:
    """Squeezing through detection efficiency η:
    variance = k²·N·(η·e^{-2r} + (1−η)). η=1 → ideal squeezing; η=0 → shot noise
    of the un-squeezed vacuum that leaks back in."""
    r: float
    eta: float
    k: float = 1.0

    def std(self, signal: np.ndarray) -> np.ndarray:
        n = np.maximum(signal, 0.0)
        var = self.k ** 2 * n * (self.eta * np.exp(-2 * self.r) + (1 - self.eta))
        return np.sqrt(var)

    def label(self) -> str:
        return f"squeezed(r={self.r},eta={self.eta})"


@dataclass
class AdditiveFloor:
    """Electronic/dark-count floor: adds floor_rms in quadrature to `inner`."""
    inner: NoiseModel
    floor_rms: float

    def std(self, signal: np.ndarray) -> np.ndarray:
        return np.sqrt(self.inner.std(signal) ** 2 + self.floor_rms ** 2)

    def label(self) -> str:
        return f"{self.inner.label()}+floor({self.floor_rms})"


@dataclass
class TissueNoise:
    """Perfusion/motion-proportional noise — the dominant term in any real
    wearable optical measurement: std += sigma_rel·signal in quadrature."""
    inner: NoiseModel
    sigma_rel: float

    def std(self, signal: np.ndarray) -> np.ndarray:
        return np.sqrt(self.inner.std(signal) ** 2 +
                       (self.sigma_rel * np.asarray(signal, dtype=float)) ** 2)

    def label(self) -> str:
        return f"{self.inner.label()}+tissue({self.sigma_rel})"


@dataclass
class AmbientLeakage:
    """Emitter-independent ambient photon leakage: constant additive floor."""
    inner: NoiseModel
    level: float

    def std(self, signal: np.ndarray) -> np.ndarray:
        return np.sqrt(self.inner.std(signal) ** 2 + self.level ** 2)

    def label(self) -> str:
        return f"{self.inner.label()}+ambient({self.level})"
