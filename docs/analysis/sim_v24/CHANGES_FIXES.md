# CHANGES_FIXES — review fixes applied to PR #20 (branch `docs/sim-v24`)

Independent-review fixes, applied as new commits on top of the original
`docs/sim-v24` analysis. Everything stays inside `docs/analysis/sim_v24/`.

## 1. Missing `data/` folder — fixed by rename to `results/`

**Problem.** The repo `.gitignore` has a `data/` rule, so every CSV/JSON the
report cited (`data/power_budget.csv`, `data/runtime.csv`, `data/netlist.json`,
`data/rf_analysis.json`, `data/gerber_keepout_hits.json`) was never committed.

**Fix.** Renamed `data/` → `results/` (merged with the existing ngspice
`results/` folder) and updated every script and doc reference. Also committed
the *inputs* needed to re-run from the repo alone: `inputs/vitalq_v2.kicad_pcb`
/`.kicad_sch`/`.kicad_pro`/`.kicad_dru` (the exact fix_v24 snapshot analysed)
and `inputs/v24.net` (the netlist export parsed), plus the DRC/ERC result
files (`results/drc_v24.json`, `results/erc_v24.json`).

**Evidence.**

```
$ git ls-files docs/analysis/sim_v24/results/ | head
all_nets.txt  drc_v24.json  erc_v24.json  gerber_keepout_hits.json
netlist.json  power_budget.csv  rf_analysis.json  runtime.csv  v24_*.csv/.log
$ python3 check_refs.py        # scans REPORT/SIM_README/PROGRESS/CHANGES_FIXES
96 referenced paths checked, 0 missing
```

## 2. Battery-life numbers — 90 % usable applied

**Problem.** The report text claimed "usable ≈ 90 %" but `power_budget.py`
divided by the full 300 mAh.

**Fix.** `power_budget.py` now uses `CAP_USABLE_MAH = 0.90 × 300 = 270 mAh`,
and a 4th scenario (PPG-only LED duty) was added so every report number comes
from the script. Regenerated `results/runtime.csv` + report table:

| scenario | before (300 mAh) | after (270 mAh usable) |
|---|---|---|
| deep sleep (RTC) | ≈1423 h (~59 d) | ≈**1281 h** (~53 d), 0.211 mA |
| BLE streaming | ≈3.5 h | ≈**3.1 h**, 86.6 mA |
| 1-min-10 sampling | ≈6.6 h | ≈**6.0 h**, 45.2 mA |
| same, PPG-only LED | ≈22 h (hand math) | ≈**17.6 h**, 15.4 mA — now script-generated |

300 mAh is stated as an **assumption (guess)** for the 402535-class pouch on
J2 — repo sources: `hardware/case/docs/PRINT_NOTES.md` ("402535 LiPo pouch,
~300–350 mAh") and `hardware/pcb/docs/BOM_COSTED_V22.md`; the BOM
(`hardware/pcb/docs/BOM_V22.md`) shows only a LiPo pad pair, no part number.

## 3. Labelling — inferred vs checked, legend added

**Problem.** Results from simplified self-built models (LED-supply collapse,
+3V3 dip, charge profile — the cases where TI's own macros would not run in
ngspice-47) were labelled `checked (sim)`.

**Fix.** Legend added at the top of `REPORT.md`:

- **checked** = computed directly from design files or datasheet values
- **inferred** = from simplified models or reasoning
- **guess** = assumption

Applied consistently: TX_5V A/B decks, +3V3 loadstep, BQ25170 charge profile,
LDO-dropout sims and all runtime figures → **inferred**; TPS7A20 vendor-model
deck, DRC/ERC, RF audit, rail map, BQ25170 setpoints, netlist → **checked**;
thermal + 300 mAh capacity → **guess**. Deck table now carries a label column.

## 4. LED firing claim — corrected

**Problem.** The report said one-group-at-a-time LED firing is "already the
firmware rule".

**Fix.** Corrected in `REPORT.md` §Boost headroom (verified against firmware
source on this branch):

- `led_sched` (`firmware/vitalquant/components/vq/include/led_sched.h`) enforces
  non-overlap only via its cb_on/cb_off mechanism for the chains firmware
  drives directly.
- **AFE4900** times its own pulses on an internal engine; the driver only
  gates the engine via a grant watchdog
  (`firmware/vitalquant/components/vq_drv_afe4900/src/vq_drv_afe4900.c` ~L467-519).
- **MAX86141** is fully autonomous — `mx_led_cb_on`/`mx_led_cb_off` are empty
  no-ops at `firmware/vitalquant/components/vq_drv_max86141/src/vq_drv_max86141.c`
  **L260-261** (verified); its own header says the GPIO1 sync input is
  unconnected, so the scheduler cannot interleave its pulses.
- Consequence: no slot-level no-overlap between the two AFE chains. Safe at
  today's currents (~5 mA AFE4900 `AFE_LED_CODE_RUN=0x1A`,
  ~15–23 mA MAX86141 `MX_LED*_PA`), but the ~250 mA TX_5V bound must be
  respected if currents are raised. Firmware untouched.

## 5. Small points

- **Paths:** all `/Users/deepakdiwan` paths removed from scripts and
  `SIM_README.md`; every path is relative to the script file or a `--pcb`/
  `--gerbers`/`--out` argument; tools located via `PATH` + standard install
  dirs (`scripts/toolfind.py`).
- **Board references:** `fix_v24/vitalq_v2.kicad_pcb` → real repo path
  `hardware/pcb/vitalquant_v2.kicad_pcb` (verified present), with the explicit
  note that it holds fix_v24 **only once PR #19 merges**; the analysed file is
  committed as `inputs/vitalq_v2.kicad_pcb` (md5 `3cf1ca4d…`, see
  `inputs/README.md`).
- **Counts:** PR description and `REPORT.md` now agree — **6/6 decks ran to
  completion** (5 analysis + 1 vendor-evidence; the vendor deck completes but
  documents non-regulation) and charge time **~2.2 h** (300 mAh / 200 mA =
  1.5 h CC + ~0.7 h taper, per `scripts/power_budget.py` + the behavioural
  charge sim).

## Verification performed (second fresh clone, repo alone)

```sh
git clone --branch docs/sim-v24 <repo> /tmp/vq_verify
cd /tmp/vq_verify/docs/analysis/sim_v24
python3 scripts/parse_netlist.py    # 302 comps, 255 nets -> results/*.json|txt  IDENTICAL to committed
python3 scripts/power_budget.py     # -> results/power_budget.csv, runtime.csv   IDENTICAL
python3 scripts/parse_board.py      # 0 track/via/pad hits; 5.70mm overhang; 5.85mm nearest  IDENTICAL
python3 scripts/gerber_keepout.py   # fresh kicad-cli gerber+drill export -> 0 hits, 0 drill  IDENTICAL counts
python3 scripts/run_drc_erc.py      # DRC 0 viol / 0 unconn / 0 parity; ERC 0   (same modulo date)
cd decks && for d in ...; do ngspice -b -o ../results/$d.log $d.sp; done
                                    # 6/6 decks: DONE_* markers; logs+CSVs byte-identical
python3 scripts/plot_sims.py && python3 scripts/plot_antenna.py   # 5 PNGs regenerated
python3 check_refs.py               # -> 0 missing (after this file was added)
grep -r "/Users/" docs/analysis/sim_v24/   # -> nothing
git diff --stat origin/main...docs/sim-v24 # -> only docs/analysis/sim_v24/** paths
```
