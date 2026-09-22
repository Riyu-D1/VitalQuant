# 08 — Quantum-Inspired Simulation Architecture (Step 8)

Scope (spec §24–26): VitalQ contains **no quantum hardware**. This module is a controlled
simulation environment answering: *"Under which physically-defensible assumptions could a
squeezed-light readout improve measurement SNR, and where does the advantage vanish?"*
All its outputs carry `data_class='simulated'` and are labelled accordingly everywhere.

## 1. Audit of the inherited toy model (spec §25)

| Parameter | Status | Physics |
|---|---|---|
| 1600–1700 cm⁻¹ window, peak 1650 | **Illustrative** | Amide-I Raman band (~1655 cm⁻¹); protein-aggregation studies do show amide-I shifts — reasonable as a *toy lineshape* |
| `shift = 20 × agg_frac` | Placeholder | Linear shift proxy; not an established quantitative relation — mark `illustrative` |
| width 8, amplitude 1000 | Placeholder | Lineshape params; keep configurable |
| noise `= √signal · e⁻ʳ`, `r=0.8` | **Half-right** | √N shot noise ✓; squeezing reduces *variance* by e⁻²ʳ → std by e⁻ʳ ✓ — **iff shot-noise-limited and lossless**. r=0.8 ⇒ std ×0.45 (≈7 dB) |
| SNR threshold 3 | Convention | Keep, configurable |

Missing physics (research §E): detection loss η (`V_meas = ηe⁻²ʳ + (1−η)`), quadrature
angle, non-quantum noise floor (ambient, tissue/perfusion, electronics), photon budget.
Each is a named term in the model below — the honest experiment is *where the advantage
dies*, not whether it exists.

## 2. Module design (`vitalq-quantum/`)

```python
# noise_models.py — composable, all parameters explicit
class NoiseModel(Protocol):
    def std(self, signal: np.ndarray) -> np.ndarray: ...

ShotNoise()                      # std = k·√N
SqueezedShotNoise(r)             # std = k·√N·e^{-r}        (ideal, lossless)
LossySqueezedNoise(r, eta)       # variance = k²·N·(ηe^{-2r} + (1−η))
AdditiveFloor(floor_rms)         # electronic/dark floor, wraps any other model
TissueNoise(sigma_rel)           # perfusion/motion proportional term — the dominant
                                 # term in any real wearable optical measurement
AmbientLeakage(level)            # emitter-independent offset noise

# signal_models.py
LorentzianPeak(center, width, amp)
RamanToyModel(config)            # formalises the inherited §25 parameterisation
AbsorptionDip(...)               # reflectance-mode analogy (VitalQ's actual regime)

# experiment.py
run(matrix: ExperimentMatrix) -> ExperimentResult  # param sweep → metrics tables
```

## 3. Experiment matrix (spec §26)

Axes (all swept, not cherry-picked):
- `r ∈ {0, 0.25, 0.5, 0.8, 1.0}` (0 = classical baseline — squeezing model must reduce to it)
- `η ∈ {0.5 … 1.0}` — detection efficiency incl. tissue+optics losses
- `tissue_noise / ambient ∈ {0, small, realistic, dominant}` — *the axis that matters*
- signal amplitude / photon budget — wearable power constraint
- SNR threshold τ ∈ {1, 3, 5}

Outputs per cell: SNR, minimum detectable peak shift/amplitude change, ROC-style
detection rates on labelled *simulated* positives (allowed — labels are synthetic and
declared), false-positive rate vs threshold.

## 4. The headline result to produce honestly

A single figure: **advantage boundary map** — squeezing benefit (ΔSNR vs classical) as a
function of `r`/`η` under increasing non-quantum noise. Expectation from physics:
advantage collapses once tissue/ambient noise exceeds shot noise by ~3 dB. *If* the sim
says that, the honest VitalQ conclusion is: quantum-inspired readout is a long-term
sensitivity argument for a shot-noise-limited optical head, not a claim about the
current device. Publishable either way; fraudulent if the floor terms are omitted.

## 5. Separation rules (spec §24 — enforced, not suggested)

- Module is a leaf dependency: `vitalq-processing` and the API never import it.
- Every sim result row/file carries `data_class='simulated'` and `sim_spec` metadata.
- Dashboard surfaces it only under "Simulation" pages with an explicit banner.
- `RamanToyModel` is tagged `regime: 'raman_illustrative'` — not conflated with the
  AS7341 reflectance regime VitalQ actually measures in.

## 6. Reproducibility

Each `ExperimentResult` stores the full matrix spec, seeds, code commit, package version —
matching `ml.experiments` conventions (spec §32–33). Runs are deterministic
(`numpy.random.Generator(seed)`), pure CPU, laptop-scale (vectorised sweeps, no GPU).
