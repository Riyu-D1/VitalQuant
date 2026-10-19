"""
Variant B - snap-electrode base (IDEAS.md I8).

Two Ø3.9 mm male ECG snap studs protrude from the skin floor at the
+Y end (board's electrode-pad edge) so standard hydrogel snap electrodes
can attach directly, complementing the tail exits. Studs are trimmed by
the chest-curve cylinder so they sit ~0.4 mm proud of the skin surface;
Ø4 mm bores run up into the cavity for the lead/snap receptacle (PCB
side would need matching contacts - flagged in IDEAS.md).

Run:  case/.venv/bin/python case/concepts/variant_b.py
Renders (iso/top/bottom) go to case/ as variant_b_*.png/svg.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import case  # noqa: E402
from build123d import *  # noqa: E402,F403

SNAP_D = 3.9          # standard ECG snap stud
BOSS_D = 10.5         # raised boss the snap seats onto
HOLE_D = 4.0          # lead/receptacle bore
SNAP_XY = [(-13.0, 32.5), (13.0, 32.5)]   # case coords, near +Y end


def build_variant():
    base = case.build_base()

    for sx, sy in SNAP_XY:
        # boss + stud, then trim the boss by the skin cylinder so its
        # skin face follows the chest curve and sits slightly proud
        stud = Pos(sx, sy, -case.SAG - 2.0) * Cylinder(
            BOSS_D / 2, case.SAG + 4.0,
            align=(Align.CENTER, Align.CENTER, Align.MIN))
        stud += Pos(sx, sy, -case.SAG - 2.0) * Cylinder(
            SNAP_D / 2, case.SAG + 2.6,
            align=(Align.CENTER, Align.CENTER, Align.MIN))
        stud -= case.y_cyl(case.R_SKIN - 0.4, -case.R_SKIN)
        base += stud
        # lead bore straight through floor and stud
        base -= Pos(sx, sy, -case.SAG - 2.0) * Cylinder(
            HOLE_D / 2, case.Z_FLOOR + case.SAG + 2.0,
            align=(Align.CENTER, Align.CENTER, Align.MIN))

    return base


def main():
    out = case.OUT
    part = build_variant()
    export_step(part, out / "variant_b.step")
    export_stl(part, out / "variant_b.stl")
    case.render([(part, (0.25, 0.25, 0.25))], "variant_b_iso",
                (0.9, -1.0, 0.55))
    case.render([(part, (0.25, 0.25, 0.25))], "variant_b_top",
                (0.01, -0.02, 1))
    case.render([(part, (0.25, 0.25, 0.25))], "variant_b_bottom",
                (0.01, -0.02, -1), look_up=(0, 1, 0))
    print("variant_b: snap studs", SNAP_XY, "Ø", SNAP_D)


if __name__ == "__main__":
    main()
