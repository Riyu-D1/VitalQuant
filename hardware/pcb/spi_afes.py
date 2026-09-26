"""SPI analog front ends: AFE4900 (PPG), AD5940 (EDA / impedance), ADS1292R (ECG + respiration).

One shared SCK/MOSI/MISO bus. Each device has its own chip-select.
ADS1292R is SPI mode 1; AD5940 is mode 0. That is a firmware constraint,
not a wiring change. Electrode networks are datasheet application circuits
for a research prototype. They are not a patient-protection or safety design.
"""

from skidl import Net, subcircuit

from common import C, R, local_part, nc, pwr_flag, series, shunt, stock


def _header(npins, value, designator, description):
    sym = {2: "Conn_01x02_Pin", 3: "Conn_01x03_Pin", 4: "Conn_01x04_Pin"}[npins]
    fp = {
        2: "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
        3: "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
        4: "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    }[npins]
    return stock("Connector", sym, value, designator, fp, "generic 2.54 mm header", description)


def _cs_pullup(net, v3d):
    r = R("10k")
    r[1] += v3d
    r[2] += net


@subcircuit
def afe4900(gnd, v3d, v3a, vsys, sck, mosi, miso, cs, reset, adc_rdy):
    """AFE4900 on SPI. TX_SUP is the cell/VSYS rail. No boost converter."""
    u = local_part(
        "AFE4900YZR",
        "AFE4900YZR",
        "U2",
        "vitalq:AFE4900YZR",
        "AFE4900YZR",
        "PPG AFE. SPI mode (I2C_SPI_SEL high). The I2C address 0x5B applies only when SEN is low and is unused here.",
    )
    u["RX_SUP"] += v3a
    u["IO_SUP"] += v3a
    u["RX_GND"] += gnd
    u["TX_GND"] += gnd
    u["TX_SUP"] += vsys
    shunt(v3a, gnd, "1uF")
    shunt(v3a, gnd, "1uF")
    shunt(vsys, gnd, "4.7uF", bulk=True)
    shunt(u["BG"], gnd, "100nF")
    shunt(u["RLD_OUT"], gnd, "100pF")

    # Internal LDOs on. SPI selected. Levels are 0 to RX_SUP, which is 3.3 V.
    u["CONTROL1"] += gnd
    u["I2C_SPI_SEL"] += v3a

    u["I2C_CLK"] += sck
    u["I2C_DAT"] += mosi
    u["SDOUT"] += miso
    u["SEN"] += cs
    _cs_pullup(cs, v3d)
    u["ADC_RDY"] += adc_rdy
    u["RESETZ"] += reset
    r_rst = R("10k")
    r_rst[1] += v3a
    r_rst[2] += reset

    # CLK can be an output before OSC_ENABLE is set. 1k keeps it from floating
    # without a hard short. Internal oscillator is a firmware setting.
    series(u["CLK"], gnd, "1k")

    # Common-anode LEDs on TX_SUP. Assumption: 1206, because 200 mA pulses are
    # a poor fit for 0603. Wavelengths are targets, not a selected MPN.
    for designator, value, tx, note in (
        ("D2", "LED_GREEN_525nm", "TX1", "Green ~525 nm into TX1."),
        ("D4", "LED_RED_660nm", "TX2", "Red ~660 nm into TX2."),
        ("D3", "LED_IR_940nm", "TX3", "IR ~940 nm into TX3."),
    ):
        led = stock(
            "Device",
            "LED",
            value,
            designator,
            "LED_SMD:LED_1206_3216Metric",
            "generic 1206",
            note + " Assumption: generic 1206. Not an optically specified part.",
        )
        led["A"] += vsys
        led["K"] += u[tx]

    # Assumption: SOD-123 stands in for a large-area Si PIN until a PD is chosen.
    # Cathode to INP (ball A2), anode to INM (ball A3).
    pd = stock(
        "Device",
        "D_Photo",
        "PIN_PHOTODIODE",
        "D6",
        "Diode_SMD:D_SOD-123",
        "generic Si PIN",
        "PPG photodiode placeholder. Cathode on INP, anode on INM.",
    )
    pd["K"] += u["INP"]
    pd["A"] += u["INM"]

    # Separate ECG pins from the ADS1292R pair. Series 10k is a prototype
    # resistor, not an IEC patient-protection network.
    ecg = _header(
        3,
        "AFE_ECG",
        "J6",
        "AFE4900 ECG electrodes INP, INM, RLD. Assumption: 2.54 mm header.",
    )
    series(u["INP_ECG"], ecg[1], "10k")
    series(u["INM_ECG"], ecg[2], "10k")
    series(u["RLD_OUT"], ecg[3], "10k")

    nc(u, "DNC", "INP2", "INM2", "INP3", "INM3", "TX4", "PROG_OUT1")


@subcircuit
def ad5940(gnd, v3a, sck, mosi, miso, cs, reset, gpio0):
    """AD5940 bio-impedance / EDA front end. Internal HFOSC, crystal is DNP."""
    u = local_part(
        "AD5940BCBZ",
        "AD5940BCBZ",
        "U3",
        "vitalq:AD5940BCBZ",
        "AD5940BCBZ-RL7",
        "Impedance / EDA AFE, WLCSP-56. Crystal not fitted; firmware uses the internal oscillator.",
    )
    u["AVDD"] += v3a
    u["DVDD"] += v3a
    u["AGND"] += gnd
    u["DGND"] += gnd
    u["AGND_REF"] += gnd
    shunt(v3a, gnd, "10uF", bulk=True)
    shunt(v3a, gnd, "100nF")
    shunt(v3a, gnd, "100nF")
    shunt(v3a, gnd, "1uF")
    shunt(v3a, gnd, "100nF")

    # DVDD is the same rail as IOVDD. A 10 ohm + 1 uF slows IOVDD by about 10 us.
    # The datasheet requires DVDD before IOVDD. This is a prototype measure.
    iovdd = Net("IOVDD_AD5940")
    r_seq = R("10")
    r_seq[1] += v3a
    r_seq[2] += iovdd
    shunt(iovdd, gnd, "1uF")
    pwr_flag(iovdd)
    u["IOVDD"] += iovdd

    shunt(u["VREF_1V82"], gnd, "4.7uF", bulk=True)
    shunt(u["VREF_2V5"], gnd, "470nF")
    shunt(u["VBIAS_CAP"], gnd, "470nF")
    shunt(u["DVDD_REG_1V8"], gnd, "470nF")
    # Pin-table value for AVDD_REG is not stated. 1 uF is an assumption.
    shunt(u["AVDD_REG"], gnd, "1uF")
    shunt(u["VZERO0"], gnd, "100nF")
    shunt(u["VBIAS0"], gnd, "100nF")
    shunt(u["AIN4_LPF0"], gnd, "1uF")
    c_rc = C("100nF")
    c_rc[1] += u["RC0_0"]
    c_rc[2] += u["RC0_1"]
    nc(u, "RC0_2")

    # RCAL matches the datasheet Z = 1 kΩ accuracy condition.
    series(u["RCAL0"], u["RCAL1"], "1k 0.1%")

    # Series CISO + 1k RLIMIT, from the Rev G Z-measurement condition:
    # CISO1 = 15 nF on CE, CISO2/3/4 = 470 nF on RE, SE, DE.
    # This is an AC impedance network. It blocks DC. DC skin conductance is an open item.
    eda = _header(
        4,
        "EDA",
        "J4",
        "AD5940 electrodes CE, RE, SE, DE. Assumption: 2.54 mm header. Series CISO is the datasheet AC test network.",
    )
    for chip, cap, hdr, name in (
        ("CE0", "15nF", 1, "EDA_CE"),
        ("RE0", "470nF", 2, "EDA_RE"),
        ("SE0", "470nF", 3, "EDA_SE"),
        ("DE0", "470nF", 4, "EDA_DE"),
    ):
        mid = Net(name)
        series(u[chip], mid, "1k")
        c = C(cap)
        c[1] += mid
        c[2] += eda[hdr]

    u["MOSI"] += mosi
    u["MISO"] += miso
    u["SCLK"] += sck
    u["CS"] += cs
    u["RESET"] += reset
    r_rst = R("10k")
    r_rst[1] += iovdd
    r_rst[2] += reset
    u["GPIO0"] += gpio0

    # 16 MHz crystal footprint only. Load caps assume an 8 pF crystal (12 pF each).
    y = stock(
        "Device",
        "Crystal",
        "16MHz",
        "Y1",
        "Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm",
        "generic 16 MHz",
        "DNP. AD5940 external crystal footprint. Assumption: 2-pin 3215, 8 pF load.",
    )
    y.dnp = True
    y[1] += u["XTALI"]
    y[2] += u["XTALO"]
    for pin in ("XTALI", "XTALO"):
        c = C("12pF")
        c.dnp = True
        c[1] += u[pin]
        c[2] += gnd

    nc(u, "DNC", "AFE1", "AFE2", "AFE3", "AFE4", "AIN0", "AIN1", "AIN2", "AIN3", "AIN6")
    for n in range(1, 8):
        if n == 0:
            continue
        nc(u, f"GPIO{n}")


@subcircuit
def ads1292(gnd, v3d, v3a, sck, mosi, miso, cs, pwdn, start, drdy):
    """ADS1292R: channel 1 respiration, channel 2 ECG, same LA/RA leads, plus RLD.

    Capacitors follow SBAS502C §11.1.1.1 and Fig 73: 10 uF + 0.1 uF on each
    supply and on VREFP, 1 uF on VCAP1 and VCAP2, 47 nF on PGA1 (respiration),
    4.7 nF on PGA2. The electrode resistors follow Fig 68 (p. 62).
    """
    u = local_part(
        "ADS1292R",
        "ADS1292RIPBSR",
        "U4",
        "Package_QFP:TQFP-32_5x5mm_P0.5mm",
        "ADS1292RIPBSR",
        "ECG + respiration AFE. Internal 512 kHz clock (CLKSEL high, CLK pin left open).",
    )
    u["AVDD"] += v3a
    u["DVDD"] += v3a
    u["AVSS"] += gnd
    u["DGND"] += gnd
    u["VREFN"] += gnd
    for _ in range(2):
        shunt(v3a, gnd, "10uF", bulk=True)
        shunt(v3a, gnd, "100nF")
    shunt(u["VREFP"], gnd, "10uF", bulk=True)
    shunt(u["VREFP"], gnd, "100nF")
    shunt(u["VCAP1"], gnd, "1uF")
    shunt(u["VCAP2"], gnd, "1uF")

    u["CLKSEL"] += v3a
    nc(u, "CLK", "GPIO1", "GPIO2")

    u["CS"] += cs
    _cs_pullup(cs, v3d)
    u["DIN"] += mosi
    u["SCLK"] += sck
    u["DOUT"] += miso
    u["DRDY"] += drdy
    u["START"] += start
    u["PWDN"] += pwdn
    r_pwdn = R("10k")
    r_pwdn[1] += v3a
    r_pwdn[2] += pwdn

    c_pga1 = C("47nF")
    c_pga1[1] += u["PGA1P"]
    c_pga1[2] += u["PGA1N"]
    c_pga2 = C("4.7nF")
    c_pga2[1] += u["PGA2P"]
    c_pga2[2] += u["PGA2N"]

    # LA, RA, RL. The 10k series parts are a prototype choice on top of Fig 68.
    ecg = _header(
        3,
        "ECG",
        "J5",
        "ADS1292R electrodes LA, RA, RL. Separate from the AFE4900 and AD5940 pairs.",
    )
    la = Net("ECG_LA")
    ra = Net("ECG_RA")
    series(ecg[1], la, "10k")
    series(ecg[2], ra, "10k")
    series(u["RLDOUT"], ecg[3], "10k")

    u["IN1P"] += la
    u["IN2P"] += la
    u["IN1N"] += ra
    u["IN2N"] += ra
    shunt(la, gnd, "2.2nF")
    shunt(ra, gnd, "2.2nF")

    rldref = Net("RLDREF")
    series(v3a, rldref, "1M")
    series(rldref, gnd, "1M")
    shunt(rldref, gnd, "100nF")
    u["RLDREF"] += rldref
    series(la, rldref, "10M")
    series(ra, rldref, "10M")
    series(u["RLDOUT"], u["RLDINV"], "1M")
    series(u["IN2P"], u["RLDINV"], "1M")
    series(u["IN2N"], u["RLDINV"], "1M")

    for mod, electrode, name in (
        ("RESP_MODP", la, "RESP_P"),
        ("RESP_MODN", ra, "RESP_N"),
    ):
        mid = Net(name)
        c = C("100nF")
        c[1] += u[mod]
        c[2] += mid
        series(mid, electrode, "40.2k")


@subcircuit
def spi_afes(
    gnd,
    v3d,
    v3a,
    vsys,
    sck,
    mosi,
    miso,
    cs_ad,
    cs_ads,
    cs_afe,
    afe_rst,
    afe_rdy,
    ad_rst,
    ad_gpio0,
    ads_pwdn,
    ads_start,
    ads_drdy,
):
    afe4900(gnd, v3d, v3a, vsys, sck, mosi, miso, cs_afe, afe_rst, afe_rdy, tag="afe4900")
    ad5940(gnd, v3a, sck, mosi, miso, cs_ad, ad_rst, ad_gpio0, tag="ad5940")
    ads1292(gnd, v3d, v3a, sck, mosi, miso, cs_ads, ads_pwdn, ads_start, ads_drdy, tag="ads1292")
