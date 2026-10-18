"""Register a device + print its one-time API key. `vitalq-device`.

Usage:  vitalq-device --label "bench-01" --hardware-revision hw_v0
Writes the sha256 of the key to meta.devices; the raw key is printed once and
never stored.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import secrets

import asyncpg

from vitalq.ingest.auth import hash_key


async def _create(dsn: str, label: str, hw_rev: str) -> tuple[str, str]:
    key = "vq_" + secrets.token_urlsafe(24)
    conn = await asyncpg.connect(dsn)
    try:
        device_id = await conn.fetchval(
            """insert into meta.devices (label, hardware_revision, api_key_hash)
               values ($1,$2,$3) returning device_id""",
            label, hw_rev or None, hash_key(key),
        )
    finally:
        await conn.close()
    return str(device_id), key


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--label", required=True)
    p.add_argument("--hardware-revision", default=None)
    args = p.parse_args()
    dsn = os.environ.get("DATABASE_URL")
    if not dsn:
        raise SystemExit("DATABASE_URL not set")
    device_id, key = asyncio.run(_create(dsn, args.label, args.hardware_revision))
    print(f"device_id: {device_id}")
    print(f"api_key:   {key}   (store this — it is not recoverable)")


if __name__ == "__main__":
    main()
