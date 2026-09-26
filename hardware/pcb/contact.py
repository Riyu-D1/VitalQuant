"""FSR402 force divider and the battery-voltage divider.

Both analog nets land on ESP32 ADC1. ADC2 is not used, because it is
unavailable while Wi-Fi is on. The FSR itself is off-board; only the header
is a PCB part.
"""

from skidl import subcircuit

from common import R, pwr_flag, shunt, stock


@subcircuit
def contact(gnd, v3s, vbat_sw, fsr, vbat_adc):
    # 3V3_S -> header pin 1 -> FSR -> header pin 2 -> 10k to GND -> GPIO36.
    hdr = stock(
        "Connector",
        "Conn_01x02_Pin",
        "FSR402",
        "J1",
        "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
        "generic 2.54 mm header",
        "FSR402 connector. The force sensor is off-board.",
    )
    hdr[1] += v3s
    hdr[2] += fsr
    r = R("10k")
    r[1] += fsr
    r[2] += gnd
    shunt(fsr, gnd, "100nF")
    # GPIO36 is input-only. The divider is the only circuit on this net.
    pwr_flag(fsr)

    # Half of the switched pack voltage. 4.2 V becomes 2.1 V. Taken after the
    # power switch so the cell is not loaded while the board is off.
    top = R("100k")
    bot = R("100k")
    top[1] += vbat_sw
    top[2] += vbat_adc
    bot[1] += vbat_adc
    bot[2] += gnd
    shunt(vbat_adc, gnd, "100nF")
    pwr_flag(vbat_adc)
