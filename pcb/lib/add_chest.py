#!/usr/bin/env python3
"""Symbols, datasheet lands, and simple 3D boxes for the chest-board additions.

Official KiCad footprints are copied only when the system 3D model is missing,
so a correctly scaled body can be attached. Lands that KiCad does not ship are
drawn from the datasheet figures named in SOURCES.md.
"""

from __future__ import annotations

import re
import uuid
from pathlib import Path

import make_lib

LIB = Path(__file__).resolve().parent
PRETTY = LIB / "vitalq.pretty"
THREED = LIB / "vitalq.3d"
SYM = LIB / "vitalq.kicad_sym"
KFP = Path("/usr/share/kicad/footprints")
KSYM = Path("/usr/share/kicad/symbols")


def uid() -> str:
    return str(uuid.uuid4())


def box_wrl(path: Path, sx_mm: float, sy_mm: float, sz_mm: float, rgb=(0.22, 0.24, 0.28)) -> None:
    """KiCad treats VRML units as 0.1 inch. The box sits on z = 0."""
    u = 1.0 / 2.54
    sx, sy, sz = sx_mm * u, sy_mm * u, sz_mm * u
    path.write_text(
        "#VRML V2.0 utf8\n"
        f"# Body {sx_mm:.2f} x {sy_mm:.2f} x {sz_mm:.2f} mm, seated on z=0.\n"
        "Transform {\n"
        f"  translation 0 0 {sz / 2:.6f}\n"
        "  children [\n"
        "    Shape {\n"
        f"      appearance Appearance {{ material Material {{ diffuseColor {rgb[0]} {rgb[1]} {rgb[2]} }} }}\n"
        f"      geometry Box {{ size {sx:.6f} {sy:.6f} {sz:.6f} }}\n"
        "    }\n"
        "  ]\n"
        "}\n",
        encoding="utf-8",
    )


def model_clause(wrl_name: str) -> str:
    return (
        f'\t(model "${{KIPRJMOD}}/lib/vitalq.3d/{wrl_name}"\n'
        "\t\t(offset (xyz 0 0 0))\n"
        "\t\t(scale (xyz 1 1 1))\n"
        "\t\t(rotate (xyz 0 0 0))\n"
        "\t)\n"
    )


def copy_fp(src: Path, name: str, wrl: str) -> None:
    text = src.read_text(encoding="utf-8")
    text = re.sub(r'\(footprint "[^"]+"', f'(footprint "{name}"', text, count=1)
    text = re.sub(r"\t\(model [\s\S]*?\n\t\)\n", "", text, count=1)
    text = text.rstrip()
    if not text.endswith(")"):
        raise SystemExit(f"bad footprint {src}")
    text = text[:-1] + "\n" + model_clause(wrl) + ")\n"
    (PRETTY / f"{name}.kicad_mod").write_text(text, encoding="utf-8")


def fp_two_pad(name, descr, pads, body, courtyard, silk_pin1=None):
    """pads: list of (num, x, y, w, h). body/courtyard are (x0,y0,x1,y1)."""
    lines = [
        f'(footprint "{name}"',
        "\t(version 20241229)",
        '\t(generator "vitalq")',
        '\t(layer "F.Cu")',
        f'\t(descr "{descr}")',
        "\t(attr smd)",
        f'\t(property "Reference" "REF**" (at 0 {courtyard[1] - 0.6:.2f} 0) (layer "F.Fab")',
        f'\t\t(uuid "{uid()}")',
        "\t\t(effects (font (size 0.35 0.35) (thickness 0.07)))",
        "\t)",
        f'\t(property "Value" "{name}" (at 0 {courtyard[3] + 0.4:.2f} 0) (layer "F.Fab")',
        "\t\t(hide yes)",
        f'\t\t(uuid "{uid()}")',
        "\t\t(effects (font (size 0.35 0.35) (thickness 0.07)))",
        "\t)",
        f'\t(fp_rect (start {body[0]:.2f} {body[1]:.2f}) (end {body[2]:.2f} {body[3]:.2f})',
        "\t\t(stroke (width 0.1) (type solid)) (fill no) (layer \"F.Fab\")",
        f'\t\t(uuid "{uid()}")',
        "\t)",
        f'\t(fp_rect (start {courtyard[0]:.2f} {courtyard[1]:.2f}) (end {courtyard[2]:.2f} {courtyard[3]:.2f})',
        "\t\t(stroke (width 0.05) (type solid)) (fill no) (layer \"F.CrtYd\")",
        f'\t\t(uuid "{uid()}")',
        "\t)",
    ]
    if silk_pin1:
        x, y = silk_pin1
        lines.append(
            f'\t(fp_circle (center {x:.2f} {y:.2f}) (end {x + 0.12:.2f} {y:.2f})'
            f' (stroke (width 0.1) (type solid)) (fill yes) (layer "F.Fab") (uuid "{uid()}"))'
        )
    for num, x, y, w, h in pads:
        lines.append(
            f'\t(pad "{num}" smd roundrect (at {x:.3f} {y:.3f}) (size {w:.3f} {h:.3f})'
            f' (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15) (uuid "{uid()}"))'
        )
    lines.append(")")
    (PRETTY / f"{name}.kicad_mod").write_text("\n".join(lines) + "\n", encoding="utf-8")


def sfh7072():
    """SFH 7072 v1.6 recommended land, built from the package drawing.

    Body 7.5 x 3.9 mm. Two rows of six pads. The centre gap is wide enough for
    an 0.8 mm optical-barrier slot (JLCPCB minimum slot) with >= 0.5 mm copper
    clearance. Pin 1 is the upper-left pad in the top view used by the
    recommended-land figure: row y = +1.20 is pins 1..6 left to right, row
    y = -1.20 is pins 12..7 left to right.
    """
    xs_left = [-3.00, -2.15, -1.30]
    xs_right = [1.30, 2.15, 3.00]
    xs = xs_left + xs_right
    pads = []
    for i, x in enumerate(xs):
        pads.append((str(i + 1), x, 1.20, 0.55, 0.70))
    bottom = [12, 11, 10, 9, 8, 7]
    for num, x in zip(bottom, xs):
        pads.append((str(num), x, -1.20, 0.55, 0.70))
    name = "SFH7072"
    # Courtyard clears the pads. The window outline is on User.1, not F.CrtYd,
    # so it does not collide with neighbouring courtyards.
    lines = [
        f'(footprint "{name}"',
        "\t(version 20241229)",
        '\t(generator "vitalq")',
        '\t(layer "F.Cu")',
        '\t(descr "SFH 7072 v1.6 land. 2x6 pads, centre slot for the optical barrier. Datasheet body 7.5 x 3.9 x 0.9 mm.")',
        "\t(attr smd)",
        f'\t(property "Reference" "REF**" (at 0 -2.7 0) (layer "F.Fab") (uuid "{uid()}")',
        "\t\t(effects (font (size 0.35 0.35) (thickness 0.07)))",
        "\t)",
        f'\t(property "Value" "SFH7072" (at 0 2.7 0) (layer "F.Fab") (hide yes) (uuid "{uid()}")',
        "\t\t(effects (font (size 0.35 0.35) (thickness 0.07)))",
        "\t)",
        f'\t(fp_rect (start -3.75 -1.95) (end 3.75 1.95) (stroke (width 0.1) (type solid)) (fill no) (layer "F.Fab") (uuid "{uid()}"))',
        f'\t(fp_rect (start -4.05 -2.30) (end 4.05 2.30) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd") (uuid "{uid()}"))',
        # Window keep-out drawn just outside the body, on the drawing layer.
        f'\t(fp_rect (start -4.30 -2.50) (end 4.30 2.50) (stroke (width 0.12) (type default)) (fill no) (layer "User.1") (uuid "{uid()}"))',
        f'\t(fp_text user "WINDOW" (at 0 2.15) (layer "User.1") (uuid "{uid()}") (effects (font (size 0.4 0.4) (thickness 0.08))))',
        # Optical barrier slot. Inner pad edges are at x = +/-1.025. Slot edges at +/-0.40.
        f'\t(fp_line (start -0.40 -1.70) (end 0.40 -1.70) (stroke (width 0.05) (type solid)) (layer "Edge.Cuts") (uuid "{uid()}"))',
        f'\t(fp_line (start 0.40 -1.70) (end 0.40 1.70) (stroke (width 0.05) (type solid)) (layer "Edge.Cuts") (uuid "{uid()}"))',
        f'\t(fp_line (start 0.40 1.70) (end -0.40 1.70) (stroke (width 0.05) (type solid)) (layer "Edge.Cuts") (uuid "{uid()}"))',
        f'\t(fp_line (start -0.40 1.70) (end -0.40 -1.70) (stroke (width 0.05) (type solid)) (layer "Edge.Cuts") (uuid "{uid()}"))',
        f'\t(fp_circle (center -3.55 1.70) (end -3.43 1.70) (stroke (width 0.1) (type solid)) (fill yes) (layer "F.Fab") (uuid "{uid()}"))',
    ]
    for num, x, y, w, h in pads:
        lines.append(
            f'\t(pad "{num}" smd roundrect (at {x:.2f} {y:.2f}) (size {w:.2f} {h:.2f})'
            f' (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15) (uuid "{uid()}"))'
        )
    lines.append(model_clause("SFH7072.wrl").rstrip())
    lines.append(")")
    (PRETTY / "SFH7072.kicad_mod").write_text("\n".join(lines) + "\n", encoding="utf-8")
    box_wrl(THREED / "SFH7072.wrl", 7.50, 3.90, 0.90, (0.05, 0.05, 0.05))


def max17048():
    """8-bump WLP, 0.4 mm pitch, 2 rows x 4. Ball map is the published WLP figure.

    A1 CTG, A2 CELL, A3 VDD, A4 GND on one row; B1 SDA, B2 SCL, B3 QSTRT, B4 ALRT.
    Body used here is 1.61 x 0.86 mm, which is the WLP outline commonly published
    for the T10 reel. NSMD pad 0.25 mm.
    """
    cols = [-0.60, -0.20, 0.20, 0.60]
    rows = {"A": 0.20, "B": -0.20}
    names = {
        "A": ["A1", "A2", "A3", "A4"],
        "B": ["B1", "B2", "B3", "B4"],
    }
    pads = []
    for row, y in rows.items():
        for num, x in zip(names[row], cols):
            pads.append((num, x, y, 0.25, 0.25))
    fp_two_pad(
        "MAX17048_WLP",
        "MAX17048 WLP 8-bump 0.4 mm pitch. Land from the WLP outline, NSMD 0.25 mm.",
        pads,
        (-0.80, -0.43, 0.80, 0.43),
        (-1.05, -0.70, 1.05, 0.70),
        silk_pin1=(-0.70, 0.35),
    )
    # fp_two_pad already closed the file without a model. Append the model.
    path = PRETTY / "MAX17048_WLP.kicad_mod"
    text = path.read_text(encoding="utf-8").rstrip()
    text = text[:-1] + "\n" + model_clause("MAX17048_WLP.wrl") + ")\n"
    path.write_text(text, encoding="utf-8")
    box_wrl(THREED / "MAX17048_WLP.wrl", 1.61, 0.86, 0.40, (0.15, 0.15, 0.18))


def nf2w():
    """Nichia NF2W757G-F1 is a 3.0 mm package. Two-pad land.

    Pad size 1.10 x 2.10 mm, gap 0.80 mm, pin 1 anode on the marked side.
    The Nichia drawing was not available as text; this is the 3030 land
    scaled to the 3.0 x 3.0 mm body with the anode on pin 1.
    """
    fp_two_pad(
        "NF2W757G",
        "NF2W757G-F1 3030 white LED. Anode is pin 1. Land scaled to the 3.0 mm body.",
        [("1", -0.95, 0, 1.10, 2.10), ("2", 0.95, 0, 1.10, 2.10)],
        (-1.50, -1.50, 1.50, 1.50),
        (-1.85, -1.85, 1.85, 1.85),
        silk_pin1=(-1.65, 1.35),
    )
    path = PRETTY / "NF2W757G.kicad_mod"
    text = path.read_text(encoding="utf-8").rstrip()
    text = text[:-1] + "\n" + model_clause("NF2W757G.wrl") + ")\n"
    path.write_text(text, encoding="utf-8")
    box_wrl(THREED / "NF2W757G.wrl", 3.00, 3.00, 0.70, (0.85, 0.85, 0.75))


def pads_ecg5():
    """Five flat electrode pads. No header."""
    labels = [("1", "A+"), ("2", "A-"), ("3", "RLD"), ("4", "B+"), ("5", "B-")]
    pitch = 1.70
    xs = [(i - 2) * pitch for i in range(5)]
    lines = [
        '(footprint "Pads_ECG"',
        "\t(version 20241229)",
        '\t(generator "vitalq")',
        '\t(layer "F.Cu")',
        '\t(descr "Five ECG solder pads: ADS pair, RLD, AFE pair. No header.")',
        "\t(attr smd)",
        f'\t(property "Reference" "J**" (at 0 -2.2 0) (layer "F.Fab") (uuid "{uid()}")',
        "\t\t(effects (font (size 0.35 0.35) (thickness 0.07)))",
        "\t)",
        f'\t(property "Value" "Pads_ECG" (at 0 1.6 0) (layer "F.Fab") (hide yes) (uuid "{uid()}")',
        "\t\t(effects (font (size 0.35 0.35) (thickness 0.07)))",
        "\t)",
        f'\t(fp_rect (start -4.55 -1.55) (end 4.55 1.55) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd") (uuid "{uid()}"))',
    ]
    for (num, _lab), x in zip(labels, xs):
        lines.append(
            f'\t(pad "{num}" smd roundrect (at {x:.2f} 0) (size 1.15 1.70)'
            f' (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15) (uuid "{uid()}"))'
        )
    lines.append(")")
    (PRETTY / "Pads_ECG.kicad_mod").write_text("\n".join(lines) + "\n", encoding="utf-8")


def append_symbols(blocks: list[str]) -> None:
    text = SYM.read_text(encoding="utf-8")
    present = set(re.findall(r'\(symbol "([^"]+)"', text))
    add = []
    for block in blocks:
        m = re.match(r'\s*\(symbol "([^"]+)"', block)
        if not m:
            raise SystemExit("symbol block has no name")
        if m.group(1) in present:
            continue
        add.append(block if block.endswith("\n") else block + "\n")
    if not add:
        return
    text = text.rstrip()
    if not text.endswith(")"):
        raise SystemExit("symbol lib did not end with )")
    text = text[:-1] + "\n" + "\n".join(add) + ")\n"
    SYM.write_text(text, encoding="utf-8")


def renamed_flat(text: str, src: str, dest: str, footprint: str, description: str) -> str:
    block = make_lib.flatten(text, src)
    block = block.replace(f'(symbol "{src}"', f'(symbol "{dest}"', 1)
    block = block.replace(f'"{src}_', f'"{dest}_')
    block = re.sub(r'\(property "Value" "[^"]*"', f'(property "Value" "{dest}"', block, count=1)
    block = re.sub(r'\(property "Footprint" "[^"]*"', f'(property "Footprint" "{footprint}"', block, count=1)
    block = re.sub(
        r'\(property "Description" "[^"]*"',
        f'(property "Description" "{description}"',
        block,
        count=1,
    )
    return block


def main() -> None:
    THREED.mkdir(exist_ok=True)
    # Official lands whose STEP/WRL is not installed. Bodies from the datasheets.
    copy_fp(
        KFP / "Package_BGA.pretty" / "Texas_DSBGA-6_0.855x1.255mm_Layout2x3_P0.4mm_LevelC.kicad_mod",
        "TPS61240_YFF",
        "TPS61240_YFF.wrl",
    )
    box_wrl(THREED / "TPS61240_YFF.wrl", 0.86, 1.26, 0.50, (0.12, 0.12, 0.12))
    copy_fp(
        KFP / "Package_DFN_QFN.pretty" / "UQFN-16_1.8x2.6mm_P0.4mm.kicad_mod",
        "TCA6408A_RSV",
        "TCA6408A_RSV.wrl",
    )
    box_wrl(THREED / "TCA6408A_RSV.wrl", 1.80, 2.60, 0.50, (0.15, 0.15, 0.18))
    copy_fp(
        KFP / "Package_SON.pretty" / "WSON-8-1EP_8x6mm_P1.27mm_EP3.4x4.3mm.kicad_mod",
        "W25Q512_WSON8",
        "W25Q512_WSON8.wrl",
    )
    # Footprint fab is 8 mm in X (across the two pin rows) and 6 mm in Y.
    box_wrl(THREED / "W25Q512_WSON8.wrl", 8.00, 6.00, 0.80, (0.12, 0.12, 0.14))
    copy_fp(
        KFP / "Package_SON.pretty" / "Texas_DPY0002A_0.6x1mm_P0.65mm.kicad_mod",
        "TPD1E10B06_DPY",
        "TPD1E10B06_DPY.wrl",
    )
    box_wrl(THREED / "TPD1E10B06_DPY.wrl", 1.00, 0.60, 0.40, (0.2, 0.2, 0.2))
    copy_fp(
        KFP / "Inductor_SMD.pretty" / "L_Murata_DFE201610P.kicad_mod",
        "L_DFE201612E",
        "L_DFE201612E.wrl",
    )
    # TI table 10-2 lists DFE201612E at 2.0 x 1.6 x 1.2 mm. The 201610 model is 1.0 mm tall.
    box_wrl(THREED / "L_DFE201612E.wrl", 2.00, 1.60, 1.20, (0.35, 0.35, 0.38))
    # TPS63802 official WSON-10 has a STEP, but confirm height. Keep official footprint.
    sfh7072()
    max17048()
    nf2w()
    pads_ecg5()

    blocks = []
    blocks.append(
        make_lib.kicad9_symbol(
            "TPS63802",
            "U",
            "TPS63802 3.3 V buck-boost, DLA 10-pin VSON-HR",
            "Package_SON:WSON-10-1EP_2x3mm_P0.5mm_EP0.84x2.4mm",
            [
                ("10", "VIN", "power_in", "L"),
                ("1", "EN", "input", "L"),
                ("2", "MODE", "input", "L"),
                ("6", "VOUT", "power_out", "R"),
                ("9", "L1", "passive", "R"),
                ("7", "L2", "passive", "R"),
                ("4", "FB", "input", "R"),
                ("5", "PG", "open_collector", "R"),
                ("8", "GND", "power_in", "D"),
                ("3", "AGND", "power_in", "D"),
                ("11", "EP", "passive", "D"),
            ],
            width=10.16,
        )
    )
    blocks.append(
        make_lib.kicad9_symbol(
            "TPS61240",
            "U",
            "TPS61240 5 V boost, YFF DSBGA-6",
            "vitalq:TPS61240_YFF",
            [
                ("A1", "VIN", "power_in", "L"),
                ("C1", "EN", "input", "L"),
                ("B1", "L", "passive", "L"),
                ("B2", "VOUT", "power_out", "R"),
                ("C2", "FB", "input", "R"),
                ("A2", "GND", "power_in", "D"),
            ],
            width=7.62,
        )
    )
    blocks.append(
        make_lib.kicad9_symbol(
            "MAX17048",
            "U",
            "MAX17048 fuel gauge, 8-bump WLP, I2C 0x36",
            "vitalq:MAX17048_WLP",
            [
                ("A2", "CELL", "power_in", "L"),
                ("A3", "VDD", "power_in", "L"),
                ("B1", "SDA", "bidirectional", "R"),
                ("B2", "SCL", "input", "R"),
                ("B4", "ALRT", "open_collector", "R"),
                ("A1", "CTG", "passive", "D"),
                ("B3", "QSTRT", "input", "D"),
                ("A4", "GND", "power_in", "D"),
            ],
            width=10.16,
        )
    )
    blocks.append(
        make_lib.kicad9_symbol(
            "SFH7072",
            "U",
            "ams OSRAM SFH 7072 PPG module, 2 green, red, IR, 2 photodiodes",
            "vitalq:SFH7072",
            [
                ("1", "PD1_C", "passive", "L"),
                ("2", "PD1_A", "passive", "L"),
                ("3", "PD2_C", "passive", "L"),
                ("10", "PD2_A", "passive", "L"),
                ("4", "IR_A", "passive", "R"),
                ("9", "IR_C", "passive", "R"),
                ("5", "G1_A", "passive", "R"),
                ("6", "G1_C", "passive", "R"),
                ("7", "R_A", "passive", "R"),
                ("8", "R_C", "passive", "R"),
                ("11", "G2_A", "passive", "R"),
                ("12", "G2_C", "passive", "R"),
            ],
            width=12.70,
        )
    )
    blocks.append(
        make_lib.kicad9_symbol(
            "LED_AK",
            "D",
            "LED, pin 1 anode, pin 2 cathode",
            "",
            [("1", "A", "passive", "U"), ("2", "K", "passive", "D")],
            width=2.54,
        )
    )
    blocks.append(
        make_lib.kicad9_symbol(
            "L",
            "L",
            "Inductor",
            "",
            [("1", "1", "passive", "U"), ("2", "2", "passive", "D")],
            width=2.54,
        )
    )
    blocks.append(
        make_lib.kicad9_symbol(
            "D_TVS_2",
            "D",
            "Bidirectional TVS, two pins",
            "",
            [("1", "1", "passive", "U"), ("2", "2", "passive", "D")],
            width=2.54,
        )
    )
    blocks.append(
        make_lib.kicad9_symbol(
            "Conn_01x05_Pin",
            "J",
            "Connector 5 pins",
            "",
            [(str(i), f"Pin_{i}", "passive", "R") for i in range(1, 6)],
            width=2.54,
        )
    )
    flash = KSYM.joinpath("Memory_Flash.kicad_sym").read_text(encoding="utf-8", errors="replace")
    blocks.append(
        renamed_flat(
            flash,
            "W25Q128JVE",
            "W25Q512JVEIQ",
            "vitalq:W25Q512_WSON8",
            "W25Q512JV 64 MB SPI flash, WSON-8 8x6 mm. Pinout matches the JV 8-pad WSON.",
        )
    )
    io = KSYM.joinpath("Interface_Expansion.kicad_sym").read_text(encoding="utf-8", errors="replace")
    blocks.append(
        renamed_flat(
            io,
            "TCA6408ARSV",
            "TCA6408ARSV",
            "vitalq:TCA6408A_RSV",
            "TCA6408A 8-bit I2C expander, RSV UQFN 1.8x2.6 mm. ADDR low is 0x20.",
        )
    )
    fet = KSYM.joinpath("Transistor_FET.kicad_sym").read_text(encoding="utf-8", errors="replace")
    blocks.append(
        renamed_flat(
            fet,
            "CSD13380F3",
            "CSD13380F3",
            "Package_DFN_QFN:Texas_PicoStar_DFN-3_0.69x0.60mm",
            "CSD13380F3 N-MOSFET, PicoStar 0.73 x 0.64 mm, 12 V.",
        )
    )
    append_symbols(blocks)
    print("chest library updated")


if __name__ == "__main__":
    main()
