"""Async Postgres access for the ingest API (asyncpg)."""

from __future__ import annotations

import json
import os
from uuid import UUID

import asyncpg

DSN_ENV = "DATABASE_URL"

_pool: asyncpg.Pool | None = None


async def _init_jsonb(conn: asyncpg.Connection) -> None:
    """Map jsonb ↔ dict so reads/writes never need manual dumps/loads."""
    await conn.set_type_codec("jsonb", encoder=json.dumps, decoder=json.loads,
                            schema="pg_catalog", format="text")


async def connect(dsn: str) -> asyncpg.Connection:
    """Raw connection with the jsonb codec applied (workers, CLI tools)."""
    conn = await asyncpg.connect(dsn)
    await _init_jsonb(conn)
    return conn


async def get_pool() -> asyncpg.Pool:
    global _pool
    if _pool is None:
        dsn = os.environ.get(DSN_ENV)
        if not dsn:
            raise RuntimeError(f"{DSN_ENV} not set")
        _pool = await asyncpg.create_pool(dsn, min_size=1, max_size=6,
                                          init=_init_jsonb)
    return _pool


async def close_pool() -> None:
    global _pool
    if _pool is not None:
        await _pool.close()
        _pool = None


async def device_for_key_hash(key_hash: str):
    pool = await get_pool()
    return await pool.fetchrow(
        "select device_id, hardware_revision, revoked from meta.devices where api_key_hash = $1",
        key_hash,
    )


async def create_session(device_id: UUID, s) -> None:
    pool = await get_pool()
    await pool.execute(
        """insert into meta.sessions
           (session_id, device_id, subject_id, firmware_version, hardware_revision,
            data_class, body_site, started_at, notes)
           values ($1,$2,$3,$4,$5,$6,$7,$8,$9)""",
        s.session_id, device_id, s.subject_id, s.firmware_version,
        s.hardware_revision, s.data_class, s.body_site, s.started_at, s.notes,
    )


async def end_session(device_id: UUID, session_id: UUID) -> bool:
    pool = await get_pool()
    res = await pool.execute(
        """update meta.sessions set ended_at = now()
           where session_id = $1 and device_id = $2 and ended_at is null""",
        session_id, device_id,
    )
    return res.endswith("1")


async def session_row(session_id: UUID):
    pool = await get_pool()
    return await pool.fetchrow(
        """select session_id, device_id, subject_id, firmware_version, hardware_revision,
                  data_class, body_site, started_at, ended_at, notes
           from meta.sessions where session_id = $1""",
        session_id,
    )


async def list_sessions(device_id: UUID | None, limit: int = 100):
    pool = await get_pool()
    if device_id:
        return await pool.fetch(
            "select * from meta.sessions where device_id=$1 order by started_at desc limit $2",
            device_id, limit)
    return await pool.fetch(
        "select * from meta.sessions order by started_at desc limit $1", limit)


async def insert_batch_header(device_id: UUID, session_id: UUID, wall_time,
                              monotonic_us: int, batch_id: UUID, payload_bytes: int) -> bool:
    """Insert the batch anchor row. Returns False if replayed (idempotent)."""
    pool = await get_pool()
    res = await pool.execute(
        """insert into raw.ingest_batches
           (batch_id, device_id, session_id, clock_wall_time, clock_monotonic_us, payload_bytes)
           values ($1,$2,$3,$4,$5,$6)
           on conflict (device_id, batch_id) do nothing""",
        batch_id, device_id, session_id, wall_time, monotonic_us, payload_bytes,
    )
    return res.endswith("1")


async def insert_scalars(rows: list[tuple]) -> None:
    pool = await get_pool()
    await pool.executemany(
        """insert into raw.measurements_scalar
           (device_id, session_id, channel_id, sample_time, device_time_us, value, data_class)
           values ($1,$2,$3,$4,$5,$6,$7)
           on conflict do nothing""",
        rows,
    )


async def insert_windows(rows: list[tuple]) -> list[int]:
    pool = await get_pool()
    ids: list[int] = []
    async with pool.acquire() as conn:
        for r in rows:
            wid = await conn.fetchval(
                """insert into raw.signal_windows
                   (device_id, session_id, channel_id, window_start, sample_rate_hz,
                    n_samples, device_time_start_us, samples, data_class)
                   values ($1,$2,$3,$4,$5,$6,$7,$8,$9)
                   on conflict do nothing
                   returning window_id""",
                *r,
            )
            ids.append(wid)
    return ids


async def insert_frames(rows: list[tuple]) -> None:
    pool = await get_pool()
    await pool.executemany(
        """insert into raw.spectral_frames
           (device_id, session_id, sample_time, device_time_us, read_group,
            channels, gain_x, integ_time_ms, illumination, sensor_temp_c, data_class)
           values ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11)
           on conflict do nothing""",
        rows,
    )


async def insert_events(rows: list[tuple]) -> None:
    pool = await get_pool()
    await pool.executemany(
        """insert into raw.device_events (device_id, session_id, event_time, kind, detail)
           values ($1,$2,$3,$4,$5) on conflict do nothing""",
        rows,
    )


async def session_clock_anchors(session_id: UUID):
    pool = await get_pool()
    return await pool.fetch(
        """select clock_monotonic_us, clock_wall_time
           from raw.ingest_batches where session_id=$1
           order by clock_monotonic_us""",
        session_id,
    )


async def update_batch_clock(batch_id: UUID, device_id: UUID,
                             offset_us: int, drift_ppm: float) -> None:
    pool = await get_pool()
    await pool.execute(
        """update raw.ingest_batches set clock_offset_us=$3, clock_drift_ppm=$4
           where batch_id=$1 and device_id=$2""",
        batch_id, device_id, offset_us, drift_ppm,
    )


async def get_windows(session_id: UUID, channel: str, limit: int = 500):
    pool = await get_pool()
    return await pool.fetch(
        """select channel_id, window_start, sample_rate_hz, n_samples, samples, quality_flag
           from raw.signal_windows
           where session_id=$1 and channel_id=$2 order by window_start limit $3""",
        session_id, channel, limit)


async def get_scalars(session_id: UUID, channel: str, limit: int = 5000):
    pool = await get_pool()
    return await pool.fetch(
        """select sample_time, value, quality_flag from raw.measurements_scalar
           where session_id=$1 and channel_id=$2 order by sample_time limit $3""",
        session_id, channel, limit)


async def get_quality(session_id: UUID):
    pool = await get_pool()
    return await pool.fetch(
        """select channel_id, window_start, sqi, sqi_detail, pipeline_version
           from quality.channel_quality where session_id=$1 order by window_start""",
        session_id)


async def get_features(session_id: UUID, feature_set: str | None = None):
    pool = await get_pool()
    if feature_set:
        return await pool.fetch(
            """select * from features.windows
               where session_id=$1 and feature_set=$2 order by window_start""",
            session_id, feature_set)
    return await pool.fetch(
        "select * from features.windows where session_id=$1 order by window_start",
        session_id)


async def get_predictions(session_id: UUID):
    pool = await get_pool()
    return await pool.fetch(
        "select * from ml.predictions where session_id=$1 order by window_start",
        session_id)
