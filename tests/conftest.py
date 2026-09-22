import asyncio
import os
import secrets
import sys
from pathlib import Path

import asyncpg
import httpx
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

PROFILE = ROOT / "config" / "hardware.example.yaml"
DATABASE_URL = os.environ.get("DATABASE_URL")

requires_db = pytest.mark.skipif(not DATABASE_URL, reason="DATABASE_URL not set")


@pytest.fixture(scope="session")
def seeded():
    """Migrate schema + register one device; returns (device_id, api_key)."""
    from vitalq.ingest.auth import hash_key
    from vitalq.ingest.migrate import _run

    asyncio.run(_run(DATABASE_URL, PROFILE.parents[1] / "supabase" / "migrations"))

    async def _mk():
        conn = await asyncpg.connect(DATABASE_URL)
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
