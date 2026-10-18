# Task: initial chest-worn enclosure concept for the VitalQ PCB

Work in ~/VitalQuant-place (branch devin/hw-v2-pcb). Commit locally only. Do NOT push.

## Tooling (keep disk use small)
- Use `uv` (`brew install uv` if missing). Create ONE venv: `uv venv --python 3.12 ~/VitalQuant-place/case/.venv`, then install ONLY `build123d` into it (`uv pip install --python case/.venv build123d`).
- Optional: build123d-mcp for render_view feedback. Run it with `uv tool run --python 3.12 build123d-mcp@latest` and add it to ~/.config/devin/mcp_config.json:
  {"mcpServers":{"build123d-mcp":{"command":"uv","args":["tool","run","--python","3.12","build123d-mcp@latest"]}}}
  If the MCP is flaky, fall back to plain scripts and make the renders yourself (build123d SVG export, or a small off-screen render).
- No conda, FreeCAD or Fusion.
- After installing, run `uv cache clean` and report `du -sh case/.venv`. Flag it if it is over about 1.5 GB.
- Add case/.venv to .gitignore.

## Goal
Make an INITIAL concept of the overall shape of a chest-worn enclosure for the VitalQ PCB: 46 x 70 mm, about 1.6 mm thick, 6 layers, skin-facing sensors on the B (bottom) side. Board placement is NOT final, because Quilter is laying it out. So make EVERYTHING parametric. Sensor windows are simple placeholders for now.
- If useful, export the board model with: `/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb export step -o case/vitalq_pcb.step ~/Downloads/VitalQ_Quilter_v6/vitalq_v2.kicad_pcb`. Use it for the outline and fit checks.
- Briefly research chest-patch wearable ergonomics and apply them:
  - Low profile: total thickness about 10–12 mm or less.
  - A gently curved skin-side base that follows the chest curvature, with a parametric radius.
  - Rounded edges and no sharp corners.
  - A compact footprint.

## The housing must include
- The PCB, held on standoffs or clips.
- A slim LiPo pouch-cell compartment, size parametric. Propose a capacity that fits, e.g. a 300–500 mAh, roughly 502535-size cell.
- A USB-C charging-port opening aligned to the board-edge connector, with parametric position.
- Skin-side openings as placeholders, so the electrodes and optical sensors sit flush and close to the skin.
- A two-part shell (top cover plus skin-side base) with a snap-fit or screw closure.
- Two strap loops or slots on opposite sides for an adjustable elastic chest band about 25 mm wide.
- A wall thickness of about 1.6–2 mm, FDM-printable with no or minimal supports, plus sensible fit tolerances.

## Outputs in ~/VitalQuant-place/case/
- `case.py`: parametric, with ALL dimensions as named constants at the top.
- STEP and STL for each part.
- PNG renders: iso, top, bottom and section views.
- `README.md`: a short summary of the design choices and key dimensions.
- Copy the STLs, STEP files and PNGs into a NEW folder, ~/Downloads/VitalQ_Case_v1/.
- Commit locally (no push). At the end, report the venv size, the overall case dimensions and the commit hash.
