# SIM_README — how to reproduce sim_v24

Everything here is **reports only**; `fix_v24/` board files are never modified (all work happens on copies or in `sim_v24/`).

## Tool versions used

| tool | version |
|---|---|
| kicad-cli | `/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli` → **10.0.4** |
| ngspice | `/usr/local/bin/ngspice` → **ngspice-47** (KLU) |
| python | `/Library/Frameworks/Python.framework/Versions/3.12/bin/python3` → 3.12 |
| numpy / scipy / matplotlib | 2.4.2 / 1.17.1 / 3.10.8 |

## Directory map

```
sim_v24/
├── REPORT.md            main report (labels: checked/inferred/guess)
├── PROGRESS.md          step log
├── decks/               ngspice decks (*.sp) + shared includes (*.inc)
├── models/              SPICE models copied from sims/fullsim_v20 (read-only source)
├── scripts/             python parsers + budget + plotting
├── data/                netlist.json, power_budget.csv, runtime.csv, rf_analysis.json…
├── results/             ngspice CSVs + logs
├── plots/               PNGs (antenna_keepout, tx5v_pulse, v33/v18, bq25170_charge)
└── _work/               scratch: drc/ board copies, gerbers, datasheets, repo clone
```

## Re-run everything

```sh
cd /Users/deepakdiwan/Downloads/VitalQ_Hand/sim_v24

# 1) schematic → netlist → rail map (fresh from fix_v24, read-only)
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli sch export netlist \
    ../fix_v24/vitalq_v2.kicad_sch -o _work/v24.net
python3 scripts/parse_netlist.py        # writes data/netlist.json, data/all_nets.txt

# 2) power budget + runtime
python3 scripts/power_budget.py         # writes data/power_budget.csv, data/runtime.csv

# 3) SPICE (from decks/ so relative paths resolve)
cd decks
/usr/local/bin/ngspice -b -o ../results/v24_tx5v_vendor.log     v24_tx5v_vendor.sp
/usr/local/bin/ngspice -b -o ../results/v24_tx5v_A_200mA.log    v24_tx5v_A_200mA.sp
/usr/local/bin/ngspice -b -o ../results/v24_tx5v_B_900mA.log    v24_tx5v_B_900mA.sp
/usr/local/bin/ngspice -b -o ../results/v24_3v3_loadstep.log    v24_3v3_loadstep.sp
/usr/local/bin/ngspice -b -o ../results/v24_1v8_loadstep.log    v24_1v8_loadstep.sp
/usr/local/bin/ngspice -b -o ../results/v24_bq25170_charge.log  v24_bq25170_charge.sp
cd ..

# 4) plots
python3 scripts/plot_sims.py            # writes plots/*.png

# 5) RF keep-out
python3 scripts/parse_board.py          # data/rf_analysis.json
python3 scripts/gerber_keepout.py       # data/gerber_keepout_hits.json  (needs _work/gerbers)
python3 scripts/plot_antenna.py         # plots/antenna_keepout.png

# 6) DRC / ERC on board copies (fix_v19 rules + fix_v23 .kicad_dru)
cd _work/drc
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb drc \
    --severity-error --schematic-parity --format json vitalq_v2.kicad_pcb -o drc_v24.json
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli sch erc \
    --severity-error --format json vitalq_v2.kicad_sch -o erc_v24.json
```

## Model provenance

| file | source | type |
|---|---|---|
| `models/TPS61240_TRANS.LIB` | TI SLVMAT6 | vendor — **does not regulate in ngspice-47** (see `v24_tx5v_vendor.sp` log) |
| `models/TPS63802_TRANS.LIB` | TI | vendor — non-convergent in ngspice-47 (inherited finding, re-verified) |
| `models/tps7a20-adj_trans.lib` | TI SBVM961 | vendor — works (used for +1V8) |
| `models/bq25170_v47.inc`, `common.inc`, `v18_pp.inc` | fullsim_v20 | **behavioural**, datasheet-bounded |
| `models/pca9306_process.lib`, `sn74axc2t245.ibs`, `TPD1E10B06.lib` | fullsim_v20 | vendor/IBIS (not exercised by v24 decks) |
| `decks/v24_tx5v_ledpulse.inc` | this task | **behavioural averaged boost** (datasheet-bounded) |

Where a model is behavioural it is bounded by datasheet EC values — see comments at the top of each deck.

## Data provenance for the power budget

Per-load currents come from the local datasheet-text library `verify/datasheets_v18_txt/*.txt`
(+ TI PDFs in `_work/datasheets/`). Each CSV row carries `label` ∈ {checked, inferred, guess} and a `source` citation.
