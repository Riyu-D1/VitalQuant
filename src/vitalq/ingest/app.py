"""vitalq-ingest FastAPI app — device + dashboard endpoints (docs/05)."""

from __future__ import annotations

import logging
import os
import time
from collections import defaultdict
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from uuid import UUID

import asyncpg
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse

import vitalq
from vitalq.core.clock import ClockAnchor, ClockModel
from vitalq.core.types import (
    BatchIngest,
    DeviceEventIn,
    LabelIn,
    SessionCreate,
    SubjectCreate,
)
from vitalq.ingest import db
from vitalq.ingest.auth import Device, DeviceDep, UserDep

log = logging.getLogger("vitalq.ingest")


@asynccontextmanager
async def _lifespan(app_: FastAPI):
    yield
    await db.close_pool()


app = FastAPI(title="vitalq-ingest", version=vitalq.__version__, lifespan=_lifespan)


# ── rate limiting (write endpoints) ──────────────────────────────────────────
# Token bucket per client key (device key hash or IP). In-memory — per-process;
# Supabase deploys get real limiting at the gateway. VITALQ_RATE_RPS tunes it.

_RATE_RPS = float(os.environ.get("VITALQ_RATE_RPS", "50"))
_RATE_BURST = _RATE_RPS * 2
_buckets: dict[str, list[float]] = defaultdict(list)   # key -> [tokens, last_ts]


def _rate_ok(key: str) -> bool:
    tok, last = _buckets.get(key, [_RATE_BURST, 0.0])
    now = time.monotonic()
    tok = min(_RATE_BURST, tok + (now - last) * _RATE_RPS)
    if tok < 1.0:
        _buckets[key] = [tok, now]
        return False
    _buckets[key] = [tok - 1.0, now]
    return True


@app.middleware("http")
async def rate_limit(request: Request, call_next):
    if request.method == "POST" and request.url.path.startswith("/v1/"):
        key = request.headers.get("X-Device-Key") or (
            request.client.host if request.client else "anon")
        if not _rate_ok(key):
            return JSONResponse({"error": {"code": "rate.limited",
                                           "message": "rate limit exceeded"}},
                                status_code=429)
    return await call_next(request)


@app.exception_handler(HTTPException)
async def http_exc(request: Request, exc: HTTPException):
    """VitalQ errors are {error:{code,message}} — unwrap FastAPI's detail wrapper."""
    detail = exc.detail
    if isinstance(detail, dict) and "error" in detail:
        return JSONResponse(detail, status_code=exc.status_code)
    return JSONResponse({"error": {"code": "error", "message": str(detail)}},
                        status_code=exc.status_code)


@app.exception_handler(Exception)
async def unhandled(request: Request, exc: Exception):  # pragma: no cover
    log.exception("unhandled error")
    return JSONResponse({"error": {"code": "internal", "message": str(exc)}},
                        status_code=500)


# ── device endpoints ─────────────────────────────────────────────────────────

@app.post("/v1/sessions", status_code=201)
async def open_session(body: SessionCreate, device: Device = DeviceDep):
    try:
        await db.create_session(device.device_id, body)
    except asyncpg.ForeignKeyViolationError as e:
        raise HTTPException(400, {"error": {"code": "schema.fk",
                "message": "unknown subject_id or hardware_revision", "detail": str(e)}}) from e
    except asyncpg.UniqueViolationError:
        raise HTTPException(409, {"error": {"code": "session.exists",
                "message": "session_id already registered"}}) from None
    return {"session_id": str(body.session_id)}


@app.post("/v1/sessions/{session_id}/end")
async def close_session(session_id: UUID, device: Device = DeviceDep):
    ok = await db.end_session(device.device_id, session_id)
    if not ok:
        raise HTTPException(409, {"error": {"code": "session.state",
                "message": "session not found, not owned, or already ended"}})
    return {"session_id": str(session_id), "ended": True}


@app.post("/v1/ingest/batch", status_code=202)
async def ingest_batch(body: BatchIngest, request: Request, device: Device = DeviceDep):
    session = await db.session_row(body.session_id)
    if session is None:
        raise HTTPException(404, {"error": {"code": "session.unknown",
                "message": "open the session before ingesting"}})
    if session["device_id"] != device.device_id:
        raise HTTPException(403, {"error": {"code": "auth.session_mismatch",
                "message": "session belongs to a different device"}})
    if session["ended_at"] is not None:
        raise HTTPException(409, {"error": {"code": "session.state",
                "message": "session is closed"}})
    data_class = session["data_class"]

    raw = await request.body()
    if len(raw) > 2_000_000:
        raise HTTPException(413, {"error": {"code": "payload.too_large"}})

    # Clock model: session anchors + this batch's anchor → corrected timezone.utc stamps
    prior = await db.session_clock_anchors(body.session_id)
    anchors = [ClockAnchor(r["clock_monotonic_us"], r["clock_wall_time"]) for r in prior]
    model = ClockModel(anchors)
    clock_flags = model.add_anchor(
        ClockAnchor(body.clock.monotonic_us, body.clock.wall_time))

    fresh = await db.insert_batch_header(
        device.device_id, body.session_id, body.clock.wall_time,
        body.clock.monotonic_us, body.batch_id, len(raw))
    if not fresh:
        raise HTTPException(409, {"error": {"code": "batch.replay",
                "message": "batch_id already ingested — safe to ignore on retransmission"}})

    def utc(t_us: int) -> datetime:
        return model.to_utc(t_us)

    accepted = 0
    try:
        await db.insert_scalars([
            (device.device_id, body.session_id, s.channel, utc(s.t_us), s.t_us, s.v, data_class)
            for s in body.scalars])
        accepted += len(body.scalars)
        await db.insert_windows([
            (device.device_id, body.session_id, w.channel, utc(w.t_start_us),
             w.rate_hz, len(w.samples), w.t_start_us, w.samples, data_class)
            for w in body.windows])
        accepted += len(body.windows)
        await db.insert_frames([
            (device.device_id, body.session_id, utc(f.t_us), f.t_us, f.read_group,
             f.channels, f.gain_x, f.integ_ms,
             f.illumination, f.sensor_temp_c, data_class)
            for f in body.frames])
        accepted += len(body.frames)
        await db.insert_events([
            (device.device_id, body.session_id, utc(e.t_us), e.kind,
             e.detail)
            for e in body.events])
        accepted += len(body.events)
    except asyncpg.PostgresError as e:
        # Roll back the anchor row: the payload never landed, so a corrected
        # retransmission of the same batch_id must not be treated as a replay.
        await db.delete_batch_header(device.device_id, body.batch_id)
        raise HTTPException(400, {"error": {"code": "ingest.db",
                "message": f"batch rejected: {e.message}"}}) from e

    if clock_flags:
        await db.insert_events([(
            device.device_id, body.session_id, datetime.now(timezone.utc),
            "clock_jump", {"batch_id": str(body.batch_id)})])

    await db.update_batch_clock(body.batch_id, device.device_id,
                                model.offset_us(body.clock.monotonic_us), model.drift_ppm())
    return {"accepted": accepted, "batch_id": str(body.batch_id),
            "clock_flags": clock_flags,
            "server_time": datetime.now(timezone.utc).isoformat(),
            "clock_correction": {"offset_us": model.offset_us(body.clock.monotonic_us),
                                 "drift_ppm": model.drift_ppm()}}


@app.post("/v1/events", status_code=202)
async def device_events(events: list[DeviceEventIn], session_id: UUID,
                        device: Device = DeviceDep):
    model = ClockModel()  # events here carry wall time? keep device t_us anchored by last batch
    prior = await db.session_clock_anchors(session_id)
    for r in prior:
        model.add_anchor(ClockAnchor(r["clock_monotonic_us"], r["clock_wall_time"]))
    if not model.anchors:
        raise HTTPException(409, {"error": {"code": "clock.unanchored",
                "message": "no ingest batch seen yet — send events inside a batch first"}})
    await db.insert_events([(
        device.device_id, session_id, model.to_utc(e.t_us), e.kind,
        e.detail) for e in events])
    return {"accepted": len(events)}


# ── subjects + ground-truth labels (docs/04, docs/07 L4 plumbing) ─────────────

@app.post("/v1/subjects", status_code=201, dependencies=[UserDep])
async def create_subject(body: SubjectCreate):
    sid = await db.create_subject(body.external_ref, body.consent_ref)
    return {"subject_id": str(sid)}


@app.post("/v1/sessions/{session_id}/labels", status_code=201)
async def add_label(session_id: UUID, body: LabelIn, request: Request):
    # labels arrive from the device stream OR a researcher — accept either auth
    if request.headers.get("X-Device-Key"):
        from vitalq.ingest.auth import device_auth
        device = await device_auth(request)
        session = await db.session_row(session_id)
        if session and session["device_id"] != device.device_id:
            raise HTTPException(403, {"error": {"code": "auth.session_mismatch"}})
    else:
        from vitalq.ingest.auth import user_auth
        await user_auth(request)
    if await db.session_row(session_id) is None:
        raise HTTPException(404, {"error": {"code": "session.unknown"}})
    fresh = await db.insert_label(session_id, body.label_time, body.kind,
                                  body.value, body.provenance)
    return {"label_id": str(session_id), "inserted": fresh}


@app.get("/v1/sessions/{session_id}/labels", dependencies=[UserDep])
async def get_labels(session_id: UUID):
    return _rows(await db.get_labels(session_id))


# ── read endpoints (dashboard) ────────────────────────────────────────────────

def _rows(rs) -> list[dict]:
    return [dict(r) for r in rs]


@app.get("/v1/sessions/{session_id}", dependencies=[UserDep])
async def get_session(session_id: UUID):
    row = await db.session_row(session_id)
    if row is None:
        raise HTTPException(404, {"error": {"code": "session.unknown"}})
    return dict(row)


@app.get("/v1/sessions", dependencies=[UserDep])
async def sessions(device_id: UUID | None = None, limit: int = 100):
    return _rows(await db.list_sessions(device_id, min(limit, 500)))


@app.get("/v1/signals/{session_id}/{channel}", dependencies=[UserDep])
async def signals(session_id: UUID, channel: str, limit: int = 500):
    return _rows(await db.get_windows(session_id, channel, min(limit, 2000)))


@app.get("/v1/scalars/{session_id}/{channel}", dependencies=[UserDep])
async def scalars(session_id: UUID, channel: str, limit: int = 5000):
    return _rows(await db.get_scalars(session_id, channel, min(limit, 20000)))


@app.get("/v1/quality/{session_id}", dependencies=[UserDep])
async def quality(session_id: UUID):
    return _rows(await db.get_quality(session_id))


@app.get("/v1/features/{session_id}", dependencies=[UserDep])
async def features(session_id: UUID, set: str | None = None):
    return _rows(await db.get_features(session_id, set))


@app.get("/v1/predictions/{session_id}", dependencies=[UserDep])
async def predictions(session_id: UUID):
    return _rows(await db.get_predictions(session_id))


@app.get("/v1/health")
async def health():
    return {"status": "ok"}


@app.get("/v1/version")
async def version():
    return {"version": vitalq.__version__}
