#!/usr/bin/env python3
"""Generate VitalQ hw_v1 project-local KiCad 9 symbols and footprints.

Run from anywhere: python3 hardware/pcb/lib/make_libs.py

Symbols and footprints are written next to this file. Datasheet pages are
recorded in each symbol Description and in SOURCES.md.
"""

from __future__ import annotations

import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
PRETTY = HERE / "vitalq.pretty"

NS = uuid.UUID("6f0c1a2e-7b44-4e1a-9c3d-a1b2c3d4e5f6")


def uid(key: str) -> str:
    return str(uuid.uuid5(NS, key))


def sexp_str(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


# ---------------------------------------------------------------------------
# Symbols
# ---------------------------------------------------------------------------

def _pin(num, name, etype, x, y, angle):
    return f"""\t\t\t(pin {etype} line
\t\t\t\t(at {x:.2f} {y:.2f} {angle})
\t\t\t\t(length 2.54)
\t\t\t\t(name {sexp_str(name)}
\t\t\t\t\t(effects (font (size 1.27 1.27)))
\t\t\t\t)
\t\t\t\t(number {sexp_str(str(num))}
\t\t\t\t\t(effects (font (size 1.27 1.27)))
\t\t\t\t)
\t\t\t)"""


def emit_symbol(name, ref, footprint, description, datasheet, pins):
    """pins: list of (number, name, electrical_type, side) side in L/R."""
    left = [p for p in pins if p[3] == "L"]
    right = [p for p in pins if p[3] == "R"]
    if len(left) + len(right) != len(pins):
        raise SystemExit(f"{name}: pin side must be L or R")
    name_w = max((len(p[1]) for p in pins), default=4)
    half_w = max(10.16, name_w * 0.7 + 2.54)
    n = max(len(left), len(right), 1)
    half_h = n * 2.54 / 2 + 1.27

    def ys(group):
        if not group:
            return []
        top = (len(group) - 1) * 2.54 / 2
        return [top - i * 2.54 for i in range(len(group))]

    pin_sexprs = []
    for (num, pname, etype, _side), y in zip(left, ys(left)):
        pin_sexprs.append(_pin(num, pname, etype, -(half_w + 2.54), y, 0))
    for (num, pname, etype, _side), y in zip(right, ys(right)):
        pin_sexprs.append(_pin(num, pname, etype, half_w + 2.54, y, 180))

    body = f"""\t\t(symbol {sexp_str(name + "_0_1")}
\t\t\t(rectangle
\t\t\t\t(start {-half_w:.2f} {half_h:.2f})
\t\t\t\t(end {half_w:.2f} {-half_h:.2f})
\t\t\t\t(stroke (width 0.254) (type default))
\t\t\t\t(fill (type background))
\t\t\t)
\t\t)"""
    pin_unit = f"""\t\t(symbol {sexp_str(name + "_1_1")}
{chr(10).join(pin_sexprs)}
\t\t)"""
    return f"""\t(symbol {sexp_str(name)}
\t\t(pin_names (offset 0.254))
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(property "Reference" {sexp_str(ref)}
\t\t\t(at 0 {half_h + 1.27:.2f} 0)
\t\t\t(effects (font (size 1.27 1.27)))
\t\t)
\t\t(property "Value" {sexp_str(name)}
\t\t\t(at 0 {-half_h - 1.27:.2f} 0)
\t\t\t(effects (font (size 1.27 1.27)))
\t\t)
\t\t(property "Footprint" {sexp_str(footprint)}
\t\t\t(at 0 0 0)
\t\t\t(effects (font (size 1.27 1.27)) (hide yes))
\t\t)
\t\t(property "Datasheet" {sexp_str(datasheet)}
\t\t\t(at 0 0 0)
\t\t\t(effects (font (size 1.27 1.27)) (hide yes))
\t\t)
\t\t(property "Description" {sexp_str(description)}
\t\t\t(at 0 0 0)
\t\t\t(effects (font (size 1.27 1.27)) (hide yes))
\t\t)
{body}
{pin_unit}
\t\t(embedded_fonts no)
\t)"""


# ---------------------------------------------------------------------------
# Footprints
# ---------------------------------------------------------------------------

def _fp_header(name, descr, tags):
    return f"""(footprint {sexp_str(name)}
\t(version 20241229)
\t(generator "vitalq_make_libs")
\t(generator_version "1")
\t(layer "F.Cu")
\t(descr {sexp_str(descr)})
\t(tags {sexp_str(tags)})
\t(property "Reference" "REF**"
\t\t(at 0 -4 0)
\t\t(layer "F.SilkS")
\t\t(uuid {sexp_str(uid(name + ":ref"))})
\t\t(effects (font (size 1 1) (thickness 0.15)))
\t)
\t(property "Value" {sexp_str(name)}
\t\t(at 0 4 0)
\t\t(layer "F.Fab")
\t\t(uuid {sexp_str(uid(name + ":val"))})
\t\t(effects (font (size 1 1) (thickness 0.15)))
\t)
\t(attr smd)
"""


def _line(name, idx, x1, y1, x2, y2, layer, width):
    return f"""\t(fp_line
\t\t(start {x1:.4f} {y1:.4f})
\t\t(end {x2:.4f} {y2:.4f})
\t\t(stroke (width {width}) (type solid))
\t\t(layer {sexp_str(layer)})
\t\t(uuid {sexp_str(uid(f"{name}:line:{layer}:{idx}:{x1}:{y1}"))})
\t)
"""


def _rect(name, prefix, x0, y0, x1, y1, layer, width):
    corners = [(x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)]
    return "".join(_line(name, f"{prefix}{i}", *c, layer, width) for i, c in enumerate(corners))


def _pad(name, num, x, y, w, h, shape="circle"):
    size = f"{w:.4f} {h:.4f}"
    return f"""\t(pad {sexp_str(str(num))} smd {shape}
\t\t(at {x:.4f} {y:.4f})
\t\t(size {size})
\t\t(layers "F.Cu" "F.Paste" "F.Mask")
\t\t(uuid {sexp_str(uid(f"{name}:pad:{num}"))})
\t)
"""


def _circle(name, x, y, r, layer):
    return f"""\t(fp_circle
\t\t(center {x:.4f} {y:.4f})
\t\t(end {x + r:.4f} {y:.4f})
\t\t(stroke (width 0.12) (type solid))
\t\t(fill none)
\t\t(layer {sexp_str(layer)})
\t\t(uuid {sexp_str(uid(f"{name}:circ:{x}:{y}:{layer}"))})
\t)
"""


def write_fp(name, body):
    PRETTY.mkdir(parents=True, exist_ok=True)
    (PRETTY / f"{name}.kicad_mod").write_text(body, encoding="utf-8")


def fp_bga(name, descr, pads, pitch_note, body_x, body_y, pad_d):
    """pads: list of (pad_name, x, y) with Y up, A1 at top-left of the land."""
    xs = [p[1] for p in pads]
    ys = [p[2] for p in pads]
    m = 0.6
    silk = _rect(name, "silk", min(xs) - m, min(ys) - m, max(xs) + m, max(ys) + m, "F.SilkS", 0.12)
    fab = _rect(name, "fab", -body_x / 2, -body_y / 2, body_x / 2, body_y / 2, "F.Fab", 0.1)
    crt = _rect(name, "crt", -body_x / 2 - 0.3, -body_y / 2 - 0.3, body_x / 2 + 0.3, body_y / 2 + 0.3, "F.CrtYd", 0.05)
    # Pin-1 dot just outside the top-left corner.
    dot = _circle(name, -body_x / 2 - 0.35, body_y / 2 + 0.35, 0.15, "F.SilkS")
    pad_txt = "".join(_pad(name, num, x, y, pad_d, pad_d) for num, x, y in pads)
    text = (
        _fp_header(name, descr + " " + pitch_note, "VitalQ BGA")
        + silk
        + fab
        + crt
        + dot
        + pad_txt
        + "\t(embedded_fonts no)\n)\n"
    )
    write_fp(name, text)


def fp_lsm6():
    """DS14764 Rev 2 Fig 5 bottom view: pin 1 is top-right when looking at the lands.

    Y-up footprint, body 3.0 mm (X) by 2.5 mm (Y). Terminals from Fig 24 p. 158
    are 0.25 x 0.475 mm; pads here are enlarged slightly for a prototype land
    (TN0018 is the official land pattern and was not copied).
    """
    name = "LSM6DSV80X"
    pads = []
    # Right column, top to bottom: pins 1..4
    for i, y in enumerate((0.75, 0.25, -0.25, -0.75)):
        pads.append((str(i + 1), 1.15, y, 0.50, 0.30))
    # Bottom, right to left: pins 5..7
    for i, x in enumerate((0.5, 0.0, -0.5)):
        pads.append((str(i + 5), x, -0.90, 0.30, 0.50))
    # Left column, bottom to top: pins 8..11
    for i, y in enumerate((-0.75, -0.25, 0.25, 0.75)):
        pads.append((str(i + 8), -1.15, y, 0.50, 0.30))
    # Top, left to right: pins 12..14
    for i, x in enumerate((-0.5, 0.0, 0.5)):
        pads.append((str(i + 12), x, 0.90, 0.30, 0.50))
    body = _fp_header(
        name,
        "LSM6DSV80X LGA-14 3.0x2.5 mm. Pin 1 corner follows DS14764 Rev 2 Fig 5 bottom view (pin 1 on the right). Land is derived from Fig 24, not TN0018.",
        "LGA-14 LSM6DSV80X",
    )
    body += _rect(name, "fab", -1.5, -1.25, 1.5, 1.25, "F.Fab", 0.1)
    body += _rect(name, "silk", -1.7, -1.45, 1.7, 1.45, "F.SilkS", 0.12)
    body += _rect(name, "crt", -1.85, -1.6, 1.85, 1.6, "F.CrtYd", 0.05)
    body += _circle(name, 1.7, 1.15, 0.12, "F.SilkS")
    for num, x, y, w, h in pads:
        body += _pad(name, num, x, y, w, h, shape="rect")
    body += "\t(embedded_fonts no)\n)\n"
    write_fp(name, body)


def fp_mlx():
    """MLX90632 single-row 5-pin land. Table 17 p. 46: 5 pins, 0.50 mm pitch.

    Bottom view (Fig 25) shows pins 1-5 left to right. No exposed center pad:
    the pin table lists five terminals only.
    """
    name = "MLX90632"
    xs = [-1.0, -0.5, 0.0, 0.5, 1.0]
    body = _fp_header(
        name,
        "MLX90632 SFN 3x3 mm, 5 pins at 0.50 mm on one edge. DOC 3901090632 Rev 13 Table 17 p. 46 and Fig 25 bottom view.",
        "MLX90632",
    )
    body += _rect(name, "fab", -1.5, -1.5, 1.5, 1.5, "F.Fab", 0.1)
    body += _rect(name, "silk", -1.7, -1.7, 1.7, 1.55, "F.SilkS", 0.12)
    body += _rect(name, "crt", -1.9, -2.1, 1.9, 1.9, "F.CrtYd", 0.05)
    body += _circle(name, -1.35, -1.85, 0.12, "F.SilkS")
    for i, x in enumerate(xs, start=1):
        body += _pad(name, str(i), x, -1.35, 0.40, 0.55, shape="rect")
    body += "\t(embedded_fonts no)\n)\n"
    write_fp(name, body)


def afe_pads():
    """YZ0030-C01, 6x5, 0.4 mm. A1 is top-left of the land (SBAS861B p. 5)."""
    rows = "ABCDEF"
    pads = []
    for ri, row in enumerate(rows):
        for ci in range(5):
            pads.append((f"{row}{ci + 1}", -0.8 + ci * 0.4, 1.0 - ri * 0.4))
    return pads


def ad_pads():
    """CB-56-3, 8 columns x 7 rows, 0.4 mm. Top view ball-side-down, A1 top-left.

    8-column span is the 4.2 mm body axis (X). 7-row span is the 3.6 mm axis (Y).
    NSMD diameter 0.22 mm is an assumption; the public extract did not state it.
    """
    rows = "ABCDEFG"
    pads = []
    for ri, row in enumerate(rows):
        for ci in range(8):
            pads.append((f"{row}{ci + 1}", -1.4 + ci * 0.4, 1.2 - ri * 0.4))
    return pads


# ---------------------------------------------------------------------------
# Pin tables. side L = left of the symbol, R = right.
# ---------------------------------------------------------------------------

PASS = "passive"
PIN = "input"
POUT = "output"
PBI = "bidirectional"
PTRI = "tri_state"
PWR = "power_in"
PWRO = "power_out"
POC = "open_collector"
NC = "no_connect"


def side_for(name, right_names):
    return "R" if name in right_names else "L"


AFE_BALLS = {
    "A1": ("INM_ECG", PASS),
    "A2": ("INP", PASS),
    "A3": ("INM", PASS),
    "A4": ("I2C_SPI_SEL", PIN),
    "A5": ("INM3", PASS),
    "B1": ("INP_ECG", PASS),
    "B2": ("INP2", PASS),
    "B3": ("INM2", PASS),
    "B4": ("CONTROL1", PIN),
    "B5": ("INP3", PASS),
    "C1": ("RX_SUP", PWR),
    "C2": ("RLD_OUT", PASS),
    "C3": ("DNC", NC),
    "C4": ("IO_SUP", PWR),
    "C5": ("TX4", PASS),
    "D1": ("RX_GND", PWR),
    "D2": ("BG", PASS),
    "D3": ("PROG_OUT1", PASS),
    "D4": ("TX1", PASS),
    "D5": ("TX_GND", PWR),
    "E1": ("RESETZ", PIN),
    "E2": ("SDOUT", PTRI),
    "E3": ("SEN", PIN),
    "E4": ("TX3", PASS),
    "E5": ("TX2", PASS),
    "F1": ("CLK", PBI),
    "F2": ("I2C_DAT", PBI),
    "F3": ("I2C_CLK", PIN),
    "F4": ("ADC_RDY", POUT),
    "F5": ("TX_SUP", PWR),
}

AD_ROWS = {
    "A": ["AFE4", "AFE3", "AIN2", "AVDD", "VREF_1V82", "SE0", "CE0", "RE0"],
    "B": ["RCAL1", "AFE1", "AIN1", "AIN4_LPF0", "AIN3", "DE0", "VZERO0", "RC0_1"],
    "C": ["RCAL0", "AFE2", "DNC", "AGND", "AIN6", "RC0_2", "VBIAS0", "RC0_0"],
    "D": ["VBIAS_CAP", "AIN0", "DNC", "DNC", "AGND_REF", "GPIO1", "VREF_2V5", "AVDD_REG"],
    "E": ["GPIO2", "GPIO3", "AGND", "DGND", "DGND", "DGND", "MOSI", "MISO"],
    "F": ["RESET", "AVDD", "DVDD", "GPIO6", "GPIO0", "GPIO5", "CS", "SCLK"],
    "G": ["DNC", "IOVDD", "DVDD_REG_1V8", "GPIO7", "XTALI", "XTALO", "GPIO4", "DNC"],
}

AD_TYPE = {
    "AVDD": PWR,
    "DVDD": PWR,
    "IOVDD": PWR,
    "AGND": PWR,
    "DGND": PWR,
    "AGND_REF": PWR,
    "DNC": NC,
    "MOSI": PIN,
    "MISO": PTRI,
    "SCLK": PIN,
    "CS": PIN,
    "RESET": PIN,
    "XTALI": PASS,
    "XTALO": PASS,
}
for i in range(8):
    AD_TYPE[f"GPIO{i}"] = PBI


ADS_PINS = [
    (1, "PGA1N", PASS),
    (2, "PGA1P", PASS),
    (3, "IN1N", PASS),
    (4, "IN1P", PASS),
    (5, "IN2N", PASS),
    (6, "IN2P", PASS),
    (7, "PGA2N", PASS),
    (8, "PGA2P", PASS),
    (9, "VREFP", PASS),
    (10, "VREFN", PWR),
    (11, "VCAP1", PASS),
    (12, "AVDD", PWR),
    (13, "AVSS", PWR),
    (14, "CLKSEL", PIN),
    (15, "PWDN", PIN),
    (16, "START", PIN),
    (17, "CLK", PTRI),
    (18, "CS", PIN),
    (19, "DIN", PIN),
    (20, "SCLK", PIN),
    (21, "DOUT", PTRI),
    (22, "DRDY", POUT),
    (23, "DVDD", PWR),
    (24, "DGND", PWR),
    (25, "GPIO2", PBI),
    (26, "GPIO1", PBI),
    (27, "VCAP2", PASS),
    (28, "RLDINV", PASS),
    (29, "RLDREF", PASS),
    (30, "RLDOUT", PASS),
    (31, "RESP_MODP", PASS),
    (32, "RESP_MODN", PASS),
]


def build_symbols():
    afe_right = {"RESETZ", "SEN", "I2C_CLK", "I2C_DAT", "SDOUT", "ADC_RDY", "CLK"}
    afe_pins = []
    for row in "ABCDEF":
        for col in range(1, 6):
            num = f"{row}{col}"
            name, etype = AFE_BALLS[num]
            afe_pins.append((num, name, etype, side_for(name, afe_right)))

    ad_right = {"MOSI", "MISO", "SCLK", "CS", "RESET", "XTALI", "XTALO"} | {f"GPIO{i}" for i in range(8)}
    ad_pins = []
    for row, names in AD_ROWS.items():
        for col, name in enumerate(names, start=1):
            etype = AD_TYPE.get(name, PASS)
            ad_pins.append((f"{row}{col}", name, etype, side_for(name, ad_right)))

    ads_right_nums = set(range(17, 33))
    ads_pins = [(n, name, etype, "R" if n in ads_right_nums else "L") for n, name, etype in ADS_PINS]

    lsm = [
        (1, "SDO_TA0", PIN, "L"),
        (2, "SDx", PIN, "L"),
        (3, "SCx", PIN, "L"),
        (4, "INT1", POUT, "R"),
        (5, "VDDIO", PWR, "L"),
        (6, "GND", PWR, "L"),
        (7, "GND", PWR, "L"),
        (8, "VDD", PWR, "L"),
        (9, "INT2", POUT, "R"),
        (10, "NC", NC, "R"),
        (11, "NC", NC, "R"),
        (12, "CS", PIN, "R"),
        (13, "SCL", PIN, "R"),
        (14, "SDA", PBI, "R"),
    ]
    mlx = [
        (1, "SDA", PBI, "R"),
        (2, "VDD", PWR, "L"),
        (3, "GND", PWR, "L"),
        (4, "SCL", PIN, "R"),
        (5, "ADDR", PIN, "L"),
    ]
    tps = [
        (1, "IN", PWR, "L"),
        (2, "GND", PWR, "L"),
        (3, "EN", PIN, "R"),
        (4, "NC", NC, "R"),
        (5, "OUT", PWRO, "R"),
    ]
    xc = [
        (1, "VSS", PWR, "L"),
        (2, "VIN", PWR, "L"),
        (3, "VOUT", PWRO, "R"),
    ]
    tmp = [
        (1, "SCL", PIN, "R"),
        (2, "GND", PWR, "L"),
        (3, "ALERT", POC, "R"),
        (4, "ADD0", PIN, "L"),
        (5, "V+", PWR, "L"),
        (6, "SDA", PBI, "R"),
        (7, "GND", PWR, "L"),
    ]

    symbols = [
        emit_symbol(
            "AFE4900YZR",
            "U",
            "vitalq:AFE4900YZR",
            "AFE4900 YZ0030-C01. Pin functions from the full ball table; SBAS861B (8 pages) has the land pattern on p. 5 (0.23 mm NSMD) but no pin list. ALDO_1V8 and DLDO_1V8 are internal nodes, not balls.",
            "https://www.ti.com/lit/ds/symlink/afe4900.pdf",
            afe_pins,
        ),
        emit_symbol(
            "AD5940BCBZ",
            "U",
            "vitalq:AD5940BCBZ",
            "AD5940 WLCSP-56 CB-56-3. Ball map from AD5940/AD5941 Rev G pin-function table. Pad diameter 0.22 mm is an assumption.",
            "https://www.analog.com/media/en/technical-documentation/data-sheets/ad5940-5941.pdf",
            ad_pins,
        ),
        emit_symbol(
            "ADS1292R",
            "U",
            "Package_QFP:TQFP-32_5x5mm_P0.5mm",
            "ADS1292R PBS TQFP-32. Pinout SBAS502C pp. 4-5. Uses the official KiCad TQFP-32 footprint.",
            "https://www.ti.com/lit/ds/symlink/ads1292r.pdf",
            ads_pins,
        ),
        emit_symbol(
            "LSM6DSV80X",
            "U",
            "vitalq:LSM6DSV80X",
            "LSM6DSV80X LGA-14. Pins DS14764 Rev 2 Table 2 pp. 9-11. Footprint pin-1 corner follows Fig 5 bottom view p. 9.",
            "https://www.st.com/resource/en/datasheet/lsm6dsv80x.pdf",
            lsm,
        ),
        emit_symbol(
            "MLX90632",
            "U",
            "vitalq:MLX90632",
            "MLX90632. Pins Table 5 p. 8 of DOC 3901090632 Rev 13. Single-row land Table 17 p. 46.",
            "https://www.melexis.com/en/documents/documentation/datasheets/datasheet-mlx90632",
            mlx,
        ),
        emit_symbol(
            "TPS7A2033PDBVR",
            "U",
            "Package_TO_SOT_SMD:SOT-23-5",
            "TPS7A2033 SOT-23-5 DBV. Pin functions SBVS338H p. 3. KiCad only ships the X2SON TPS7A20 symbol, so this one is local. Footprint is the official SOT-23-5.",
            "https://www.ti.com/lit/ds/symlink/tps7a20.pdf",
            tps,
        ),
        emit_symbol(
            "XC6206",
            "U",
            "Package_TO_SOT_SMD:SOT-23",
            "Torex XC6206 SOT-23: pin 1 VSS, pin 2 VIN, pin 3 VOUT. The KiCad XC6206PxxxMR symbol extends a part whose pin numbers are swapped, so it is not used.",
            "https://www.torexsemi.com/file/xc6206/XC6206.pdf",
            xc,
        ),
        emit_symbol(
            "TMP117",
            "U",
            "Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm",
            "TMP117 WSON-6. Pins match SNOSD82D Table 5-1 p. 4. Pin 7 is the exposed pad, tied to GND, because the official symbol has no EP pin and the WSON footprint pad 7 would otherwise be unconnected.",
            "https://www.ti.com/lit/ds/symlink/tmp117.pdf",
            tmp,
        ),
    ]
    text = (
        "(kicad_symbol_lib\n"
        "\t(version 20241209)\n"
        '\t(generator "vitalq_make_libs")\n'
        '\t(generator_version "1")\n'
        + "\n".join(symbols)
        + "\n)\n"
    )
    (HERE / "vitalq.kicad_sym").write_text(text, encoding="utf-8")
    return len(afe_pins), len(ad_pins)


def main():
    n_afe, n_ad = build_symbols()
    if n_afe != 30 or n_ad != 56:
        raise SystemExit(f"pin count mismatch afe={n_afe} ad={n_ad}")
    fp_bga(
        "AFE4900YZR",
        "AFE4900 YZ0030-C01 0.4 mm, NSMD pad 0.23 mm from SBAS861B p. 5.",
        afe_pads(),
        "A1 top-left.",
        body_x=2.06,
        body_y=2.56,
        pad_d=0.23,
    )
    fp_bga(
        "AD5940BCBZ",
        "AD5940 CB-56-3 3.6 x 4.2 mm, 0.4 mm pitch. Pad 0.22 mm is an assumption.",
        ad_pads(),
        "A1 top-left, top view ball side down.",
        body_x=4.2,
        body_y=3.6,
        pad_d=0.22,
    )
    fp_lsm6()
    fp_mlx()
    (HERE / "SOURCES.md").write_text(SOURCES, encoding="utf-8")
    print(f"wrote {HERE / 'vitalq.kicad_sym'} and {PRETTY}")


SOURCES = """# Local symbol and footprint sources

These parts are not used from the stock KiCad libraries, or the stock symbol
disagrees with the manufacturer pinout. Official KiCad footprints are reused
where the land pattern matches.

| Part | Symbol | Footprint | Datasheet evidence |
| --- | --- | --- | --- |
| AFE4900YZR | local | local `vitalq:AFE4900YZR` | Ball functions from the full AFE4900 pin table (the public SBAS861B PDF is 8 pages and has no pin list). Land: SBAS861B p. 5, YZ0030-C01, 0.4 mm pitch, 0.23 mm NSMD, A1 top-left. ALDO_1V8 and DLDO_1V8 are internal, not balls. |
| AD5940BCBZ | local | local `vitalq:AD5940BCBZ` | AD5940/AD5941 Rev G pin-function table, package CB-56-3, 3.6 x 4.2 mm, 0.4 mm. Top view, ball side down, A1 top-left. Pad diameter 0.22 mm is an assumption. |
| ADS1292R | local | official `Package_QFP:TQFP-32_5x5mm_P0.5mm` | SBAS502C pp. 4-5 pinout. No ADS1292R symbol in KiCad 9. |
| LSM6DSV80X | local | local `vitalq:LSM6DSV80X` | DS14764 Rev 2 Table 2 pp. 9-11, Fig 5 p. 9 bottom view (pin 1 on the right), Fig 24 p. 158 terminals. TN0018 (p. 157) is the official land pattern and was not copied; the stock KiCad LGA-14 pin-1 corner does not match Fig 5. |
| MLX90632 | local | local `vitalq:MLX90632` | DOC 3901090632 Rev 13 Table 5 p. 8 (pin names) and Table 17 p. 46 (one row of 5 pins, 0.50 mm). Fig 25 bottom view: pins 1-5 left to right. No center pad. |
| TPS7A2033PDBVR | local | official `SOT-23-5` | SBVS338H pin functions p. 3 (1 IN, 2 GND, 3 EN, 4 N/C, 5 OUT). KiCad only has the X2SON symbol. |
| XC6206 | local | official `SOT-23` | Torex pin 1 VSS, pin 2 VIN, pin 3 VOUT. KiCad `XC6206PxxxMR` extends a symbol whose pin numbers are swapped. |
| TMP117 | local | official `WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm` | SNOSD82D Table 5-1 p. 4. Pin 7 added so the exposed pad is grounded. The stock symbol's footprint field points at SOT-563. |
"""


if __name__ == "__main__":
    main()
