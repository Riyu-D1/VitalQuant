"""API contract + vertical-slice integration test (Postgres-backed).

Skipped without DATABASE_URL — run `docker compose up -d db && vitalq-migrate`
locally; CI provides the postgres service and runs migrations first.
"""

import asyncio
import os
import secrets
from datetime import datetime, timezone
from uuid import UUID, uuid4

import asyncpg
import httpx
import pytest
from conftest import PROFILE, requires_db

pytestmark = requires_db

DSN = os.environ.get("DATABASE_URL")


@pytest.fixture(scope="session")
def seeded():
    """Migrate schema + register one device; returns (device_id, api_key)."""
    from vitalq.ingest.auth import hash_key
    from vitalq.ingest.migrate import _run

    asyncio.run(_run(DSN, PROFILE.parents[1] / "supabase" / "migrations"))

    async def _mk():
        conn = await asyncpg.connect(DSN)
        try:
            key = "vq_test_" + secrets.token_urlsafe(12)
            for rev in ("hw_v0", "hw_v1"):
                await conn.execute(
                    """insert into config.hardware_revisions (revision, profile)
                       values ($1,'{}'::jsonb) on conflict do nothing""", rev)
            did = await conn.fetchval(
                """insert into meta.devices (label, hardware_revision, api_key_hash)
                   values ($1,'hw_v1',$2) returning device_id""",
                "pytest-device", hash_key(key))
            return str(did), key
        finally:
            await conn.close()

    return asyncio.run(_mk())


@pytest.fixture()
async def client():
    from vitalq.ingest import db
    from vitalq.ingest.app import app
    async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app),
                                 base_url="http://test") as c:
        yield c
    if db._pool is not None:
        await db.close_pool()


async def _open_session(client, key):
    sid = str(uuid4())
    r = await client.post("/v1/sessions", headers={"X-Device-Key": key}, json={
        "session_id": sid, "firmware_version": "test-0.0.1",
        "hardware_revision": "hw_v1", "data_class": "synthetic",
        "body_site": "bench", "started_at": datetime.now(timezone.utc).isoformat()})
    assert r.status_code == 201, r.text
    return sid


async def test_health(client):
    assert (await client.get("/v1/health")).json()["status"] == "ok"


async def test_auth_required(client):
    assert (await client.post("/v1/sessions", json={})).status_code == 401
    assert (await client.post("/v1/sessions", headers={"X-Device-Key": "vq_bad"},
                              json={})).status_code == 401


async def test_vertical_slice(client, seeded):
    _, key = seeded
    sid = await _open_session(client, key)

    # synthetic batches via the real generator (schema-consistent by construction)
    from vitalq.core.config import load_profile
    from vitalq.synth.generator import CorruptionSpec, SynthConfig, SyntheticDevice
    prof = load_profile(str(PROFILE))
    dev = SyntheticDevice(prof, SynthConfig(
        session_id=UUID(sid), duration_s=240, batch_s=15,
        seed=5, corruptions=CorruptionSpec()))
    batches = [b.model_dump(mode="json") for _, b in dev.batches()]
    for payload in batches:
        r = await client.post("/v1/ingest/batch", json=payload,
                              headers={"X-Device-Key": key})
        assert r.status_code == 202, r.text
        assert r.json()["accepted"] > 0

    # idempotent replay → 409 batch.replay
    r = await client.post("/v1/ingest/batch", json=batches[0],
                          headers={"X-Device-Key": key})
    assert r.status_code == 409
    assert r.json()["error"]["code"] == "batch.replay"

    # reads
    got = (await client.get(f"/v1/sessions/{sid}")).json()
    assert got["session_id"] == sid
    assert (await client.get(f"/v1/signals/{sid}/ppg.ir")).json()
    assert (await client.get(f"/v1/scalars/{sid}/temp.object")).json()

    assert (await client.post(f"/v1/sessions/{sid}/end",
                              headers={"X-Device-Key": key})).status_code == 200

    # processing → quality + features
    from vitalq.processing.worker import process_session
    stats = await process_session(DSN, UUID(sid))
    assert stats["features"] > 0

    q = (await client.get(f"/v1/quality/{sid}")).json()
    assert any(row["channel_id"] == "ppg.ir" for row in q)
    f = (await client.get(f"/v1/features/{sid}")).json()
    assert f and all(row["data_class"] == "synthetic" for row in f)

    # ML baseline → predictions
    from vitalq.ml.baselines import train_personal_baseline
    out = await train_personal_baseline(DSN, [UUID(sid)])
    assert out.get("scored", 0) > 0
    p = (await client.get(f"/v1/predictions/{sid}")).json()
    assert p and all(row["kind"] == "anomaly_score" for row in p)
