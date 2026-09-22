"""Apply supabase/migrations/*.sql to DATABASE_URL in order. `vitalq-migrate`."""

from __future__ import annotations

import asyncio
import os
from pathlib import Path

import asyncpg

from vitalq.core.channels import CHANNELS

MIGRATIONS_DIR = Path(__file__).resolve().parents[3] / "supabase" / "migrations"


async def _run(dsn: str, migrations_dir: Path) -> list[str]:
    conn = await asyncpg.connect(dsn)
    applied: list[str] = []
    try:
        await conn.execute("create schema if not exists meta")
        await conn.execute(
            "create table if not exists meta._migrations "
            "(name text primary key, applied_at timestamptz default now())"
        )
        done = {r["name"] for r in await conn.fetch("select name from meta._migrations")}
        for path in sorted(migrations_dir.glob("*.sql")):
            if path.name in done:
                continue
            await conn.execute(path.read_text())
            await conn.execute("insert into meta._migrations (name) values ($1)", path.name)
            applied.append(path.name)
        # config.channels is seeded by 0001 but the Python registry is the source of
        # truth — upsert every registered channel so the FK tables never drift.
        await conn.executemany(
            "insert into config.channels (channel_id, sensor_type, unit, sample_role) "
            "values ($1, $2, $3, $4) on conflict (channel_id) do nothing",
            [
                (c.channel_id, c.sensor_type.value, c.unit, c.sample_role.value)
                for c in CHANNELS.values()
            ],
        )
    finally:
        await conn.close()
    return applied


def main() -> None:
    dsn = os.environ.get("DATABASE_URL")
    if not dsn:
        raise SystemExit("DATABASE_URL not set")
    migrations_dir = Path(os.environ.get("VITALQ_MIGRATIONS_DIR", MIGRATIONS_DIR))
    applied = asyncio.run(_run(dsn, migrations_dir))
    print("applied:" if applied else "up to date", ", ".join(applied))


if __name__ == "__main__":
    main()
