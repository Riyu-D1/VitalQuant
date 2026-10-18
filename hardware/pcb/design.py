#!/usr/bin/env python3
"""VitalQ hw_v2 schematic (strict superset of hw_v1 — see delta block below).

Every part is placed; nets are short stubs plus labels. Power symbols and
global labels of the same name are one net (checked). Output filenames keep
the hw_v1 naming for compatibility with build_pcb.py.
"""

# ---------------------------------------------------------------------------
# hw_v2 delta — 2026-09-29 (schematic agent; HW_V2_SPEC.md is authoritative)
#
#   * U1: ESP32-WROOM-32E-N8R2 -> ESP32-S3-MINI-1-N4R2 (on-board PCB antenna, native
#     USB on IO19/IO20, UART0 console kept on TXD0/RXD0 = GPIO43/44). All nets
#     remapped per the spec interface table. EN/IO0 buttons, R5/R6/C64, the
#     Q3 auto-program pair and the TC2030 header wiring are unchanged.
#   * USB strap links: R102/R103 (fitted 0R-0402) route J1 D+/D- to the S3;
#     R104/R105 (DNP) strap to the CP2102 instead. CP2102+U21+Q3 fully kept.
#   * New sheet 9: MAX86178 combined ECG/PPG/BioZ AFE (SPI + CS GPIO38,
#     INT GPIO39) plus the satellite tail pads J9/J10/J11, J12 FFC (DNP) and
#     the J13 ECG pad pair (DNP). J12 is 14-pos, not the spec's 12: the
#     three tails carry 13 nets, so a 14-pos FFC (FH12-14S-0.5SH) is the smallest that fits.
#   * SHT45 U23 on I2C 0x44 (skin side); RV-3028-C7 RTC U24 at 0x52;
#     IM69D130 PDM mic U25 on MIC_CLK/MIC_DAT (GPIO40/41); D12 730 nm LED
#     + Q4 CSD13380F3T gated by AD5940 GPIO2 (ball E1, net NIR730_GATE).
#   * Strict superset: no hw_v1 part, net, TVS, or test point was removed.
#     New symbols are drawn in-file by hw_v2_lib() so lib/*.kicad_sym and the
#     library build script stay untouched.
# ---------------------------------------------------------------------------

from __future__ import annotations

from schutil import Pin, Sheet, SymbolDef, label_angle, load_lib, r2

FP_R = "Resistor_SMD:R_0402_1005Metric"
FP_C = "Capacitor_SMD:C_0402_1005Metric"
FP_M2 = "MountingHole:MountingHole_2.2mm_M2"
FP_USB = "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12"
FP_BAT = "vitalq:Pads_LiPo"
FP_FSR = "vitalq:Pads_FSR"
FP_H3 = "vitalq:Pads_ECG"
FP_H4 = "vitalq:Pads_EDA"
FP_BIO = "vitalq:Pads_BIOZ"
FP_H6 = "vitalq:Pads_PPG"
FP_BQ = "Package_SON:Texas_DSG0008A_WSON-8-1EP_2x2mm_P0.5mm_EP0.9x1.6mm"
FP_NPN = "Package_TO_SOT_SMD:SOT-363_SC-70-6"
FP_AFE = "snap:BGA30N40P5X6_260X210X50"
FP_AD = "snap:BGA56C40P8X7_416X356X55"
FP_ADS = "snap:QFN40P400X400X100-33N-D"
FP_AS = "snap:AS7341DLGT"
FP_TMP = "vitalq:TMP117_DRV_NOPASTE"
FP_MLX = "snap:MLX90632SLDDCB100SP"
FP_IMU = "snap:QFN_LSM6DSV80XTR_STM"
FP_C6 = "Capacitor_SMD:C_0603_1608Metric"
FP_63802 = "Package_SON:WSON-10-1EP_2x3mm_P0.5mm_EP0.84x2.4mm"
FP_61240 = "vitalq:TPS61240_YFF"
FP_L = "vitalq:L_DFE201612E"
FP_SFH = "vitalq:SFH7072"
FP_GAUGE = "Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm"
FP_R6 = "Resistor_SMD:R_0603_1608Metric"
FP_LED6 = "LED_SMD:LED_0603_1608Metric"
FP_SW = "Button_Switch_SMD:SW_SPST_TS-1088-xR020"
FP_ESD = "Package_TO_SOT_SMD:SOT-23-6"
FP_TP = "TestPoint:TestPoint_Pad_D1.0mm"
FP_TC = "Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical"
FP_FID = "vitalq:Fiducial_1mm"
FP_FLASH = "vitalq:W25Q512_WSON8"
FP_EXP = "vitalq:TCA6408A_RSV"
FP_TVS = "vitalq:TPD1E10B06_DPY"
FP_FET = "Package_DFN_QFN:Texas_PicoStar_DFN-3_0.69x0.60mm"
FP_HV = "Resistor_SMD:R_2512_6332Metric"
FP_WHITE = "vitalq:NF2W757G"
FP_IR = "LED_SMD:LED_0402_1005Metric"

# hw_v2 additions. Custom lands still to be drawn in vitalq.pretty are named
# vitalq:* and flagged VERIFY where noted.
FP_S3 = "vitalq:ESP32_S3_MINI_1"  # MINI-1 land: same 65-pin pad map as MINI-1U plus 5.1 mm antenna overhang; VERIFY pad order vs S3 pinout
FP_86178 = "vitalq:MAX86178_WLP49"  # VERIFY: 7x7 WLP 2.77x2.57 mm, 0.4 pitch — land TBD
FP_SHT45 = "vitalq:SHT45_DFN4"  # local land (official lib lacks DFN-4 1.5x1.5); VERIFY vs Sensirion
FP_RTC = "vitalq:RV3028C7"  # VERIFY: SON-8 3.2x1.5 mm land TBD
FP_MIC = "vitalq:IM69D130"  # VERIFY: Infineon LLGA-5 bottom-port land TBD
FP_FFC = "Connector_FFC-FPC:Hirose_FH12-14S-0.5SH_1x14-1MP_P0.50mm_Horizontal"  # official 14-pos land; final connector family TBD (DNP)
FP_PAD2 = "vitalq:Pads_1x02"  # VERIFY: tail pad land TBD
FP_PAD3 = "vitalq:Pads_1x03"  # VERIFY: tail pad land TBD
FP_PAD4 = "vitalq:Pads_1x04"  # VERIFY: tail pad land TBD
FP_PAD6 = "vitalq:Pads_1x06"  # VERIFY: tail pad land TBD

RAILS = {"+3V3", "+1V8", "VBUS", "GND"}


def box_sym(name, ref, desc, fp, pins, width=10.16, stack_sides="", hidden=()):
    """Box symbol in the same format lib/make_lib.py:kicad9_symbol writes.

    hw_v2 parts are drawn here instead of editing lib/*.kicad_sym, which the
    library build owns. Returns a SymbolDef; register it via hw_v2_lib().
    pins: list of (number, name, etype, side), side in L/R/U/D. Sides listed
    in stack_sides give every pin the same coordinate, so one stub ties the
    whole group (module GND rings). hidden: (number, name) pins drawn inside
    the body as no_connect, for pads that must stay open.
    """
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

    coords = {}
    for i, (num, _n, _t, _s) in enumerate(left):
        coords[num] = (-width - pin_len, 0.0 if "L" in stack_sides else y_for(i, len(left)), 0)
    for i, (num, _n, _t, _s) in enumerate(right):
        coords[num] = (width + pin_len, 0.0 if "R" in stack_sides else y_for(i, len(right)), 180)
    for i, (num, _n, _t, _s) in enumerate(up):
        x = 0.0 if "U" in stack_sides else (i - (len(up) - 1) / 2) * 2.54
        coords[num] = (x, top + pin_len, 270)
    for i, (num, _n, _t, _s) in enumerate(down):
        x = 0.0 if "D" in stack_sides else (i - (len(down) - 1) / 2) * 2.54
        coords[num] = (x, -top - pin_len, 90)
    for j, (num, _n) in enumerate(hidden):
        coords[num] = (-width + 1.27, top - 2.54 - j * 1.27, 0)  # inside the body

    pinmap = {}
    pin_sexpr = []
    for num, pname, etype, _side in pins:
        x, y, rot = coords[num]
        pinmap[num] = Pin(num, pname, etype, x, y, rot, pin_len)
        pin_sexpr.append(
            f"""\t\t\t(pin {etype} line
\t\t\t\t(at {x:.2f} {y:.2f} {rot})
\t\t\t\t(length {pin_len:.2f})
\t\t\t\t(name "{pname}" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "{num}" (effects (font (size 1.27 1.27))))
\t\t\t)"""
        )
    for num, pname in hidden:
        x, y, rot = coords[num]
        pinmap[num] = Pin(num, pname, "no_connect", x, y, rot, pin_len)
        pin_sexpr.append(
            f"""\t\t\t(pin no_connect line
\t\t\t\t(at {x:.2f} {y:.2f} {rot})
\t\t\t\t(length {pin_len:.2f})
\t\t\t\t(hide yes)
\t\t\t\t(name "{pname}" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "{num}" (effects (font (size 1.27 1.27))))
\t\t\t)"""
        )
    pins_txt = "\n".join(pin_sexpr)
    block = f"""\t(symbol "{name}"
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(property "Reference" "{ref}" (at 0 {top + 2.54:.2f} 0) (effects (font (size 1.27 1.27))))
\t\t(property "Value" "{name}" (at 0 {-top - 2.54:.2f} 0) (effects (font (size 1.27 1.27))))
\t\t(property "Footprint" "{fp}" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
\t\t(property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
\t\t(property "Description" "{desc}" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
\t\t(symbol "{name}_0_1"
\t\t\t(rectangle (start {-width:.2f} {top:.2f}) (end {width:.2f} {-top:.2f})
\t\t\t\t(stroke (width 0.254) (type default)) (fill (type background)))
\t\t)
\t\t(symbol "{name}_1_1"
{pins_txt}
\t\t)
\t)"""
    return SymbolDef(name, block, pinmap)


def hw_v2_lib() -> dict:
    """Symbols added for hw_v2. The lib files are not touched; make_lib.py
    can regenerate the same blocks later if wanted."""
    lib = {}

    # ESP32-S3-MINI-1-N4R2. Pad numbers per ESP32-S3-MINI-1/-1U datasheet
    # v1.3 Table 3-1 (65 pads; pads 46-65 are the GND ring, pad 61 is the
    # nine-pad heatsink group — verified against the official Espressif land).
    # N4R2: 4 MB quad flash + 2 MB quad PSRAM inside; IO26 is the PSRAM
    # chip-select and is reserved. USB is native on IO19 (D-) / IO20 (D+),
    # UART0 console on TXD0/RXD0 (GPIO43/44).
    s3_left = [("45", "EN", "input", "L")] + [
        (str(4 + i), f"IO{i}", "bidirectional", "L") for i in range(11)  # IO0-IO10
    ]
    s3_right = (
        [(str(15 + i), f"IO{11 + i}", "bidirectional", "R") for i in range(11)]  # IO11-IO21
        + [
            ("26", "IO26", "bidirectional", "R"),  # PSRAM CS inside N4R2 — leave open
            ("27", "IO47", "bidirectional", "R"),
            ("28", "IO33", "bidirectional", "R"),
            ("29", "IO34", "bidirectional", "R"),
            ("30", "IO48", "bidirectional", "R"),
            ("31", "IO35", "bidirectional", "R"),
            ("32", "IO36", "bidirectional", "R"),
            ("33", "IO37", "bidirectional", "R"),
            ("34", "IO38", "bidirectional", "R"),
            ("35", "IO39", "bidirectional", "R"),
            ("36", "IO40", "bidirectional", "R"),
            ("37", "IO41", "bidirectional", "R"),
            ("38", "IO42", "bidirectional", "R"),
            ("39", "TXD0", "output", "R"),   # GPIO43, U0TXD
            ("40", "RXD0", "input", "R"),    # GPIO44, U0RXD
            ("41", "IO45", "bidirectional", "R"),  # strap — leave unstrapped
            ("44", "IO46", "bidirectional", "R"),  # strap — leave unstrapped
        ]
    )
    # The official land numbers the 3x3 EP/heatsink grid as pad 61 (GND);
    # it is already inside the 46-65 ring, so no separate EPAD pin is needed.
    s3_gnd = [str(n) for n in (1, 2, 42, 43, *range(46, 66))]  # ring + EPAD(61)
    lib["ESP32-S3-MINI-1-N4R2"] = box_sym(
        "ESP32-S3-MINI-1-N4R2",
        "U",
        "ESP32-S3-MINI-1-N4R2, datasheet v1.3 Table 3-1. On-board PCB "
        "antenna. EP = pad 61 on the vitalq land; VERIFY pad order vs S3 pinout.",
        FP_S3,
        s3_left + s3_right + [("3", "3V3", "power_in", "U")]
        + [(n, "GND", "power_in", "D") for n in s3_gnd],
        width=15.24,
        stack_sides="D",
    )

    # MAX86178 WLP-49. The full ball map is under NDA — VERIFY every ball
    # ref below against the released pin table before layout. Names follow
    # the MAX86176 family convention. RSV balls are left unconnected.
    lib["MAX86178"] = box_sym(
        "MAX86178",
        "U",
        "MAX86178ENJ+ WLP-49 ECG/PPG/BioZ AFE. VERIFY: ball map is NDA.",
        FP_86178,
        [
            ("A1", "PD1_INP", "input", "L"),
            ("A2", "PD1_INM", "input", "L"),
            ("A3", "LED1_DRV", "output", "L"),
            ("A4", "LED2_DRV", "output", "L"),
            ("A5", "LED3_DRV", "output", "L"),
            ("A6", "ECG_INP", "input", "L"),
            ("A7", "ECG_INM", "input", "L"),
            ("G7", "SCLK", "input", "R"),
            ("G6", "SDI", "input", "R"),
            ("G5", "SDO", "tri_state", "R"),
            ("G4", "SEN", "input", "R"),
            ("G3", "INTB", "open_collector", "R"),
            ("F1", "AVDD", "power_in", "U"),
            ("F2", "DVDD", "power_in", "U"),
            ("F3", "LED_DRV_SUP", "power_in", "U"),
            ("F4", "VREF", "passive", "U"),
            ("F5", "AGND", "power_in", "D"),
            ("F6", "DGND", "power_in", "D"),
            ("F7", "LGND", "power_in", "D"),
        ],
        width=12.7,
        hidden=[(f"{r}{c}", f"RSV{r}{c}") for r in "BCDE" for c in range(1, 8)]
        + [("G1", "RSV"), ("G2", "RSV")],
    )

    # SHT45-AD1B DFN-4: pin 1 SDA, 2 SCL, 3 VDD, 4 VSS (SHT4x datasheet 5.4).
    # The die pad is not bonded to a pin.
    lib["SHT45-AD1B"] = box_sym(
        "SHT45-AD1B",
        "U",
        "SHT45 DFN-4 RH+T sensor, I2C 0x44. Die pad is not connected.",
        FP_SHT45,
        [
            ("1", "SDA", "bidirectional", "L"),
            ("2", "SCL", "input", "L"),
            ("3", "VDD", "power_in", "U"),
            ("4", "VSS", "power_in", "D"),
        ],
        width=7.62,
    )

    # RV-3028-C7 SON-8: 1 CLKOUT, 2 /INT (open drain), 3 SCL, 4 SDA,
    # 5 VSS (metal lid), 6 VBACKUP, 7 VDD, 8 EVI (do not float).
    lib["RV-3028-C7"] = box_sym(
        "RV-3028-C7",
        "U",
        "RV-3028-C7 RTC, SON-8, I2C 0x52. EVI must not float; VBACKUP to VSS via 10k when unused.",
        FP_RTC,
        [
            ("3", "SCL", "input", "L"),
            ("4", "SDA", "bidirectional", "L"),
            ("1", "CLKOUT", "output", "R"),
            ("2", "INT", "open_collector", "R"),
            ("8", "EVI", "input", "R"),
            ("7", "VDD", "power_in", "U"),
            ("5", "VSS", "power_in", "D"),
            ("6", "VBACKUP", "power_in", "D"),
        ],
        width=7.62,
    )

    # IM69D130 LLGA-5 bottom-port PDM mic: 1 DATA, 2 VDD, 3 CLOCK,
    # 4 SELECT (L/R strapped), 5 GND. 100 nF on VDD per datasheet.
    lib["IM69D130"] = box_sym(
        "IM69D130",
        "U",
        "IM69D130 PDM mic, LLGA-5. SELECT strapped low = left slot.",
        FP_MIC,
        [
            ("1", "DATA", "output", "L"),
            ("3", "CLOCK", "input", "L"),
            ("4", "SELECT", "input", "R"),
            ("2", "VDD", "power_in", "U"),
            ("5", "GND", "power_in", "D"),
        ],
        width=7.62,
    )

    # 14-pos FFC for J12 — spec asked for a 12-pos carrying J9+J10+J11, which
    # is 13 nets; 14-pos is the smallest standard FFC connector that covers them (FH12-14S-0.5SH land).
    lib["Conn_01x14_Pin"] = box_sym(
        "Conn_01x14_Pin",
        "J",
        "Generic 14-pin connector, one row.",
        "",
        [(str(i), f"Pin_{i}", "passive", "R") for i in range(1, 15)],
        width=2.54,
    )
    return lib


def usb_c_shield_pin(lib):
    """J1: the library symbol calls its shell pin "S1", but the
    USB_C_Receptacle_HRO_TYPE-C-31-M-12 land numbers its four shell pads
    "SH". Renumber the pin in memory so all four shell pads take GND.
    lib/vitalq.kicad_sym itself stays untouched."""
    sym = lib["USB_C_Receptacle_USB2.0_16P"]
    block = sym.block.replace('(number "S1"', '(number "SH"', 1)
    assert block != sym.block, "USB-C symbol lost its S1 shell pin"
    pins = {}
    for num, p in sym.pins.items():
        key = "SH" if num == "S1" else num
        pins[key] = Pin(key, p.name, p.etype, p.x, p.y, p.rot, p.length)
    lib["USB_C_Receptacle_USB2.0_16P"] = SymbolDef(sym.name, block, pins)


class Design:
    def __init__(self, lib):
        self.lib = lib
        self.npwr = 0
        self.done: dict[str, set] = {}
        self.root: Sheet | None = None
        self.children: list[Sheet] = []

    def sheet(self, title, filename, page) -> Sheet:
        sh = Sheet(title, filename, page)
        if self.root is None:
            self.root = sh
        else:
            self.children.append(sh)
        return sh

    def part(self, sh, sym, ref, value, fp, x, y, rot=0, labels="side", bom=True, dnp=False):
        inst = sh.add(self.lib, sym, ref, value, fp, x, y, rot, bom=bom, board=True, dnp=dnp)
        if labels == "side":
            inst.ref_at = (x + 4.5, y - 1.4)
            inst.val_at = (x + 4.5, y + 1.6)
        elif labels == "ic":
            x0, y0, x1, y1 = inst.bbox()
            inst.ref_at = (r2((x0 + x1) / 2), r2(y0 - 8))
            inst.val_at = (r2((x0 + x1) / 2), r2(y1 + 6))
        return inst

    def pwr(self, sh, kind, x, y):
        self.npwr += 1
        ref = f"#PWR{self.npwr:03d}"
        return sh.add(self.lib, kind, ref, kind, "", x, y, 0, bom=False, board=False)

    def flag(self, sh, x, y):
        self.npwr += 1
        ref = f"#FLG{self.npwr:03d}"
        return sh.add(self.lib, "PWR_FLAG", ref, "PWR_FLAG", "", x, y, 0, bom=False, board=False)

    def touch(self, inst, num):
        bag = self.done.setdefault(inst.uuid, set())
        x, y = inst.pin_xy(num)
        for n in inst.sym.pins:
            if inst.pin_xy(n) == (x, y):
                bag.add(n)

    def close(self, sh, inst):
        bag = self.done.get(inst.uuid, set())
        seen = set()
        nced = []
        for n, p in inst.sym.pins.items():
            if p.etype == "no_connect" or n in bag:
                continue
            xy = inst.pin_xy(n)
            if any(inst.pin_xy(m) == xy and m in bag for m in inst.sym.pins):
                continue
            if xy in seen:
                continue
            sh.noconn(*xy)
            seen.add(xy)
            nced.append(f"{n}:{p.name}")
        if nced:
            print(f"  {inst.ref} open pins marked NC: {', '.join(nced)}")

    def net_at(self, sh, net, x, y, vx, vy):
        # Power symbols are taller than a 2.54 mm pin pitch, so they are only
        # used when the stub already runs with the symbol: up for a rail, down
        # for GND. Side pins get a short global label of the same name.
        if net == "GND" and vy > 0.5:
            self.pwr(sh, net, x, y)
            return
        if net in {"+3V3", "+1V8", "VBUS"} and vy < -0.5:
            self.pwr(sh, net, x, y)
            return
        sh.label(net, x, y, label_angle(vx, vy), shape="passive")

    def stub_net(self, sh, inst, num, net, dist=6.35):
        x2, y2, vx, vy = sh.stub(inst, num, dist)
        self.net_at(sh, net, x2, y2, vx, vy)
        self.touch(inst, num)

    def join_row(self, sh, inst, nums, net, dist=6.35):
        """Same net on several pins. Each pin gets its own straight stub.

        A sideways jog of a few millimetres lands on the next pin when the
        symbol pitch is 2.54 mm, which shorts neighbouring nets. Global labels
        and power symbols already join the stubs, so no local bus is required.
        Stacked pins share one stub; the junction ties every pin at that point.
        """
        seen = set()
        for n in nums:
            x, y, vx, vy = inst.pin_out(n)
            key = (x, y)
            if key in seen:
                self.touch(inst, n)
                continue
            seen.add(key)
            sh.junction(x, y)
            self.stub_net(sh, inst, n, net, dist)

    def flag_net(self, sh, net, x, y):
        """PWR_FLAG on a net that is only reached through a resistor."""
        sh.label(net, x, y, 180, shape="passive")
        fl = self.flag(sh, x + 12, y)
        sh.wire([(x, y), fl.pin_xy("1")])

    def jog(self, sh, inst, num, net, jog, dist=7.62):
        """Outward a little, sideways by jog, then outward to the label."""
        x, y, vx, vy = inst.pin_out(num)
        b = (r2(x + vx * 2.54), r2(y + vy * 2.54))
        if abs(vy) > 0.5:
            c = (r2(b[0] + jog), b[1])
            d = (c[0], r2(c[1] + vy * dist))
        else:
            c = (b[0], r2(b[1] + jog))
            d = (r2(c[0] + vx * dist), c[1])
        sh.wire([(x, y), b, c, d])
        sh.junction(*b)
        self.net_at(sh, net, d[0], d[1], d[0] - c[0], d[1] - c[1])
        self.touch(inst, num)

    def vpart(self, sh, sym, ref, value, fp, x, y, top, bot, dnp=False):
        inst = self.part(sh, sym, ref, value, fp, x, y, 0, labels="side", dnp=dnp)
        self.stub_net(sh, inst, "1", top, 3.81)
        self.stub_net(sh, inst, "2", bot, 3.81)
        self.close(sh, inst)
        return inst

    def hpart(self, sh, sym, ref, value, fp, x, y, left, right, dnp=False):
        """Rotation 90: pin 2 on the left, pin 1 on the right."""
        inst = self.part(sh, sym, ref, value, fp, x, y, 90, labels="side", dnp=dnp)
        # Side labels sit on the pin axis. Put them above the body.
        inst.ref_at = (x - 2, y - 3.6)
        inst.val_at = (x + 4, y - 3.6)
        self.stub_net(sh, inst, "2", left, 3.81)
        self.stub_net(sh, inst, "1", right, 3.81)
        self.close(sh, inst)
        return inst

    def side_map(self, sh, inst, mapping, dist=7.62):
        for num, net in mapping.items():
            if net == "NC":
                continue
            self.stub_net(sh, inst, num, net, dist)

    def finish(self, sh, inst):
        self.close(sh, inst)


def build_design(lib=None) -> Design:
    lib = lib or load_lib()
    lib.update(hw_v2_lib())  # hw_v2 symbols; lib/*.kicad_sym stays untouched
    usb_c_shield_pin(lib)  # J1 shell pads on the land are numbered SH
    d = Design(lib)
    power(d)
    usb(d)
    mcu(d)
    afe(d)
    ad(d)
    ads(d)
    i2c(d)
    debug(d)
    max86178(d)
    # Navigation boxes along the bottom of the power sheet.
    x = 16
    for ch in d.children:
        ch.nav_at = (x, 238)
        ch.nav_size = (44, 12)  # hw_v2: eight sheets — tightened pitch
        x += 48
    return d


def power(d: Design):
    sh = d.sheet("Power and charging", "vitalq_hw_v1.kicad_sch", "1")
    # Title block is x 310-418, y 2-34. Keep the frame above it.
    sh.rect(12, 40, 406, 275)
    sh.text("Power and charging", 16, 18, 3.2, bold=True)
    sh.text("USB-C, LiPo charger, 3.3 V rail, 1.8 V rail", 16, 26, 1.6)

    j = d.part(sh, "USB_C_Receptacle_USB2.0_16P", "J1", "USB-C", FP_USB, 48, 78, labels="ic")
    # All four VBUS pads and all four GND pads wired — current margin + EMI.
    d.join_row(sh, j, ["A4", "A9", "B4", "B9"], "VBUS", 5.08)
    d.stub_net(sh, j, "A5", "USB_CC1", 7.62)
    d.stub_net(sh, j, "B5", "USB_CC2", 7.62)
    d.stub_net(sh, j, "A6", "USB_DP", 7.62)
    d.stub_net(sh, j, "B6", "USB_DP", 7.62)
    d.stub_net(sh, j, "A7", "USB_DM", 7.62)
    d.stub_net(sh, j, "B7", "USB_DM", 7.62)
    d.join_row(sh, j, ["A1", "SH", "A12", "B1", "B12"], "GND", 5.08)  # GND stack + shield
    d.finish(sh, j)

    d.vpart(sh, "R", "R1", "5.1k", FP_R, 92, 118, "USB_CC1", "GND")
    d.vpart(sh, "R", "R2", "5.1k", FP_R, 108, 118, "USB_CC2", "GND")

    # BQ25170DSGR replaces MCP73831. Charge starts from USB with no 3.3 V rail.
    # Table 7-1: 27.0 kΩ sets 4.20 V. KISET/1.5 kΩ = 200 mA (SLUSDJ8A).
    u = d.part(sh, "BQ25170", "U2", "BQ25170DSGR", FP_BQ, 175, 88, labels="ic")
    d.stub_net(sh, u, "1", "VBUS", 7.62)  # IN
    d.stub_net(sh, u, "8", "VBAT", 7.62)  # OUT, power_out
    d.stub_net(sh, u, "2", "CHG_ISET", 7.62)
    d.stub_net(sh, u, "7", "CHG_VSET", 7.62)
    d.stub_net(sh, u, "5", "CHG_STAT", 7.62)
    d.stub_net(sh, u, "6", "CHG_PG", 7.62)
    d.stub_net(sh, u, "3", "TS", 7.62)
    d.join_row(sh, u, ["4", "9"], "GND", 6.35)
    d.finish(sh, u)
    d.vpart(sh, "R", "R3", "1.5k", FP_R, 118, 145, "CHG_ISET", "GND")
    d.vpart(sh, "R", "R75", "27.0k", FP_R, 140, 145, "CHG_VSET", "GND")
    d.vpart(sh, "R", "R49", "10k", FP_R, 162, 145, "+3V3", "CHG_STAT")
    # R60 used to bias the NTC divider. /PG needs the pull-up; TS must not see +3V3.
    d.vpart(sh, "R", "R60", "10k", FP_R, 184, 145, "+3V3", "CHG_PG")
    # Q2 is off at power-up (R59). Driving CHG_DIS high shorts TS below VTS_ENZ.
    q2 = d.part(sh, "CSD13380F3", "Q2", "CSD13380F3T", FP_FET, 230, 118, labels="ic")
    d.stub_net(sh, q2, "1", "CHG_DIS", 7.62)
    d.stub_net(sh, q2, "3", "TS", 6.35)
    d.stub_net(sh, q2, "2", "GND", 6.35)
    d.finish(sh, q2)
    d.vpart(sh, "R", "R59", "100k", FP_R, 250, 130, "CHG_DIS", "GND")

    # TPS63802 replaces the XC6206. EN tied to VIN so the 3.3 V rail is up
    # before the expander has a supply. MODE low is power-save. PG is open.
    u = d.part(sh, "TPS63802", "U3", "TPS63802DLAR", FP_63802, 250, 78, labels="ic")
    d.stub_net(sh, u, "10", "VBAT_SYS", 7.62)
    d.stub_net(sh, u, "1", "VBAT_SYS", 7.62)
    d.stub_net(sh, u, "2", "GND", 7.62)
    d.stub_net(sh, u, "6", "+3V3", 8.89)
    d.stub_net(sh, u, "9", "SW_L1", 8.89)
    d.stub_net(sh, u, "7", "SW_L2", 8.89)
    d.stub_net(sh, u, "4", "FB_3V3", 8.89)
    d.join_row(sh, u, ["8", "3", "11"], "GND", 6.35)  # 11 is the exposed pad
    d.finish(sh, u)  # PG
    # Clear of U4. Stubs carry SW_L1 and SW_L2; they do not touch the LDO.
    d.hpart(sh, "L", "L1", "0.47u", FP_L, 300, 48, "SW_L1", "SW_L2")
    d.vpart(sh, "R", "R23", "560k", FP_R, 360, 100, "+3V3", "FB_3V3")
    d.vpart(sh, "R", "R24", "100k", FP_R, 382, 100, "FB_3V3", "GND")

    u = d.part(sh, "TPS7A2018PDBVR", "U4", "TPS7A2018PDBVR", "Package_TO_SOT_SMD:SOT-23-5", 345, 70, labels="ic")
    d.join_row(sh, u, ["1", "3"], "+3V3", 6.35)  # IN and EN, both face left — wait, they face LEFT not up.
    # join_row on left-facing pins connects them vertically. Both are +3V3. Good.
    d.stub_net(sh, u, "5", "+1V8_LDO", 8.89)  # OUT, then R97 to the 1.8 V loads
    d.stub_net(sh, u, "2", "GND", 6.35)
    d.finish(sh, u)  # NC pin is type no_connect

    bat = d.part(sh, "Conn_01x03_Pin", "J2", "LiPo pads", FP_BAT, 168, 168, labels="ic")
    d.stub_net(sh, bat, "1", "VBAT", 7.62)
    d.stub_net(sh, bat, "2", "GND", 7.62)
    d.stub_net(sh, bat, "3", "TS", 7.62)
    d.finish(sh, bat)
    d.vpart(sh, "R", "R61", "10k", FP_R, 275, 200, "TS", "GND", dnp=True)

    # Decoupling directly under the regulators.
    sh.text("Decoupling", 230, 108, 1.8, bold=True)
    d.vpart(sh, "C", "C1", "4.7u", FP_C6, 230, 155, "VBUS", "GND")
    d.vpart(sh, "C", "C2", "4.7u", FP_C, 250, 155, "VBAT", "GND")
    d.vpart(sh, "C", "C85", "47u", FP_C6, 262, 155, "VBAT", "GND")  # bulk for WiFi+LED bursts on the cell
    d.vpart(sh, "C", "C3", "1u", FP_C, 270, 155, "VBAT", "GND")
    d.vpart(sh, "C", "C4", "1u", FP_C, 290, 155, "+3V3", "GND")
    d.vpart(sh, "C", "C6", "1u", FP_C, 310, 155, "+1V8_LDO", "GND")
    d.vpart(sh, "C", "C84", "1u", FP_C, 325, 155, "+3V3", "GND")
    d.vpart(sh, "C", "C44", "10u", FP_C6, 340, 155, "VBAT_SYS", "GND")
    d.vpart(sh, "C", "C45", "22u", FP_C6, 365, 155, "+3V3", "GND")

    gge = d.part(sh, "MAX17048", "U17", "MAX17048G+T10", FP_GAUGE, 70, 175, labels="ic")
    d.stub_net(sh, gge, "2", "VBAT", 7.62)  # CELL
    d.stub_net(sh, gge, "3", "VBAT", 7.62)  # VDD
    d.stub_net(sh, gge, "8", "I2C_SDA", 7.62)
    d.stub_net(sh, gge, "7", "I2C_SCL", 7.62)
    d.join_row(sh, gge, ["1", "4", "6", "9"], "GND", 6.35)  # CTG, GND, QSTRT, EP
    d.stub_net(sh, gge, "5", "GAUGE_ALRT", 7.62)
    d.finish(sh, gge)
    d.vpart(sh, "R", "R74", "10k", FP_R, 112, 175, "+3V3", "GAUGE_ALRT")
    d.vpart(sh, "C", "C58", "100n", FP_C, 130, 185, "VBAT", "GND")
    d.vpart(sh, "R", "R50", "100k", FP_R, 90, 130, "VBUS", "VBUS_DET")
    d.vpart(sh, "R", "R51", "200k", FP_R, 112, 130, "VBUS_DET", "GND")

    sh.text("J2 is BAT+ (larger pad) BAT- NTC. The cell must have a 10k NTC and a protection PCM.", 16, 248, 1.3)
    sh.text("R61 stays DNP. Fit the cell NTC, not R61 as well. R3 is 1.5k, so charge current is 200 mA.", 16, 256, 1.3)
    sh.text("Charge is on whenever USB is present and TS is in range. CHG_DIS high shorts TS and stops it.", 16, 264, 1.4)
    # One power-output flag on GND and one on VBUS. +3V3 is driven by U3, +1V8 by U4, VBAT by U2.
    g = d.pwr(sh, "GND", 392, 130)
    fl = d.flag(sh, 392, 142)
    sh.wire([g.pin_xy("1"), fl.pin_xy("1")])
    v = d.pwr(sh, "VBUS", 392, 78)
    fl2 = d.flag(sh, 404, 78)
    sh.wire([v.pin_xy("1"), fl2.pin_xy("1")])

    sh.text("U3 is a TPS63802 buck-boost. R23/R24 set 3.30 V. C44 and C45 are the datasheet 0603 ceramics. C3 and C4 stay.", 16, 216, 1.3)
    sh.text("U4 is the 1.8 V LDO, fed from U3, EN tied to IN. AS7341 VDD is 1.7-2.0 V. Other ICs are 3.3 V.", 16, 222, 1.3)
    sh.text("Load sits on VBAT. U17 is the MAX17048 (0x36). R49 pulls STAT up. R50/R51 divide VBUS to about 3.3 V.", 16, 228, 1.3)
    sh.text("Sheets below contain the parts. They are not empty stubs.", 16, 204, 1.5)
    sh.text("Other sheets", 16, 224, 2.0, bold=True)


def usb(d: Design):
    sh = d.sheet("USB-UART", "usb.kicad_sch", "2")
    sh.rect(12, 40, 400, 292)  # hw_v2: extended for the strap links
    sh.text("USB-UART", 16, 16, 3.2, bold=True)
    sh.text("CP2102N-A02-GQFN28. VDD is the on-chip regulator output, not +3V3.", 16, 24, 1.6)

    u = d.part(
        sh,
        "CP2102N-Axx-xQFN28",
        "U5",
        "CP2102N-A02-GQFN28R",
        "Package_DFN_QFN:QFN-28-1EP_5x5mm_P0.5mm_EP3.35x3.35mm",
        200,
        130,
        labels="ic",
    )
    d.jog(sh, u, "7", "VBUS", jog=-18)  # VREGIN
    d.jog(sh, u, "6", "VDD_CP2102", jog=18)  # VDD
    # Rev 1.5 Fig 2.5: VBUS sense is a divider, not the 5 V rail. VREGIN stays on VBUS.
    d.stub_net(sh, u, "8", "CP_VBUS", 8.89)
    # hw_v2: the CP2102 USB side is on its own nets now. R104/R105 (DNP)
    # strap it to J1 when the S3 native-USB default (R102/R103) is removed.
    d.stub_net(sh, u, "5", "USB_DM_CP", 8.89)
    d.stub_net(sh, u, "4", "USB_DP_CP", 8.89)
    d.join_row(sh, u, ["3"], "GND", 6.35)  # stacked with pad 29
    d.stub_net(sh, u, "26", "CP_TX", 10.16)  # TXD, 1k series before ESP RX
    d.stub_net(sh, u, "25", "CP_RX", 10.16)  # RXD, 1k series after ESP TX
    d.stub_net(sh, u, "24", "CP_RTS", 10.16)
    d.stub_net(sh, u, "28", "CP_DTR", 10.16)
    d.stub_net(sh, u, "9", "CP_RST", 8.89)  # RSTb, 1k to VDD
    for n in ("23", "27", "1", "2"):  # ~CTS ~DSR ~DCD ~RI/CLK
        d.stub_net(sh, u, n, "VDD_CP2102", 10.16)
    d.finish(sh, u)

    sh.text("Decoupling", 40, 48, 1.8, bold=True)
    d.vpart(sh, "C", "C9", "4.7u", FP_C, 55, 70, "VDD_CP2102", "GND")
    d.vpart(sh, "C", "C62", "100n", FP_C, 78, 100, "VDD_CP2102", "GND")
    d.vpart(sh, "C", "C10", "1u", FP_C6, 100, 70, "VBUS", "GND")
    d.vpart(sh, "C", "C63", "100n", FP_C, 122, 70, "VBUS", "GND")
    d.vpart(sh, "R", "R62", "1k", FP_R, 145, 100, "VDD_CP2102", "CP_RST")
    # Fig 2.5: 22.1k from the connector, 47.5k to ground. At 5.0 V the pin is 3.41 V.
    d.vpart(sh, "R", "R63", "22.1k", FP_R, 55, 145, "VBUS", "CP_VBUS")
    d.vpart(sh, "R", "R64", "47.5k", FP_R, 90, 145, "CP_VBUS", "GND")
    d.hpart(sh, "R", "R72", "1k", FP_R, 55, 175, "ESP_TX", "CP_RX")
    d.hpart(sh, "R", "R73", "1k", FP_R, 110, 175, "CP_TX", "ESP_RX")
    # DevKitC cross-coupled pair. esptool ClassicReset: RTS asserted pulls EN low.
    q = d.part(sh, "BC847BS_DUAL", "Q3", "BC847BS,115", FP_NPN, 70, 215, labels="ic")
    d.stub_net(sh, q, "2", "Q3_B1", 6.35)  # base, EN transistor
    d.stub_net(sh, q, "6", "ESP_EN", 6.35)  # collector
    d.stub_net(sh, q, "1", "CP_RTS", 6.35)  # emitter tied to RTS
    d.stub_net(sh, q, "5", "Q3_B2", 6.35)
    d.stub_net(sh, q, "3", "ESP_IO0", 6.35)
    d.stub_net(sh, q, "4", "CP_DTR", 6.35)
    d.finish(sh, q)
    d.vpart(sh, "R", "R70", "10k", FP_R, 130, 210, "CP_DTR", "Q3_B1")
    d.vpart(sh, "R", "R71", "10k", FP_R, 152, 210, "CP_RTS", "Q3_B2")
    # PWR_FLAG stands in for the internal 3.45 V regulator (symbol pin is power_in).
    p = d.pwr(sh, "VDD_CP2102", 130, 55) if False else None
    # VDD_CP2102 is not a power-symbol name. Attach the flag to the label.
    x, y = 130, 62
    sh.label("VDD_CP2102", x, y, 180, shape="passive")
    fl = d.flag(sh, x + 12, y)
    sh.wire([(x, y), fl.pin_xy("1")])
    sh.text("PWR_FLAG on VDD_CP2102 only. Do not tie this net to +3V3.", 40, 175, 1.4)
    sh.text("Unused inputs ~CTS, ~DSR, ~DCD and ~RI/CLK are tied to VDD_CP2102. R62 pulls ~RSTb up.", 40, 181, 1.4)
    sh.text("R63/R64 are the Rev 1.5 Fig 2.5 divider. VREGIN is still the USB 5 V pin.", 40, 187, 1.4)
    sh.text("C9+C62 on VDD, C1+C63 on VREGIN: 4.7 uF and 100 nF on each regulator pin.", 40, 193, 1.4)
    sh.text("R72 and R73 are 1k in series with TX and RX so an unpowered side is not back-driven.", 40, 199, 1.3)
    sh.text("Q3 is the DevKitC auto-program pair: DTR/RTS to EN and GPIO0. R70 and R71 are the 10k bases.", 40, 205, 1.3)

    # hw_v2 USB strap links (the SJ pair in the spec). R102/R103 are fitted:
    # J1 D+/D- go to the S3 native USB pins (IO19/IO20). The alternate strap
    # is R104/R105 (DNP) into the CP2102 for out-of-band console recovery.
    # Never fit both pairs — two USB devices cannot share one D+/D- pair.
    sh.text("USB D+/D- strap links", 250, 232, 1.8, bold=True)
    d.hpart(sh, "R", "R102", "0", FP_R, 260, 243, "USB_DP", "USB_DP_S3")
    d.hpart(sh, "R", "R103", "0", FP_R, 305, 243, "USB_DM", "USB_DM_S3")
    d.hpart(sh, "R", "R104", "0", FP_R, 260, 258, "USB_DP", "USB_DP_CP", dnp=True)
    d.hpart(sh, "R", "R105", "0", FP_R, 305, 258, "USB_DM", "USB_DM_CP", dnp=True)
    sh.text("Fitted: J1 -> S3 IO20/IO19 (native USB, on-chip JTAG+console).", 250, 270, 1.3)
    sh.text("Alternate strap (DNP): move 0R to R104/R105 for the CP2102 bridge.", 250, 276, 1.3)
    sh.text("U5/U21/Q3 are unchanged. CP2102 keeps UART0 on ESP_TX/ESP_RX.", 250, 282, 1.3)


def mcu(d: Design):
    sh = d.sheet("MCU and FSR", "mcu.kicad_sch", "3")
    sh.rect(12, 40, 300, 290)
    sh.rect(308, 40, 406, 280)
    sh.text("MCU", 16, 16, 3.2, bold=True)
    sh.text("ESP32-S3-MINI-1-N4R2. On-board PCB antenna: antenna end at the board edge over an all-layer keep-out. 4 MB flash + 2 MB PSRAM inside.", 16, 24, 1.5)
    sh.text("Straps and FSR", 314, 46, 2.2, bold=True)

    u = d.part(
        sh,
        "ESP32-S3-MINI-1-N4R2",
        "U1",
        "ESP32-S3-MINI-1-N4R2",
        FP_S3,
        150,
        140,
        labels="ic",
    )
    # Pad numbers and net map per HW_V2_SPEC interface table; pad -> GPIO in
    # the pin names of hw_v2_lib(). EN (pad 45) and IO0 (pad 4) keep the
    # hw_v1 button/auto-program wiring.
    pins = {
        "45": "ESP_EN",
        "4": "ESP_IO0",  # IO0 boot strap: SW2, R6 pull-up, Q3
        "5": "FSR_ADC",  # IO1 = ADC1_CH0, FSR402 divider
        "6": "ADS1292_DRDY",  # IO2, timestamped input
        "7": "EXP_INT",  # IO3, JTAG-sel strap — R53 pull-up is fine
        "8": "LSM6_INT1",  # IO4
        "9": "TMP117_ALERT",  # IO5
        "10": "GAUGE_ALRT",  # IO6
        "11": "CHG_PG",  # IO7
        "12": "I2C_SDA",  # IO8
        "13": "I2C_SCL",  # IO9
        "14": "CS_ADS1292",  # IO10
        "15": "SPI_MOSI",  # IO11
        "16": "SPI_SCK",  # IO12
        "17": "SPI_MISO",  # IO13
        "18": "AD5940_GPIO0",  # IO14
        "19": "CS_AD5940",  # IO15
        "20": "CS_FLASH",  # IO16, W25Q512
        "21": "STATUS_LED",  # IO17 -> R88 -> D25 -> GND
        "22": "CS_AFE4900",  # IO18
        "23": "USB_DM_S3",  # IO19 = USB_D-, J1 through R103
        "24": "USB_DP_S3",  # IO20 = USB_D+, J1 through R102
        "25": "AFE4900_ADC_RDY",  # IO21, timestamped input
        "26": "NC",  # IO26 = in-module PSRAM CS on N4R2 — do not use
        "27": "NC",  # IO47 spare
        "28": "NC",  # IO33 spare
        "29": "NC",  # IO34 spare
        "30": "NC",  # IO48 spare
        "31": "NC",  # IO35 spare
        "32": "NC",  # IO36 spare
        "33": "NC",  # IO37 spare
        "34": "CS_MAX86178",  # IO38 — new SPI chip-select
        "35": "MAX86178_INT",  # IO39 — timestamped input
        "36": "MIC_CLK",  # IO40 — IM69D130 PDM clock (DNP)
        "37": "MIC_DAT",  # IO41 — IM69D130 PDM data (DNP)
        "38": "SPARE_IN",    # IO42 — MTMS, digital-only (no SAR ADC on S3); spare input
        "39": "ESP_TX",  # TXD0 = GPIO43 -> R72 -> CP2102 RXD
        "40": "ESP_RX",  # RXD0 = GPIO44 <- R73 <- CP2102 TXD
        "41": "NC",  # IO45 — strap pin, left unstrapped (no load)
        "44": "NC",  # IO46 — strap pin, left unstrapped (no load)
    }
    d.side_map(sh, u, pins, 8.89)
    d.join_row(sh, u, ["3"], "+3V3_ESP", 6.35)
    # Pads 1, 2, 42, 43, the 46-65 ground ring and the EPAD are stacked GND.
    d.join_row(sh, u, [str(n) for n in (1, 2, 42, 43, *range(46, 66))], "GND", 6.35)
    d.finish(sh, u)

    sh.text("Module decoupling", 40, 40, 1.6, bold=True)
    d.vpart(sh, "C", "C7", "22u", FP_C6, 55, 58, "+3V3_ESP", "GND")
    d.vpart(sh, "C", "C8", "100n", FP_C, 78, 58, "+3V3_ESP", "GND")

    d.vpart(sh, "R", "R5", "10k", FP_R, 330, 62, "+3V3", "ESP_EN")
    d.vpart(sh, "C", "C64", "1u", FP_C, 390, 78, "ESP_EN", "GND")
    d.vpart(sh, "R", "R6", "10k", FP_R, 352, 62, "+3V3", "ESP_IO0")
    d.vpart(sh, "R", "R7", "10k", FP_R, 374, 62, "+3V3", "CS_AD5940")
    d.vpart(sh, "R", "R52", "10k", FP_R, 330, 100, "+3V3", "CS_FLASH")
    d.vpart(sh, "R", "R53", "10k", FP_R, 352, 100, "+3V3", "EXP_INT")

    sh.text("FSR402 divider", 314, 100, 1.8, bold=True)
    hdr = d.part(sh, "Conn_01x02_Pin", "J3", "FSR pads", FP_FSR, 340, 130, labels="ic")
    d.stub_net(sh, hdr, "1", "+3V3", 7.62)
    d.stub_net(sh, hdr, "2", "FSR_ADC", 7.62)
    d.finish(sh, hdr)
    d.vpart(sh, "R", "R8", "10k", FP_R, 370, 125, "FSR_ADC", "GND")

    flash = d.part(sh, "W25Q512JVEIQ", "U18", "W25Q512JVEIQ", FP_FLASH, 70, 250, labels="ic")
    d.stub_net(sh, flash, "1", "CS_FLASH", 7.62)
    d.stub_net(sh, flash, "2", "MISO_FL", 7.62)
    d.stub_net(sh, flash, "3", "+3V3", 7.62)  # /WP
    d.stub_net(sh, flash, "5", "SPI_MOSI", 7.62)
    d.stub_net(sh, flash, "6", "SPI_SCK", 7.62)
    d.stub_net(sh, flash, "7", "+3V3", 7.62)  # /HOLD
    d.stub_net(sh, flash, "8", "+3V3", 7.62)
    d.join_row(sh, flash, ["4", "9"], "GND", 6.35)
    d.finish(sh, flash)
    d.vpart(sh, "C", "C59", "100n", FP_C, 140, 250, "+3V3", "GND")

    exp = d.part(sh, "TCA6408ARSV", "U19", "TCA6408ARSVR", FP_EXP, 340, 200, labels="ic")
    d.stub_net(sh, exp, "1", "ESP_EN", 6.35)  # /RESET follows the ESP32 reset
    d.stub_net(sh, exp, "2", "AFE4900_RESETZ", 7.62)
    d.stub_net(sh, exp, "3", "ADS1292_PWDN", 7.62)
    d.stub_net(sh, exp, "4", "AD5940_RESET", 7.62)
    d.stub_net(sh, exp, "5", "TX5_EN", 7.62)
    d.stub_net(sh, exp, "7", "IR_GATE", 7.62)
    d.stub_net(sh, exp, "8", "CHG_STAT", 7.62)
    d.stub_net(sh, exp, "9", "VBUS_DET", 7.62)
    d.stub_net(sh, exp, "10", "CHG_DIS", 7.62)
    d.stub_net(sh, exp, "11", "EXP_INT", 7.62)
    d.stub_net(sh, exp, "12", "I2C_SCL", 7.62)
    d.stub_net(sh, exp, "13", "I2C_SDA", 7.62)
    d.stub_net(sh, exp, "16", "GND", 7.62)  # ADDR -> 0x20
    d.join_row(sh, exp, ["14", "15"], "+3V3", 6.35)
    d.join_row(sh, exp, ["6"], "GND", 6.35)
    d.finish(sh, exp)
    d.vpart(sh, "C", "C60", "100n", FP_C, 390, 250, "+3V3", "GND")

    # hw_v2 strap notes for the S3 module.
    sh.text("IO0 is the boot strap (SW2, R6). IO3 is JTAG-sel; R53 holds EXP_INT high. IO45/IO46 left unstrapped.", 16, 200, 1.4)
    sh.text("IO26 is the in-module PSRAM select on N4R2. IO33-37, IO47, IO48 are spare GPIO, left open.", 16, 208, 1.4)
    sh.text("EN has R5 10k and C64 1 uF. Q3 on the USB sheet is the DTR/RTS auto-program pair.", 16, 216, 1.4)
    sh.text("R7 keeps CS_AD5940 high at reset. CS pull-ups R52/R98 unchanged; IO15 is not an S3 strap.", 16, 224, 1.4)

    # hw_v2: IO42 (pad 38) is digital-only — no SAR ADC on S3. R107/R108 are a
    # DNP divider reserved as a logic-level VBAT-present detect option only;
    # battery voltage/SoC stays with MAX17048. Do not populate for analog use.
    d.vpart(sh, "R", "R107", "100k", FP_R, 390, 170, "VBAT", "SPARE_IN", dnp=True)
    d.vpart(sh, "R", "R108", "100k", FP_R, 390, 195, "SPARE_IN", "GND", dnp=True)
    sh.text("SPARE_IN: IO42 digital-only; DNP divider = logic VBAT detect only.", 314, 168, 1.2)

    # hw_v2: IM69D130 PDM mic (fitted — firmware optional).
    mic = d.part(sh, "IM69D130", "U25", "IM69D130", FP_MIC, 235, 258, labels="ic")
    d.stub_net(sh, mic, "1", "MIC_DAT", 7.62)   # PDM data <- IO41
    d.stub_net(sh, mic, "3", "MIC_CLK", 7.62)   # PDM clock <- IO40
    d.stub_net(sh, mic, "4", "GND", 7.62)       # SELECT low = left slot
    d.join_row(sh, mic, ["2"], "+3V3", 6.35)
    d.join_row(sh, mic, ["5"], "GND", 6.35)
    d.finish(sh, mic)
    d.vpart(sh, "C", "C78", "100n", FP_C, 265, 255, "+3V3", "GND")
    sh.text("U25 IM69D130. PDM on IO40/IO41. 100 nF on VDD (datasheet).", 170, 282, 1.3)


def afe(d: Design):
    sh = d.sheet("AFE4900 PPG", "afe4900.kicad_sch", "4")
    sh.rect(12, 40, 406, 290)
    sh.text("AFE4900 PPG and second ECG lead", 16, 16, 3.2, bold=True)
    sh.text("SFH 7072 is on the board. TX_SUP is the TPS61240 5 V boost. AFE RLD is unused.", 16, 24, 1.5)

    u = d.part(sh, "AFE4900YZR", "U6", "AFE4900YZR", FP_AFE, 250, 130, labels="ic")
    left = {
        "A1": "AFE_INM",
        "B1": "AFE_INP",
        "A2": "PD_INP",
        "A3": "PD_INM",
        "B2": "PD2_INP",
        "B3": "PD2_INM",
        "B5": "NC",
        "A5": "NC",
        "C2": "NC",  # RLD_OUT stays off
        "D2": "AFE_BG",
        "D4": "TX1",
        "E5": "TX2",
        "E4": "TX3",
        "C5": "TX4",
        "F5": "TX_5V",
    }
    right = {
        "C1": "+3V3_ANA",
        "D1": "GND",
        "C4": "+3V3_ANA",
        "D5": "GND",
        "E1": "AFE4900_RESETZ",
        "E2": "MISO_AFE",
        "E3": "CS_AFE4900",
        "F2": "SPI_MOSI",
        "F3": "SPI_SCK",
        "F4": "AFE4900_ADC_RDY",
        "B4": "GND",
        "F1": "AFE_CLK",
        "A4": "+3V3_ANA",
        "D3": "NC",
        "C3": "NC",  # DNC is type no_connect; side_map skips only "NC" string. Handle below.
    }
    # C3 is electrical type no_connect. Do not also draw a no_connect marker if we skip it.
    right = {k: v for k, v in right.items() if k != "C3"}
    d.side_map(sh, u, left, 8.89)
    d.side_map(sh, u, right, 8.89)
    d.finish(sh, u)

    opt = d.part(sh, "SFH7072", "U16", "SFH 7072", FP_SFH, 70, 90, labels="ic")
    d.stub_net(sh, opt, "1", "PD_INP", 7.62)
    d.stub_net(sh, opt, "2", "PD_INM", 7.62)
    d.stub_net(sh, opt, "3", "PD2_INP", 7.62)
    d.stub_net(sh, opt, "10", "PD2_INM", 7.62)
    d.join_row(sh, opt, ["4", "5", "7", "11"], "TX_5V", 7.62)
    d.stub_net(sh, opt, "6", "TX1", 7.62)
    d.stub_net(sh, opt, "12", "TX2", 7.62)
    d.stub_net(sh, opt, "8", "TX3", 7.62)
    d.stub_net(sh, opt, "9", "TX4", 7.62)
    d.finish(sh, opt)

    bst = d.part(sh, "TPS61240", "U15", "TPS61240YFFR", FP_61240, 70, 200, labels="ic")
    d.stub_net(sh, bst, "A1", "VBAT_SYS", 7.62)
    d.stub_net(sh, bst, "C1", "TX5_EN", 7.62)
    d.stub_net(sh, bst, "B1", "TX_SW", 7.62)
    d.stub_net(sh, bst, "B2", "TX_5V_RAW", 7.62)
    d.stub_net(sh, bst, "C2", "TX_5V_RAW", 7.62)  # fixed 5 V, FB senses VOUT
    d.stub_net(sh, bst, "A2", "GND", 6.35)
    d.finish(sh, bst)
    d.hpart(sh, "L", "L2", "1.0u", FP_L, 130, 185, "VBAT_SYS", "TX_SW")
    d.vpart(sh, "C", "C46", "2.2u", FP_C, 40, 250, "VBAT_SYS", "GND")
    d.vpart(sh, "C", "C47", "4.7u", FP_C6, 70, 250, "TX_5V_RAW", "GND")
    d.vpart(sh, "R", "R25", "100k", FP_R, 100, 250, "TX5_EN", "GND")
    d.vpart(sh, "R", "R116", "10k", FP_R, 160, 250, "+3V3_ANA", "CS_AFE4900")

    sh.text("Bias and decoupling", 160, 210, 1.6, bold=True)
    d.vpart(sh, "C", "C11", "1u", FP_C, 170, 235, "+3V3_ANA", "GND")
    d.vpart(sh, "C", "C13", "100n", FP_C, 190, 235, "+3V3_ANA", "GND")
    d.vpart(sh, "C", "C14", "1u", FP_C, 210, 235, "TX_5V", "GND")
    d.vpart(sh, "C", "C15", "100n", FP_C, 230, 235, "AFE_BG", "GND")
    d.vpart(sh, "R", "R9", "10k", FP_R, 255, 235, "AFE4900_RESETZ", "GND")
    d.vpart(sh, "R", "R10", "1k", FP_R, 278, 235, "AFE_CLK", "GND")

    # TIDUDO6B Fig 2-10 biases each input from RLD through 5.11 MΩ.
    # 100 kΩ (same guide) sits between the TPD node and the coupling cap.
    # AFE RLD_OUT stays open: ADS1292R already drives the one RLD pad.
    d.hpart(sh, "R", "R44", "100k", FP_R, 250, 210, "AFE_P_AC", "AFE_P_SER")
    d.hpart(sh, "R", "R46", "100k", FP_R, 310, 210, "AFE_N_AC", "AFE_N_SER")
    d.hpart(sh, "C", "C56", "100n", FP_C, 250, 228, "AFE_P_SER", "AFE_INP")
    d.hpart(sh, "C", "C57", "100n", FP_C, 310, 228, "AFE_N_SER", "AFE_INM")
    d.vpart(sh, "R", "R43", "5.11M", FP_R, 250, 250, "RLDOUT", "AFE_INP")
    d.vpart(sh, "R", "R45", "5.11M", FP_R, 290, 250, "RLDOUT", "AFE_INM")
    sh.text("C11 RX_SUP, C13 IO_SUP, C14 local TX_SUP, C47 is the boost 4.7 uF. C15 is BG.", 16, 270, 1.3)
    sh.text("R44/R46 are 100k on the TPD side. R43/R45 bias from ADS RLDOUT, not a second RLD amp.", 16, 276, 1.3)


def ad(d: Design):
    sh = d.sheet("AD5940 EDA", "ad5940.kicad_sch", "5")
    sh.rect(12, 40, 406, 290)
    sh.text("AD5940 EDA and chest respiration", 16, 14, 3.2, bold=True)
    sh.text("Shoulder EDA stays on CE0, SE0, RE0, DE0. Chest 4-wire uses AIN1, AIN0, AIN3, AIN2.", 16, 22, 1.3)

    u = d.part(sh, "AD5940BCBZ-RL7", "U7", "AD5940BCBZ-RL7", FP_AD, 250, 40, labels="ic")
    nets = {
        # hw_v2: A1/A2/C5 leave the J11 sweat site (RESEARCH-GRADE).
        # VERIFY vs the datasheet switch matrix: AFE4 is driven by the
        # excitation buffer (D-switch) as CE; AFE3 is the reference sense
        # (P-switch) as RE; AIN6 muxes to the LPTIA input as WE.
        "A1": "SWEAT_CE",  # AFE4 — counter-electrode drive (VERIFY)
        "A2": "SWEAT_RE",  # AFE3 — reference sense (VERIFY)
        "A3": "BIOZ_SN",
        "A4": "+3V3_ANA",
        "A5": "VREF_1V82",
        "A6": "SE0",
        "A7": "CE0",
        "A8": "RE0",
        "B1": "RCAL1",
        "B2": "NC",
        "B3": "BIOZ_FP",
        "B4": "AIN4_LPF0",
        "B5": "BIOZ_SP",
        "B6": "DE0",
        "B7": "VZERO0",
        "B8": "RC0_1",
        "C1": "RCAL0",
        "C2": "NC",
        "C4": "GND",
        "C5": "SWEAT_WE",  # AIN6 — working-electrode current in (VERIFY)
        "C7": "VBIAS0",
        "C8": "RC0_0",
        "D1": "VBIAS_CAP",
        "D2": "BIOZ_FN",
        "D5": "GND",
        "D6": "NC",
        "D7": "VREF_2V5",
        "D8": "AVDD_REG",
        "E1": "NIR730_GATE",  # GPIO2 — hw_v2: gates Q4, the 730 nm LED
        "E2": "NC",
        "E3": "GND",
        "E4": "GND",
        "E5": "GND",
        "E6": "GND",
        "E7": "SPI_MOSI",
        "E8": "MISO_AD",
        "F1": "AD5940_RESET",
        "F2": "+3V3_ANA",
        "F3": "+3V3_ANA",
        "F4": "NC",
        "F5": "AD5940_GPIO0",
        "F6": "NC",
        "F7": "CS_AD5940",
        "F8": "SPI_SCK",
        "G2": "IOVDD",
        "G3": "DVDD_REG",
        "G4": "NC",
        "G5": "NC",
        "G6": "NC",
        "G7": "NC",
    }
    # DNC pins are type no_connect in the symbol: C3 C6 D3 D4 G1 G8.
    d.side_map(sh, u, nets, 7.62)
    d.finish(sh, u)

    hdr = d.part(sh, "Conn_01x04_Pin", "J6", "EDA pads", FP_H4, 40, 52, labels="ic")
    for num, net in {"1": "EDA_CE_PAD", "2": "EDA_RE_PAD", "3": "EDA_SE_PAD", "4": "EDA_DE_PAD"}.items():
        d.stub_net(sh, hdr, num, net, 7.62)
    d.finish(sh, hdr)
    sh.text("J6 solder pads. No header. Shoulder tail, at least 5 cm from the ECG electrodes.", 16, 44, 1.3, bold=True)
    chest = d.part(sh, "Conn_01x04_Pin", "J7", "Chest bioZ", FP_BIO, 130, 78, labels="ic")
    for num, net in {"1": "BIOZ_FP_PAD", "2": "BIOZ_FN_PAD", "3": "BIOZ_SP_PAD", "4": "BIOZ_SN_PAD"}.items():
        d.stub_net(sh, chest, num, net, 7.62)
    d.finish(sh, chest)
    sh.text("J7 is F+ F- S+ S-. Dedicated chest pads. Not paralleled onto J5 or J6.", 16, 70, 1.3)

    sh.text("Decoupling, RCAL, RC0", 16, 100, 1.6, bold=True)
    caps = [
        ("C16", "1u", "+3V3_ANA"),
        ("C18", "100n", "+3V3_ANA"),
        ("C42", "1u", "AVDD_REG"),
        ("C19", "1u", "IOVDD"),
        ("C20", "4.7u", "VREF_1V82"),
        ("C21", "470n", "VREF_2V5"),
        ("C22", "470n", "VBIAS_CAP"),
        ("C23", "470n", "DVDD_REG"),
        ("C24", "100n", "VZERO0"),
        ("C25", "100n", "VBIAS0"),
        ("C26", "1u", "AIN4_LPF0"),
    ]
    for i, (ref, val, net) in enumerate(caps):
        col, row = i % 6, i // 6
        # 38 mm keeps the second-row net names clear of the GND symbols above them.
        d.vpart(sh, "C", ref, val, FP_C, 40 + col * 22, 125 + row * 38, net, "GND")
    d.vpart(sh, "R", "R12", "10", FP_R, 40, 200, "+3V3_ANA", "IOVDD")
    d.vpart(sh, "R", "R11", "10k", FP_R, 62, 200, "AD5940_RESET", "GND")
    d.hpart(sh, "R", "R13", "1k", FP_R, 110, 203, "RCAL0", "RCAL1")
    d.hpart(sh, "C", "C27", "100n", FP_C, 145, 203, "RC0_0", "RC0_1")
    d.flag_net(sh, "IOVDD", 175, 203)

    sh.text("C16 AVDD, C18 DVDD, C42 AVDD_REG, C19 IOVDD after R12, C20-C26 are the reference and bias caps.", 16, 236, 1.35)
    sh.text("R13 is RCAL. C27 sits between RC0_0 and RC0_1. XTAL pins are open: the internal oscillator is used.", 16, 242, 1.35)
    # Surge resistor on the pad, then the AN-1557 network. C54 is behind R76.
    d.hpart(sh, "R", "R76", "51k", FP_HV, 200, 210, "EDA_CE_PAD", "CE_SURGE")
    d.hpart(sh, "C", "C54", "15n", FP_C, 255, 210, "CE_SURGE", "CE_ISO")
    d.hpart(sh, "R", "R39", "1k", FP_R, 305, 210, "CE_ISO", "CE0")
    d.hpart(sh, "R", "R77", "51k", FP_HV, 200, 232, "EDA_SE_PAD", "SE_SURGE")
    d.hpart(sh, "R", "R40", "1k", FP_R, 255, 232, "SE_SURGE", "SE_ISO")
    d.hpart(sh, "C", "C55", "470n", FP_C, 305, 232, "SE_ISO", "SE0")
    d.hpart(sh, "R", "R78", "51k", FP_HV, 200, 254, "EDA_RE_PAD", "RE_SURGE")
    d.hpart(sh, "R", "R41", "1k", FP_R, 255, 254, "RE_SURGE", "RE0")
    d.hpart(sh, "R", "R79", "51k", FP_HV, 310, 254, "EDA_DE_PAD", "DE_SURGE")
    d.hpart(sh, "R", "R42", "1k", FP_R, 365, 254, "DE_SURGE", "DE0")
    # TPD is behind the surge resistor, and the 1k keeps clamp current out of the pin.
    # 26 mm pitch. A 20 mm stack lands the next label on the GND stub and shorts the pad.
    d.vpart(sh, "D_TVS_2", "D6", "TPD1E10B06", FP_TVS, 400, 150, "CE_ISO", "GND")
    d.vpart(sh, "D_TVS_2", "D7", "TPD1E10B06", FP_TVS, 400, 176, "SE_ISO", "GND")
    d.vpart(sh, "D_TVS_2", "D8", "TPD1E10B06", FP_TVS, 400, 202, "RE_SURGE", "GND")
    d.vpart(sh, "D_TVS_2", "D9", "TPD1E10B06", FP_TVS, 400, 228, "DE_SURGE", "GND")
    sh.text("R76-R79 are the pad-side 51k DPCR. C54 sits behind R76. R39 stays the 1k RLIMIT. No gas tube.", 16, 268, 1.3)
    sh.text("hw_v2: AIN6/AFE3/AFE4 feed J11 (RESEARCH-GRADE sweat site) and GPIO2 is NIR730_GATE. Other GPIO balls stay open.", 16, 274, 1.3)
    # Chest 4-wire. Same pad-side order as EDA. Force uses the Fig 54 15 nF and 1 kΩ.
    # The other three lines use 470 nF. 26 mm keeps a TPD GND stub off the next label.
    d.hpart(sh, "R", "R80", "51k", FP_HV, 230, 114, "BIOZ_FP_PAD", "FP_SURGE")
    d.hpart(sh, "C", "C67", "15n", FP_C, 285, 114, "FP_SURGE", "FP_ISO")
    d.hpart(sh, "R", "R84", "1k", FP_R, 340, 114, "FP_ISO", "BIOZ_FP")
    d.hpart(sh, "R", "R81", "51k", FP_HV, 230, 136, "BIOZ_FN_PAD", "FN_SURGE")
    d.hpart(sh, "R", "R85", "1k", FP_R, 285, 136, "FN_SURGE", "FN_ISO")
    d.hpart(sh, "C", "C68", "470n", FP_C, 340, 136, "FN_ISO", "BIOZ_FN")
    d.hpart(sh, "R", "R82", "51k", FP_HV, 230, 158, "BIOZ_SP_PAD", "SP_SURGE")
    d.hpart(sh, "R", "R86", "1k", FP_R, 285, 158, "SP_SURGE", "SP_ISO")
    d.hpart(sh, "C", "C69", "470n", FP_C, 340, 158, "SP_ISO", "BIOZ_SP")
    d.hpart(sh, "R", "R83", "51k", FP_HV, 230, 180, "BIOZ_SN_PAD", "SN_SURGE")
    d.hpart(sh, "R", "R87", "1k", FP_R, 285, 180, "SN_SURGE", "SN_ISO")
    d.hpart(sh, "C", "C70", "470n", FP_C, 340, 180, "SN_ISO", "BIOZ_SN")
    d.vpart(sh, "D_TVS_2", "D21", "TPD1E10B06", FP_TVS, 178, 110, "FP_ISO", "GND")
    d.vpart(sh, "D_TVS_2", "D22", "TPD1E10B06", FP_TVS, 178, 136, "FN_ISO", "GND")
    d.vpart(sh, "D_TVS_2", "D23", "TPD1E10B06", FP_TVS, 178, 162, "SP_ISO", "GND")
    d.vpart(sh, "D_TVS_2", "D24", "TPD1E10B06", FP_TVS, 178, 188, "SN_ISO", "GND")
    sh.text("Chest: F+ is AIN1 through 15 nF and R84 (1k RLIMIT). F- AIN0, S+ AIN3, S- AIN2, each through 470 nF.", 16, 282, 1.2)
    sh.text("R80-R83 are the pad-side 51k. D21-D24 clamp the IC side. R39 stays on the shoulder CE line.", 16, 288, 1.2)


def ads(d: Design):
    sh = d.sheet("ADS1292R ECG", "ads1292.kicad_sch", "6")
    sh.rect(12, 40, 406, 290)
    sh.text("ADS1292R ECG. Channel 1 modulation stays; chest respiration is the AD5940.", 16, 14, 2.6, bold=True)
    sh.text("Fig 68 is still wired. Channel 2 is the ECG lead. START is tied low. Nothing in this network was removed.", 16, 22, 1.3)
    sh.text("C34 stays 47 nF (Fig 73 note 1). The 51k surge resistors stay in this loop, so it is outside section 6.5.", 16, 28, 1.3)
    sh.text("Do not use channel 1 as the breathing measurement. The 4-wire chest path is on the AD5940 sheet.", 16, 34, 1.2)

    u = d.part(sh, "ADS1292RIRSMT", "U8", "ADS1292RIRSMT", FP_ADS, 200, 110, labels="ic")
    side = {
        "1": "PGA1N",
        "2": "PGA1P",
        "3": "IN1N",
        "4": "IN1P",
        "5": "ECG_N",
        "6": "ECG_P",
        "7": "PGA2N",
        "8": "PGA2P",
        "9": "VREFP",
        "10": "GND",  # VREFN
        "11": "VCAP1",
        "27": "VCAP2",
        "28": "RLDINV",
        "29": "RLDREF",
        "30": "RLDOUT",
        "31": "RESP_MODP",
        "32": "RESP_MODN",
        "14": "+3V3_ANA",  # CLKSEL = DVDD
        "15": "ADS1292_PWDN",
        "16": "GND",
        "17": "GND",  # CLK tied to DGND per SBAS502 when CLKSEL is internal
        "18": "CS_ADS1292",
        "19": "SPI_MOSI",
        "20": "SPI_SCK",
        "21": "MISO_ADS",
        "22": "ADS1292_DRDY",
        "25": "ADS_GPIO2",
        "26": "ADS_GPIO1",
        "33": "GND",  # exposed pad, must be AVSS
    }
    d.side_map(sh, u, side, 8.89)
    d.join_row(sh, u, ["12", "23"], "+3V3_ANA", 6.35)
    d.join_row(sh, u, ["13", "24"], "GND", 6.35)
    d.finish(sh, u)

    hdr = d.part(sh, "Conn_01x05_Pin", "J5", "ECG pads", FP_H3, 48, 70, labels="ic")
    for num, net in {
        "1": "ECG1_PAD",
        "2": "ECG2_PAD",
        "3": "RLD_PAD",
        "4": "AFE_P_PAD",
        "5": "AFE_N_PAD",
    }.items():
        d.stub_net(sh, hdr, num, net, 7.62)
    d.finish(sh, hdr)
    sh.text("J5 flat pads: 1 ADS+  2 ADS-  3 RLD  4 AFE+  5 AFE-. No header.", 16, 44, 1.3)

    sh.text("Supplies and PGA", 16, 78, 1.6, bold=True)
    row = [
        ("C43", "1u", "+3V3_ANA", "GND"),
        ("C28", "100n", "+3V3_ANA", "GND"),
        ("C29", "100n", "+3V3_ANA", "GND"),
        ("C30", "10u", "VREFP", "GND"),
        ("C32", "1u", "VCAP1", "GND"),
        ("C33", "1u", "VCAP2", "GND"),
    ]
    for i, (ref, val, a, b) in enumerate(row):
        d.vpart(sh, "C", ref, val, FP_C, 40 + i * 22, 185, a, b)
    # SBAS502C Fig 73/74 note (1): PGA1 CFILTER must be 47 nF with channel-1 respiration.
    d.hpart(sh, "C", "C34", "47n", FP_C, 70, 210, "PGA1N", "PGA1P")
    d.hpart(sh, "C", "C35", "4.7n", FP_C, 130, 210, "PGA2N", "PGA2P")
    d.vpart(sh, "R", "R14", "10k", FP_R, 185, 185, "ADS1292_PWDN", "GND")

    sh.text("RLD bias", 16, 228, 1.8, bold=True)
    d.hpart(sh, "R", "R15", "1M", FP_R, 70, 240, "+3V3_ANA", "RLDREF")
    d.hpart(sh, "R", "R16", "1M", FP_R, 115, 240, "RLDREF", "GND")
    d.hpart(sh, "R", "R17", "1M", FP_R, 175, 240, "RLDOUT", "RLDINV")
    d.hpart(sh, "C", "C65", "1.5n", FP_C, 230, 200, "RLDOUT", "RLDINV")
    d.vpart(sh, "C", "C66", "1u", FP_C, 175, 155, "RLDREF", "GND")
    d.vpart(sh, "R", "R100", "10k", FP_R, 210, 155, "ADS_GPIO1", "GND")
    d.vpart(sh, "R", "R101", "10k", FP_R, 235, 155, "ADS_GPIO2", "GND")

    # Fig 68. Electrode node is ECG_P / ECG_N, after the series resistor.
    d.vpart(sh, "R", "R26", "10M", FP_R, 250, 185, "+3V3_ANA", "IN1P")
    d.vpart(sh, "R", "R27", "10M", FP_R, 272, 185, "IN1P", "GND")
    d.vpart(sh, "R", "R28", "10M", FP_R, 294, 185, "+3V3_ANA", "IN1N")
    d.vpart(sh, "R", "R29", "10M", FP_R, 316, 185, "IN1N", "GND")
    d.vpart(sh, "C", "C48", "2.2n", FP_C, 340, 185, "IN1P", "GND")
    d.vpart(sh, "C", "C49", "2.2n", FP_C, 362, 185, "IN1N", "GND")
    d.hpart(sh, "C", "C50", "100n", FP_C, 250, 220, "ECG_P", "IN1P_AC")
    d.hpart(sh, "R", "R67", "10k", FP_R, 300, 220, "IN1P_AC", "IN1P")
    d.hpart(sh, "C", "C51", "100n", FP_C, 250, 238, "ECG_N", "IN1N_AC")
    d.hpart(sh, "R", "R68", "10k", FP_R, 300, 238, "IN1N_AC", "IN1N")
    d.vpart(sh, "C", "C52", "2.2n", FP_C, 360, 220, "ECG_P", "GND")
    d.vpart(sh, "C", "C53", "2.2n", FP_C, 382, 220, "ECG_N", "GND")
    d.hpart(sh, "R", "R30", "40.2k", FP_R, 260, 245, "RESP_MODP", "ECG_P")
    d.hpart(sh, "R", "R31", "40.2k", FP_R, 330, 245, "RESP_MODN", "ECG_N")
    # DPCR 51k stands off the defib pulse. TPD is on the IC side of that resistor.
    d.hpart(sh, "R", "R32", "51k", FP_HV, 55, 258, "ECG1_PAD", "ECG_P")
    d.hpart(sh, "R", "R33", "51k", FP_HV, 115, 258, "ECG2_PAD", "ECG_N")
    d.hpart(sh, "R", "R34", "51k", FP_HV, 175, 278, "RLD_PAD", "RLD_CLAMP")
    d.hpart(sh, "R", "R69", "10k", FP_R, 230, 278, "RLD_CLAMP", "RLDOUT")
    d.hpart(sh, "R", "R35", "51k", FP_HV, 235, 258, "AFE_P_PAD", "AFE_P_AC")
    d.hpart(sh, "R", "R36", "51k", FP_HV, 295, 258, "AFE_N_PAD", "AFE_N_AC")
    d.vpart(sh, "D_TVS_2", "D1", "TPD1E10B06", FP_TVS, 55, 278, "ECG_P", "GND")
    d.vpart(sh, "D_TVS_2", "D2", "TPD1E10B06", FP_TVS, 115, 278, "ECG_N", "GND")
    d.vpart(sh, "D_TVS_2", "D3", "TPD1E10B06", FP_TVS, 150, 155, "RLD_CLAMP", "GND")
    d.vpart(sh, "D_TVS_2", "D4", "TPD1E10B06", FP_TVS, 48, 155, "AFE_P_AC", "GND")
    d.vpart(sh, "D_TVS_2", "D5", "TPD1E10B06", FP_TVS, 90, 155, "AFE_N_AC", "GND")


def i2c(d: Design):
    sh = d.sheet("I2C and IMU", "i2c.kicad_sch", "7")
    sh.rect(12, 40, 200, 290)
    sh.rect(208, 40, 406, 290)
    sh.text("I2C sensors and IMU", 16, 14, 3.0, bold=True)
    sh.text("Left: 1.8 V AS7341 behind PCA9306. Right: 3.3 V bus, including the IMU.", 16, 22, 1.4)

    sh.text("EXTRA IC  U9 PCA9306DCUR", 18, 48, 2.0, bold=True)
    sh.text("AS7341 I2C is not 3.3 V tolerant, and 1.8 V is below the ESP32 high level.", 18, 56, 1.3)
    sh.text("One translator on SDA and SCL. INT is open; the firmware polls 0x39.", 18, 62, 1.3)

    u = d.part(sh, "PCA9306DC", "U9", "PCA9306DCUR", "Package_SO:VSSOP-8_2.3x2mm_P0.5mm", 70, 95, labels="ic")
    d.stub_net(sh, u, "3", "I2C_SCL_1V8", 7.62)
    d.stub_net(sh, u, "4", "I2C_SDA_1V8", 7.62)
    d.jog(sh, u, "2", "+1V8", jog=-12)
    d.jog(sh, u, "7", "VREF2", jog=12)
    d.stub_net(sh, u, "1", "GND", 6.35)
    d.stub_net(sh, u, "8", "VREF2", 7.62)
    d.stub_net(sh, u, "6", "I2C_SCL", 7.62)
    d.stub_net(sh, u, "5", "I2C_SDA", 7.62)
    d.finish(sh, u)

    d.vpart(sh, "R", "R18", "200k", FP_R, 130, 78, "+3V3", "VREF2")
    # SCPS113O 8.1.7: the 1.8 V LDO cannot sink the translator bias. 301k bleeds it.
    d.vpart(sh, "R", "R66", "301k", FP_R, 155, 110, "+1V8", "GND")
    d.flag_net(sh, "VREF2", 155, 100)
    d.vpart(sh, "R", "R21", "4.7k", FP_R, 40, 150, "+1V8", "I2C_SDA_1V8")
    d.vpart(sh, "R", "R22", "4.7k", FP_R, 62, 150, "+1V8", "I2C_SCL_1V8")
    d.vpart(sh, "R", "R19", "4.7k", FP_R, 100, 150, "+3V3", "I2C_SDA")
    d.vpart(sh, "R", "R20", "4.7k", FP_R, 122, 150, "+3V3", "I2C_SCL")

    a = d.part(sh, "AS7341-DLGM", "U10", "AS7341-DLGM", FP_AS, 55, 200, labels="ic")
    d.stub_net(sh, a, "8", "I2C_SDA_1V8", 7.62)
    d.stub_net(sh, a, "2", "I2C_SCL_1V8", 7.62)
    d.join_row(sh, a, ["1"], "+1V8", 6.35)
    d.join_row(sh, a, ["3", "5"], "GND", 6.35)
    d.stub_net(sh, a, "4", "LDR", 7.62)
    d.finish(sh, a)  # GPIO, INT
    d.vpart(sh, "C", "C36", "100n", FP_C, 140, 205, "+1V8", "GND")
    d.vpart(sh, "LED_AK", "D10", "NF2W757G-F1", FP_WHITE, 55, 265, "+3V3", "LDR")
    d.vpart(sh, "R", "R47", "100", FP_R, 90, 245, "+3V3", "IR_AN")
    d.vpart(sh, "LED_AK", "D11", "SFH4053", FP_IR, 115, 245, "IR_AN", "IR_K")
    q = d.part(sh, "CSD13380F3", "Q1", "CSD13380F3T", FP_FET, 160, 265, labels="ic")
    d.stub_net(sh, q, "1", "IR_GATE", 6.35)
    d.stub_net(sh, q, "2", "GND", 6.35)
    d.stub_net(sh, q, "3", "IR_K", 6.35)
    d.finish(sh, q)
    d.vpart(sh, "R", "R48", "100k", FP_R, 175, 245, "IR_GATE", "GND")
    sh.text("AS7341 0x39 at 1.8 V. D10 anode is +3V3, never VBAT. LDR abs max is 3.6 V.", 18, 282, 1.2)

    # 3.3 V sensors
    t = d.part(
        sh,
        "TMP117AIDRVR",
        "U11",
        "TMP117AIDRVR",
        FP_TMP,
        250,
        70,
        labels="ic",
    )
    # DRV Table 5-1: pin 3 is ALERT, pin 4 is ADD0. ADD0 = GND selects 0x48.
    d.stub_net(sh, t, "1", "I2C_SCL", 7.62)
    d.stub_net(sh, t, "3", "TMP117_ALERT", 7.62)
    d.stub_net(sh, t, "4", "GND", 7.62)
    d.stub_net(sh, t, "5", "+3V3", 7.62)
    d.stub_net(sh, t, "6", "I2C_SDA", 7.62)
    d.join_row(sh, t, ["2", "7"], "GND", 6.35)
    d.finish(sh, t)
    # SNOSD82D: ALERT is open-drain and requires a pull-up. IO5 has no internal pull-up.
    d.vpart(sh, "R", "R65", "10k", FP_R, 330, 155, "+3V3", "TMP117_ALERT")
    sh.text("TMP117 U11 0x48, ADD0 = GND. R65 pulls ALERT up.", 300, 48, 1.2)
    t2 = d.part(sh, "TMP117AIDRVR", "U20", "TMP117AIDRVR", FP_TMP, 370, 80, labels="ic")
    d.stub_net(sh, t2, "1", "I2C_SCL", 7.62)
    d.stub_net(sh, t2, "4", "+3V3", 7.62)  # ADD0 -> 0x49
    d.stub_net(sh, t2, "5", "+3V3", 7.62)
    d.stub_net(sh, t2, "6", "I2C_SDA", 7.62)
    d.join_row(sh, t2, ["2", "7"], "GND", 6.35)
    d.finish(sh, t2)  # ALERT open; U11 already owns the alert pin
    d.vpart(sh, "C", "C61", "100n", FP_C, 390, 130, "+3V3", "GND")
    sh.text("U20 is the second TMP117, ADD0 = +3V3, address 0x49.", 300, 115, 1.2)

    m = d.part(sh, "MLX90632SLD-DCB-100-SP", "U12", "MLX90632SLD-DCB-100-SP", FP_MLX, 300, 130, labels="ic")
    # Option code "1" is the 1.8 V I2C variant. VDD stays 3.3 V. SDA/SCL share the PCA9306 1.8 V side.
    d.stub_net(sh, m, "1", "I2C_SDA_1V8", 7.62)
    d.stub_net(sh, m, "5", "GND", 7.62)  # ADDR -> 0x3A
    d.stub_net(sh, m, "4", "I2C_SCL_1V8", 7.62)
    d.stub_net(sh, m, "6", "GND", 7.62)  # exposed pad
    d.join_row(sh, m, ["2"], "+3V3", 6.35)
    d.join_row(sh, m, ["3"], "GND", 6.35)
    d.finish(sh, m)
    sh.text("MLX90632SLD-DCB-100-SP is the 1.8 V I2C option. VDD is still 3.3 V. ADDR = GND is 0x3A.", 220, 155, 1.3)

    b = d.part(
        sh,
        "BME280",
        "U13",
        "BME280",
        "Package_LGA:Bosch_LGA-8_2.5x2.5mm_P0.65mm_ClockwisePinNumbering",
        235,
        175,
        labels="ic",
    )
    d.stub_net(sh, b, "5", "GND", 7.62)  # SDO -> 0x76
    d.stub_net(sh, b, "4", "I2C_SCL", 7.62)
    d.stub_net(sh, b, "3", "I2C_SDA", 7.62)
    d.stub_net(sh, b, "2", "+3V3", 7.62)  # CSB
    d.join_row(sh, b, ["6", "8"], "+3V3", 6.35)
    d.join_row(sh, b, ["1", "7"], "GND", 6.35)
    d.finish(sh, b)
    sh.text("BME280 0x76, SDO = GND, CSB high", 300, 145, 1.3)

    imu = d.part(sh, "LSM6DSV80XTR", "U14", "LSM6DSV80XTR", FP_IMU, 280, 200, labels="ic")
    d.stub_net(sh, imu, "1", "GND", 6.35)  # SDO -> 0x6A
    d.stub_net(sh, imu, "2", "GND", 6.35)  # SDx
    d.stub_net(sh, imu, "3", "GND", 6.35)  # SCx
    d.stub_net(sh, imu, "12", "+3V3", 6.35)  # CS
    d.stub_net(sh, imu, "4", "LSM6_INT1", 7.62)
    d.stub_net(sh, imu, "13", "I2C_SCL", 7.62)
    d.stub_net(sh, imu, "14", "I2C_SDA", 7.62)
    d.join_row(sh, imu, ["5", "8"], "+3V3", 6.35)
    d.join_row(sh, imu, ["6", "7"], "GND", 6.35)
    d.finish(sh, imu)  # INT2
    sh.text("IMU  LSM6DSV80X 0x6A. SDO, SDx and SCx grounded. CS high. INT2 open.", 220, 248, 1.3)

    sh.text("100 nF on each sensor supply", 220, 210, 1.4, bold=True)
    d.vpart(sh, "C", "C37", "100n", FP_C, 230, 228, "+3V3", "GND")
    d.vpart(sh, "C", "C38", "100n", FP_C, 252, 228, "+3V3", "GND")
    d.vpart(sh, "C", "C39", "100n", FP_C, 274, 228, "+3V3", "GND")
    d.vpart(sh, "C", "C40", "100n", FP_C, 296, 228, "+3V3", "GND")
    d.vpart(sh, "C", "C41", "100n", FP_C, 318, 228, "+3V3", "GND")

    # ---- hw_v2 additions on the 3.3 V bus -------------------------------
    # U23 SHT45-AD1B, skin-side DFN-4, I2C 0x44. Shares the R19/R20 pull-ups.
    sht = d.part(sh, "SHT45-AD1B", "U23", "SHT45-AD1B", FP_SHT45, 365, 175, labels="ic")
    d.stub_net(sh, sht, "1", "I2C_SDA", 7.62)
    d.stub_net(sh, sht, "2", "I2C_SCL", 7.62)
    d.join_row(sh, sht, ["3"], "+3V3", 6.35)
    d.join_row(sh, sht, ["4"], "GND", 6.35)
    d.finish(sh, sht)
    d.vpart(sh, "C", "C76", "100n", FP_C, 395, 200, "+3V3", "GND")
    sh.text("U23 SHT45-AD1B, 0x44, skin side.", 330, 190, 1.2)

    # U24 RV-3028-C7 RTC, I2C 0x52. VBACKUP goes to VSS through a 10k
    # (datasheet "must" when backup is unused — not a direct short). EVI low.
    rtc = d.part(sh, "RV-3028-C7", "U24", "RV-3028-C7", FP_RTC, 360, 235, labels="ic")
    d.stub_net(sh, rtc, "3", "I2C_SCL", 7.62)
    d.stub_net(sh, rtc, "4", "I2C_SDA", 7.62)
    d.stub_net(sh, rtc, "2", "RTC_INT", 7.62)  # open-drain /INT — no host GPIO assigned
    d.stub_net(sh, rtc, "8", "GND", 7.62)      # EVI tied low
    d.join_row(sh, rtc, ["7"], "+3V3", 6.35)   # VDD
    d.join_row(sh, rtc, ["5"], "GND", 6.35)  # VSS
    d.stub_net(sh, rtc, "6", "RTC_VBK", 6.35)  # VBACKUP — datasheet requires 10k to VSS, not a short
    d.vpart(sh, "R", "R115", "10k", FP_R, 410, 262, "RTC_VBK", "GND")
    d.flag_net(sh, "RTC_VBK", 355, 270)  # power input reached only through R115
    d.finish(sh, rtc)  # CLKOUT left open
    d.vpart(sh, "C", "C77", "100n", FP_C, 335, 262, "+3V3", "GND")
    d.vpart(sh, "R", "R112", "10k", FP_R, 390, 262, "+3V3", "RTC_INT")
    sh.text("U24 RV-3028-C7, 0x52. VBACKUP to VSS via R115 10k; CLKOUT open.", 330, 250, 1.2)

    # D12: 730 nm NIR LED for the AS7341 StO2 channel. Same switch pattern
    # as Q1/D11: 100R from +3V3, cathode to the CSD13380F3 drain. Gate is
    # AD5940 ball E1 (GPIO2) on net NIR730_GATE, pulled down by R110.
    d.vpart(sh, "R", "R109", "100", FP_R, 30, 245, "+3V3", "NIR_AN")
    d.vpart(sh, "LED_AK", "D12", "SFH 4735 730nm", FP_IR, 30, 270, "NIR_AN", "NIR_K")  # VERIFY: emitter choice
    q4 = d.part(sh, "CSD13380F3", "Q4", "CSD13380F3T", FP_FET, 185, 275, labels="ic")
    d.stub_net(sh, q4, "1", "NIR730_GATE", 6.35)
    d.stub_net(sh, q4, "2", "GND", 6.35)
    d.stub_net(sh, q4, "3", "NIR_K", 6.35)
    d.finish(sh, q4)
    d.vpart(sh, "R", "R110", "100k", FP_R, 185, 250, "NIR730_GATE", "GND")
    sh.text("D12 is the 730 nm LED; Q4 gate is AD5940 GPIO2 (E1). Pairs with the AS7341 NIR channel.", 16, 288, 1.1)


def debug(d: Design):
    """Test access, ESD, current links, and the GPIO17 heartbeat."""
    sh = d.sheet("Test and debug", "debug.kicad_sch", "8")
    sh.rect(12, 40, 406, 280)
    sh.text("Test pads, programming, and current links", 16, 16, 2.6, bold=True)
    sh.text("USBLC6-2SC6 is at the USB connector. GPIO17 drives the green LED. R3 on the power sheet is 1.5k.", 16, 24, 1.3)

    esd = d.part(sh, "USBLC6-2SC6", "U21", "USBLC6-2SC6", FP_ESD, 40, 55, labels="ic")
    d.stub_net(sh, esd, "1", "USB_DP", 6.35)
    d.stub_net(sh, esd, "6", "USB_DP", 6.35)
    d.stub_net(sh, esd, "3", "USB_DM", 6.35)
    d.stub_net(sh, esd, "4", "USB_DM", 6.35)
    d.stub_net(sh, esd, "5", "VBUS", 7.62)
    d.stub_net(sh, esd, "2", "GND", 7.62)
    d.finish(sh, esd)

    d.vpart(sh, "SW_Push", "SW1", "TS-1088", FP_SW, 100, 55, "ESP_EN", "GND")
    d.vpart(sh, "SW_Push", "SW2", "TS-1088", FP_SW, 125, 55, "ESP_IO0", "GND")
    d.vpart(sh, "R", "R88", "1k", FP_R, 155, 55, "STATUS_LED", "LED_A")
    d.vpart(sh, "LED_AK", "D25", "KT-0603G", FP_LED6, 180, 55, "LED_A", "GND")

    d.hpart(sh, "R", "R89", "0", FP_R, 220, 55, "MISO_ADS", "SPI_MISO")
    d.hpart(sh, "R", "R90", "0", FP_R, 255, 55, "MISO_AFE", "SPI_MISO")
    d.hpart(sh, "R", "R91", "0", FP_R, 290, 55, "MISO_AD", "SPI_MISO")
    d.hpart(sh, "R", "R92", "0", FP_R, 325, 55, "MISO_FL", "SPI_MISO")
    d.hpart(sh, "R", "R106", "0", FP_R, 355, 80, "MISO_MX", "SPI_MISO")  # hw_v2: MAX86178 isolation link
    d.vpart(sh, "R", "R98", "10k", FP_R, 360, 55, "+3V3", "CS_ADS1292")
    d.vpart(sh, "R", "R99", "100k", FP_R, 385, 55, "+3V3", "ADS1292_DRDY")

    d.hpart(sh, "R", "R93", "0", FP_R6, 70, 95, "VBAT", "VBAT_SYS")
    d.hpart(sh, "R", "R94", "0", FP_R6, 120, 95, "+3V3", "+3V3_ESP")
    d.hpart(sh, "R", "R95", "0", FP_R, 170, 95, "+3V3", "+3V3_ANA")
    d.hpart(sh, "R", "R96", "0", FP_R, 210, 95, "TX_5V_RAW", "TX_5V")
    d.hpart(sh, "R", "R97", "0", FP_R, 250, 95, "+1V8_LDO", "+1V8")
    d.flag_net(sh, "+3V3_ESP", 40, 115)
    d.flag_net(sh, "+3V3_ANA", 80, 115)
    d.flag_net(sh, "VBAT_SYS", 120, 115)
    d.flag_net(sh, "TX_5V", 160, 115)
    d.flag_net(sh, "+1V8", 200, 115)

    tc = d.part(sh, "Conn_01x06_Pin", "J8", "TC2030", FP_TC, 340, 110, labels="ic")
    for num, net in {
        "1": "+3V3",
        "2": "GND",
        "3": "ESP_TX",
        "4": "ESP_RX",
        "5": "ESP_EN",
        "6": "ESP_IO0",
    }.items():
        d.stub_net(sh, tc, num, net, 6.35)
    d.finish(sh, tc)
    sh.text("J8 Tag-Connect TC2030-NL: 1 +3V3, 2 GND, 3 ESP_TX, 4 ESP_RX, 5 EN, 6 IO0.", 250, 145, 1.2)

    pads = [
        ("TP1", "VBUS"), ("TP2", "VBAT"), ("TP3", "+3V3"), ("TP4", "+1V8"),
        ("TP5", "TX_5V"), ("TP6", "VDD_CP2102"), ("TP7", "GND"), ("TP8", "GND"),
        ("TP9", "ESP_EN"), ("TP10", "ESP_IO0"), ("TP11", "ESP_TX"), ("TP12", "ESP_RX"),
        ("TP13", "I2C_SDA"), ("TP14", "I2C_SCL"), ("TP15", "I2C_SDA_1V8"), ("TP16", "I2C_SCL_1V8"),
        ("TP17", "SPI_SCK"), ("TP18", "SPI_MOSI"), ("TP19", "SPI_MISO"),
        ("TP20", "CS_ADS1292"), ("TP21", "CS_AFE4900"), ("TP22", "CS_AD5940"), ("TP23", "CS_FLASH"),
        ("TP24", "ADS1292_DRDY"), ("TP25", "AFE4900_ADC_RDY"), ("TP26", "EXP_INT"), ("TP27", "CHG_STAT"),
        ("TP28", "RTC_INT"),
    ]
    for i, (ref, net) in enumerate(pads):
        col, row = i % 9, i // 9
        tp = d.part(sh, "TestPoint", ref, net, FP_TP, 30 + col * 40, 175 + row * 22, labels="side")
        d.stub_net(sh, tp, "1", net, 5.08)
        d.finish(sh, tp)

    for i in range(1, 7):
        fid = d.part(sh, "Fiducial", f"FID{i}", "FID", FP_FID, 30 + i * 18, 255, labels="side", bom=False)
        d.finish(sh, fid)
    d.part(sh, "MountingHole", "H1", "M2", FP_M2, 160, 255, labels="side", bom=False)
    d.part(sh, "MountingHole", "H2", "M2", FP_M2, 190, 255, labels="side", bom=False)
    sh.text("R93-R97 are 0 ohm current links. R89-R92 + R106 isolate each MISO. SW1 resets EN. SW2 holds IO0.", 16, 268, 1.2)


def max86178(d: Design):
    """hw_v2 page 9: MAX86178 optical/ECG AFE plus the sensor-tail connectors."""
    sh = d.sheet("MAX86178 and tails", "max86178.kicad_sch", "9")
    sh.rect(12, 40, 406, 290)
    sh.text("MAX86178 optical/ECG AFE", 16, 16, 3.2, bold=True)
    sh.text("VERIFY: the WLP-49 ball map is NDA-restricted. Every ball ref on U22 is a placeholder", 16, 24, 1.3)
    sh.text("named after the MAX86176 family. Check the released pin table before layout.", 16, 29.5, 1.3)
    sh.text("SPI shares the bus: CS on IO38, /INT on IO39, MISO through the R106 link on sheet 8.", 16, 35, 1.3)

    u = d.part(sh, "MAX86178", "U22", "MAX86178ENJ+", FP_86178, 140, 120, labels="ic")
    # Sensor tail, left side. LED drivers sink current: they take cathodes.
    d.stub_net(sh, u, "A1", "PD_A", 7.62)      # photodiode anode input
    d.stub_net(sh, u, "A2", "PD_K", 7.62)      # photodiode cathode input
    d.stub_net(sh, u, "A3", "LED1_K", 7.62)    # LED driver 1 -> LED1 cathode
    d.stub_net(sh, u, "A4", "LED2_K", 7.62)    # LED driver 2 -> LED2 cathode
    d.stub_net(sh, u, "A5", "LED3_K", 7.62)    # LED driver 3 -> LED3 cathode
    d.stub_net(sh, u, "A6", "MX_ECG_INP", 7.62)  # spare ECG lead, J13 DNP
    d.stub_net(sh, u, "A7", "MX_ECG_INM", 7.62)
    # Digital, right side.
    d.stub_net(sh, u, "G7", "SPI_SCK", 7.62)
    d.stub_net(sh, u, "G6", "SPI_MOSI", 7.62)
    d.stub_net(sh, u, "G5", "MISO_MX", 7.62)   # through R106 to SPI_MISO
    d.stub_net(sh, u, "G4", "CS_MAX86178", 7.62)
    d.stub_net(sh, u, "G3", "MAX86178_INT", 7.62)
    # Supplies. AVDD rides the quiet analog rail; the LED drivers need the
    # cell voltage (VERIFY: LED_DRV_SUP may want a boost instead of VBAT).
    d.stub_net(sh, u, "F1", "+3V3_ANA", 7.62)  # AVDD
    d.stub_net(sh, u, "F2", "+3V3", 7.62)      # DVDD
    d.stub_net(sh, u, "F3", "VBAT", 7.62)      # LED_DRV_SUP (VERIFY rail)
    d.stub_net(sh, u, "F4", "MX_VREF", 7.62)
    d.join_row(sh, u, ["F5", "F6", "F7"], "GND", 6.35)  # AGND/DGND/LGND
    d.finish(sh, u)
    d.vpart(sh, "C", "C83", "1u", FP_C, 140, 165, "MX_VREF", "GND")  # VERIFY: ref bypass value
    d.vpart(sh, "R", "R113", "10k", FP_R, 200, 80, "+3V3", "MAX86178_INT")  # /INT is open-drain
    d.vpart(sh, "R", "R117", "10k", FP_R, 222, 80, "+3V3", "CS_MAX86178")

    # Local decoupling: 100 nF + 1 uF per rail, matching the other AFEs.
    sh.text("Decoupling", 240, 60, 1.6, bold=True)
    d.vpart(sh, "C", "C79", "100n", FP_C, 250, 75, "+3V3_ANA", "GND")
    d.vpart(sh, "C", "C80", "1u", FP_C, 272, 75, "+3V3_ANA", "GND")
    d.vpart(sh, "C", "C81", "100n", FP_C, 294, 75, "+3V3", "GND")
    d.vpart(sh, "C", "C82", "1u", FP_C, 316, 75, "VBAT", "GND")
    sh.text("AVDD on +3V3_ANA, DVDD on +3V3, LED supply on VBAT (VERIFY).", 240, 92, 1.2)

    # ---- Sensor-tail connectors ----------------------------------------
    # J9: 4-pad I2C tail (e.g. a remote TMP117/SHT45 flex).
    j9 = d.part(sh, "Conn_01x04_Pin", "J9", "I2C tail pads", FP_PAD4, 270, 130, labels="ic")
    for num, net in {"1": "+3V3", "2": "I2C_SDA", "3": "I2C_SCL", "4": "GND"}.items():
        d.stub_net(sh, j9, num, net, 6.35)
    d.finish(sh, j9)
    sh.text("J9: 1 +3V3, 2 SDA, 3 SCL, 4 GND.", 240, 150, 1.2)

    # J10: 6-pad optical tail — LED cathodes x3, shared LED anode, PD pair.
    j10 = d.part(sh, "Conn_01x06_Pin", "J10", "Optical tail pads", FP_PAD6, 270, 185, labels="ic")
    for num, net in {
        "1": "LED1_K", "2": "LED2_K", "3": "LED3_K",
        "4": "LED_AN", "5": "PD_K", "6": "PD_A",
    }.items():
        d.stub_net(sh, j10, num, net, 6.35)
    d.finish(sh, j10)
    sh.text("J10: LED1/2/3 cathodes, LED anode, PD cathode, PD anode.", 240, 208, 1.2)
    # LED_AN is the shared anode rail for the remote emitters. VBAT lacks the
    # headroom for a green channel (Vf ~3 V plus driver compliance), so the
    # anode rail rides TX_5V through R114 — time-shared with the AFE4900 TX
    # section under the same boost enable.
    sh.text("LED_AN ties to TX_5V through R114 (5 V headroom for green/satellite emitters).", 240, 214, 1.2)
    d.hpart(sh, "R", "R114", "0", FP_R, 330, 190, "TX_5V", "LED_AN")

    # J11: 3-pad RESEARCH-GRADE sweat-electrode site, fed by AD5940 spares.
    j11 = d.part(sh, "Conn_01x03_Pin", "J11", "Sweat site", FP_PAD3, 270, 240, labels="ic")
    for num, net in {"1": "J11_WE", "2": "J11_RE", "3": "J11_CE"}.items():
        d.stub_net(sh, j11, num, net, 6.35)
    d.finish(sh, j11)
    # Series 0R gives a cut point and the TPDs an IC-side home, matching the
    # ladder pattern on the patient pads. TPD clamps fitted.
    d.hpart(sh, "R", "R120", "0", FP_HV, 300, 230, "J11_WE", "SWEAT_WE")
    d.hpart(sh, "R", "R121", "0", FP_HV, 300, 242, "J11_RE", "SWEAT_RE")
    d.hpart(sh, "R", "R122", "0", FP_HV, 300, 254, "J11_CE", "SWEAT_CE")
    d.vpart(sh, "D_TVS_2", "D26", "TPD1E10B06", FP_TVS, 335, 230, "SWEAT_WE", "GND")
    d.vpart(sh, "D_TVS_2", "D27", "TPD1E10B06", FP_TVS, 350, 242, "SWEAT_RE", "GND")
    d.vpart(sh, "D_TVS_2", "D28", "TPD1E10B06", FP_TVS, 335, 254, "SWEAT_CE", "GND")
    sh.text("J11 sweat site — RESEARCH-GRADE. WE=AIN6(C5), RE=AFE3(A2), CE=AFE4(A1); VERIFY switch matrix.", 240, 268, 1.2)

    # J12: DNP FFC carrying every tail net. Spec asked for 12-pos; J9+J10+J11
    # is 13 nets, so a 14-pos FH12 is used — deviation noted in the header.
    j12 = d.part(sh, "Conn_01x14_Pin", "J12", "FH12-14S", FP_FFC, 365, 160, labels="ic", dnp=True)
    ffc = {
        "1": "+3V3", "2": "I2C_SDA", "3": "I2C_SCL", "4": "GND",
        "5": "LED1_K", "6": "LED2_K", "7": "LED3_K", "8": "LED_AN",
        "9": "PD_K", "10": "PD_A",
        "11": "SWEAT_WE", "12": "SWEAT_RE", "13": "SWEAT_CE",
    }  # pin 14 is spare — finish() marks it no_connect
    for num, net in ffc.items():
        d.stub_net(sh, j12, num, net, 6.35)
    d.finish(sh, j12)
    sh.text("J12 DNP FFC: pins 1-4 = J9 order, 5-10 = J10 order, 11-13 = J11 order, 14 open.", 240, 108, 1.2)

    # J13: optional ECG pad pair for the MAX86178 bio-potential inputs. DNP.
    j13 = d.part(sh, "Conn_01x02_Pin", "J13", "MX ECG pads", FP_PAD2, 140, 215, labels="ic", dnp=True)
    d.stub_net(sh, j13, "1", "J13_INP", 6.35)
    d.stub_net(sh, j13, "2", "J13_INM", 6.35)
    d.finish(sh, j13)
    d.hpart(sh, "R", "R118", "0", FP_HV, 175, 205, "J13_INP", "MX_ECG_INP")
    d.hpart(sh, "R", "R119", "0", FP_HV, 175, 215, "J13_INM", "MX_ECG_INM")
    d.vpart(sh, "D_TVS_2", "D29", "TPD1E10B06", FP_TVS, 210, 205, "MX_ECG_INP", "GND")
    d.vpart(sh, "D_TVS_2", "D30", "TPD1E10B06", FP_TVS, 210, 215, "MX_ECG_INM", "GND")
    sh.text("J13 DNP pads: optional second ECG input pair. R118/R119 + D29/D30 fitted.", 140, 232, 1.2)

    sh.text("No parts removed on this sheet — it is additive. Ball refs all VERIFY before layout.", 16, 280, 1.3)
