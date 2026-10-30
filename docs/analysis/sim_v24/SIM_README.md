# SIM_README — how to reproduce sim_v24

Everything here is **reports only**; the board under analysis is never
modified — all work happens on the committed snapshot in `inputs/` (see
`inputs/README.md` for provenance/hashes) or in the git-ignored `_work/`
scratch dir that scripts create on demand.

The repo path `hardware/pcb/vitalquant_v2.kicad_pcb` holds fix_v24 once
PR #19 merges; until then it is an earlier revision, which is why the
snapshot is committed.

## Tools

Scripts find tools on `PATH` first, then standard install dirs
(`scripts/toolfind.py`) — nothing is machine-specific.

| tool | used for | version (original run) |
|---|---|---|
| kicad-cli | netlist/gerber export, DRC/ERC | 10.0.4 |
| ngspice | decks in `decks/` | ngspice-47 (KLU) |
| python3 + numpy + matplotlib | parsers, budget, plots | 3.12 / 2.4.2 / 3.10.8 (any 3.9+ works) |

## Directory map

```
docs/analysis/sim_v24/
├── REPORT.md            main report (labels: checked/inferred/guess)
├── SIM_README.md        this file
├── PROGRESS.md          step log
├── check_refs.py        verifies every doc-referenced path exists in git
├── decks/               ngspice decks (*.sp) + shared includes (*.inc)
├── models/              SPICE models copied from sims/fullsim_v20 (read-only source)
├── inputs/              committed snapshots: fix_v24 pcb/sch/pro/dru + v24.net
├── results/             all data: ngspice CSVs+logs, netlist.json, budget CSVs,
│                        rf_analysis.json, gerber_keepout_hits.json, drc/erc JSONs
├── plots/               PNGs (antenna_keepout, tx5v_pulse, v33/v18, bq25170_charge)
├── scripts/             python parsers + budget + plotting + drc runner
└── _work/               scratch created on demand (gerber exports) — git-ignored
```

## Re-run everything

```sh
cd docs/analysis/sim_v24      # repo-relative

# 1) schematic netlist → rail map
python3 scripts/parse_netlist.py        # reads inputs/v24.net → results/netlist.json, all_nets.txt
#    (to re-export the netlist first:
#     kicad-cli sch export netlist inputs/vitalq_v2.kicad_sch --output inputs/v24.net)

# 2) power budget + runtime (90 % usable of the assumed 300 mAh cell)
python3 scripts/power_budget.py         # → results/power_budget.csv, results/runtime.csv

# 3) SPICE (from decks/ so relative paths resolve)
cd decks
ngspice -b -o ../results/v24_tx5v_vendor.log     v24_tx5v_vendor.sp
ngspice -b -o ../results/v24_tx5v_A_200mA.log    v24_tx5v_A_200mA.sp
ngspice -b -o ../results/v24_tx5v_B_900mA.log    v24_tx5v_B_900mA.sp
ngspice -b -o ../results/v24_3v3_loadstep.log    v24_3v3_loadstep.sp
ngspice -b -o ../results/v24_1v8_loadstep.log    v24_1v8_loadstep.sp
ngspice -b -o ../results/v24_bq25170_charge.log  v24_bq25170_charge.sp
cd ..

# 4) plots
python3 scripts/plot_sims.py            # → plots/*.png

# 5) RF keep-out
python3 scripts/parse_board.py          # → results/rf_analysis.json
python3 scripts/gerber_keepout.py       # exports gerbers via kicad-cli → results/gerber_keepout_hits.json
python3 scripts/plot_antenna.py         # → plots/antenna_keepout.png

# 6) DRC / ERC on the committed snapshot (fix_v19 rules project + fix_v23 .kicad_dru)
python3 scripts/run_drc_erc.py          # → results/drc_v24.json, results/erc_v24.json

# 7) doc reference check
python3 check_refs.py                   # must report 0 missing
```

## Model provenance

| file | source | type |
|---|---|---|
| `models/TPS61240_TRANS.LIB` | TI SLVMAT6 | vendor — **does not regulate in ngspice-47** (see `results/v24_tx5v_vendor.log`) |
| `models/TPS63802_TRANS.LIB` | TI | vendor — non-convergent in ngspice-47 (inherited finding, re-verified) |
| `models/tps7a20-adj_trans.lib` | TI SBVM961 | vendor — works (used for +1V8) |
| `models/bq25170_v47.inc`, `common.inc`, `v18_pp.inc` | fullsim_v20 | **behavioural**, datasheet-bounded |
| `models/pca9306_process.lib`, `TPD1E10B06.lib` | fullsim_v20 | vendor/IBIS (not exercised by v24 decks) |
| `decks/v24_tx5v_ledpulse.inc` | this task | **behavioural averaged boost** (datasheet-bounded) |

Where a model is behavioural it is bounded by datasheet EC values — see
comments at the top of each deck, and the label legend in `REPORT.md`
(behavioural-model results are **inferred**, not checked).

## Data provenance for the power budget

Per-load currents are embedded in `scripts/power_budget.py`, each with a
datasheet citation string (part + table/section). The original run pulled
them from a local datasheet-text library; the citations are kept inline so
the script is self-contained. Each CSV row carries
`label` ∈ {checked, inferred, guess} and a `source` citation.
