# inputs/ — committed snapshots of the files analysed

These are the exact fix_v24 project files the sim_v24 analysis ran against
(read-only). They are committed so every script can be re-run from the repo
alone — until PR #19 merges, the repo path
`hardware/pcb/vitalquant_v2.kicad_pcb` still holds an earlier revision.

| file | what it is | md5 |
|---|---|---|
| `vitalq_v2.kicad_pcb` | fix_v24 board layout | `4f424829d027f22d73b992fc3e0dfbb8` |
| `vitalq_v2.kicad_sch` | fix_v24 schematic | `4f47338fc704c954ca7fc799374ddd70` |
| `vitalq_v2.kicad_pro` | fix_v19-rules project (shared basename → loaded by kicad-cli) | `0a296ad92bb19186e327e37cab91a7b8` |
| `vitalq_v2.kicad_dru` | fix_v23 custom design rules (Q1/Q2/Q4 pad clearance 0.09 mm) | `9401e48a97a447576526c171d0d3a9ac` |
| `v24.net` | `kicad-cli sch export netlist` of the sch above (what `parse_netlist.py` parses) | `b1df432bc0faa93aa6260f23af874d4d` |

Two dead metadata strings were scrubbed of the author's local absolute
paths (electrically irrelevant, KiCad ignores them for DRC/ERC/netlist use):
the footprint 3D `model` path in `vitalq_v2.kicad_pcb` (→ `${KIPRJMOD}/…`,
same convention the file already uses for its other project-lib models) and
the netlist `source` attribute in `v24.net` (→ `vitalq_v2.kicad_sch`).

Regenerate `v24.net` with:

```sh
kicad-cli sch export netlist inputs/vitalq_v2.kicad_sch --output inputs/v24.net
```

The DRC/ERC commands in `scripts/run_drc_erc.py` run on the `.kicad_pcb` /
`.kicad_sch` here; KiCad picks up the same-basename `.kicad_pro` + `.kicad_dru`
automatically — this mirrors the original `_work/drc` setup exactly.
