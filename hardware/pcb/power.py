"""Battery, charge path, load switch, and the four regulators.

VBUS and the cell share VSYS. The P-channel FET body diode must not charge
the cell from USB, so the source sits on VSYS and the drain sits on the
switched battery node. The power switch is on the battery side of that FET:
USB still runs the board, and the charger still sees the cell, when the
switch is off.
"""

from skidl import Net, subcircuit

from common import R, local_part, nc, pwr_flag, shunt, stock


@subcircuit
def power(gnd, vbus, vbat, vsys, v3d, v3a, v3s, v1v8):
    # --- cell connector and power switch ---
    # Assumption: JST PH B2B is the 1-cell LiPo. Pin 1 is pack positive.
    bat = stock(
        "Connector",
        "Conn_01x02_Pin",
        "JST_PH_B2B",
        "J3",
        "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical",
        "JST B2B-PH-K-S",
        "1-cell LiPo. Assumption: JST PH 2-pin, pin 1 positive.",
    )
    bat[1] += vbat
    bat[2] += gnd

    # Assumption: C&K PCM12 footprint, pad 2 is the common (center) pin.
    # Wired as SPST between pad 1 and pad 2. Pad 3 is unused.
    sw = stock(
        "Switch",
        "SW_SPDT",
        "PCM12_as_SPST",
        "SW1",
        "Button_Switch_SMD:SW_SPDT_PCM12",
        "C&K PCM12SMTR",
        "Power switch. Assumption: pad 2 is common; pad 3 is no-connect.",
    )
    vbat_sw = Net("VBAT_SW")
    sw[1] += vbat
    sw[2] += vbat_sw
    nc(sw, 3)

    # --- ideal-ish load share: Schottky from VBUS, P-FET from the cell ---
    diode = stock(
        "Diode",
        "SS14",
        "SS14",
        "D5",
        "Diode_SMD:D_SMA",
        "SS14",
        "VBUS to VSYS Schottky. Footprint overridden to SMA; the symbol default is a through-hole DO-41.",
    )
    diode["A"] += vbus
    diode["K"] += vsys

    fet = stock(
        "Transistor_FET",
        "AO3401A",
        "AO3401A",
        "Q1",
        "Package_TO_SOT_SMD:SOT-23",
        "AO3401A",
        "Load switch. Source = VSYS, drain = switched battery, so the body diode does not back-feed the cell.",
    )
    gate = Net("Q1_GATE")
    fet["S"] += vsys
    fet["D"] += vbat_sw
    fet["G"] += gate
    # Gate is biased only by the two 100k resistors. The flag is an ERC marker.
    pwr_flag(gate)
    series_gate_gnd = R("100k")
    series_gate_gnd[1] += gate
    series_gate_gnd[2] += gnd
    series_gate_vbus = R("100k")
    series_gate_vbus[1] += gate
    series_gate_vbus[2] += vbus

    # VSYS is passive (diode + FET). One power flag tells ERC the net is driven.
    pwr_flag(vsys)

    # --- MCP73831-2, 4.20 V, 100 mA (RPROG 10k) ---
    chg = stock(
        "Battery_Management",
        "MCP73831-2-OT",
        "MCP73831T-2ACI/OT",
        "U10",
        "Package_TO_SOT_SMD:SOT-23-5",
        "MCP73831T-2ACI/OT",
        "Li-ion charger, 4.20 V. RPROG 10k sets 100 mA.",
    )
    chg["V_{DD}"] += vbus
    chg["V_{SS}"] += gnd
    chg["V_{BAT}"] += vbat
    rprog = R("10k")
    rprog[1] += chg["PROG"]
    rprog[2] += gnd
    shunt(vbus, gnd, "4.7uF", bulk=True)
    shunt(vbat, gnd, "4.7uF", bulk=True)

    # STAT is open-drain, low while charging. LED from VBUS.
    led = stock(
        "Device",
        "LED",
        "CHARGE",
        "D1",
        "LED_SMD:LED_0603_1608Metric",
        "generic red 0603",
        "Charge LED. Assumption: generic 0603 red. Anode toward VBUS.",
    )
    rled = R("1k")
    rled[1] += vbus
    rled[2] += led["A"]
    led["K"] += chg["STAT"]

    # --- 3V3_D, ESP32 rail, AP2112K-3.3, EN tied to VIN ---
    u13 = stock(
        "Regulator_Linear",
        "AP2112K-3.3",
        "AP2112K-3.3TRG1",
        "U13",
        "Package_TO_SOT_SMD:SOT-23-5",
        "AP2112K-3.3TRG1",
        "600 mA 3.3 V LDO for the ESP32 rail.",
    )
    u13["VIN"] += vsys
    u13["EN"] += vsys
    u13["GND"] += gnd
    u13["VOUT"] += v3d
    nc(u13, "NC")
    shunt(vsys, gnd, "1uF")
    shunt(v3d, gnd, "1uF")

    # --- 3V3_A quiet rail, TPS7A2033 ---
    u12 = local_part(
        "TPS7A2033PDBVR",
        "TPS7A2033PDBVR",
        "U12",
        "Package_TO_SOT_SMD:SOT-23-5",
        "TPS7A2033PDBVR",
        "300 mA low-noise 3.3 V LDO for AD5940, ADS1292R, and AFE4900 Rx/IO.",
    )
    u12["IN"] += vsys
    u12["EN"] += vsys
    u12["GND"] += gnd
    u12["OUT"] += v3a
    nc(u12, "NC")
    shunt(vsys, gnd, "1uF")
    shunt(v3a, gnd, "1uF")

    # --- 3V3_S I2C sensor rail, Torex XC6206 ---
    u11 = local_part(
        "XC6206",
        "XC6206P332MR-G",
        "U11",
        "Package_TO_SOT_SMD:SOT-23",
        "XC6206P332MR-G",
        "3.3 V I2C-sensor rail. Local symbol uses the Torex pinout.",
    )
    u11["VIN"] += vsys
    u11["VSS"] += gnd
    u11["VOUT"] += v3s
    shunt(vsys, gnd, "1uF")
    shunt(v3s, gnd, "1uF")

    # --- 1.8 V for the AS7341, fed from 3V3_S ---
    u14 = local_part(
        "XC6206",
        "XC6206P182MR-G",
        "U14",
        "Package_TO_SOT_SMD:SOT-23",
        "XC6206P182MR-G",
        "1.8 V rail for the AS7341 and the low side of the I2C translator.",
    )
    u14["VIN"] += v3s
    u14["VSS"] += gnd
    u14["VOUT"] += v1v8
    shunt(v3s, gnd, "1uF")
    shunt(v1v8, gnd, "1uF")

    return vbat_sw
