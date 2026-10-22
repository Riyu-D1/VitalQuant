# 00 — Audit target pin

## Revision under audit

- Repo: `github.com/Riyu-D1/VitalQuant`, branch `feature/vitalq-firmware-v2`
- Commit: `b9002188655c7bb9fbea04f7cae67a769a4d691f`
- Working tree: **dirty** — one untracked file, `hardware/pcb/DEVIN_PREFAB_AUDIT_PROMPT.md`
  (the audit brief itself; no audited artifact is modified or untracked
  except this file and `reports/`). Judged benign to the audit target.

## Artifact hashes (sha256)

| File | sha256 |
|---|---|
| `hardware/pcb/vitalq_v2.kicad_pcb` | `7844150655ab983eb00a81855474900df8f12ee16e7af0bdd834f6fcc11a69d0` |
| `hardware/pcb/vitalq_v2.kicad_sch` | `cc4cbab5855a9a73a33374e610a4bb7bb7123275cb9dbce9e88f145151975417` |
| `hardware/pcb/fab/VitalQ_v2_fixv19_fabrication.zip` | `5f976845f422c6288c8b449167d16541d53471016cee02109fcbd1cccfbefd52` |

## Provenance

**Cannot cryptographically prove** that `vitalq_v2.kicad_pcb` @ this hash is
the exact source the fix_v19 zip was generated from — the zip's
`SHA256SUMS.txt` does not include the source board file. Board-derived facts
are therefore cross-checked against the zip contents (gerbers, drill, BOM,
CPL) in `06_tool_output/`; where the two disagree, the **zip is the fab
authority**.

Note also the documented inconsistency: `FAB_VERIFICATION.json` inside the
fix_v19 zip names `"zip": "fab/VitalQ_v2_fixv18_fabrication.zip"` and quotes
61 DRC violations vs 48 warnings in `pin_audit_r9/ROUND9_REVIEWS.md` —
resolved in `06`/`08`, not assumed.

## Mechanical extraction

- `kicad-cli`: **not installed** in this environment. `pcbnew` python module:
  not installed. Netlist/pad truth therefore extracted by pure s-expression
  parse of `vitalq_v2.kicad_pcb` (`reports/tools/extract_pads.py`).
- Result: **302 footprints, 1,027 pads** (8 `np_thru_hole`), matching the
  stated 302 placements / 1,027 pads exactly. CSV: `01_netlist_pinmap.csv`.
- ERC/DRC re-run requires `kicad-cli` — attempted workarounds and residual
  coverage documented in `09_unverifiable.md` if unavailable.

## Naming

Every finding names: revision **fix_v19** @ commit `b9002188` and the artifact
file it was derived from.
