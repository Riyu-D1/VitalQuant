"""VitalQ research dashboard v0.1 — Streamlit (M1 fast path; Next.js deferred).

Reads Postgres directly (DATABASE_URL). Waveforms, per-channel SQI, trends,
experimental model output — research terminology only: "anomaly score",
never a diagnosis.

Run:  streamlit run apps/dashboard/app.py
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import pandas as pd
import plotly.graph_objects as go
import streamlit as st
from sqlalchemy import create_engine

st.set_page_config(page_title="VitalQ research dashboard", layout="wide")
st.title("VitalQ — research dashboard (prototype)")


@st.cache_resource
def engine():
    dsn = os.environ.get("DATABASE_URL")
    if not dsn:
        st.error("DATABASE_URL not set")
        st.stop()
    # asyncpg DSN → psycopg2
    return create_engine(dsn.replace("postgresql+asyncpg://", "postgresql://"))


@st.cache_data(ttl=5)
def load_sessions() -> pd.DataFrame:
    return pd.read_sql(
        """select s.session_id, s.started_at, s.ended_at, s.data_class, s.body_site,
                  d.label as device, s.firmware_version, s.hardware_revision
           from meta.sessions s join meta.devices d using (device_id)
           order by s.started_at desc limit 200""", engine())


sessions = load_sessions()
if sessions.empty:
    st.info("No sessions yet — run `vitalq-synth` to generate one.")
    st.stop()

sel = st.sidebar.selectbox(
    "Session",
    sessions.apply(lambda r: f"{r['started_at']} · {r['device']} · {r['data_class']}", axis=1))
session_id = sessions.iloc[int(
    sessions.index[sessions.apply(
        lambda r: f"{r['started_at']} · {r['device']} · {r['data_class']}",
        axis=1) == sel][0])]["session_id"]

row = sessions[sessions.session_id == session_id].iloc[0]
if row["data_class"] != "real":
    st.warning(f"data_class = {row['data_class']} — not real sensor data", icon="⚠️")

# ── waveforms ────────────────────────────────────────────────────────────────
st.subheader("Signals")
wins = pd.read_sql(
    """select channel_id, window_start, sample_rate_hz, samples
       from raw.signal_windows where session_id = %(s)s order by window_start""",
    engine(), params={"s": str(session_id)})

if wins.empty:
    st.info("No waveform data in this session.")
else:
    channel = st.selectbox("Channel", sorted(wins.channel_id.unique()))
    ch = wins[wins.channel_id == channel]
    sqi_map = pd.read_sql(
        """select window_start, sqi from quality.channel_quality
           where session_id=%(s)s and channel_id=%(c)s""",
        engine(), params={"s": str(session_id), "c": channel})
    sqi_map = {r.window_start: r.sqi for r in sqi_map.itertuples()}
    st.caption("Trace colour = per-window SQI (red < 0.3 = untrusted).")
    fig = go.Figure()
    for _, r in ch.iterrows():
        t = pd.date_range(r["window_start"], periods=len(r["samples"]),
                          freq=f"{1/r['sample_rate_hz']*1e6:.0f}us")
        # quality rows are per 10 s processing window, raw rows per 5 s ingest
        # window — look up the containing processing window
        q = sqi_map.get(pd.Timestamp(r["window_start"]).floor("10s"), 1.0)
        col = "#c33" if q < 0.3 else ("#da3" if q < 0.6 else "#3a6")
        fig.add_trace(go.Scatter(
            x=t, y=r["samples"], mode="lines", name=channel,
            showlegend=False, opacity=0.85, line=dict(color=col, width=1),
            hovertemplate=f"SQI={q:.2f}<br>%{{x}}<br>%{{y:.0f}}"))
    fig.update_layout(height=320, margin=dict(l=10, r=10, t=10, b=10),
                      xaxis_title="time", yaxis_title=channel)
    st.plotly_chart(fig, use_container_width=True)

    with st.expander("Session compare (fused HR)"):
        other = st.selectbox(
            "Compare with", sessions.index,
            format_func=lambda i: f"{sessions.loc[i,'started_at']} · "
                                  f"{sessions.loc[i,'device']}")
        oid = sessions.loc[other, "session_id"]
        cf = pd.read_sql(
            """select session_id, window_start, values->>'fused_hr_bpm'::float hr
               from features.windows
               where session_id in (%(a)s, %(b)s) and feature_set='fusion_v2'
               order by window_start""",
            engine(), params={"a": str(session_id), "b": str(oid)})
        if cf.empty:
            st.info("No fusion_v2 features for these sessions.")
        else:
            fgc = go.Figure()
            for sid, g in cf.groupby("session_id"):
                fgc.add_trace(go.Scatter(x=g.window_start, y=g.hr,
                                         mode="lines+markers",
                                         name=str(sid)[:8]))
            fgc.update_layout(height=220, yaxis_title="fused_hr_bpm")
            st.plotly_chart(fgc, use_container_width=True)

# ── scalars / trends ─────────────────────────────────────────────────────────
st.subheader("Trends")
scalars = pd.read_sql(
    """select channel_id, sample_time, value from raw.measurements_scalar
       where session_id = %(s)s order by sample_time""",
    engine(), params={"s": str(session_id)})
if not scalars.empty:
    fig2 = go.Figure()
    for ch_id, g in scalars.groupby("channel_id"):
        fig2.add_trace(go.Scatter(x=g.sample_time, y=g.value, mode="lines", name=ch_id))
    fig2.update_layout(height=300, margin=dict(l=10, r=10, t=10, b=10))
    st.plotly_chart(fig2, use_container_width=True)

# ── quality ──────────────────────────────────────────────────────────────────
st.subheader("Data quality")
qual = pd.read_sql(
    """select channel_id, window_start, sqi from quality.channel_quality
       where session_id = %(s)s order by window_start""",
    engine(), params={"s": str(session_id)})
conf = pd.read_sql(
    """select window_start, contact_quality, motion_score, sensor_confidence
       from quality.contact_state where session_id = %(s)s order by window_start""",
    engine(), params={"s": str(session_id)})
if not qual.empty:
    figq = go.Figure()
    for ch_id, g in qual.groupby("channel_id"):
        figq.add_trace(go.Scatter(x=g.window_start, y=g.sqi, name=f"SQI {ch_id}"))
    for col in ("contact_quality", "motion_score", "sensor_confidence"):
        if not conf.empty:
            figq.add_trace(go.Scatter(x=conf.window_start, y=conf[col], name=col,
                                      line=dict(dash="dash")))
    figq.update_layout(height=280, yaxis_range=[0, 1.05],
                       margin=dict(l=10, r=10, t=10, b=10))
    st.plotly_chart(figq, use_container_width=True)
else:
    st.info("No quality rows — run `vitalq-worker` on this session.")

# ── features + model output ──────────────────────────────────────────────────
st.subheader("Features")
feats = pd.read_sql(
    """select window_start, values from features.windows
       where session_id = %(s)s and feature_set='fusion_v2' order by window_start""",
    engine(), params={"s": str(session_id)})
if feats.empty:
    st.info("No features yet — run the worker.")
else:
    st.dataframe(pd.json_normalize(feats["values"]).assign(
        window_start=feats["window_start"]).set_index("window_start"))

st.subheader("Experimental model output")
preds = pd.read_sql(
    """select p.window_start, p.kind, p.value, p.detail
       from ml.predictions p where p.session_id = %(s)s order by p.window_start""",
    engine(), params={"s": str(session_id)})
if preds.empty:
    st.info("No model output — run `python -m vitalq.ml.baselines --sessions <id>`.")
else:
    figp = go.Figure()
    detail = preds["detail"].fillna({})
    hover = [f"top: {', '.join(d.get('top_deviant', []))}"
             if isinstance(d, dict) else "" for d in detail]
    figp.add_trace(go.Scatter(x=preds.window_start, y=preds.value,
                              mode="lines+markers", name="anomaly_score",
                              hovertext=hover))
    figp.add_hline(y=0.8, line_dash="dot", line_color="red",
                   annotation_text="flag threshold (research heuristic)")
    figp.update_layout(height=220, yaxis_title="experimental anomaly score (0–1)")
    st.plotly_chart(figp, use_container_width=True)
    st.caption("Research output only — not a clinical measurement.")
    flagged = preds[preds.value > 0.8]
    if not flagged.empty:
        st.caption(f"{len(flagged)} window(s) above flag threshold")

# ── spectral frames ──────────────────────────────────────────────────────────
spec = pd.read_sql(
    """select sample_time, channels, illumination, read_group
       from raw.spectral_frames where session_id = %(s)s
       order by sample_time desc limit 50""",
    engine(), params={"s": str(session_id)})
if not spec.empty:
    with st.expander("Latest spectral frame"):
        fr = spec.iloc[0]
        st.caption(f"{fr.sample_time} · illumination={fr.illumination} · group={fr.read_group}")
        st.bar_chart(pd.Series(fr["channels"]))


# ── simulation (quantum readout) ────────────────────────────────────────────
# docs/08 §5: surfaced only under an explicit SIMULATED banner; never mixed
# with real telemetry.
sim_files = sorted(Path("experiments/quantum").glob("*.json")) \
    if Path("experiments/quantum").exists() else []
with st.expander("Quantum readout simulation (SIMULATED)", expanded=False):
    st.warning("SIMULATED data only — `data_class='simulated'`. No quantum "
               "hardware exists in VitalQ; this is the docs/08 noise-model "
               "experiment on the raman_illustrative toy signal.")
    if not sim_files:
        st.info("No simulation results — run `vitalq-quantum`.")
    else:
        sim = json.loads(sim_files[-1].read_text())
        st.caption(f"regime: {sim.get('regime')} · cells: {len(sim['cells'])}")
        b = pd.DataFrame(sim["advantage_boundary"])
        if not b.empty:
            st.dataframe(b.set_index(["tissue_sigma", "ambient"]))
            st.caption("Squeezing advantage vs classical readout — collapses as "
                       "tissue/ambient noise dominates (the honest result).")
