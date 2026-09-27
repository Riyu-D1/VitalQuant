#!/usr/bin/env python3
"""Copy the attached SnapMagic / Ultra Librarian libraries into hardware/pcb/lib.

Rewrites footprint properties to the project nickname ``snap`` and applies two
electrical-type corrections that the vendor files get wrong:

* MCP73831 VBAT is the charger output, so it is power_out (the file says output,
  which does not drive the XC6206 power_in pin).
* LSM6DSV80X pins 6 and 7 are GND, so they are power_in (the file says power_out).
"""

from __future__ import annotations

import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = Path("/tmp/vitalq_libs")
PRETTY = ROOT / "snap.pretty"
MODELS = ROOT / "snap.3d"

PARTS = [
    ("AFE4900YZR", "AFE4900YZR.kicad_sym", "BGA30N40P5X6_260X210X50.kicad_mod", "AFE4900YZR.step"),
    ("AD5940BCBZ-RL7", "AD5940BCBZ-RL7.kicad_sym", "BGA56C40P8X7_416X356X55.kicad_mod", "AD5940BCBZ-RL7.stp"),
    ("ADS1292RIRSMT", "ADS1292RIRSMT.kicad_sym", "QFN40P400X400X100-33N-D.kicad_mod", "ADS1292RIRSMT.stp"),
    ("AS7341-DLGM", "AS7341-DLGM.kicad_sym", "AS7341DLGT.kicad_mod", "AS7341-DLGM.stp"),
    ("MLX90632SLD-DCB-100-SP", "MLX90632SLD-DCB-100-SP.kicad_sym", "MLX90632SLDDCB100SP.kicad_mod", "MLX90632SLD-DCB-100-SP.stp"),
    ("TMP117AIDRVR", "TMP117AIDRVR.kicad_sym", "SON65P200X200X80-7N.kicad_mod", "TMP117AIDRVR.stp"),
    ("LSM6DSV80XTR", "LSM6DSV80XTR.kicad_sym", "QFN_LSM6DSV80XTR_STM.kicad_mod", None),
    ("MCP73831T-2ACI_OT", "MCP73831T-2ACI_OT.kicad_sym", "SOT95P280X145-5N.kicad_mod", "MCP73831T-2ACI_OT.step"),
]


def _fix_symbol(text: str, fp_file: str) -> str:
    fp_name = fp_file.replace(".kicad_mod", "")
    # Footprint property is a bare name or "Lib:name". Point it at this project.
    text = text.replace(
        f'(property "Footprint"',
        f'(property "Footprint"',
    )
    import re

    text = re.sub(
        r'\(property "Footprint" "[^"]*"',
        f'(property "Footprint" "snap:{fp_name}"',
        text,
        count=1,
    )
    return text


def _fix_types(name: str, text: str) -> str:
    if name == "MCP73831T-2ACI_OT":
        # Only the VBAT pin (number 3) is the power output.
        old = '''(pin output line (at 17.78 -2.54 180.0) (length 5.08)
        (name "VBAT"'''
        new = '''(pin power_out line (at 17.78 -2.54 180.0) (length 5.08)
        (name "VBAT"'''
        if old not in text:
            raise SystemExit("MCP73831 VBAT pin block not found")
        text = text.replace(old, new, 1)
    if name == "LSM6DSV80XTR":
        text = text.replace("(pin power_out line", "(pin power_in line")
    return text


def _drop_models(mod: str) -> str:
    """Remove every (model ...) s-expression, respecting nested parentheses."""
    out = []
    i = 0
    while True:
        j = mod.find("(model", i)
        if j < 0:
            out.append(mod[i:])
            break
        # Keep a preceding newline tidy, but do not eat the previous token.
        out.append(mod[i:j])
        depth = 0
        k = j
        while k < len(mod):
            if mod[k] == "(":
                depth += 1
            elif mod[k] == ")":
                depth -= 1
                if depth == 0:
                    k += 1
                    break
            k += 1
        i = k
    return "".join(out)


# Vendor STEP files for these parts are drawn with height along +Y.
# KiCad wants height along +Z. Rotate X by -90 and lift by the body height.
# MCP73831 also needs a Z spin so the 2.9 mm body length follows the SOT-23-5 pads.
# TMP117 is already Z-up but its solid is shifted +0.3 mm in Y.
# LSM6 VRML is in KiCad's 0.1 inch unit; offset lifts the centred box onto the board.
MODEL_POSE = {
    "AFE4900YZR.step": ((-90, 0, 0), (0, 0, 0.50)),
    "ADS1292RIRSMT.stp": ((-90, 0, 0), (0, 0, 0.975)),
    "AS7341-DLGM.stp": ((-90, 0, 0), (0, 0, 1.10)),
    "MCP73831T-2ACI_OT.step": ((-90, 0, 90), (0, 0, 1.45)),
    "TMP117AIDRVR.stp": ((0, 0, 0), (0, -0.30, 0)),
    "LSM6DSV80XTR.wrl": ((0, 0, 0), (0, 0, 0.415)),
}


def _fix_model(mod: str, model: str | None) -> str:
    mod = _drop_models(mod)
    if model:
        rot, off = MODEL_POSE.get(model, ((0, 0, 0), (0, 0, 0)))
        block = (
            f'  (model "${{KIPRJMOD}}/lib/snap.3d/{model}"\n'
            f"    (offset (xyz {off[0]:.3f} {off[1]:.3f} {off[2]:.3f}))\n"
            "    (scale (xyz 1 1 1))\n"
            f"    (rotate (xyz {rot[0]:.0f} {rot[1]:.0f} {rot[2]:.0f}))\n"
            "  )\n"
        )
        idx = mod.rstrip().rfind(")")
        mod = mod.rstrip()[:idx] + "\n" + block + ")\n"
    return mod


def _wrl_box() -> str:
    # KiCad's VRML unit is 0.1 inch, so a 3 mm edge is 3/2.54 here.
    # LGA-14L body: 3.0 mm along X (the long pad rows), 2.5 mm along Y, 0.83 mm tall.
    sx, sy, sz = 3.0 / 2.54, 2.5 / 2.54, 0.83 / 2.54
    return f"""#VRML V2.0 utf8
# KiCad VRML unit is 0.1 inch. Body is 3.0 x 2.5 x 0.83 mm (LSM6DSV80X LGA-14).
Shape {{
  appearance Appearance {{ material Material {{ diffuseColor 0.15 0.15 0.18 }} }}
  geometry Box {{ size {sx:.6f} {sy:.6f} {sz:.6f} }}
}}
"""


def main() -> None:
    if not SRC.is_dir():
        raise SystemExit(f"missing {SRC}; unzip vitalq_libs first")
    if PRETTY.exists():
        shutil.rmtree(PRETTY)
    PRETTY.mkdir()
    MODELS.mkdir(exist_ok=True)
    symbols = []
    for folder, sym, fp, model in PARTS:
        src = SRC / folder
        text = (src / sym).read_text(encoding="utf-8")
        text = _fix_symbol(text, fp)
        text = _fix_types(folder, text)
        # Keep the inner (symbol ...) only; wrap once at the end.
        inner = text.strip()
        if inner.startswith("(kicad_symbol_lib"):
            # drop the outer wrapper
            inner = inner[inner.find("(symbol ") : inner.rfind(")")]
            inner = inner.rstrip()
            if inner.endswith(")"):
                pass
        symbols.append(inner.strip())
        mod = (src / fp).read_text(encoding="utf-8")
        mod = _fix_model(mod, model)
        (PRETTY / fp).write_text(mod, encoding="utf-8")
        if model:
            shutil.copy(src / model, MODELS / model)
    (MODELS / "LSM6DSV80XTR.wrl").write_text(_wrl_box(), encoding="utf-8")
    lsm = PRETTY / "QFN_LSM6DSV80XTR_STM.kicad_mod"
    body = lsm.read_text(encoding="utf-8")
    body = _fix_model(body, "LSM6DSV80XTR.wrl")
    lsm.write_text(body, encoding="utf-8")
    lib = (
        '(kicad_symbol_lib (version 20211014) (generator "vitalq_import")\n'
        + "\n".join(symbols)
        + "\n)\n"
    )
    (ROOT / "snap.kicad_sym").write_text(lib, encoding="utf-8")
    print(f"wrote {len(symbols)} symbols, {len(list(PRETTY.glob('*.kicad_mod')))} footprints")


if __name__ == "__main__":
    main()
