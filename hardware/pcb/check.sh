#!/usr/bin/env bash
# Regenerate the VitalQ hw_v1 netlist, schematic, and unrouted board, then ERC it.
set -euo pipefail
cd "$(dirname "$0")"

export KICAD_SYMBOL_DIR="${KICAD_SYMBOL_DIR:-/usr/share/kicad/symbols}"
for ver in 6 7 8 9 10; do
  export "KICAD${ver}_SYMBOL_DIR"="${KICAD_SYMBOL_DIR}"
done
export KICAD9_FOOTPRINT_DIR="${KICAD9_FOOTPRINT_DIR:-/usr/share/kicad/footprints}"

# kicad-cli reads the user library tables. A project table that only lists
# the local vitalq lib does not replace these; it adds to them.
KICAD_CFG="${HOME}/.config/kicad/9.0"
mkdir -p "${KICAD_CFG}"
if [[ ! -f "${KICAD_CFG}/sym-lib-table" && -f /usr/share/kicad/template/sym-lib-table ]]; then
  cp /usr/share/kicad/template/sym-lib-table "${KICAD_CFG}/sym-lib-table"
fi
if [[ ! -f "${KICAD_CFG}/fp-lib-table" && -f /usr/share/kicad/template/fp-lib-table ]]; then
  cp /usr/share/kicad/template/fp-lib-table "${KICAD_CFG}/fp-lib-table"
fi

PY="${SKIDL_PYTHON:-}"
if [[ -z "${PY}" ]]; then
  if [[ -x "${HOME}/skidl-venv/bin/python" ]]; then
    PY="${HOME}/skidl-venv/bin/python"
  else
    python3 -m venv .venv
    .venv/bin/pip install -r requirements.txt
    PY=".venv/bin/python"
  fi
fi

"${PY}" lib/make_libs.py
"${PY}" vitalq_hw_v1.py

"${PY}" - << 'PY'
from pathlib import Path
text = Path("erc_skidl.txt").read_text()
errors = int(text.split()[1])
warnings = int(text.split()[3])
print(f"SKiDL ERC errors={errors} warnings={warnings}")
if errors:
    raise SystemExit("SKiDL ERC errors")
PY

mkdir -p build export
kicad-cli sch erc --severity-error --format json -o build/erc_errors.json vitalq_hw_v1.kicad_sch
kicad-cli sch erc --severity-all --format report -o build/erc_sch.rpt vitalq_hw_v1.kicad_sch
"${PY}" - << 'PY'
import json
from pathlib import Path
data = json.loads(Path("build/erc_errors.json").read_text())
# KiCad 9 JSON is either a list of violations or an object with a violations list.
if isinstance(data, dict):
    violations = data.get("violations") or data.get("sheets") or []
    if isinstance(violations, dict):
        violations = [item for sheet in violations.values() for item in sheet]
elif isinstance(data, list):
    violations = data
else:
    violations = []
# Flatten sheet reports of the form {"sheet": ..., "violations": [...]}
flat = []
for item in violations:
    if isinstance(item, dict) and "violations" in item:
        flat.extend(item["violations"])
    else:
        flat.append(item)
errors = [v for v in flat if isinstance(v, dict) and str(v.get("severity", "error")).lower() == "error"]
print(f"kicad-cli sch erc errors={len(errors)}")
if errors:
    for err in errors[:20]:
        print(err.get("description") or err)
    raise SystemExit("schematic ERC errors")
PY

kicad-cli sch export pdf -o vitalq_hw_v1.pdf vitalq_hw_v1.kicad_sch
rm -rf export/sch
mkdir -p export/sch
kicad-cli sch export svg -o export/sch vitalq_hw_v1.kicad_sch
kicad-cli pcb export svg --layers F.Cu,F.SilkS,Edge.Cuts --page-size-mode 2 --fit-page-to-board -o export/vitalq_hw_v1.svg vitalq_hw_v1.kicad_pcb
kicad-cli pcb render --side top --background opaque -w 1600 -h 1000 -o export/vitalq_hw_v1.png vitalq_hw_v1.kicad_pcb

if command -v pdftoppm >/dev/null; then
  pdftoppm -png -r 40 vitalq_hw_v1.pdf export/sch/sheet
fi

if [[ -d /opt/cursor/artifacts ]]; then
  cp -f export/vitalq_hw_v1.png /opt/cursor/artifacts/vitalq_hw_v1_board.png
  cp -f export/vitalq_hw_v1.svg /opt/cursor/artifacts/vitalq_hw_v1_board.svg
  cp -f vitalq_hw_v1.pdf /opt/cursor/artifacts/vitalq_hw_v1_schematic.pdf
  if compgen -G "export/sch/sheet*.png" > /dev/null; then
    cp -f export/sch/sheet*.png /opt/cursor/artifacts/
  fi
  find export/sch -name '*.svg' -print0 | xargs -0 -I{} cp -f {} /opt/cursor/artifacts/ || true
fi

echo "check.sh OK"
