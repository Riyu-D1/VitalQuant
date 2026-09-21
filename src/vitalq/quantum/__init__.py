"""Quantum-inspired readout simulation (docs/08).

VitalQ contains no quantum hardware. This module is a controlled simulation
answering: under which physically-defensible assumptions could a squeezed-light
readout improve measurement SNR, and where does the advantage vanish?

Separation rules (spec §24): leaf dependency — processing/ingest never import
it; every result carries data_class='simulated'.
"""

DATA_CLASS = "simulated"
