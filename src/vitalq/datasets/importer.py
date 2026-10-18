"""`vitalq-import` CLI — CSV / WFDB records → ingest API batches.

The import posts through whatever httpx.AsyncClient it is given: a real
`base_url` in production, or `httpx.ASGITransport(app)` in tests/one-shot
local runs (`--api asgi` skips needing a running server, still uses auth).
"""

from __future__ import annotations

import argparse
import asyncio
import csv
import os
from datetime import datetime, timezone
from uuid import NAMESPACE_URL, UUID, uuid5

import numpy as np

BATCH_SECONDS = 15.0
MAX_SAMPLES_PER_WINDOW = 8192          # raw.signal_windows row size sanity cap


def _chunks(arr: np.ndarray, n: int):
    for i in range(0, len(arr) - n + 1, n):
        yield i, arr[i:i + n]


async def open_session(client, device_key: str, session_id: UUID, started_at,
                       data_class: str, notes: str) -> str:
    r = await client.post("/v1/sessions", headers={"X-Device-Key": device_key},
                          json={
        "session_id": str(session_id), "firmware_version": "import-v1",
        "hardware_revision": "hw_v1", "data_class": data_class,
        "started_at": started_at.isoformat(), "notes": notes})
    # deterministic session_id (uuid5 of the source) → re-imports hit
    # session.exists: that IS the idempotency, continue
    if r.status_code != 409:
        r.raise_for_status()
    return str(session_id)


async def send_batch(client, device_key: str, session_id: UUID, seq: int,
                     batch: dict, stream: str = "") -> dict:
    # `stream` distinguishes parallel channels sharing seq numbers in one session
    batch_id = uuid5(NAMESPACE_URL, f"vitalq-batch:{session_id}:{stream}:{seq}")
    payload = {**batch, "batch_id": str(batch_id),
               "session_id": str(session_id),
               "firmware_version": "import-v1"}
    headers = {"X-Device-Key": device_key}
    for attempt in range(6):
        r = await client.post("/v1/ingest/batch", headers=headers, json=payload)
        if r.status_code == 429:
            retry = float(r.headers.get("retry-after", 0.25 * (2 ** attempt)))
            await asyncio.sleep(min(retry, 30.0))
            continue
        break
    if r.status_code == 409:
        # deterministic batch_id + batch.replay = this exact batch already
        # landed — idempotent re-import, safe to skip
        if r.json().get("error", {}).get("code") == "batch.replay":
            return {"accepted": 0, "batch_id": str(batch_id), "replayed": True}
    r.raise_for_status()
    return r.json()


async def ingest_waveform(client, device_key: str, session_id: UUID,
                         channel: str, samples: np.ndarray, fs: float,
                         t0: datetime, scalar_map: dict | None = None,
                         batch_seconds: float = BATCH_SECONDS) -> int:
    """Batch a uniform waveform channel (+ optional scalar streams) through the
    ingest API. Returns number of batches accepted."""
    # external recordings contain NaN gaps — Postgres real[] can't store NaN
    # anyway; interpolate short gaps and drop windows that stay non-finite
    samples = np.asarray(samples, dtype=float)
    if not np.isfinite(samples).all():
        good = np.isfinite(samples)
        samples = np.interp(np.arange(len(samples)),
                            np.flatnonzero(good), samples[good]) \
            if good.any() else np.zeros(len(samples))
    n = len(samples)
    win = int(fs * 5)                     # 5 s windows — the raw table's shape
    per_batch = int(fs * batch_seconds)
    n_batches = max(1, int(np.ceil(n / per_batch)))
    t0_us = int(t0.timestamp() * 1e6)
    accepted = 0
    for b in range(n_batches):
        lo, hi = b * per_batch, min(n, (b + 1) * per_batch)
        windows = []
        for off, chunk in _chunks(samples[lo:hi], win):
            windows.append({
                "channel": channel, "rate_hz": fs,
                "t_start_us": t0_us + int((lo + off) / fs * 1e6),
                "samples": [float(v) for v in chunk],
            })
        scalars = []
        if scalar_map:
            for ch_name, (sarr, sfs) in scalar_map.items():
                s_lo = int(lo / fs * sfs)
                s_hi = int(hi / fs * sfs)
                for i in range(s_lo, min(s_hi, len(sarr))):
                    scalars.append({
                        "channel": ch_name,
                        "t_us": t0_us + int(i / sfs * 1e6),
                        "v": float(sarr[i])})
        if not (windows or scalars):
            continue
        wall = datetime.fromtimestamp(
            (t0_us + int(lo / fs * 1e6)) / 1e6, timezone.utc)
        await send_batch(client, device_key, session_id, b, {
            "clock": {"monotonic_us": t0_us + int(lo / fs * 1e6),
                      "wall_time": wall.isoformat()},
            "windows": windows, "scalars": scalars}, stream=channel)
        accepted += 1
    return accepted


def read_csv_columns(path: str, cols: list[str]) -> dict[str, np.ndarray]:
    """CSV with a header row; each named column becomes a float array."""
    out = {c: [] for c in cols}
    with open(path, newline="") as fh:
        reader = csv.DictReader(fh)
        missing = set(cols) - set(reader.fieldnames or [])
        if missing:
            raise ValueError(f"CSV missing columns: {sorted(missing)}")
        for row in reader:
            for c in cols:
                out[c].append(float(row[c]))
    return {c: np.asarray(v) for c, v in out.items()}


WFDB_CHANNEL_MAP = {"PLETH": "ppg.ir", "RESP": "resp.waveform",
                    "II": "ecg.ii", "V": "ecg.v", "AVR": "ecg.avr"}


def load_wfdb(record: str, database: str) -> tuple[dict[str, np.ndarray], float]:
    """Download a PhysioNet record (open access — no credentials needed for
    bidmc/mitdb). Returns {channel_id: samples}, base_fs."""
    try:
        import wfdb
    except ImportError as e:
        raise SystemExit("wfdb not installed — pip install 'vitalq[datasets]'"
                         ) from e
    rec = wfdb.rdrecord(record, pn_dir=database)
    chans = {}
    for i, name in enumerate(rec.sig_name):
        ch = WFDB_CHANNEL_MAP.get("".join(c for c in name.upper() if c.isalpha()))
        if ch:
            chans[ch] = np.asarray(rec.p_signal[:, i], dtype=float)
    if not chans:
        raise ValueError(f"no mappable channels in {database}/{record}; "
                         f"signals were {rec.sig_name}")
    return chans, float(rec.fs)


async def _run(args) -> None:
    import httpx
    device_key = args.device_key or os.environ.get("VITALQ_DEVICE_KEY")
    if not device_key:
        raise SystemExit("--device-key or VITALQ_DEVICE_KEY required")
    if args.api == "asgi":
        from vitalq.ingest.app import app
        client = httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://x")
    else:
        client = httpx.AsyncClient(base_url=args.api)
    t0 = datetime.now(timezone.utc)

    if args.wfdb:
        database, _, record = args.wfdb.rpartition("/")
        chans, fs = load_wfdb(record, database)
        session_id = uuid5(NAMESPACE_URL, f"vitalq-import:wfdb:{args.wfdb}")
        await open_session(client, device_key, session_id, t0, "real",
                           f"wfdb import {args.wfdb}")
        n = 0
        for ch, arr in chans.items():
            n += await ingest_waveform(client, device_key, session_id,
                                      ch, arr, fs, t0)
        print(f"session {session_id}: {n} batches ({', '.join(chans)})")
        return

    cols = [m.split("=")[1] for m in args.map]
    data = read_csv_columns(args.csv, cols)
    session_id = uuid5(NAMESPACE_URL,
                       f"vitalq-import:csv:{os.path.basename(args.csv)}")
    await open_session(client, device_key, session_id, t0, args.data_class,
                       f"csv import {args.csv}")
    n = 0
    for m in args.map:
        ch_name, col = m.split("=", 1)
        n += await ingest_waveform(client, device_key, session_id,
                                  ch_name, data[col], args.fs, t0)
    print(f"session {session_id}: {n} batches")


def main() -> None:
    p = argparse.ArgumentParser(description="Ingest CSV/WFDB data via the "
                                            "batch API (data_class=real)")
    p.add_argument("--csv")
    p.add_argument("--wfdb", help="database/record e.g. bidmc/bidmc01")
    p.add_argument("--map", action="append", default=[],
                   help="channel=csv_column (repeatable)")
    p.add_argument("--fs", type=float, default=125.0)
    p.add_argument("--api", default="asgi",
                   help="base URL or 'asgi' (in-process app)")
    p.add_argument("--device-key")
    p.add_argument("--data-class", default="real",
                   choices=["real", "synthetic", "simulated"])
    args = p.parse_args()
    if not (args.csv or args.wfdb):
        raise SystemExit("need --csv or --wfdb")
    asyncio.run(_run(args))


if __name__ == "__main__":
    main()
