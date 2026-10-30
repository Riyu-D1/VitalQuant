# `_work/` — verifier inputs (committed, ~12.3 MB total)

These three files are the real-geometry ground truth `verify_fit.py`
runs against. They are **checked** artefacts of the fix_v24 board, not
design output — commit them so the verifier works from a fresh clone.

| File | Bytes | Source (checked) |
|---|---|---|
| `board_v24_pads.glb` | 11,625,584 | `kicad-cli pcb export glb --include-pads` |
| `board_v24_slab.glb` | 84,712 | `kicad-cli pcb export glb --board-only` |
| `geometry_v24.json` | 655,832 | `pcbnew` extraction script `extract_geometry.py` |

## Regeneration (exact commands, checked on kicad-cli 10.0.4, macOS)

Board source: `hardware/pcb/vitalquant_v2.kicad_pcb` on branch
`fix/bom-v24` (the fix_v24 board; the `vitalquant_v2.kicad_pcb` on this
branch is the older fix_v22-era file — do not use it).

```sh
KICAD=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
BOARD=/path/to/vitalquant_v2.kicad_pcb            # fix_v24 copy
"$KICAD" pcb export glb --include-pads --units mm -o board_v24_pads.glb "$BOARD"
"$KICAD" pcb export glb --board-only   --units mm -o board_v24_slab.glb "$BOARD"
# geometry_v24.json: pcbnew extraction (KiCad bundled python 3.9).
# extract_geometry.py takes <board> <out.json>:
cd _work && /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 \
    ../extract_geometry.py "$BOARD" geometry_v24.json
```

(`extract_geometry.py` lives next to `verify_fit.py`; it dumps
the board outline, mounting holes, pads and courtyard boxes used by the
verifier. U2/U17 ship no 3D models — the verifier covers them with
courtyard-box points instead.)

Frame note (checked): the GLB exports arrive as
`(x = KiCad x, y = height above B.Cu, z = KiCad y)` in metres; the
verifier maps them to the case's physical frame with
`(1000*x, -1000*z, 1000*y + ZB)` mm.
