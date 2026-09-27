#!/usr/bin/env python3
"""VitalQ hw_v1 schematic. Every part is placed; nets are short stubs plus labels.

Power symbols and global labels of the same name are one net (checked).
"""

from __future__ import annotations

from schutil import Sheet, label_angle, load_lib, r2

FP_R = "Resistor_SMD:R_0402_1005Metric"
FP_C = "Capacitor_SMD:C_0402_1005Metric"
FP_M2 = "MountingHole:MountingHole_2.2mm_M2"
FP_USB = "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12"
FP_BAT = "vitalq:Pads_LiPo"
FP_FSR = "vitalq:Pads_FSR"
FP_H3 = "vitalq:Pads_ECG"
FP_H4 = "vitalq:Pads_EDA"
FP_H6 = "vitalq:Pads_PPG"
FP_MCP = "snap:SOT95P280X145-5N"
FP_AFE = "snap:BGA30N40P5X6_260X210X50"
FP_AD = "snap:BGA56C40P8X7_416X356X55"
FP_ADS = "snap:QFN40P400X400X100-33N-D"
FP_AS = "snap:AS7341DLGT"
FP_TMP = "snap:SON65P200X200X80-7N"
FP_MLX = "snap:MLX90632SLDDCB100SP"
FP_IMU = "snap:QFN_LSM6DSV80XTR_STM"
FP_C6 = "Capacitor_SMD:C_0603_1608Metric"
FP_63802 = "Package_SON:WSON-10-1EP_2x3mm_P0.5mm_EP0.84x2.4mm"
FP_61240 = "vitalq:TPS61240_YFF"
FP_L = "vitalq:L_DFE201612E"
FP_SFH = "vitalq:SFH7072"
FP_GAUGE = "vitalq:MAX17048_WLP"
FP_FLASH = "vitalq:W25Q512_WSON8"
FP_EXP = "vitalq:TCA6408A_RSV"
FP_TVS = "vitalq:TPD1E10B06_DPY"
FP_FET = "Package_DFN_QFN:Texas_PicoStar_DFN-3_0.69x0.60mm"
FP_HV = "Resistor_SMD:R_2512_6332Metric"
FP_GDT = "vitalq:GDT_S30"
FP_WHITE = "vitalq:NF2W757G"
FP_IR = "LED_SMD:LED_0402_1005Metric"

RAILS = {"+3V3", "+1V8", "VBUS", "GND"}


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

    def hpart(self, sh, sym, ref, value, fp, x, y, left, right):
        """Rotation 90: pin 2 on the left, pin 1 on the right."""
        inst = self.part(sh, sym, ref, value, fp, x, y, 90, labels="side")
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
    d = Design(lib)
    power(d)
    usb(d)
    mcu(d)
    afe(d)
    ad(d)
    ads(d)
    i2c(d)
    # Navigation boxes along the bottom of the power sheet.
    x = 18
    for ch in d.children:
        ch.nav_at = (x, 232)
        ch.nav_size = (60, 16)
        x += 66
    return d


def power(d: Design):
    sh = d.sheet("Power and charging", "vitalq_hw_v1.kicad_sch", "1")
    # Title block is x 310-418, y 2-34. Keep the frame above it.
    sh.rect(12, 40, 406, 275)
    sh.text("Power and charging", 16, 18, 3.2, bold=True)
    sh.text("USB-C, LiPo charger, 3.3 V rail, 1.8 V rail", 16, 26, 1.6)

    j = d.part(sh, "USB_C_Receptacle_USB2.0_16P", "J1", "USB-C", FP_USB, 48, 78, labels="ic")
    # Right-hand signal pins.
    d.stub_net(sh, j, "A4", "VBUS", 10.16)  # stacked VBUS
    d.stub_net(sh, j, "A5", "USB_CC1", 7.62)
    d.stub_net(sh, j, "B5", "USB_CC2", 7.62)
    d.stub_net(sh, j, "A6", "USB_DP", 7.62)
    d.stub_net(sh, j, "B6", "USB_DP", 7.62)
    d.stub_net(sh, j, "A7", "USB_DM", 7.62)
    d.stub_net(sh, j, "B7", "USB_DM", 7.62)
    d.join_row(sh, j, ["A1", "S1"], "GND", 5.08)  # GND stack + shield, both face down
    d.finish(sh, j)

    d.vpart(sh, "R", "R1", "5.1k", FP_R, 92, 118, "USB_CC1", "GND")
    d.vpart(sh, "R", "R2", "5.1k", FP_R, 108, 118, "USB_CC2", "GND")

    u = d.part(sh, "MCP73831T-2ACI_OT", "U2", "MCP73831T-2ACI/OT", FP_MCP, 168, 78, labels="ic")
    d.stub_net(sh, u, "4", "VBUS", 6.35)  # VDD top
    d.stub_net(sh, u, "2", "GND", 6.35)  # VSS bottom
    d.stub_net(sh, u, "3", "VBAT", 7.62)  # VBAT right, power_out
    d.stub_net(sh, u, "5", "CHG_PROG", 7.62)  # PROG left
    d.stub_net(sh, u, "1", "CHG_STAT", 7.62)  # STAT, open drain
    d.finish(sh, u)
    # Q2 opens R3, so PROG floats and DS20001984 5.2.2 disables charge.
    d.vpart(sh, "R", "R3", "10k", FP_R, 148, 108, "CHG_PROG", "PROG_RTN")
    q2 = d.part(sh, "CSD13380F3", "Q2", "CSD13380F3", FP_FET, 210, 118, labels="ic")
    d.stub_net(sh, q2, "1", "CHG_EN", 7.62)
    d.stub_net(sh, q2, "3", "PROG_RTN", 6.35)
    d.stub_net(sh, q2, "2", "GND", 6.35)
    d.finish(sh, q2)
    d.vpart(sh, "R", "R59", "100k", FP_R, 230, 130, "CHG_EN", "GND")
    d.vpart(sh, "R", "R49", "10k", FP_R, 168, 130, "+3V3", "CHG_STAT")

    # TPS63802 replaces the XC6206. EN tied to VIN so the 3.3 V rail is up
    # before the expander has a supply. MODE low is power-save. PG is open.
    u = d.part(sh, "TPS63802", "U3", "TPS63802DLAR", FP_63802, 250, 78, labels="ic")
    d.stub_net(sh, u, "10", "VBAT", 7.62)
    d.stub_net(sh, u, "1", "VBAT", 7.62)
    d.stub_net(sh, u, "2", "GND", 7.62)
    d.stub_net(sh, u, "6", "+3V3", 8.89)
    d.stub_net(sh, u, "9", "SW_L1", 8.89)
    d.stub_net(sh, u, "7", "SW_L2", 8.89)
    d.stub_net(sh, u, "4", "FB_3V3", 8.89)
    d.join_row(sh, u, ["8", "3", "11"], "GND", 6.35)  # 11 is the exposed pad
    d.finish(sh, u)  # PG
    d.hpart(sh, "L", "L1", "0.47u", FP_L, 330, 70, "SW_L1", "SW_L2")
    d.vpart(sh, "R", "R23", "560k", FP_R, 360, 100, "+3V3", "FB_3V3")
    d.vpart(sh, "R", "R24", "100k", FP_R, 382, 100, "FB_3V3", "GND")

    u = d.part(sh, "TPS7A2018PDBVR", "U4", "TPS7A2018PDBVR", "Package_TO_SOT_SMD:SOT-23-5", 345, 70, labels="ic")
    d.join_row(sh, u, ["1", "3"], "+3V3", 6.35)  # IN and EN, both face left — wait, they face LEFT not up.
    # join_row on left-facing pins connects them vertically. Both are +3V3. Good.
    d.stub_net(sh, u, "5", "+1V8", 8.89)  # OUT right, power_out
    d.stub_net(sh, u, "2", "GND", 6.35)
    d.finish(sh, u)  # NC pin is type no_connect

    bat = d.part(sh, "Conn_01x03_Pin", "J2", "LiPo pads", FP_BAT, 168, 168, labels="ic")
    d.stub_net(sh, bat, "1", "VBAT", 7.62)
    d.stub_net(sh, bat, "2", "GND", 7.62)
    d.stub_net(sh, bat, "3", "NTC_ADC", 7.62)
    d.finish(sh, bat)
    d.vpart(sh, "R", "R60", "10k", FP_R, 250, 230, "+3V3", "NTC_ADC")
    d.vpart(sh, "R", "R61", "10k", FP_R, 275, 230, "NTC_ADC", "GND", dnp=True)

    # Decoupling directly under the regulators.
    sh.text("Decoupling", 230, 108, 1.8, bold=True)
    d.vpart(sh, "C", "C1", "4.7u", FP_C, 230, 155, "VBUS", "GND")
    d.vpart(sh, "C", "C2", "4.7u", FP_C, 250, 155, "VBAT", "GND")
    d.vpart(sh, "C", "C3", "1u", FP_C, 270, 155, "VBAT", "GND")
    d.vpart(sh, "C", "C4", "1u", FP_C, 290, 155, "+3V3", "GND")
    d.vpart(sh, "C", "C6", "1u", FP_C, 310, 155, "+1V8", "GND")
    d.vpart(sh, "C", "C44", "10u", FP_C6, 340, 155, "VBAT", "GND")
    d.vpart(sh, "C", "C45", "22u", FP_C6, 365, 155, "+3V3", "GND")

    gge = d.part(sh, "MAX17048", "U17", "MAX17048G+T10", FP_GAUGE, 70, 175, labels="ic")
    d.stub_net(sh, gge, "A2", "VBAT", 7.62)
    d.stub_net(sh, gge, "A3", "VBAT", 7.62)
    d.stub_net(sh, gge, "B1", "I2C_SDA", 7.62)
    d.stub_net(sh, gge, "B2", "I2C_SCL", 7.62)
    d.join_row(sh, gge, ["A1", "A4", "B3"], "GND", 6.35)
    d.finish(sh, gge)  # ALRT
    d.vpart(sh, "C", "C58", "100n", FP_C, 130, 185, "VBAT", "GND")
    d.vpart(sh, "R", "R50", "100k", FP_R, 90, 130, "VBUS", "VBUS_DET")
    d.vpart(sh, "R", "R51", "200k", FP_R, 112, 130, "VBUS_DET", "GND")

    sh.text("J2 is three flat pads (BAT+ BAT- NTC), not a header. No power switch.", 16, 248, 1.4)
    sh.text("R61 is the on-board 10k NTC, DNP. Fit it or the cell NTC, not both. Q2 floats PROG until CHG_EN is high.", 16, 256, 1.4)
    sh.text("Charge stays off at power-up: the expander port is Hi-Z and R59 holds the gate low.", 16, 264, 1.4)
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
    sh.rect(12, 40, 400, 250)
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
    d.stub_net(sh, u, "5", "USB_DM", 8.89)
    d.stub_net(sh, u, "4", "USB_DP", 8.89)
    d.join_row(sh, u, ["3"], "GND", 6.35)  # stacked with pad 29
    d.stub_net(sh, u, "26", "ESP_RX", 10.16)  # TXD -> ESP RX
    d.stub_net(sh, u, "25", "ESP_TX", 10.16)  # RXD <- ESP TX
    d.stub_net(sh, u, "9", "CP_RST", 8.89)  # RSTb, 1k to VDD
    for n in ("23", "27", "1", "2"):  # ~CTS ~DSR ~DCD ~RI/CLK
        d.stub_net(sh, u, n, "VDD_CP2102", 10.16)
    d.finish(sh, u)

    sh.text("Decoupling", 40, 48, 1.8, bold=True)
    d.vpart(sh, "C", "C9", "4.7u", FP_C, 55, 70, "VDD_CP2102", "GND")
    d.vpart(sh, "C", "C62", "100n", FP_C, 78, 100, "VDD_CP2102", "GND")
    d.vpart(sh, "C", "C10", "1u", FP_C, 100, 70, "VBUS", "GND")
    d.vpart(sh, "C", "C63", "100n", FP_C, 122, 70, "VBUS", "GND")
    d.vpart(sh, "R", "R62", "1k", FP_R, 145, 100, "VDD_CP2102", "CP_RST")
    # Fig 2.5: 22.1k from the connector, 47.5k to ground. At 5.0 V the pin is 3.41 V.
    d.vpart(sh, "R", "R63", "22.1k", FP_R, 55, 145, "VBUS", "CP_VBUS")
    d.vpart(sh, "R", "R64", "47.5k", FP_R, 90, 145, "CP_VBUS", "GND")
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


def mcu(d: Design):
    sh = d.sheet("MCU and FSR", "mcu.kicad_sch", "3")
    sh.rect(12, 40, 300, 290)
    sh.rect(308, 40, 406, 280)
    sh.text("MCU", 16, 16, 3.2, bold=True)
    sh.text("ESP32-WROOM-32E-N8R2. GPIO16 is PSRAM and stays inside the module.", 16, 24, 1.5)
    sh.text("Straps and FSR", 314, 46, 2.2, bold=True)

    u = d.part(
        sh,
        "ESP32-WROOM-32E-R2",
        "U1",
        "ESP32-WROOM-32E-N8R2",
        "RF_Module:ESP32-WROOM-32E",
        150,
        140,
        labels="ic",
    )
    pins = {
        "3": "ESP_EN",
        "4": "FSR_ADC",
        "5": "NTC_ADC",
        "25": "ESP_IO0",
        "35": "ESP_TX",
        "24": "NC",
        "34": "ESP_RX",
        "26": "CS_ADS1292",
        "29": "CS_AFE4900",
        "14": "NC",
        "16": "AFE4900_ADC_RDY",
        "13": "AD5940_GPIO0",
        "23": "CS_AD5940",
        "28": "NC",
        "30": "SPI_SCK",
        "31": "SPI_MISO",
        "33": "I2C_SDA",
        "36": "I2C_SCL",
        "37": "SPI_MOSI",
        "10": "NC",
        "11": "CS_FLASH",
        "12": "NC",
        "8": "LSM6_INT1",
        "9": "TMP117_ALERT",
        "6": "ADS1292_DRDY",
        "7": "EXP_INT",
    }
    d.side_map(sh, u, pins, 8.89)
    d.join_row(sh, u, ["2"], "+3V3", 6.35)
    d.join_row(sh, u, ["1"], "GND", 6.35)
    d.finish(sh, u)

    sh.text("Module decoupling", 40, 40, 1.6, bold=True)
    d.vpart(sh, "C", "C7", "10u", FP_C, 55, 58, "+3V3", "GND")
    d.vpart(sh, "C", "C8", "100n", FP_C, 78, 58, "+3V3", "GND")

    d.vpart(sh, "R", "R5", "10k", FP_R, 330, 62, "+3V3", "ESP_EN")
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
    d.stub_net(sh, flash, "2", "SPI_MISO", 7.62)
    d.stub_net(sh, flash, "3", "+3V3", 7.62)  # /WP
    d.stub_net(sh, flash, "5", "SPI_MOSI", 7.62)
    d.stub_net(sh, flash, "6", "SPI_SCK", 7.62)
    d.stub_net(sh, flash, "7", "+3V3", 7.62)  # /HOLD
    d.stub_net(sh, flash, "8", "+3V3", 7.62)
    d.join_row(sh, flash, ["4", "9"], "GND", 6.35)
    d.finish(sh, flash)
    d.vpart(sh, "C", "C59", "100n", FP_C, 140, 250, "+3V3", "GND")

    exp = d.part(sh, "TCA6408ARSV", "U19", "TCA6408ARSVR", FP_EXP, 340, 200, labels="ic")
    d.stub_net(sh, exp, "1", "+3V3", 6.35)  # /RESET held out of reset
    d.stub_net(sh, exp, "2", "AFE4900_RESETZ", 7.62)
    d.stub_net(sh, exp, "3", "ADS1292_PWDN", 7.62)
    d.stub_net(sh, exp, "4", "AD5940_RESET", 7.62)
    d.stub_net(sh, exp, "5", "TX5_EN", 7.62)
    d.stub_net(sh, exp, "7", "IR_GATE", 7.62)
    d.stub_net(sh, exp, "8", "CHG_STAT", 7.62)
    d.stub_net(sh, exp, "9", "VBUS_DET", 7.62)
    d.stub_net(sh, exp, "10", "CHG_EN", 7.62)
    d.stub_net(sh, exp, "11", "EXP_INT", 7.62)
    d.stub_net(sh, exp, "12", "I2C_SCL", 7.62)
    d.stub_net(sh, exp, "13", "I2C_SDA", 7.62)
    d.stub_net(sh, exp, "16", "GND", 7.62)  # ADDR -> 0x20
    d.join_row(sh, exp, ["14", "15"], "+3V3", 6.35)
    d.join_row(sh, exp, ["6"], "GND", 6.35)
    d.finish(sh, exp)
    d.vpart(sh, "C", "C60", "100n", FP_C, 390, 250, "+3V3", "GND")

    sh.text("GPIO2 and GPIO12 are open so the straps stay low. GPIO15 (CS_AD5940) is pulled up.", 16, 200, 1.4)
    sh.text("GPIO6-11 are the module flash bus and have no symbol pins. ADC1 is GPIO36 and GPIO39.", 16, 208, 1.4)
    sh.text("EN and IO0 have pull-ups only. Download uses the module pads. There are no buttons.", 16, 216, 1.4)
    sh.text("R7 is the only chip-select pull-up. It is required because GPIO15 must be high at reset.", 16, 224, 1.4)


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
        "C1": "+3V3",
        "D1": "GND",
        "C4": "+3V3",
        "D5": "GND",
        "E1": "AFE4900_RESETZ",
        "E2": "SPI_MISO",
        "E3": "CS_AFE4900",
        "F2": "SPI_MOSI",
        "F3": "SPI_SCK",
        "F4": "AFE4900_ADC_RDY",
        "A4": "+3V3",
        "B4": "GND",
        "F1": "AFE_CLK",
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
    d.stub_net(sh, bst, "A1", "VBAT", 7.62)
    d.stub_net(sh, bst, "C1", "TX5_EN", 7.62)
    d.stub_net(sh, bst, "B1", "TX_SW", 7.62)
    d.stub_net(sh, bst, "B2", "TX_5V", 7.62)
    d.stub_net(sh, bst, "C2", "TX_5V", 7.62)  # fixed 5 V, FB senses VOUT
    d.stub_net(sh, bst, "A2", "GND", 6.35)
    d.finish(sh, bst)
    d.hpart(sh, "L", "L2", "1.0u", FP_L, 130, 185, "TX_SW", "TX_5V")
    d.vpart(sh, "C", "C46", "2.2u", FP_C, 40, 250, "VBAT", "GND")
    d.vpart(sh, "C", "C47", "4.7u", FP_C6, 70, 250, "TX_5V", "GND")
    d.vpart(sh, "R", "R25", "100k", FP_R, 100, 250, "TX5_EN", "GND")

    sh.text("Bias and decoupling", 160, 210, 1.6, bold=True)
    d.vpart(sh, "C", "C11", "1u", FP_C, 170, 235, "+3V3", "GND")
    d.vpart(sh, "C", "C13", "100n", FP_C, 190, 235, "+3V3", "GND")
    d.vpart(sh, "C", "C14", "1u", FP_C, 210, 235, "TX_5V", "GND")
    d.vpart(sh, "C", "C15", "100n", FP_C, 230, 235, "AFE_BG", "GND")
    d.vpart(sh, "R", "R9", "10k", FP_R, 255, 235, "AFE4900_RESETZ", "GND")
    d.vpart(sh, "R", "R10", "1k", FP_R, 278, 235, "AFE_CLK", "GND")

    # AFE ECG lead, AC-coupled. Bias is on the IC side of the capacitors.
    d.hpart(sh, "C", "C56", "100n", FP_C, 320, 255, "AFE_P_AC", "AFE_INP")
    d.hpart(sh, "C", "C57", "100n", FP_C, 370, 255, "AFE_N_AC", "AFE_INM")
    d.vpart(sh, "R", "R43", "10M", FP_R, 300, 275, "+3V3", "AFE_INP")
    d.vpart(sh, "R", "R44", "10M", FP_R, 322, 275, "AFE_INP", "GND")
    d.vpart(sh, "R", "R45", "10M", FP_R, 360, 275, "+3V3", "AFE_INM")
    d.vpart(sh, "R", "R46", "10M", FP_R, 382, 275, "AFE_INM", "GND")
    sh.text("C11 RX_SUP, C13 IO_SUP, C14 local TX_SUP, C47 is the boost 4.7 uF. C15 is BG.", 16, 270, 1.3)
    sh.text("R9 holds RESETZ down until the expander drives it. R10 holds CLK low. AFE RLD_OUT is open.", 16, 276, 1.3)


def ad(d: Design):
    sh = d.sheet("AD5940 EDA", "ad5940.kicad_sch", "5")
    sh.rect(12, 40, 406, 290)
    sh.text("AD5940 EDA / BioZ", 16, 14, 3.2, bold=True)
    sh.text("Fig 54 / AN-1557: 1k and 15 nF in series on CE0, 470 nF on SE0, plus RCAL. Flex notes are in the README.", 16, 22, 1.3)

    u = d.part(sh, "AD5940BCBZ-RL7", "U7", "AD5940BCBZ-RL7", FP_AD, 250, 40, labels="ic")
    nets = {
        "A1": "NC",
        "A2": "NC",
        "A3": "NC",
        "A4": "+3V3",
        "A5": "VREF_1V82",
        "A6": "SE0",
        "A7": "CE0",
        "A8": "RE0",
        "B1": "RCAL1",
        "B2": "NC",
        "B3": "NC",
        "B4": "AIN4_LPF0",
        "B5": "NC",
        "B6": "DE0",
        "B7": "VZERO0",
        "B8": "RC0_1",
        "C1": "RCAL0",
        "C2": "NC",
        "C4": "GND",
        "C5": "NC",
        "C7": "VBIAS0",
        "C8": "RC0_0",
        "D1": "VBIAS_CAP",
        "D2": "NC",
        "D5": "GND",
        "D6": "NC",
        "D7": "VREF_2V5",
        "D8": "AVDD_REG",
        "E1": "NC",
        "E2": "NC",
        "E3": "GND",
        "E4": "GND",
        "E5": "GND",
        "E6": "GND",
        "E7": "SPI_MOSI",
        "E8": "SPI_MISO",
        "F1": "AD5940_RESET",
        "F2": "+3V3",
        "F3": "+3V3",
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
    sh.text("J6 solder pads. No header.", 16, 44, 1.5, bold=True)

    sh.text("Decoupling, RCAL, RC0", 16, 100, 1.6, bold=True)
    caps = [
        ("C16", "1u", "+3V3"),
        ("C18", "100n", "+3V3"),
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
    d.vpart(sh, "R", "R12", "10", FP_R, 40, 200, "+3V3", "IOVDD")
    d.vpart(sh, "R", "R11", "10k", FP_R, 62, 200, "AD5940_RESET", "GND")
    d.hpart(sh, "R", "R13", "1k", FP_R, 110, 203, "RCAL0", "RCAL1")
    d.hpart(sh, "C", "C27", "100n", FP_C, 145, 203, "RC0_0", "RC0_1")
    d.flag_net(sh, "IOVDD", 175, 203)

    sh.text("C16 AVDD, C18 DVDD, C42 AVDD_REG, C19 IOVDD after R12, C20-C26 are the reference and bias caps.", 16, 236, 1.35)
    sh.text("R13 is RCAL. C27 sits between RC0_0 and RC0_1. XTAL pins are open: the internal oscillator is used.", 16, 242, 1.35)
    d.hpart(sh, "R", "R39", "1k", FP_R, 220, 210, "CE0", "CE_ISO")
    d.hpart(sh, "C", "C54", "15n", FP_C, 280, 210, "CE_ISO", "EDA_CE_PAD")
    d.hpart(sh, "C", "C55", "470n", FP_C, 220, 235, "SE0", "SE_ISO")
    d.hpart(sh, "R", "R40", "1k", FP_R, 280, 235, "SE_ISO", "EDA_SE_PAD")
    d.hpart(sh, "R", "R41", "1k", FP_R, 340, 210, "RE0", "EDA_RE_PAD")
    d.hpart(sh, "R", "R42", "1k", FP_R, 340, 235, "DE0", "EDA_DE_PAD")
    # TPD sits on the IC side of the series parts. The GDT is on the pad.
    d.vpart(sh, "D_TVS_2", "D6", "TPD1E10B06", FP_TVS, 380, 175, "CE_ISO", "GND")
    d.vpart(sh, "D_TVS_2", "D7", "TPD1E10B06", FP_TVS, 400, 175, "SE_ISO", "GND")
    d.vpart(sh, "D_TVS_2", "D8", "TPD1E10B06", FP_TVS, 380, 200, "RE0", "GND")
    d.vpart(sh, "D_TVS_2", "D9", "TPD1E10B06", FP_TVS, 400, 200, "DE0", "GND")
    d.vpart(sh, "GDT", "D17", "S30-A90X", FP_GDT, 50, 275, "EDA_CE_PAD", "GND")
    d.vpart(sh, "GDT", "D18", "S30-A90X", FP_GDT, 100, 275, "EDA_SE_PAD", "GND")
    d.vpart(sh, "GDT", "D19", "S30-A90X", FP_GDT, 150, 275, "EDA_RE_PAD", "GND")
    d.vpart(sh, "GDT", "D20", "S30-A90X", FP_GDT, 200, 275, "EDA_DE_PAD", "GND")
    sh.text("RLIMIT is R39. CISO1 is C54. CISO2 is C55. R13 stays the 1k RCAL. D17-D20 are on the pads.", 16, 255, 1.3)
    sh.text("Other AIN, AFE and GPIO balls are open. GPIO0 is the only digital sideband to the ESP32.", 16, 254, 1.35)


def ads(d: Design):
    sh = d.sheet("ADS1292R ECG", "ads1292.kicad_sch", "6")
    sh.rect(12, 40, 406, 290)
    sh.text("ADS1292R ECG and respiration", 16, 14, 3.2, bold=True)
    sh.text("Channel 1 is respiration (SBAS502C Fig 68). Channel 2 is the ECG lead. START is tied low.", 16, 22, 1.4)
    sh.text("C34 is 47 nF: Fig 73 note (1), required when channel 1 respiration is on.", 16, 28, 1.3)
    sh.text("R32-R36 are 51k DPCR. Fig 68's node is after that resistor, so modulation Z is 40.2k+51k.", 16, 34, 1.2)

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
        "14": "+3V3",  # CLKSEL = DVDD
        "15": "ADS1292_PWDN",
        "16": "GND",
        "17": "NC",  # CLK
        "18": "CS_ADS1292",
        "19": "SPI_MOSI",
        "20": "SPI_SCK",
        "21": "SPI_MISO",
        "22": "ADS1292_DRDY",
        "25": "NC",
        "26": "NC",
        "33": "GND",  # exposed pad, must be AVSS
    }
    d.side_map(sh, u, side, 8.89)
    d.join_row(sh, u, ["12", "23"], "+3V3", 6.35)
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
        ("C43", "1u", "+3V3", "GND"),
        ("C28", "100n", "+3V3", "GND"),
        ("C29", "100n", "+3V3", "GND"),
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
    d.hpart(sh, "R", "R15", "1M", FP_R, 70, 240, "+3V3", "RLDREF")
    d.hpart(sh, "R", "R16", "1M", FP_R, 115, 240, "RLDREF", "GND")
    d.hpart(sh, "R", "R17", "1M", FP_R, 175, 240, "RLDOUT", "RLDINV")

    # Fig 68. Electrode node is ECG_P / ECG_N, after the series resistor.
    d.vpart(sh, "R", "R26", "10M", FP_R, 250, 185, "+3V3", "IN1P")
    d.vpart(sh, "R", "R27", "10M", FP_R, 272, 185, "IN1P", "GND")
    d.vpart(sh, "R", "R28", "10M", FP_R, 294, 185, "+3V3", "IN1N")
    d.vpart(sh, "R", "R29", "10M", FP_R, 316, 185, "IN1N", "GND")
    d.vpart(sh, "C", "C48", "2.2n", FP_C, 340, 185, "IN1P", "GND")
    d.vpart(sh, "C", "C49", "2.2n", FP_C, 362, 185, "IN1N", "GND")
    d.hpart(sh, "C", "C50", "100n", FP_C, 260, 220, "ECG_P", "IN1P")
    d.hpart(sh, "C", "C51", "100n", FP_C, 320, 220, "ECG_N", "IN1N")
    d.vpart(sh, "C", "C52", "2.2n", FP_C, 360, 220, "ECG_P", "GND")
    d.vpart(sh, "C", "C53", "2.2n", FP_C, 382, 220, "ECG_N", "GND")
    d.hpart(sh, "R", "R30", "40.2k", FP_R, 260, 245, "RESP_MODP", "ECG_P")
    d.hpart(sh, "R", "R31", "40.2k", FP_R, 330, 245, "RESP_MODN", "ECG_N")
    # DPCR 51k stands off the defib pulse. TPD is on the IC side of that resistor.
    d.hpart(sh, "R", "R32", "51k", FP_HV, 55, 258, "ECG1_PAD", "ECG_P")
    d.hpart(sh, "R", "R33", "51k", FP_HV, 115, 258, "ECG2_PAD", "ECG_N")
    d.hpart(sh, "R", "R34", "51k", FP_HV, 175, 258, "RLD_PAD", "RLDOUT")
    d.hpart(sh, "R", "R35", "51k", FP_HV, 235, 258, "AFE_P_PAD", "AFE_P_AC")
    d.hpart(sh, "R", "R36", "51k", FP_HV, 295, 258, "AFE_N_PAD", "AFE_N_AC")
    d.vpart(sh, "D_TVS_2", "D1", "TPD1E10B06", FP_TVS, 55, 278, "ECG_P", "GND")
    d.vpart(sh, "D_TVS_2", "D2", "TPD1E10B06", FP_TVS, 115, 278, "ECG_N", "GND")
    d.vpart(sh, "D_TVS_2", "D3", "TPD1E10B06", FP_TVS, 175, 278, "RLDOUT", "GND")
    d.vpart(sh, "D_TVS_2", "D4", "TPD1E10B06", FP_TVS, 235, 278, "AFE_P_AC", "GND")
    d.vpart(sh, "D_TVS_2", "D5", "TPD1E10B06", FP_TVS, 295, 278, "AFE_N_AC", "GND")
    # 24 mm pitch. Each stub ends 7.62 mm from the symbol centre, so a 16 mm
    # pitch lands the GND wire on the next electrode label and shorts the pads.
    d.vpart(sh, "GDT", "D12", "S30-A90X", FP_GDT, 22, 100, "ECG1_PAD", "GND")
    d.vpart(sh, "GDT", "D13", "S30-A90X", FP_GDT, 22, 124, "ECG2_PAD", "GND")
    d.vpart(sh, "GDT", "D14", "S30-A90X", FP_GDT, 22, 148, "RLD_PAD", "GND")
    d.vpart(sh, "GDT", "D15", "S30-A90X", FP_GDT, 22, 172, "AFE_P_PAD", "GND")
    d.vpart(sh, "GDT", "D16", "S30-A90X", FP_GDT, 22, 196, "AFE_N_PAD", "GND")


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

    d.vpart(sh, "R", "R18", "200", FP_R, 130, 78, "+3V3", "VREF2")
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
    q = d.part(sh, "CSD13380F3", "Q1", "CSD13380F3", FP_FET, 160, 265, labels="ic")
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
    # SNOSD82D: ALERT is open-drain and requires a pull-up. GPIO33 has none inside the module.
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
