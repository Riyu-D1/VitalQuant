#!/usr/bin/env python3
"""plot_sims.py - PNG plots for sim_v24 from results/*.csv (matplotlib)."""
import csv, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "results")
PLT = os.path.join(HERE, "..", "plots")
os.makedirs(PLT, exist_ok=True)

def load(name):
    """wrdata output: whitespace rows, pairs of (index, value) per vector."""
    rows = [r.split() for r in open(os.path.join(RES, name)) if r.strip()]
    data = [[float(x) for x in r] for r in rows]
    cols = list(zip(*data))
    # keep only value columns (odd indices); col0 = first index ~ time
    vals = [cols[i] for i in range(1, len(cols), 2)]
    return cols[0], vals

# ---- TX_5V pulse scenarios A vs B ----
fig, ax = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
for name, lab, c in [("v24_tx5v_A.csv", "A: 200 mA x60 us (staggered)", "tab:green"),
                     ("v24_tx5v_B.csv", "B: ~900 mA x60 us (simultaneous)", "tab:red")]:
    t, c_ = load(name)
    vtx = c_[1]
    ax[0].plot([x*1e3 for x in t], vtx, label=lab, color=c, lw=0.8)
ax[0].axhline(3.0, ls="--", c="k", lw=0.7); ax[0].text(3.1, 3.1, "AFE4900 TX_SUP min 3.0 V", fontsize=7)
ax[0].set_ylabel("TX_5V (V)"); ax[0].set_ylim(2.2, 5.4); ax[0].legend(fontsize=7); ax[0].grid(alpha=.3)
ax[0].set_title("TX_5V boost under LED pulse load (behavioural averaged model, VIN=3.7 V)")
t, c_ = load("v24_tx5v_B.csv")
iin = c_[3]
i0 = next(i for i,x in enumerate(t) if x > 0.4e-3)
ax[1].plot([x*1e3 for x in t[i0:]], [x*1e3 for x in iin[i0:]], color="tab:red", lw=0.8, label="B 900mA")
h, c_ = load("v24_tx5v_A.csv")
t2, c2_ = load("v24_tx5v_A.csv")
i1 = next(i for i,x in enumerate(t2) if x > 0.4e-3)
ax[1].plot([x*1e3 for x in t2[i1:]], [x*1e3 for x in c2_[3][i1:]], color="tab:green", lw=0.8, label="A 200mA")
ax[1].axhline(500, ls="--", c="k", lw=0.7); ax[1].text(0.4, 520, "500 mA input valley limit", fontsize=7)
ax[1].set_ylabel("boost Iin (mA)"); ax[1].set_xlabel("time (ms)"); ax[1].grid(alpha=.3); ax[1].set_ylim(0,600); ax[1].legend(fontsize=7)
fig.tight_layout(); fig.savefig(os.path.join(PLT, "tx5v_pulse.png"), dpi=140)

# ---- +3V3 Wi-Fi burst ----
t, c_ = load("v24_3v3_loadstep.csv")
v = c_[1]; iin = c_[2]
fig, ax = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
ax[0].plot([x*1e3 for x in t], v, color="tab:blue", lw=0.8)
ax[0].axhline(3.3*0.97, ls="--", c="k", lw=0.7); ax[0].axhline(3.3*1.03, ls="--", c="k", lw=0.7)
ax[0].text(6.4, 3.16, "+3V3 +/-3% band", fontsize=7)
ax[0].set_ylabel("+3V3 (V)"); ax[0].set_ylim(3.1, 3.5); ax[0].grid(alpha=.3)
ax[0].set_title("+3V3 buck-boost (behavioural) under Wi-Fi TX bursts 15->355 mA, VIN=3.7 V")
j0 = next(i for i,x in enumerate(t) if x > 0.9e-3)
ax[1].plot([x*1e3 for x in t[j0:]], [x*1e3 for x in iin[j0:]], color="tab:orange", lw=0.8)
ax[1].set_ylim(0, 600)
ax[1].set_ylabel("VBAT_SYS draw (mA)"); ax[1].set_xlabel("time (ms)"); ax[1].grid(alpha=.3)
fig.tight_layout(); fig.savefig(os.path.join(PLT, "v33_loadstep.png"), dpi=140)

# ---- +1V8 ----
t, c_ = load("v24_1v8_loadstep.csv")
v = c_[1]
fig, ax = plt.subplots(figsize=(9, 3.6))
ax.plot([x*1e3 for x in t], v, color="tab:purple", lw=0.8)
ax.axhline(1.8*0.97, ls="--", c="k", lw=0.7); ax.axhline(1.8*1.03, ls="--", c="k", lw=0.7)
ax.set_ylabel("+1V8 (V)"); ax.set_xlabel("time (ms)")
ax.set_title("+1V8 LDO (vendor TPS7A20 model) load step 5->20 mA, VIN=3.3 V")
ax.grid(alpha=.3); fig.tight_layout()
fig.savefig(os.path.join(PLT, "v18_loadstep.png"), dpi=140)

# ---- BQ25170 charge ----
t, c_ = load("v24_bq25170_charge.csv")
vbat = c_[1]; im = c_[2]; soc = c_[3]
fig, ax = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
ax[0].plot([x/60 for x in t], vbat, color="tab:blue", lw=0.9)
ax[0].set_ylabel("VBAT (V)"); ax[0].grid(alpha=.3)
ax[0].set_title("BQ25170 charge profile (behavioural): ICHG=200 mA, VREG=4.20 V, 300 mAh cell")
ax02 = ax[0].twinx()
ax02.plot([x/60 for x in t], [x*1e3 for x in im], color="tab:red", lw=0.9)
ax02.set_ylabel("charge current (mA)", color="tab:red")
ax[1].plot([x/60 for x in t], [x*100 for x in soc], color="tab:green", lw=0.9)
ax[1].set_ylabel("SOC (%)"); ax[1].set_xlabel("time (min)"); ax[1].grid(alpha=.3)
fig.tight_layout(); fig.savefig(os.path.join(PLT, "bq25170_charge.png"), dpi=140)
print("plots written:", os.listdir(PLT))
