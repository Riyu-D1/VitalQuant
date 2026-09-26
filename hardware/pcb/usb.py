"""USB-C receptacle, CP2102N UART, and the ESP32 auto-program circuit.

The QFN-28 CP2102N has no VIO pin. VDD is the internal regulator output and
stays on its own net. The VBUS-detect divider is the orientation that clears
VIH (about 3.4 V), from CP2102N datasheet Rev 1.5 Figs 2.4 and 2.5.
"""

from skidl import POWER, Net, subcircuit

from common import R, nc, pwr_flag, shunt, stock


@subcircuit
def usb(gnd, vbus, v3d, esp_en, esp_gpio0, esp_tx, esp_rx):
    jack = stock(
        "Connector",
        "USB_C_Receptacle_USB2.0_16P",
        "USB-C",
        "J2",
        "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
        "HRO TYPE-C-31-M-12",
        "USB 2.0 Type-C receptacle. Assumption: HRO TYPE-C-31-M-12, the KiCad 16-pin land.",
    )
    jack["GND"] += gnd
    jack["SHIELD"] += gnd
    jack["VBUS"] += vbus
    nc(jack, "SBU1", "SBU2")
    # 5.1 kΩ 1% CC pulldowns advertise a default-USB sink.
    for cc in ("CC1", "CC2"):
        r = R("5.1k 1%")
        r[1] += jack[cc]
        r[2] += gnd

    esd = stock(
        "Power_Protection",
        "USBLC6-2SC6",
        "USBLC6-2SC6",
        "U17",
        "Package_TO_SOT_SMD:SOT-23-6",
        "USBLC6-2SC6",
        "USB D+/D- ESD. Footprint overridden to SOT-23-6; the symbol default is SOT-666.",
    )
    esd["VBUS"] += vbus
    esd["GND"] += gnd
    dp = Net("USB_DP")
    dm = Net("USB_DM")
    esd["I/O1"] += dp
    esd["I/O2"] += dm
    jack["D+"] += dp
    jack["D-"] += dm

    uart = stock(
        "Interface_USB",
        "CP2102N-Axx-xQFN28",
        "CP2102N-A02-GQFN28R",
        "U15",
        "Package_DFN_QFN:QFN-28-1EP_5x5mm_P0.5mm_EP3.35x3.35mm",
        "CP2102N-A02-GQFN28R",
        "USB-UART. QFN-28 has no VIO pin; VDD is the on-chip 3.3 V regulator output.",
    )
    vdd = Net("VDD_CP2102")
    vdd.drive = POWER
    uart["VREGIN"] += vbus
    uart["VDD"] += vdd
    uart["GND"] += gnd
    uart["D+"] += dp
    uart["D-"] += dm
    pwr_flag(vdd)
    shunt(vbus, gnd, "4.7uF", bulk=True)
    shunt(vbus, gnd, "100nF")
    shunt(vdd, gnd, "4.7uF", bulk=True)
    shunt(vdd, gnd, "100nF")

    # 22.1k from VBUS, 47.5k to GND -> ~3.41 V at the VBUS sense pin.
    sense = Net("CP2102_VBUS")
    r_top = R("22.1k")
    r_bot = R("47.5k")
    r_top[1] += vbus
    r_top[2] += sense
    r_bot[1] += sense
    r_bot[2] += gnd
    uart["VBUS"] += sense
    pwr_flag(sense)

    rst = Net("CP2102_RST")
    uart["~{RST}"] += rst
    r_rst = R("1k")
    r_rst[1] += vdd
    r_rst[2] += rst
    # ~RST is only pulled up. The flag marks that bias for ERC; it is not a supply.
    pwr_flag(rst)
    # Unused modem inputs tied to the local 3.3 V, per the datasheet.
    uart["~{CTS}"] += vdd
    uart["~{DSR}"] += vdd
    uart["~{DCD}"] += vdd
    uart[2] += vdd  # ~{RI}/CLK
    nc(uart, "NC", "SUSPEND", "~{SUSPEND}", "CHREN", "CHR0", "CHR1")
    nc(uart, "~{TXT}/GPIO.0", "~{RXT}/GPIO.1", "RS485/GPIO.2", "~{WAKEUP}/GPIO.3")
    nc(uart, "GPIO.4", "GPIO.5", "GPIO.6")

    # ESP32 UART0: module TX (GPIO1) -> CP2102 RXD, module RX (GPIO3) <- CP2102 TXD.
    uart["RXD"] += esp_tx
    uart["TXD"] += esp_rx

    # Espressif auto-program, two NPN, no base resistors (DevKit style).
    # Q_EN: base=RTS, emitter=DTR, collector=EN.
    # Q_IO0: base=DTR, emitter=RTS, collector=GPIO0.
    q_en = stock(
        "Transistor_BJT",
        "MMBT3904",
        "MMBT3904",
        "Q3",
        "Package_TO_SOT_SMD:SOT-23",
        "MMBT3904",
        "Auto-program transistor for EN.",
    )
    q_io0 = stock(
        "Transistor_BJT",
        "MMBT3904",
        "MMBT3904",
        "Q4",
        "Package_TO_SOT_SMD:SOT-23",
        "MMBT3904",
        "Auto-program transistor for GPIO0.",
    )
    q_en["B"] += uart["~{RTS}"]
    q_en["E"] += uart["~{DTR}"]
    q_en["C"] += esp_en
    q_io0["B"] += uart["~{DTR}"]
    q_io0["E"] += uart["~{RTS}"]
    q_io0["C"] += esp_gpio0

    # EN: 10k pull-up, 1 uF to ground, button to ground.
    r_en = R("10k")
    r_en[1] += v3d
    r_en[2] += esp_en
    shunt(esp_en, gnd, "1uF")
    # EN is an input biased by a resistor and a transistor. The flag is an ERC marker.
    pwr_flag(esp_en)
    sw_en = stock(
        "Switch",
        "SW_SPST",
        "EN",
        "SW2",
        "Button_Switch_SMD:SW_SPST_TL3342",
        "TL3342",
        "EN button. Assumption: TL3342-style 2-pad tactile switch.",
    )
    sw_en[1] += esp_en
    sw_en[2] += gnd

    # GPIO0: 10k pull-up and BOOT button. Download mode wants GPIO2 low or floating.
    r_boot = R("10k")
    r_boot[1] += v3d
    r_boot[2] += esp_gpio0
    sw_boot = stock(
        "Switch",
        "SW_SPST",
        "BOOT",
        "SW3",
        "Button_Switch_SMD:SW_SPST_TL3342",
        "TL3342",
        "BOOT / GPIO0 button.",
    )
    sw_boot[1] += esp_gpio0
    sw_boot[2] += gnd
