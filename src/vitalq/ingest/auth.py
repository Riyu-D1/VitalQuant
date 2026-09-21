"""Device auth: X-Device-Key → sha256 → meta.devices.api_key_hash.

AUTH_MODE env: 'local' (prototype/dev — device keys still required for writes;
user/JWT checks skipped) or 'supabase' (JWT verification hook — point at your
Supabase JWKS when deploying; M1 ships the hook + a loud 501 rather than a fake).
"""

from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass
from uuid import UUID

from fastapi import Depends, HTTPException, Request

from vitalq.ingest import db

AUTH_MODE = os.environ.get("VITALQ_AUTH_MODE", "local")


@dataclass
class Device:
    device_id: UUID
    hardware_revision: str | None


def hash_key(raw_key: str) -> str:
    return hashlib.sha256(raw_key.encode()).hexdigest()


async def device_auth(request: Request) -> Device:
    key = request.headers.get("X-Device-Key")
    if not key:
        raise HTTPException(401, {"error": {"code": "auth.missing_key",
                                            "message": "X-Device-Key header required"}})
    row = await db.device_for_key_hash(hash_key(key))
    if row is None:
        raise HTTPException(401, {"error": {"code": "auth.invalid_key",
                                            "message": "unknown device key"}})
    if row["revoked"]:
        raise HTTPException(403, {"error": {"code": "auth.revoked",
                                            "message": "device key revoked"}})
    return Device(device_id=row["device_id"], hardware_revision=row["hardware_revision"])


async def user_auth(request: Request) -> None:
    """Dashboard/researcher auth. Local mode is open; supabase mode needs wiring."""
    if AUTH_MODE == "local":
        return
    raise HTTPException(501, {"error": {"code": "auth.jwt_unconfigured",
                                        "message": "wire Supabase JWKS verification "
                                         "before deploying"}})


DeviceDep = Depends(device_auth)
UserDep = Depends(user_auth)
