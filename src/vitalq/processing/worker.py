"""Processing worker: raw → quality + features for a session.

Runs S4–S7 of docs/06. Batch/idempotent by (session_id, window_start,
pipeline_version). CLI: `vitalq-worker --session <uuid> | --all`.
"""

from __future__ import annotations

import argparse
import asyncio
import os
from collections import defaultdict
from datetime import datetime, timezone
from uuid import UUID

import numpy as np

from vitalq.ingest import db
from vitalq.processing import PIPELINE_VERSION, WINDOW_SECONDS
from vitalq.processing import ppg as pp
from vitalq.processing import scalars as sc


def _clean(v):
    """NaN/inf are not valid jsonb — store as absent."""
    return v if not isinstance(v, float) or np.isfinite(v) else None


def _chunks(samples: list[float], fs: float, seconds: float):
    n = int(fs * seconds)
    for i in range(0, len(samples) - n + 1, n):
        yield i // n, samples[i:i + n]


async def _load_windows(conn, session_id, channel: str):
    return await conn.fetch(
        """select window_id, window_start, sample_rate_hz, samples
           from raw.signal_windows where session_id=$1 and channel_id=$2
           order by window_start""", session_id, channel)


async def _load_scalars(conn, session_id, channel: str):
    return await conn.fetch(
        """select sample_time, value from raw.measurements_scalar
           where session_id=$1 and channel_id=$2 order by sample_time""",
        session_id, channel)


async def _load_frames(conn, session_id):
    return await conn.fetch(
        """select sample_time, channels, illumination from raw.spectral_frames
           where session_id=$1 order by sample_time""", session_id)


def _floor(t, seconds: float) -> float:
    return int(t.timestamp() // seconds * seconds)


async def process_session(dsn: str, session_id: UUID,
                          pipeline_version: str = PIPELINE_VERSION) -> dict:
    conn = await db.connect(dsn)
    stats = {"windows": 0, "quality": 0, "features": 0}
    try:
        sess = await conn.fetchrow(
            "select started_at, data_class from meta.sessions where session_id=$1",
            session_id)
        if sess is None:
            raise ValueError(f"unknown session {session_id}")
        data_class = sess["data_class"]

        # ---- waveform channels: SQI + per-modality features ------------------
        wave_feats: dict[float, dict] = defaultdict(dict)
        ppg_by_win: dict[float, dict[str, np.ndarray]] = defaultdict(dict)
        src_by_win: dict[float, list[int]] = defaultdict(list)

        quality_rows, contact_rows = [], []
        wave_channels = [r["channel_id"] for r in await conn.fetch(
            """select distinct channel_id from raw.signal_windows where session_id=$1""",
            session_id)]

        for ch in wave_channels:
            rows = await _load_windows(conn, session_id, ch)
            for r in rows:
                fs = float(r["sample_rate_hz"])
                for k, chunk in _chunks(r["samples"], fs, WINDOW_SECONDS):
                    wstart = _floor(r["window_start"], WINDOW_SECONDS) + k * WINDOW_SECONDS
                    wkey = float(wstart)
                    src_by_win[wkey].append(r["window_id"])
                    if ch.startswith("ppg."):
                        res = pp.process_ppg_window(chunk, fs)
                        ppg_by_win[wkey][ch] = np.asarray(chunk, dtype=float)
                        for f in ("hr_bpm", "rmssd_ms", "sdnn_ms", "n_beats"):
                            if res[f] is not None:
                                wave_feats[wkey][f"{ch.split('.')[1]}_{f}"] = res[f]
                        sqi = res["sqi"]
                    elif ch.startswith("motion."):
                        wave_feats[wkey].setdefault("_motion_parts", {})[ch] = (
                            np.asarray(chunk, dtype=float), fs)
                        sqi = {"sqi": 1.0}   # motion SQI: presence-only for v0.1
                    else:
                        sqi = {"sqi": 1.0}
                    sqi_val = sqi.pop("sqi", 1.0)
                    quality_rows.append((
                        session_id, ch, _ts(wkey), sqi_val,
                        {k: _clean(v) for k, v in sqi.items()},
                        pipeline_version))
                    stats["quality"] += 1
                    stats["windows"] += 1

        # r-ratio + uncalibrated SpO2 where red+ir coexist (docs/06)
        for wkey, chs in ppg_by_win.items():
            if "ppg.red" in chs and "ppg.ir" in chs:
                rr = pp.r_ratio(chs["ppg.red"], chs["ppg.ir"])
                wave_feats[wkey]["r_ratio"] = round(rr, 4) if rr else None
                wave_feats[wkey]["spo2_est_uncalibrated"] = pp.spo2_uncalibrated(rr)

        # ---- scalar channels ---------------------------------------------------
        scalar_rows: dict[str, list[tuple]] = {}
        for ch in ("temp.object", "temp.ambient", "env.temperature", "env.humidity",
                   "env.pressure", "contact.level"):
            scalar_rows[ch] = [(r["sample_time"], r["value"])
                               for r in await _load_scalars(conn, session_id, ch)]

        frames = await _load_frames(conn, session_id)

        # motion score per window from accel triple
        for feats in list(wave_feats.values()):
            parts = feats.pop("_motion_parts", None)
            if parts and all(k in parts for k in
                             ("motion.accel_x", "motion.accel_y", "motion.accel_z")):
                arrs = [parts["motion.accel_x"][0], parts["motion.accel_y"][0],
                        parts["motion.accel_z"][0]]
                fs_m = parts["motion.accel_x"][1]
                n = min(len(a) for a in arrs)
                xyz = np.stack([a[:n] for a in arrs], axis=1)
                feats["motion_score"] = sc.motion_score(xyz, fs=fs_m)

        # contact quality per window (from scalars)
        def _window_vals(rows, wkey):
            lo = _ts(wkey)
            hi = _ts(wkey + WINDOW_SECONDS)
            return np.array([v for t, v in rows if lo <= t < hi])

        all_windows = sorted(set(wave_feats) |
                         {_floor(t, WINDOW_SECONDS) for rows in scalar_rows.values()
                          for t, _ in rows})
        contact_q = {}
        for wkey in all_windows:
            cq = sc.contact_quality(_window_vals(scalar_rows.get("contact.level", []), wkey))
            contact_q[wkey] = cq
            m = wave_feats.get(wkey, {}).get("motion_score", 0.0)
            conf = round(cq * (1 - m), 3)
            contact_rows.append((session_id, _ts(wkey), cq, m, conf, pipeline_version))

        # spectral features per window
        spec_by_win: dict[float, list[dict]] = defaultdict(list)
        dark_by_win: dict[float, dict] = {}
        for fr in frames:
            wkey = float(_floor(fr["sample_time"], WINDOW_SECONDS))
            chs = fr["channels"]
            if fr["illumination"] == "dark_frame":
                dark_by_win[wkey] = chs
            elif fr["illumination"] != "ambient":
                spec_by_win[wkey].append(chs)
        for wkey, frames_ in spec_by_win.items():
            wave_feats[wkey].update(
                sc.spectral_features(frames_, dark_by_win.get(wkey)))

        # temperature features per window
        for wkey in all_windows:
            vals = _window_vals(scalar_rows.get("temp.object", []), wkey)
            times = np.array([(t - _ts(wkey)).total_seconds()
                              for t, _ in scalar_rows.get("temp.object", [])
                              if _ts(wkey) <= t < _ts(wkey + WINDOW_SECONDS)])
            wave_feats[wkey].update(sc.temperature_features(vals, times))

        # ---- write -----------------------------------------------------------
        feat_rows = [
            (session_id, _ts(wkey), WINDOW_SECONDS, "fusion_v1", pipeline_version,
             {k: _clean(v) for k, v in feats.items()
              if not k.startswith("_") and v is not None},
             src_by_win.get(wkey, []), data_class)
            for wkey, feats in wave_feats.items() if feats
        ]
        stats["features"] = len(feat_rows)

        await conn.executemany(
            """insert into quality.channel_quality
               (session_id, channel_id, window_start, sqi, sqi_detail, pipeline_version)
               values ($1,$2,$3,$4,$5::jsonb,$6) on conflict do nothing""",
            quality_rows)
        await conn.executemany(
            """insert into quality.contact_state
               (session_id, window_start, contact_quality, motion_score,
                sensor_confidence, pipeline_version)
               values ($1,$2,$3,$4,$5,$6) on conflict do nothing""",
            contact_rows)
        await conn.executemany(
            """insert into features.windows
               (session_id, window_start, window_seconds, feature_set,
                pipeline_version, values, source_window_ids, data_class)
               values ($1,$2,$3,$4,$5,$6::jsonb,$7,$8) on conflict do nothing""",
            feat_rows)
    finally:
        await conn.close()
    return stats


def _ts(epoch_s: float):
    return datetime.fromtimestamp(epoch_s, timezone.utc)


async def _run(args) -> None:
    dsn = os.environ.get("DATABASE_URL")
    if not dsn:
        raise SystemExit("DATABASE_URL not set")
    if args.all:
        conn = await db.connect(dsn)
        ids = [r["session_id"] for r in await conn.fetch(
            "select session_id from meta.sessions where ended_at is not null or true")]
        await conn.close()
    else:
        ids = [UUID(args.session)]
    for sid in ids:
        stats = await process_session(dsn, sid)
        print(f"{sid}: {stats}")


def main() -> None:
    p = argparse.ArgumentParser()
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--session")
    g.add_argument("--all", action="store_true")
    asyncio.run(_run(p.parse_args()))


if __name__ == "__main__":
    main()
