"""Experiment runner: subject-level split evaluation for the anomaly ensemble.

Honest eval without real labels (docs/07): anomalies are *injected* into held-
out sessions' feature vectors (declared multipliers in `inject`), scored, and
metrics computed against the injection mask. model_spec records the injection
params so an experiments row can never be mistaken for real-label performance.

CLI: `vitalq-experiment --sessions <uuid...>`.
"""

from __future__ import annotations

import argparse
import asyncio
import os
from uuid import UUID

import numpy as np
from sklearn.preprocessing import RobustScaler

from vitalq.ingest import db
from vitalq.ml.baselines import _commit
from vitalq.ml.detectors import AnomalyEnsemble

DEFAULT_INJECT = {"fused_hr_bpm": ("mul", 1.6), "temp_c": ("add", 3.0),
                  "fused_sqi": ("mul", 0.1)}


async def _load_feature_rows(conn, session_id, feature_set="fusion_v2"):
    return await conn.fetch(
        """select window_start, values from features.windows
           where session_id=$1 and feature_set=$2 order by window_start""",
        session_id, feature_set)


def _matrix(rows, keys):
    def num(k, v):
        # feature values may carry provenance strings (e.g. hr_source) — only
        # numerics can enter the detector matrix
        return float(v) if isinstance(v, (int, float)) else np.nan
    X = np.array([[num(k, r["values"].get(k)) for k in keys] for r in rows],
                 dtype=float)
    med = np.nanmedian(X, axis=0)
    med = np.where(np.isnan(med), 0.0, med)
    return np.where(np.isnan(X), med, X)


def auroc(y_true: np.ndarray, y_score: np.ndarray) -> float | None:
    """Rank-based AUROC (Mann–Whitney). None if a single class."""
    y_true = np.asarray(y_true, dtype=bool)
    if y_true.all() or (~y_true).all():
        return None
    order = np.argsort(y_score, kind="stable")
    ranks = np.empty(len(y_score))
    ranks[order] = np.arange(1, len(y_score) + 1)
    n_pos, n_neg = y_true.sum(), (~y_true).sum()
    return float((ranks[y_true].sum() - n_pos * (n_pos + 1) / 2)
                 / (n_pos * n_neg))


def auprc(y_true: np.ndarray, y_score: np.ndarray) -> float | None:
    """Average precision."""
    y_true = np.asarray(y_true, dtype=bool)
    if not y_true.any():
        return None
    order = np.argsort(-y_score)
    tp = np.cumsum(y_true[order])
    prec = tp / np.arange(1, len(tp) + 1)
    return float((prec * y_true[order]).sum() / y_true.sum())


def _inject(X: np.ndarray, keys: list[str], spec: dict, frac: float,
            rng: np.random.Generator) -> tuple[np.ndarray, np.ndarray]:
    """Perturb a random `frac` of windows per the declared `spec`; returns
    (X_perturbed, labels)."""
    Xi = X.copy()
    n_inj = max(1, int(frac * len(X)))
    idx = rng.choice(len(X), size=n_inj, replace=False)
    labels = np.zeros(len(X), dtype=bool)
    labels[idx] = True
    for name, (op, val) in spec.items():
        if name in keys:
            c = keys.index(name)
            Xi[idx, c] = Xi[idx, c] * val if op == "mul" else Xi[idx, c] + val
    return Xi, labels


async def run_experiment(dsn: str, session_ids: list[UUID], inject_frac=0.05,
                         contamination=0.05, seed=0) -> dict:
    """Leave-one-session-out eval of AnomalyEnsemble against injected anomalies.
    Writes an ml.experiments + ml.models row per fold."""
    conn = await db.connect(dsn)
    try:
        per_session = {s: await _load_feature_rows(conn, s)
                       for s in session_ids}
        per_session = {s: r for s, r in per_session.items() if len(r) >= 8}
        if len(per_session) < 2:
            raise ValueError("need >=2 sessions with >=8 feature windows")
        all_keys = sorted({k for rows in per_session.values() for r in rows
                           for k in r["values"]})
        rng = np.random.default_rng(seed)
        folds = []
        for held in per_session:
            train_rows = [r for s, rows in per_session.items() for r in rows
                          if s != held]
            Xtr = _matrix(train_rows, all_keys)
            scaler = RobustScaler().fit(Xtr)
            ens = AnomalyEnsemble().fit(scaler.transform(Xtr), contamination)
            Xte = _matrix(per_session[held], all_keys)
            Xi, labels = _inject(Xte, all_keys, DEFAULT_INJECT,
                                 inject_frac, rng)
            scores = ens.score_parts(scaler.transform(Xi))["ensemble"]
            folds.append({
                "session": str(held),
                "n_windows": len(Xte), "n_injected": int(labels.sum()),
                "auroc": auroc(labels, scores), "auprc": auprc(labels, scores),
                "scores": scores, "labels": labels, "rows": per_session[held],
            })

        aucs = [f["auroc"] for f in folds if f["auroc"] is not None]
        prs = [f["auprc"] for f in folds if f["auprc"] is not None]
        metrics = {"auroc_mean": float(np.mean(aucs)) if aucs else None,
                   "auprc_mean": float(np.mean(prs)) if prs else None,
                   "n_folds": len(folds),
                   "inject_frac": inject_frac}

        exp_id = await conn.fetchval(
            """insert into ml.experiments
               (name, dataset_version, feature_set, model_spec, code_commit)
               values ($1,$2,$3,$4::jsonb,$5) returning experiment_id""",
            "ensemble_loso_injected",
            f"sessions:{len(per_session)}", "fusion_v2",
            {"class": "AnomalyEnsemble",
             "detectors": ["iforest", "pca_recon", "lag_fcast"],
             "split": "leave_one_session_out",
             "injected_labels": {"frac": inject_frac, "spec": DEFAULT_INJECT},
             "honesty": "labels are injected synthetic perturbations — not "
                        "real ground truth", "seed": seed},
            _commit())
        model_id = await conn.fetchval(
            """insert into ml.models (experiment_id, metrics)
               values ($1,$2::jsonb) returning model_id""",
            exp_id, metrics)

        # store held-out scores as predictions (kind anomaly_score)
        preds = []
        for f in folds:
            for r, s, lab in zip(f["rows"], f["scores"], f["labels"],
                                 strict=False):
                preds.append((UUID(f["session"]), r["window_start"], model_id,
                              round(float(s), 4),
                              {"injected_label": bool(lab), "fold": "held_out"}))
        await conn.executemany(
            """insert into ml.predictions
               (session_id, window_start, model_id, kind, value, detail)
               values ($1,$2,$3,'anomaly_score',$4,$5::jsonb)
               on conflict do nothing""", preds)
        return {"experiment_id": str(exp_id), "model_id": str(model_id),
                **metrics}
    finally:
        await conn.close()


def main() -> None:
    dsn = os.environ.get("DATABASE_URL")
    if not dsn:
        raise SystemExit("DATABASE_URL not set")
    p = argparse.ArgumentParser()
    p.add_argument("--sessions", nargs="+", required=True)
    p.add_argument("--inject-frac", type=float, default=0.05)
    args = p.parse_args()
    print(asyncio.run(run_experiment(dsn, [UUID(s) for s in args.sessions],
                                     inject_frac=args.inject_frac)))


if __name__ == "__main__":
    main()
