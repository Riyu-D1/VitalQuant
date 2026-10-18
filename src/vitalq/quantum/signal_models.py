"""Signal models for the simulation (docs/08 §2).

`RamanToyModel` formalises the inherited spec-§25 parameterisation — a
Lorentzian Amide-I-like peak whose position shifts linearly with an
`agg_frac` proxy. It is tagged regime='raman_illustrative': the shift relation
is illustrative, not an established quantitative law, and it is NOT the
reflectance regime VitalQ's AS7341 measures in.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np


@dataclass
class LorentzianPeak:
    center: float          # wavenumber / x-unit of the peak
    width: float           # HWHM
    amp: float             # peak photon counts above baseline
    baseline: float = 0.0

    def eval(self, x: np.ndarray) -> np.ndarray:
        return self.baseline + self.amp * self.width ** 2 / (
            (x - self.center) ** 2 + self.width ** 2)


@dataclass
class RamanToyModel:
    """Spec §25 toy: 1600–1700 cm⁻¹ window, peak ~1650, linear shift proxy."""
    agg_frac: float = 0.0                  # 0..1 aggregation proxy
    center0: float = 1650.0
    width: float = 8.0
    amp: float = 1000.0
    baseline: float = 100.0
    shift_per_frac: float = 20.0           # illustrative, per docs/08 audit
    regime: str = field(default="raman_illustrative", init=False)

    def peak(self) -> LorentzianPeak:
        return LorentzianPeak(self.center0 + self.shift_per_frac * self.agg_frac,
                              self.width, self.amp, self.baseline)

    def eval(self, x: np.ndarray) -> np.ndarray:
        return self.peak().eval(x)


@dataclass
class AbsorptionDip:
    """Reflectance-mode analogy — closer to what VitalQ actually measures:
    a dip below a continuum rather than an emission peak."""
    center: float
    width: float
    depth: float                           # fraction of continuum removed
    continuum: float

    def eval(self, x: np.ndarray) -> np.ndarray:
        return self.continuum * (1 - self.depth * self.width ** 2 /
                                 ((x - self.center) ** 2 + self.width ** 2))
