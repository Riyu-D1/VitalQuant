"""Dataset ingestion: real recorded data entering through the same batch API.

`vitalq-import` accepts:
- CSV: one column per channel (uniform fs) → waveform windows or scalars
- WFDB (optional `datasets` extra): PhysioNet records, e.g. BIDMC PPG —
  PLETH→ppg.ir, RESP→resp.waveform, ECG II→ecg.ii (open access, no creds)

Batches go through the real ingest path (device key + clock anchors + batch_id
idempotency), so imported data exercises exactly what a device hits.
data_class stays 'real' — this is recorded human physiology, not simulation.
"""
