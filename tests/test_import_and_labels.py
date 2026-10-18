"""Import path + subjects/labels endpoints + rate limit tests (Postgres-backed)."""

import csv
import os
from datetime import datetime, timezone
from uuid import UUID, uuid4

import numpy as np
import pytest
from conftest import requires_db

pytestmark = requires_db

DSN = os.environ.get("DATABASE_URL")


@pytest.mark.usefixtures("seeded")
class TestImport:
    async def test_csv_import_round_trip(self, client, tmp_path, seeded):
        device_id, key = seeded
        fs, dur = 100, 30
        t = np.arange(fs * dur) / fs
        ppg = 40000 + 2000 * np.sin(2 * np.pi * 1.1 * t)
        path = tmp_path / "rec.csv"
        with open(path, "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["pleth"])
            w.writerows([[v] for v in ppg])

        from vitalq.datasets import importer
        data = importer.read_csv_columns(str(path), ["pleth"])
        sid = uuid4()
        await importer.open_session(client, key, sid,
                                    datetime.now(timezone.utc), "real",
                                    "pytest csv")
        n = await importer.ingest_waveform(client, key, sid, "ppg.ir",
                                          data["pleth"], fs,
                                          datetime.now(timezone.utc))
        assert n >= 2

        import asyncpg
        conn = await asyncpg.connect(DSN)
        try:
            cnt = await conn.fetchval(
                """select count(*) from raw.signal_windows
                   where session_id=$1 and channel_id='ppg.ir'""", sid)
            assert cnt >= 5
            cls = await conn.fetchval(
                "select data_class from meta.sessions where session_id=$1", sid)
            assert cls == "real"
        finally:
            await conn.close()

    async def test_subjects_and_labels(self, client, seeded):
        device_id, key = seeded
        r = await client.post("/v1/subjects", json={"external_ref": "S001",
                                                    "consent_ref": "C-001"})
        assert r.status_code == 201
        subject_id = UUID(r.json()["subject_id"])

        sid = uuid4()
        now = datetime.now(timezone.utc)
        r = await client.post("/v1/sessions",
                              headers={"X-Device-Key": key},
                              json={"session_id": str(sid),
                                    "firmware_version": "t", "data_class": "real",
                                    "hardware_revision": "hw_v1",
                                    "subject_id": str(subject_id),
                                    "started_at": now.isoformat()})
        assert r.status_code == 201
        r = await client.post(f"/v1/sessions/{sid}/labels",
                              headers={"X-Device-Key": key},
                              json={"label_time": now.isoformat(),
                                    "kind": "rest",
                                    "value": {"posture": "seated"},
                                    "provenance": "pytest"})
        assert r.status_code == 201 and r.json()["inserted"]
        r = await client.get(f"/v1/sessions/{sid}/labels")
        assert r.status_code == 200
        assert r.json()[0]["kind"] == "rest"

    async def test_rate_limit(self, client, seeded):
        device_id, key = seeded
        import vitalq.ingest.app as app_mod
        old = (app_mod._RATE_RPS, app_mod._RATE_BURST)
        app_mod._RATE_RPS, app_mod._RATE_BURST = 0.5, 0.6
        app_mod._buckets.clear()
        try:
            now = datetime.now(timezone.utc)
            sid = uuid4()
            body = {"session_id": str(sid), "firmware_version": "t",
                    "hardware_revision": "hw_v1", "data_class": "real",
                    "started_at": now.isoformat()}
            r1 = await client.post("/v1/sessions", json=body,
                                   headers={"X-Device-Key": key})
            # second POST with same key — bucket empty → 429
            r2 = await client.post("/v1/sessions", json={
                **body, "session_id": str(uuid4())},
                headers={"X-Device-Key": key})
            assert r1.status_code in (201, 429)
            assert r2.status_code == 429
            assert r2.json()["error"]["code"] == "rate.limited"
        finally:
            app_mod._RATE_RPS, app_mod._RATE_BURST = old
            app_mod._buckets.clear()


def test_template_match_recovers_beats():
    """A window with a corrupted segment: template match should still count ~all
    beats where the peak finder drops some."""
    from vitalq.processing import ppg
    fs, dur = 200.0, 10
    t = np.arange(int(fs * dur)) / fs
    ibi = 60.0 / 65.0
    ph = (t % ibi) / ibi
    pulse = np.exp(-0.5 * ((ph - 0.10) / 0.045) ** 2) + \
        0.35 * np.exp(-0.5 * ((ph - 0.38) / 0.07) ** 2)
    x = pulse * 2400 + np.random.default_rng(1).normal(0, 20, len(t))
    # corrupt 2 s mid-window with heavy noise — peak finder misses beats there
    x[int(4 * fs):int(6 * fs)] += np.random.default_rng(2).normal(0, 1500, int(2 * fs))
    res = ppg.process_ppg_window(x.tolist(), fs)
    assert res["tm_n_beats"] >= res["n_beats"] - 1   # recovers at least as many
    assert res["tm_ncc_mean"] is not None
    assert res["tm_hr_bpm"] is not None
    assert abs(res["tm_hr_bpm"] - 65) < 5
