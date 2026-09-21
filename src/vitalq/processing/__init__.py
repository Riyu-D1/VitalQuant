"""vitalq.processing — validation, clock fix, per-modality chains, SQI, features.

PIPELINE_VERSION is stamped on every derived row (provenance, spec §46).
Bump it whenever any stage's output changes.
"""

PIPELINE_VERSION = "0.1.0"
WINDOW_SECONDS = 10.0          # analysis window for features/quality
ABSTAIN_CONFIDENCE = 0.4       # below this, features exist but are flagged low-trust
