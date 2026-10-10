# VitalQ v9 workspace

- Read PROMPT.md, PLAN.md and PROGRESS.md before any routing. Respect the two-non-improving-batch stop and per-net attempt ledger. v8 is a read-only reference, not an output directory.
- Only native KiCad DRC with --refill-zones and the matching .kicad_pro/.kicad_dru is an acceptance result. Keep dated backups and rejected candidates; delete no files.
- KiCad 10.0.4 CLI: /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli.
- pcbnew interpreter: /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3. Standalone board loading in this environment requires wx.App(False) before pcbnew.LoadBoard; without it wxWidgets can assert and hang.
- Geometry analysis can use the existing v8 .venv/bin/python read-only (Shapely 2.1.2, NumPy 2.4.2, SciPy 1.17.1). Always use -B and PYTHONDONTWRITEBYTECODE=1 to avoid writing reference __pycache__ files.
- KiCad 10 layer IDs differ from older releases: discover from pcbnew constants/extracted layer mapping rather than assuming B.Cu=31.
- scripts/session.py handles backups, refilled DRC summaries and exactly-one-line batch logging. scripts/extract.py creates exact compressed geometry including zone holes and footprint keepouts. scripts/geometry.py tightens reference geometry checks to 0.2 mm hole clearance on all copper layers and never imports the older J8 exception-expansion policy.
- No git commits, pushes, worktrees, file deletion, netlist changes, or rule-area changes.
