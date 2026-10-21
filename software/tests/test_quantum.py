"""Quantum-sim tests: the module must reproduce the physics it's built on,
and the honest result — advantage vanishing under non-quantum noise."""
import numpy as np

from vitalq.quantum.experiment import ExperimentMatrix, run
from vitalq.quantum.noise_models import (
    AdditiveFloor,
    AmbientLeakage,
    LossySqueezedNoise,
    ShotNoise,
    SqueezedShotNoise,
    TissueNoise,
)
from vitalq.quantum.signal_models import RamanToyModel


def test_shot_noise_sqrt_scaling():
    n = np.array([100.0, 400.0, 900.0])
    s = ShotNoise().std(n)
    assert np.allclose(s, [10.0, 20.0, 30.0])


def test_squeezing_scales_std_by_e_minus_r():
    n = np.array([400.0])
    assert np.isclose(SqueezedShotNoise(r=0.8).std(n)[0],
                      20.0 * np.exp(-0.8))


def test_loss_interpolates_to_vacuum():
    n = np.array([400.0])
    # eta=1 -> ideal squeezing; eta=0 -> unsqueezed vacuum = shot noise
    assert np.isclose(LossySqueezedNoise(0.8, 1.0).std(n)[0],
                      SqueezedShotNoise(0.8).std(n)[0])
    assert np.isclose(LossySqueezedNoise(0.8, 0.0).std(n)[0],
                      ShotNoise().std(n)[0])


def test_floors_add_in_quadrature():
    n = np.array([0.0])
    floored = AdditiveFloor(ShotNoise(), 5.0).std(n)[0]
    assert floored == 5.0
    tissue = TissueNoise(ShotNoise(), 0.01).std(np.array([1000.0]))[0]
    assert np.isclose(tissue, np.sqrt(1000 * 1.0 + (0.01 * 1000) ** 2)
                      / np.sqrt(1), rtol=0)  # sqrt(shot²+tissue²)
    amb = AmbientLeakage(ShotNoise(), 7.0).std(n)[0]
    assert amb == 7.0


def test_raman_toy_shifts_linearly_and_is_tagged():
    m0, m1 = RamanToyModel(0.0), RamanToyModel(0.5)
    assert m1.peak().center - m0.peak().center == 10.0   # 20·0.5
    assert m0.regime == "raman_illustrative"


def test_experiment_is_deterministic_and_declared():
    a = run(ExperimentMatrix(n_trials=30))
    b = run(ExperimentMatrix(n_trials=30))
    assert a["cells"] == b["cells"]
    assert a["data_class"] == "simulated"


def test_advantage_vanishes_under_tissue_noise():
    """The headline physics result: squeezed advantage ≈ e^r when clean,
    collapses toward 1 when tissue noise dominates."""
    r = run(ExperimentMatrix(n_trials=50))
    b = {row["tissue_sigma"]: row["advantage_ratio"]
         for row in r["advantage_boundary"] if row["ambient"] == 0.0}
    assert b[0.0] > 2.0                    # shot-limited: real advantage
    assert b[0.05] < 1.5                   # tissue-dominant: advantage dies
    assert b[0.0] > b[0.05]                # monotone-ish ordering
