"""
Variant A - adhesive-wing concept (IDEAS.md I1).

A thin (0.7 mm) skin-curved flange extends ~8 mm around the base
perimeter, following the R_SKIN chest curve: the "rigid pod on a
flexible adhesive skirt" pattern used by Zio/CAM/VitalPatch. Accepts
double-sided medical tape / hydrocolloid rings; strap loops stay, so
the same case serves strap or adhesive mounting.

Run:  case/.venv/bin/python case/concepts/variant_a.py
Renders (iso/top/bottom) go to case/ as variant_a_*.png/svg.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import case  # noqa: E402
from build123d import *  # noqa: E402,F403

WING_W = 8.0      # flange width beyond the outer wall
WING_T = 0.7      # flange thickness
WING_DROP = 1.2   # flange rides this far below the skin apex


def build_variant():
    base = case.build_base()

    # ring band standing around the outside of the case outline
    band = case.rounded_box(case.OW + 2 * WING_W, case.OL + 2 * WING_W,
                            case.CORNER_R + WING_W,
                            -case.SAG - WING_DROP - WING_T - 0.5,
                            1.0)
    band -= case.rounded_box(case.OW + 0.6, case.OL + 0.6,
                             case.CORNER_R + 0.3,
                             -case.SAG - WING_DROP - WING_T - 0.5, 1.0)

    # keep only the sliver between the skin-surface cylinder and a
    # WING_T-thicker shell -> a WING_T flange hugging the chest curve,
    # dropped WING_DROP below the skin apex so tape fills the gap
    shell = (case.y_cyl(case.R_SKIN + WING_T, -case.R_SKIN - WING_DROP)
             - case.y_cyl(case.R_SKIN, -case.R_SKIN - WING_DROP))
    flange = band & shell

    return base + flange


def main():
    out = case.OUT
    part = build_variant()
    export_step(part, out / "variant_a.step")
    export_stl(part, out / "variant_a.stl")
    case.render([(part, (0.25, 0.25, 0.25))], "variant_a_iso",
                (0.9, -1.0, 0.55))
    case.render([(part, (0.25, 0.25, 0.25))], "variant_a_top",
                (0.01, -0.02, 1))
    case.render([(part, (0.25, 0.25, 0.25))], "variant_a_bottom",
                (0.01, -0.02, -1), look_up=(0, 1, 0))
    print("variant_a: adhesive wing",
          f"+{WING_W} mm x {WING_T} mm around {case.OW:.1f}x{case.OL:.1f}")


if __name__ == "__main__":
    main()
