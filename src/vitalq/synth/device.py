"""Synthetic device CLI — `vitalq-synth`.

Drives the real API end-to-end (open session → batches → close), or writes
JSONL batches to disk for offline worker tests. All rows carry
data_class='synthetic' — enforced via the session.
"""

from __future__ import annotations

import argparse
import json
import os
from datetime import datetime, timezone

import httpx

from vitalq.core.config import load_profile
from vitalq.synth.generator import CorruptionSpec, SynthConfig, SyntheticDevice


def main() -> None:
    p = argparse.ArgumentParser(description="VitalQ synthetic device")
    p.add_argument("--profile", required=True, help="hardware profile YAML")
    p.add_argument("--duration", type=float, default=300)
    p.add_argument("--batch-s", type=float, default=30)
    p.add_argument("--hr-bpm", type=float, default=65)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--api", help="base URL, e.g. http://localhost:8000")
    p.add_argument("--device-key", default=os.environ.get("VITALQ_DEVICE_KEY"))
    p.add_argument("--subject-id")
    p.add_argument("--body-site", default="bench")
    p.add_argument("--out", help="write JSONL batches here instead of POSTing")
    # corruptions
    p.add_argument("--dropout-p", type=float, default=0.0)
    p.add_argument("--ntp-jump-p", type=float, default=0.0)
    p.add_argument("--clip-p", type=float, default=0.0)
    p.add_argument("--fault-p", type=float, default=0.0)
    args = p.parse_args()

    profile = load_profile(args.profile)
    cfg = SynthConfig(
        duration_s=args.duration, batch_s=args.batch_s, hr_bpm=args.hr_bpm,
        seed=args.seed,
        corruptions=CorruptionSpec(
            dropout_channel_p=args.dropout_p, ntp_jump_p=args.ntp_jump_p,
            clipping_p=args.clip_p, sensor_fault_p=args.fault_p))
    dev = SyntheticDevice(profile, cfg, start=datetime.now(timezone.utc).replace(microsecond=0))
    meta = dev.describe()

    if args.out or not args.api:
        out = args.out or "batches.jsonl"
        with open(out, "w") as f:
            f.write(json.dumps({"session": meta}) + "\n")
            for _, batch in dev.batches():
                f.write(batch.model_dump_json() + "\n")
        print(f"wrote {out}")
        return

    if not args.device_key:
        raise SystemExit("--device-key or VITALQ_DEVICE_KEY required for API mode")

    hdr = {"X-Device-Key": args.device_key}
    with httpx.Client(base_url=args.api, headers=hdr, timeout=30) as c:
        r = c.post("/v1/sessions", json={
            "session_id": meta["session_id"], "subject_id": args.subject_id,
            "firmware_version": meta["firmware_version"],
            "hardware_revision": meta["profile"], "data_class": "synthetic",
            "body_site": args.body_site, "started_at": meta["started_at"],
            "notes": "synthetic device run"})
        r.raise_for_status()
        n_ok = 0
        for _, batch in dev.batches():
            r = c.post("/v1/ingest/batch", content=batch.model_dump_json(),
                       headers={"Content-Type": "application/json"})
            if r.status_code == 409:
                continue  # replay → idempotent
            r.raise_for_status()
            n_ok += 1
        c.post(f"/v1/sessions/{meta['session_id']}/end").raise_for_status()
        print(f"session {meta['session_id']}: {n_ok} batches ingested")


if __name__ == "__main__":
    main()
