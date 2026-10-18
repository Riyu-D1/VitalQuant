"""Processing worker v0.2: raw → clean → quality + features for a session.

Stages (docs/06): S4 validate/clock → S5 resample to `clean.signal_windows` →
S6 per-channel chains (PPG v2: onsets, cleaned IBI, freq PRV, morphology,
respiration, SQI v2) → multi-channel SQI-weighted fusion → scalar chains
(temperature/env/spectral/contact with hysteresis) → S7 `features.windows`
(feature_set='fusion_v2'). Idempotent via PK conflicts; pipeline_version and
source_window_ids are stamped on every derived row.

CLI: `vitalq-worker --session <uuid> | --all`.
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

CLEAN_RESAMPLE_HZ = 100.0      # uniform grid for the clean layer
PPG_CHANNELS_PREFIX = "ppg."


def _clean(v):
    """NaN/inf are not valid jsonb — store as absent."""
    return v if not isinstance(v, float) or np.isfinite(v) else None


def _chunks(samples: list[float], fs: float, seconds: float):
    n = int(fs * seconds)
    for i in range(0, len(samples) - n + 1, n):
        yield i // n, samples[i:i + n]


def _resample_uniform(chunk: np.ndarray, fs: float,
                      target_fs: float) -> tuple[np.ndarray, np.ndarray]:
    """Resample a contiguous chunk onto a uniform target_fs grid.
    gap_mask is all-real here — gaps live *between* windows; interpolation only
    bridges the source's own (already uniform) sampling. Kept explicit so
    dropout-filled data can mark mask=1 later."""
    n_out = int(round(len(chunk) / fs * target_fs))
    if n_out < 2:
        return np.array([]), np.array([])
    t_src = np.arange(len(chunk)) / fs
    t_dst = np.arange(n_out) / target_fs
    out = np.interp(t_dst, t_src, chunk)
    return out, np.zeros(n_out, dtype=int)


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


def _stream_runs(rows, gap_tol_s: float = 0.05):
    """Concatenate consecutive raw windows of one channel into contiguous streams.

    Raw windows may be shorter than the processing window (WINDOW_SECONDS) —
    e.g. an import writing 5 s chunks — so process a run of back-to-back
    windows as a single stream. A run breaks on a sampling-rate change or a
    timestamp gap larger than gap_tol_s. Yields
    (base_epoch_s, fs, spans, samples) where spans is a list of
    (window_id, sample_offset_start, sample_offset_end) for provenance.
    """
    base = cur_fs = None
    spans: list[tuple[int, int, int]] = []
    stream: list[float] = []

    def emit():
        return (base, cur_fs, spans, stream)

    for r in rows:
        n = len(r["samples"])
        t0 = r["window_start"].timestamp()
        fs = float(r["sample_rate_hz"])
        expected = base + (len(stream) / cur_fs) if base is not None else None
        contiguous = (base is not None and fs == cur_fs
                      and abs(t0 - expected) <= gap_tol_s)
        if not contiguous and base is not None:
            yield emit()
            spans, stream = [], []
            base = None
        if base is None:
            base, cur_fs = t0, fs
        spans.append((r["window_id"], len(stream), len(stream) + n))
        stream.extend(r["samples"])
    if base is not None:
        yield emit()


def _ts(epoch_s: float):
    return datetime.fromtimestamp(epoch_s, timezone.utc)


async def process_session(dsn: str, session_id: UUID,
                          pipeline_version: str = PIPELINE_VERSION) -> dict:
    conn = await db.connect(dsn)
    stats = {"windows": 0, "cleaned": 0, "quality": 0, "features": 0}
    try:
        sess = await conn.fetchrow(
            "select started_at, data_class from meta.sessions where session_id=$1",
            session_id)
        if sess is None:
            raise ValueError(f"unknown session {session_id}")
        data_class = sess["data_class"]

        # ---- waveform channels: clean layer, SQI, per-modality features ------
        wave_feats: dict[float, dict] = defaultdict(dict)
        ppg_by_win: dict[float, dict[str, np.ndarray]] = defaultdict(dict)
        ppg_sqi: dict[float, dict[str, float]] = defaultdict(dict)
        ppg_fs: dict[float, float] = {}
        src_by_win: dict[float, list[int]] = defaultdict(list)
        clean_rows: list[tuple] = []

        quality_rows, contact_rows = [], []
        wave_channels = [r["channel_id"] for r in await conn.fetch(
            """select distinct channel_id from raw.signal_windows where session_id=$1""",
            session_id)]

        for ch in wave_channels:
            rows = await _load_windows(conn, session_id, ch)
            for base, fs, spans, stream in _stream_runs(rows):
                # align the stream to the WINDOW_SECONDS feature grid — a run
                # that doesn't start on a boundary drops its leading partial
                boundary = int(np.ceil(base / WINDOW_SECONDS) * WINDOW_SECONDS)
                skip = int(round((boundary - base) * fs))
                for k, chunk in _chunks(stream[skip:], fs, WINDOW_SECONDS):
                    wkey = float(boundary + k * WINDOW_SECONDS)
                    c_lo, c_hi = skip + k * int(fs * WINDOW_SECONDS), \
                        skip + (k + 1) * int(fs * WINDOW_SECONDS)
                    src_by_win[wkey].extend(
                        wid for wid, s, e in spans if s < c_hi and e > c_lo)
                    arr = np.asarray(chunk, dtype=float)

                    # clean layer: resample to uniform grid
                    rs, mask = _resample_uniform(arr, fs, CLEAN_RESAMPLE_HZ)
                    if len(rs):
                        clean_rows.append((
                            session_id, ch, _ts(wkey), CLEAN_RESAMPLE_HZ,
                            len(rs), rs.tolist(), mask.tolist(),
                            pipeline_version, data_class))
                        stats["cleaned"] += 1

                    if ch.startswith(PPG_CHANNELS_PREFIX):
                        res = pp.process_ppg_window(chunk, fs)
                        ppg_by_win[wkey][ch] = arr
                        ppg_fs[wkey] = fs
                        for f in ("hr_bpm", "rmssd_ms", "sdnn_ms", "ibi_cv",
                                  "n_beats", "n_ibi_removed",
                                  "prv_vlf_ms2", "prv_lf_ms2", "prv_hf_ms2",
                                  "prv_lf_hf", "resp_hz_prv",
                                  "pulse_amp_mean", "rise_time_s",
                                  "pulse_width50_s", "area_ratio",
                                  "reflection_index_s", "crest_time_s",
                                  "resp_hz_env", "resp_confidence",
                                  "tm_n_beats", "tm_ncc_mean", "tm_hr_bpm"):
                            if res.get(f) is not None:
                                wave_feats[wkey][f"{ch.split('.')[1]}_{f}"] = res[f]
                        sqi = res["sqi"]
                        ppg_sqi[wkey][ch] = sqi["sqi"]
                    elif ch.startswith("motion."):
                        wave_feats[wkey].setdefault("_motion_parts", {})[ch] = (
                            arr, fs)
                        sqi = {"sqi": 1.0}   # motion SQI: presence-only for v0.2
                    else:
                        sqi = {"sqi": 1.0}
                    sqi_val = sqi.pop("sqi", 1.0)
                    quality_rows.append((
                        session_id, ch, _ts(wkey), sqi_val,
                        {k: _clean(v) for k, v in sqi.items()},
                        pipeline_version))
                    stats["quality"] += 1
                    stats["windows"] += 1

        # SQI-weighted multi-channel fusion → fused_* features (docs/06 §5)
        for wkey, chs in ppg_by_win.items():
            fs = ppg_fs[wkey]
            fused = pp.fuse_windows(chs, ppg_sqi[wkey])
            if len(fused) < int(fs * 4):
                continue
            res = pp.process_ppg_window(fused.tolist(), fs)
            for k, v in res.items():
                if k == "sqi" or v is None:
                    continue
                wave_feats[wkey][f"fused_{k}"] = v
            wave_feats[wkey]["fused_sqi"] = res["sqi"]["sqi"]
            wave_feats[wkey]["fusion_n_channels"] = len(ppg_sqi[wkey])

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

        # contact quality per window (from scalars) + temporal hysteresis
        def _window_vals(rows, wkey):
            lo = _ts(wkey)
            hi = _ts(wkey + WINDOW_SECONDS)
            return np.array([v for t, v in rows if lo <= t < hi])

        all_windows = sorted(set(wave_feats) |
                         {_floor(t, WINDOW_SECONDS) for rows in scalar_rows.values()
                          for t, _ in rows})
        per_win_q = [(wkey, sc.contact_quality(
            _window_vals(scalar_rows.get("contact.level", []), wkey)))
            for wkey in all_windows]
        on_skin = sc.contact_hysteresis(per_win_q)
        for (wkey, cq), skin in zip(per_win_q, on_skin, strict=False):
            m = wave_feats.get(wkey, {}).get("motion_score", 0.0)
            conf = round(cq * (1 - m) * (1.0 if skin else 0.2), 3)
            contact_rows.append((session_id, _ts(wkey), cq, m, conf, skin,
                                 pipeline_version))
            if not skin:
                wave_feats.setdefault(wkey, {})["flag_contact_off"] = 1

        # spectral features per window (dark-frame subtraction)
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

        # temperature + environment features per window
        for wkey in all_windows:
            vals = _window_vals(scalar_rows.get("temp.object", []), wkey)
            times = np.array([(t - _ts(wkey)).total_seconds()
                              for t, _ in scalar_rows.get("temp.object", [])
                              if _ts(wkey) <= t < _ts(wkey + WINDOW_SECONDS)])
            amb = _window_vals(scalar_rows.get("temp.ambient", []), wkey)
            wave_feats[wkey].update(sc.temperature_features(vals, times,
                                                            ambient_vals=amb))
            wave_feats[wkey].update(sc.env_features(
                {ch: _window_vals(scalar_rows.get(ch, []), wkey)
                 for ch in ("env.humidity", "env.pressure")}))

        # session-level estimates: respiration and frequency-domain PRV are only
        # meaningful over >>10 s — computed once on the concatenated fused stream
        # and stamped onto each window prefixed `sess_` (they are NOT
        # per-window estimates).
        sess_feats: dict[str, float] = {}
        ordered = [w for w in sorted(ppg_by_win) if len(ppg_by_win[w]) >= 1]
        if ordered and ppg_fs:
            fs0 = next(iter(ppg_fs.values()))
            fused_chunks = [pp.fuse_windows(ppg_by_win[w], ppg_sqi[w])
                            for w in ordered]
            sig = np.concatenate([c for c in fused_chunks if len(c)])
            if len(sig) >= int(fs0 * 20):
                xf = pp.bandpass(sig, fs0)
                pk = pp.detect_beats(xf, fs0)
                on = pp.refine_onsets(xf, pk, fs0)
                ibi = np.diff(on) / fs0 if len(on) >= 2 else np.array([])
                ibi = ibi[(ibi >= pp.MIN_IBI_S) & (ibi <= pp.MAX_IBI_S)]
                ibi, _ = pp.clean_ibi(ibi)
                sess_feats = {f"sess_{k}": v
                              for k, v in pp.prv_frequency(on, ibi).items()
                              if v is not None}
                sess_feats.update(
                    {f"sess_{k}": v
                     for k, v in pp.resp_from_envelope(xf, fs0).items()
                     if v is not None})
        for feats in wave_feats.values():
            feats.update(sess_feats)

        # honest flags from the fused/single-channel HR (thresholds documented in
        # docs/07 — research heuristics, not clinical thresholds)
        for feats in wave_feats.values():
            hr = (feats.get("fused_hr_bpm") or feats.get("ir_hr_bpm")
                  or feats.get("red_hr_bpm"))
            if hr is not None:
                if hr > 100:
                    feats["flag_tachy"] = 1
                elif hr < 45:
                    feats["flag_brady"] = 1
            sq = feats.get("fused_sqi")
            if sq is not None and sq < 0.2:
                feats["flag_low_sqi"] = 1

        # ---- write -----------------------------------------------------------
        feat_rows = [
            (session_id, _ts(wkey), WINDOW_SECONDS, "fusion_v2", pipeline_version,
             {k: _clean(v) for k, v in feats.items()
              if not k.startswith("_") and v is not None},
             src_by_win.get(wkey, []), data_class)
            for wkey, feats in wave_feats.items() if feats
        ]
        stats["features"] = len(feat_rows)

        await conn.executemany(
            """insert into clean.signal_windows
               (session_id, channel_id, window_start, resample_hz, n_samples,
                samples, gap_mask, pipeline_version, data_class)
               values ($1,$2,$3,$4,$5,$6,$7,$8,$9) on conflict do nothing""",
            clean_rows)
        await conn.executemany(
            """insert into quality.channel_quality
               (session_id, channel_id, window_start, sqi, sqi_detail, pipeline_version)
               values ($1,$2,$3,$4,$5,$6) on conflict do nothing""",
            quality_rows)
        await conn.executemany(
            """insert into quality.contact_state
               (session_id, window_start, contact_quality, motion_score,
                sensor_confidence, on_skin, pipeline_version)
               values ($1,$2,$3,$4,$5,$6,$7) on conflict do nothing""",
            contact_rows)
        await conn.executemany(
            """insert into features.windows
               (session_id, window_start, window_seconds, feature_set,
                pipeline_version, values, source_window_ids, data_class)
               values ($1,$2,$3,$4,$5,$6,$7,$8) on conflict do nothing""",
            feat_rows)
    finally:
        await conn.close()
    return stats


async def _run(args) -> None:
    dsn = os.environ.get("DATABASE_URL")
    if not dsn:
        raise SystemExit("DATABASE_URL not set")
    if args.all:
        conn = await db.connect(dsn)
        ids = [r["session_id"] for r in await conn.fetch(
            "select session_id from meta.sessions")]
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
