"""Phase-C baseline: per-subject anomaly detection on feature windows.

IsolationForest on windowed features — no labels required (spec §20 L3).
Outputs kind='anomaly_score' only. Never a diagnosis.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import subprocess
from uuid import UUID

import numpy as np
from asyncpg import Range
from sklearn.ensemble import IsolationForest

from vitalq.ingest import db


def _commit() -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], text=True,
            stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unknown"


async def train_personal_baseline(dsn: str, session_ids: list[UUID],
                                  feature_set: str = "fusion_v1",
                                  contamination: float = 0.05) -> dict:
    """Fit IsolationForest on a subject's feature windows; score every window.

    Returns {"experiment_id": ..., "model_id": ..., "scored": n}.
    """
    conn = await db.connect(dsn)
    try:
        rows = await conn.fetch(
            """select session_id, window_start, values from features.windows
               where session_id = any($1::uuid[]) and feature_set=$2
               order by window_start""", session_ids, feature_set)
        if len(rows) < 20:
            return {"error": "insufficient feature windows (<20)"}

        keys = sorted({k for r in rows for k in r["values"]})
        X = np.array([[r["values"].get(k, np.nan) for k in keys]
                      for r in rows], dtype=float)
        # simple missing handling: column median; fully-nan columns dropped
        col_ok = ~np.isnan(X).all(axis=0)
        X, keys = X[:, col_ok], [k for k, ok in zip(keys, col_ok, strict=False) if ok]
        med = np.nanmedian(X, axis=0)
        X = np.where(np.isnan(X), med, X)

        model = IsolationForest(n_estimators=100, contamination=contamination,
                                random_state=0)
        model.fit(X)
        scores = -model.score_samples(X)   # higher = more anomalous
        lo, hi = float(scores.min()), float(scores.max())
        norm = (scores - lo) / max(hi - lo, 1e-9)

        exp_id = await conn.fetchval(
            """insert into ml.experiments
               (name, dataset_version, feature_set, model_spec, code_commit)
               values ($1,$2,$3,$4::jsonb,$5) returning experiment_id""",
            "personal_baseline_isoforest", f"sessions:{len(session_ids)}",
            feature_set,
            {"class": "IsolationForest", "features": keys,
             "contamination": contamination, "seed": 0},
            _commit())
        model_id = await conn.fetchval(
            """insert into ml.models (experiment_id, metrics, train_window)
               values ($1,$2::jsonb,$3) returning model_id""",
            exp_id, {"n_windows": len(rows), "score_range": [lo, hi]},
            Range(rows[0]["window_start"], rows[-1]["window_start"],
                  lower_inc=True, upper_inc=False, empty=False))

        await conn.executemany(
            """insert into ml.predictions
               (session_id, window_start, model_id, kind, value, detail)
               values ($1,$2,$3,'anomaly_score',$4,$5::jsonb)
               on conflict do nothing""",
            [(r["session_id"], r["window_start"], model_id, round(float(s), 4),
              {"n_features": len(keys)})
             for r, s in zip(rows, norm, strict=False)])
        return {"experiment_id": str(exp_id), "model_id": str(model_id),
                "scored": len(rows)}
    finally:
        await conn.close()


def main() -> None:
    dsn = os.environ.get("DATABASE_URL")
    if not dsn:
        raise SystemExit("DATABASE_URL not set")
    p = argparse.ArgumentParser()
    p.add_argument("--sessions", nargs="+", required=True)
    args = p.parse_args()
    print(asyncio.run(train_personal_baseline(dsn, [UUID(s) for s in args.sessions])))


if __name__ == "__main__":
    main()
