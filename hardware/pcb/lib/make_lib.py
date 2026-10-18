#!/usr/bin/env python3
"""Build hardware/pcb/lib from KiCad official symbols and checked downloads.

Footprints that already exist in KiCad and match the manufacturer pin
numbering are not copied. This script only writes the local library.
"""

import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "lib"
PRETTY = LIB / "vitalq.pretty"
KSYM = Path("/usr/share/kicad/symbols")
EE_SYM = Path("/tmp/cad/ee/parts.kicad_sym")
EE_FP = Path("/tmp/cad/ee/parts.pretty")

OUT_SYM = LIB / "vitalq.kicad_sym"


def extract(text, name):
    token = f'(symbol "{name}"'
    start = text.find(token)
    if start < 0:
        raise SystemExit(f"symbol {name} not found")
    # Prefer a top-level definition (newline before the paren).
    depth = 0
    i = start
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
        i += 1
    raise SystemExit(f"unclosed {name}")


def flatten(text, name):
    block = extract(text, name)
    m = re.search(r'\(extends "([^"]+)"\)', block)
    if not m:
        return block
    parent = m.group(1)
    parent_block = extract(text, parent)
    units = re.findall(
        rf'\n(\t\t\(symbol "{re.escape(parent)}_\d+_\d+"[\s\S]*?\n\t\t\))',
        parent_block,
    )
    if not units:
        # units indented with two tabs inside the parent; fall back to any nested symbol
        units = re.findall(
            rf'(\(symbol "{re.escape(parent)}_\d+_\d+"[\s\S]*?\n\t\))',
            parent_block,
        )
    renamed = []
    for unit in units:
        renamed.append(unit.replace(f'"{parent}_', f'"{name}_'))
    block = re.sub(r'\s*\(extends "[^"]+"\)', "", block, count=1)
    # insert units before the final closing paren
    body = "\n" + "\n".join(renamed) + "\n"
    return block[:-1] + body + ")"


def rename_lib(block, libname):
    """Turn a bare symbol name into Lib:Name for embedding later. Keep file names bare."""
    return block


def kicad9_symbol(name, ref, description, footprint, pins, width=10.16, value=None):
    """pins: list of (number, name, etype, side) side in L/R/U/D. Y-up, 2.54 grid."""
    left = [p for p in pins if p[3] == "L"]
    right = [p for p in pins if p[3] == "R"]
    up = [p for p in pins if p[3] == "U"]
    down = [p for p in pins if p[3] == "D"]
    nside = max(len(left), len(right), 1)
    height = max(nside * 2.54 + 2.54, 7.62)
    top = height / 2
    pin_len = 2.54

    def y_for(i, n):
        if n == 1:
            return 0.0
        span = (n - 1) * 2.54
        return span / 2 - i * 2.54

    pin_sexpr = []
    for i, (num, pname, etype, side) in enumerate(left):
        y = y_for(i, len(left))
        pin_sexpr.append(
            f"""\t\t\t(pin {etype} line
\t\t\t\t(at {-width - pin_len:.2f} {y:.2f} 0)
\t\t\t\t(length {pin_len:.2f})
\t\t\t\t(name "{pname}" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "{num}" (effects (font (size 1.27 1.27))))
\t\t\t)"""
        )
    for i, (num, pname, etype, side) in enumerate(right):
        y = y_for(i, len(right))
        pin_sexpr.append(
            f"""\t\t\t(pin {etype} line
\t\t\t\t(at {width + pin_len:.2f} {y:.2f} 180)
\t\t\t\t(length {pin_len:.2f})
\t\t\t\t(name "{pname}" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "{num}" (effects (font (size 1.27 1.27))))
\t\t\t)"""
        )
    for i, (num, pname, etype, side) in enumerate(up):
        x = (i - (len(up) - 1) / 2) * 2.54
        pin_sexpr.append(
            f"""\t\t\t(pin {etype} line
\t\t\t\t(at {x:.2f} {top + pin_len:.2f} 270)
\t\t\t\t(length {pin_len:.2f})
\t\t\t\t(name "{pname}" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "{num}" (effects (font (size 1.27 1.27))))
\t\t\t)"""
        )
    for i, (num, pname, etype, side) in enumerate(down):
        x = (i - (len(down) - 1) / 2) * 2.54
        pin_sexpr.append(
            f"""\t\t\t(pin {etype} line
\t\t\t\t(at {x:.2f} {-top - pin_len:.2f} 90)
\t\t\t\t(length {pin_len:.2f})
\t\t\t\t(name "{pname}" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "{num}" (effects (font (size 1.27 1.27))))
\t\t\t)"""
        )
    pins_txt = "\n".join(pin_sexpr)
    val = value or name
    return f"""\t(symbol "{name}"
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(property "Reference" "{ref}" (at 0 {top + 2.54:.2f} 0) (effects (font (size 1.27 1.27))))
\t\t(property "Value" "{val}" (at 0 {-top - 2.54:.2f} 0) (effects (font (size 1.27 1.27))))
\t\t(property "Footprint" "{footprint}" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
\t\t(property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
\t\t(property "Description" "{description}" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
\t\t(symbol "{name}_0_1"
\t\t\t(rectangle (start {-width:.2f} {top:.2f}) (end {width:.2f} {-top:.2f})
\t\t\t\t(stroke (width 0.254) (type default)) (fill (type background)))
\t\t)
\t\t(symbol "{name}_1_1"
{pins_txt}
\t\t)
\t)"""


def fp_header(name, descr):
    return f"""(footprint "{name}"
\t(version 20241229)
\t(generator "vitalq")
\t(layer "F.Cu")
\t(descr "{descr}")
"""


def fp_footer():
    return ")\n"


def write_afe4900_fp():
    """YZ0030-C01 from SBAS861B p. 5. Hand drawn: LCSC C2651595 is a different IC."""
    # 6 rows A-F, 5 cols, 0.4 mm, NSMD 0.23 mm. A1 top-left, KiCad Y up.
    cols = [-0.8, -0.4, 0.0, 0.4, 0.8]
    rows = {"A": 1.0, "B": 0.6, "C": 0.2, "D": -0.2, "E": -0.6, "F": -1.0}
    lines = [fp_header("AFE4900YZR", "AFE4900 YZ0030-C01, SBAS861B p.5, 0.23 mm NSMD. Hand-drawn last resort.")]
    lines.append('\t(attr smd)')
    lines.append('\t(fp_text reference "REF**" (at 0 -2.2) (layer "F.SilkS") (effects (font (size 0.4 0.4) (thickness 0.06))))')
    lines.append('\t(fp_text value "AFE4900" (at 0 2.2) (layer "F.Fab") (effects (font (size 0.4 0.4) (thickness 0.06))))')
    # body 2.06 x 2.56 (X x Y). Courtyard 0.25 mm outside pads.
    lines.append('\t(fp_rect (start -1.03 -1.28) (end 1.03 1.28) (stroke (width 0.1) (type solid)) (fill no) (layer "F.Fab"))')
    lines.append('\t(fp_circle (center -1.15 1.15) (end -1.05 1.15) (stroke (width 0.1) (type solid)) (fill no) (layer "F.SilkS"))')
    lines.append('\t(fp_rect (start -1.2 -1.45) (end 1.2 1.45) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd"))')
    balls = {
        "A": ["INM_ECG", "INP", "INM", "I2C_SPI_SEL", "INM3"],
        "B": ["INP_ECG", "INP2", "INM2", "CONTROL1", "INP3"],
        "C": ["RX_SUP", "RLD_OUT", "DNC", "IO_SUP", "TX4"],
        "D": ["RX_GND", "BG", "PROG_OUT1", "TX1", "TX_GND"],
        "E": ["RESETZ", "SDOUT", "SEN", "TX3", "TX2"],
        "F": ["CLK", "I2C_DAT", "I2C_CLK", "ADC_RDY", "TX_SUP"],
    }
    for row, names in balls.items():
        for i, _ in enumerate(names):
            num = f"{row}{i+1}"
            x, y = cols[i], rows[row]
            lines.append(
                f'\t(pad "{num}" smd circle (at {x:.2f} {y:.2f}) (size 0.23 0.23) '
                f'(layers "F.Cu" "F.Paste" "F.Mask"))'
            )
    lines.append(fp_footer())
    (PRETTY / "AFE4900YZR.kicad_mod").write_text("\n".join(lines), encoding="utf-8")


def write_mlx_fp():
    """Five-pin land. LCSC C7541116 adds a pad 6 that the pin table does not have."""
    lines = [fp_header("MLX90632", "MLX90632 SLD, DOC 3901090632 Rev 13 Table 17. Five pins, no center pad.")]
    lines.append("\t(attr smd)")
    lines.append('\t(fp_text reference "REF**" (at 0 -2.4) (layer "F.SilkS") (effects (font (size 0.5 0.5) (thickness 0.08))))')
    lines.append('\t(fp_text value "MLX90632" (at 0 2.4) (layer "F.Fab") (effects (font (size 0.5 0.5) (thickness 0.08))))')
    lines.append('\t(fp_rect (start -1.5 -1.5) (end 1.5 1.5) (stroke (width 0.1) (type solid)) (fill no) (layer "F.Fab"))')
    lines.append('\t(fp_circle (center -1.7 1.2) (end -1.55 1.2) (stroke (width 0.12) (type solid)) (fill no) (layer "F.SilkS"))')
    lines.append('\t(fp_rect (start -1.8 -1.9) (end 1.8 1.7) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd"))')
    for i, x in enumerate([-1.0, -0.5, 0.0, 0.5, 1.0]):
        lines.append(
            f'\t(pad "{i+1}" smd rect (at {x:.2f} -1.35) (size 0.40 0.55) '
            f'(layers "F.Cu" "F.Paste" "F.Mask"))'
        )
    lines.append(fp_footer())
    (PRETTY / "MLX90632.kicad_mod").write_text("\n".join(lines), encoding="utf-8")


def convert_lga14():
    """LSM6DSV16X LCSC land, Y kept so pin 1 stays on the right (DS14764 Fig 5)."""
    src = (EE_FP / "LGA-14_L3.0-W2.5-P0.50-BR.kicad_mod").read_text()
    pads = re.findall(
        r'\(pad (\d+) smd rect \(at ([-\d.]+) ([-\d.]+) [-\d.]+\) \(size ([-\d.]+) ([-\d.]+)\)',
        src,
    )
    lines = [fp_header("LSM6DSV80X", "LGA-14 3.0x2.5 from LCSC C5267406 (LSM6DSV16X). Same ST outline; DSV80X pins 10 and 11 are NC.")]
    lines.append("\t(attr smd)")
    lines.append('\t(fp_text reference "REF**" (at 0 -2.2) (layer "F.SilkS") (effects (font (size 0.4 0.4) (thickness 0.06))))')
    lines.append('\t(fp_text value "LSM6DSV80X" (at 0 2.2) (layer "F.Fab") (effects (font (size 0.4 0.4) (thickness 0.06))))')
    lines.append('\t(fp_rect (start -1.5 -1.25) (end 1.5 1.25) (stroke (width 0.1) (type solid)) (fill no) (layer "F.Fab"))')
    lines.append('\t(fp_circle (center 1.7 1.05) (end 1.55 1.05) (stroke (width 0.12) (type solid)) (fill no) (layer "F.SilkS"))')
    lines.append('\t(fp_rect (start -1.9 -1.6) (end 1.9 1.6) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd"))')
    for num, x, y, w, h in pads:
        lines.append(
            f'\t(pad "{num}" smd rect (at {float(x):.2f} {float(y):.2f}) (size {float(w):.2f} {float(h):.2f}) '
            f'(layers "F.Cu" "F.Paste" "F.Mask"))'
        )
    lines.append(fp_footer())
    (PRETTY / "LSM6DSV80X.kicad_mod").write_text("\n".join(lines), encoding="utf-8")


def convert_ad5940():
    """Y-flip the LCSC BGA so A1 is top-left, matching AD5940 Rev G top view."""
    src = (EE_FP / "BGA-56_L4.2-W3.6-R8-C7-P0.40-TL.kicad_mod").read_text()
    pads = re.findall(
        r'\(pad (\w+) smd circle \(at ([-\d.]+) ([-\d.]+) [-\d.]+\) \(size ([-\d.]+) ([-\d.]+)\)',
        src,
    )
    lines = [fp_header("AD5940BCBZ", "WLCSP-56 from LCSC C650308, Y-flipped so A1 is top-left per AD5940/AD5941 Rev G.")]
    lines.append("\t(attr smd)")
    lines.append('\t(fp_text reference "REF**" (at 0 -2.8) (layer "F.SilkS") (effects (font (size 0.4 0.4) (thickness 0.06))))')
    lines.append('\t(fp_text value "AD5940" (at 0 2.8) (layer "F.Fab") (effects (font (size 0.4 0.4) (thickness 0.06))))')
    lines.append('\t(fp_rect (start -2.1 -1.8) (end 2.1 1.8) (stroke (width 0.1) (type solid)) (fill no) (layer "F.Fab"))')
    lines.append('\t(fp_circle (center -2.3 1.5) (end -2.15 1.5) (stroke (width 0.12) (type solid)) (fill no) (layer "F.SilkS"))')
    lines.append('\t(fp_rect (start -2.5 -2.2) (end 2.5 2.2) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd"))')
    for num, x, y, w, h in pads:
        yy = -float(y)
        lines.append(
            f'\t(pad "{num}" smd circle (at {float(x):.2f} {yy:.2f}) (size {float(w):.2f} {float(h):.2f}) '
            f'(layers "F.Cu" "F.Paste" "F.Mask"))'
        )
    lines.append(fp_footer())
    (PRETTY / "AD5940BCBZ.kicad_mod").write_text("\n".join(lines), encoding="utf-8")


def copy_kicad_symbols(text_out):
    wanted = {
        "RF_Module.kicad_sym": ["ESP32-WROOM-32E-R2"],
        "Battery_Management.kicad_sym": ["MCP73831-2-OT"],
        "Regulator_Linear.kicad_sym": ["XC6206PxxxMR"],
        "Interface_USB.kicad_sym": ["CP2102N-Axx-xQFN28"],
        "Sensor.kicad_sym": ["BME280"],
        "Sensor_Optical.kicad_sym": ["AS7341DLG"],
        "Interface.kicad_sym": ["PCA9306DC"],
        "Sensor_Temperature.kicad_sym": [],
        "Connector.kicad_sym": ["USB_C_Receptacle_USB2.0_16P", "Conn_01x02_Pin", "Conn_01x03_Pin", "Conn_01x04_Pin", "Conn_01x06_Pin"],
        "Device.kicad_sym": ["R", "C"],
        "power.kicad_sym": ["GND", "+3V3", "+1V8", "VBUS", "PWR_FLAG"],
        "Connector_Generic.kicad_sym": [],
    }
    blocks = []
    for fname, names in wanted.items():
        raw = (KSYM / fname).read_text(encoding="utf-8", errors="replace")
        for name in names:
            blocks.append(flatten(raw, name))
    return blocks


def main():
    PRETTY.mkdir(parents=True, exist_ok=True)
    blocks = copy_kicad_symbols("")
    # TPS7A2018: DBV pinout matches TI SBVS338. Symbol drawn to that pinout.
    # Footprint is the official SOT-23-5 (not copied).
    blocks.append(
        kicad9_symbol(
            "TPS7A2018PDBVR",
            "U",
            "1.8 V 300 mA LDO, SOT-23-5 DBV. Pins from SBVS338.",
            "Package_TO_SOT_SMD:SOT-23-5",
            [
                ("1", "IN", "power_in", "L"),
                ("2", "GND", "power_in", "D"),
                ("3", "EN", "input", "L"),
                ("4", "NC", "no_connect", "R"),
                ("5", "OUT", "power_out", "R"),
            ],
            width=7.62,
        )
    )
    # TMP117 DRV pinout from SNOSD82 Table 5-1. LCSC symbol swaps ADD0 and ALERT.
    blocks.append(
        kicad9_symbol(
            "TMP117AIDRVR",
            "U",
            "TMP117 WSON-6 DRV, SNOSD82 Table 5-1. Pin 7 is the exposed pad.",
            "Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm",
            [
                ("1", "SCL", "input", "L"),
                ("2", "GND", "power_in", "D"),
                ("3", "ADD0", "input", "L"),
                ("4", "V+", "power_in", "U"),
                ("5", "ALERT", "open_collector", "R"),
                ("6", "SDA", "bidirectional", "R"),
                ("7", "EP", "power_in", "D"),
            ],
            width=7.62,
        )
    )
    blocks.append(
        kicad9_symbol(
            "MLX90632",
            "U",
            "MLX90632 SLD, DOC 3901090632 Rev 13 Table 5.",
            "vitalq:MLX90632",
            [
                ("1", "SDA", "bidirectional", "L"),
                ("2", "VDD", "power_in", "U"),
                ("3", "GND", "power_in", "D"),
                ("4", "SCL", "input", "R"),
                ("5", "ADDR", "input", "L"),
            ],
            width=7.62,
        )
    )
    blocks.append(
        kicad9_symbol(
            "LSM6DSV80X",
            "U",
            "LSM6DSV80X LGA-14, DS14764 Rev 2 Table 2. Pins 10 and 11 are NC.",
            "vitalq:LSM6DSV80X",
            [
                ("1", "SDO", "input", "L"),
                ("2", "SDx", "input", "L"),
                ("3", "SCx", "input", "L"),
                ("4", "INT1", "output", "R"),
                ("5", "VDDIO", "power_in", "U"),
                ("6", "GND", "power_in", "D"),
                ("7", "GND2", "power_in", "D"),
                ("8", "VDD", "power_in", "U"),
                ("9", "INT2", "output", "R"),
                ("10", "NC1", "no_connect", "R"),
                ("11", "NC2", "no_connect", "R"),
                ("12", "CS", "input", "L"),
                ("13", "SCL", "input", "R"),
                ("14", "SDA", "bidirectional", "R"),
            ],
            width=10.16,
        )
    )
    # ADS1292 symbol uses official TQFP footprint. Pin numbers are SBAS502C pp. 4-5,
    # same numbering as the LCSC symbol which was checked against that table.
    blocks.append(
        kicad9_symbol(
            "ADS1292RIPBSR",
            "U",
            "ADS1292R TQFP-32, SBAS502C pp. 4-5. Footprint is the official KiCad TQFP-32.",
            "Package_QFP:TQFP-32_5x5mm_P0.5mm",
            [
                ("1", "PGA1N", "passive", "L"),
                ("2", "PGA1P", "passive", "L"),
                ("3", "IN1N", "input", "L"),
                ("4", "IN1P", "input", "L"),
                ("5", "IN2N", "input", "L"),
                ("6", "IN2P", "input", "L"),
                ("7", "PGA2N", "passive", "L"),
                ("8", "PGA2P", "passive", "L"),
                ("9", "VREFP", "passive", "L"),
                ("10", "VREFN", "passive", "L"),
                ("11", "VCAP1", "passive", "L"),
                ("12", "AVDD", "power_in", "U"),
                ("13", "AVSS", "power_in", "D"),
                ("14", "CLKSEL", "input", "R"),
                ("15", "PWDN", "input", "R"),
                ("16", "START", "input", "R"),
                ("17", "CLK", "input", "R"),
                ("18", "CS", "input", "R"),
                ("19", "DIN", "input", "R"),
                ("20", "SCLK", "input", "R"),
                ("21", "DOUT", "tri_state", "R"),
                ("22", "DRDY", "output", "R"),
                ("23", "DVDD", "power_in", "U"),
                ("24", "DGND", "power_in", "D"),
                ("25", "GPIO2", "bidirectional", "R"),
                ("26", "GPIO1", "bidirectional", "R"),
                ("27", "VCAP2", "passive", "L"),
                ("28", "RLDINV", "input", "L"),
                ("29", "RLDREF", "input", "L"),
                ("30", "RLDOUT", "output", "L"),
                ("31", "RESP_MODP", "output", "L"),
                ("32", "RESP_MODN", "output", "L"),
            ],
            width=12.7,
        )
    )
    # AD5940 balls from the LCSC symbol, which matches Rev G. Local symbol so the
    # sheet is a normal left/right box instead of a 74 mm import.
    ad_left = [
        ("A1", "AFE4", "passive"),
        ("A2", "AFE3", "passive"),
        ("A3", "AIN2", "passive"),
        ("A4", "AVDD", "power_in"),
        ("A5", "VREF_1V82", "passive"),
        ("A6", "SE0", "passive"),
        ("A7", "CE0", "passive"),
        ("A8", "RE0", "passive"),
        ("B1", "RCAL1", "passive"),
        ("B2", "AFE1", "passive"),
        ("B3", "AIN1", "passive"),
        ("B4", "AIN4_LPF0", "passive"),
        ("B5", "AIN3", "passive"),
        ("B6", "DE0", "passive"),
        ("B7", "VZERO0", "passive"),
        ("B8", "RC0_1", "passive"),
        ("C1", "RCAL0", "passive"),
        ("C2", "AFE2", "passive"),
        ("C3", "DNC1", "no_connect"),
        ("C4", "AGND", "power_in"),
        ("C5", "AIN6", "passive"),
        ("C6", "RC0_2", "no_connect"),
        ("C7", "VBIAS0", "passive"),
        ("C8", "RC0_0", "passive"),
        ("D1", "VBIAS_CAP", "passive"),
        ("D2", "AIN0", "passive"),
        ("D3", "DNC2", "no_connect"),
        ("D4", "DNC3", "no_connect"),
    ]
    ad_right = [
        ("D5", "AGND_REF", "power_in"),
        ("D6", "GPIO1", "bidirectional"),
        ("D7", "VREF_2V5", "passive"),
        ("D8", "AVDD_REG", "passive"),
        ("E1", "GPIO2", "bidirectional"),
        ("E2", "GPIO3", "bidirectional"),
        ("E3", "AGND2", "power_in"),
        ("E4", "DGND", "power_in"),
        ("E5", "DGND2", "power_in"),
        ("E6", "DGND3", "power_in"),
        ("E7", "MOSI", "input"),
        ("E8", "MISO", "tri_state"),
        ("F1", "RESET", "input"),
        ("F2", "AVDD2", "power_in"),
        ("F3", "DVDD", "power_in"),
        ("F4", "GPIO6", "bidirectional"),
        ("F5", "GPIO0", "bidirectional"),
        ("F6", "GPIO5", "bidirectional"),
        ("F7", "CS", "input"),
        ("F8", "SCLK", "input"),
        ("G1", "DNC4", "no_connect"),
        ("G2", "IOVDD", "power_in"),
        ("G3", "DVDD_REG", "passive"),
        ("G4", "GPIO7", "bidirectional"),
        ("G5", "XTALI", "input"),
        ("G6", "XTALO", "output"),
        ("G7", "GPIO4", "bidirectional"),
        ("G8", "DNC5", "no_connect"),
    ]
    blocks.append(
        kicad9_symbol(
            "AD5940BCBZ",
            "U",
            "AD5940 WLCSP-56 ball names from AD5940/AD5941 Rev G, checked against LCSC C650308.",
            "vitalq:AD5940BCBZ",
            [(n, name, et, "L") for n, name, et in ad_left]
            + [(n, name, et, "R") for n, name, et in ad_right],
            width=15.24,
        )
    )
    afe_l = [
        ("A1", "INM_ECG", "input"),
        ("B1", "INP_ECG", "input"),
        ("A2", "INP", "input"),
        ("A3", "INM", "input"),
        ("B2", "INP2", "input"),
        ("B3", "INM2", "input"),
        ("B5", "INP3", "input"),
        ("A5", "INM3", "input"),
        ("C2", "RLD_OUT", "output"),
        ("D2", "BG", "passive"),
        ("D4", "TX1", "output"),
        ("E5", "TX2", "output"),
        ("E4", "TX3", "output"),
        ("C5", "TX4", "output"),
        ("F5", "TX_SUP", "power_in"),
    ]
    afe_r = [
        ("C1", "RX_SUP", "power_in"),
        ("D1", "RX_GND", "power_in"),
        ("C4", "IO_SUP", "power_in"),
        ("D5", "TX_GND", "power_in"),
        ("E1", "RESETZ", "input"),
        ("E2", "SDOUT", "tri_state"),
        ("E3", "SEN", "input"),
        ("F2", "MOSI", "input"),
        ("F3", "SCLK", "input"),
        ("F4", "ADC_RDY", "output"),
        ("A4", "I2C_SPI_SEL", "input"),
        ("B4", "CONTROL1", "input"),
        ("F1", "CLK", "input"),
        ("D3", "PROG_OUT1", "output"),
        ("C3", "DNC", "no_connect"),
    ]
    blocks.append(
        kicad9_symbol(
            "AFE4900YZR",
            "U",
            "AFE4900 YZ ball names. Public SBAS861B has no ball list; names from the full pin-function table. Footprint hand-drawn.",
            "vitalq:AFE4900YZR",
            [(n, name, et, "L") for n, name, et in afe_l]
            + [(n, name, et, "R") for n, name, et in afe_r],
            width=12.7,
        )
    )
    # Test point, single pin, not a power symbol.
    blocks.append(
        kicad9_symbol(
            "TestPoint",
            "TP",
            "Programming pad. Not a pushbutton.",
            "TestPoint:TestPoint_Pad_D1.5mm",
            [("1", "P", "passive", "R")],
            width=2.54,
        )
    )
    blocks.append(
        kicad9_symbol(
            "MountingHole",
            "H",
            "M2 non-plated mounting hole.",
            "MountingHole:MountingHole_2.2mm_M2",
            [("1", "MH", "no_connect", "R")],
            width=2.54,
        )
    )
    sym = "(kicad_symbol_lib\n\t(version 20231120)\n\t(generator \"vitalq\")\n" + "\n".join(blocks) + "\n)\n"
    # KiCad's ESP32-WROOM-32E-R2 symbol still points at the 32D land. The 32E
    # footprint is the matching official module outline (antenna at -Y).
    sym = sym.replace(
        'property "Footprint" "RF_Module:ESP32-WROOM-32D"',
        'property "Footprint" "RF_Module:ESP32-WROOM-32E"',
        1,
    )
    OUT_SYM.write_text(sym, encoding="utf-8")
    write_afe4900_fp()
    write_mlx_fp()
    if EE_FP.exists():
        convert_lga14()
        convert_ad5940()
    else:
        raise SystemExit("missing EasyEDA footprints in /tmp/cad/ee")
    print(f"wrote {OUT_SYM} and {PRETTY}")


if __name__ == "__main__":
    main()
