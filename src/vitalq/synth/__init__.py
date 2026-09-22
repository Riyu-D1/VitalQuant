"""Synthetic device: generates labelled `data_class='synthetic'` ingest batches.

Used for the M1 vertical slice and the §28 corruption test battery.
Deterministic under `seed`; never emit real-looking data without data_class.
"""

from vitalq.synth.generator import CorruptionSpec, SynthConfig, SyntheticDevice

__all__ = ["CorruptionSpec", "SynthConfig", "SyntheticDevice"]
