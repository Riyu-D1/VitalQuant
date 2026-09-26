"""ESP32-WROOM-32E-N8R2.

GPIO16 is PSRAM on the N8R2 module (symbol pin 27 is NC). GPIO6-11 are the
internal flash and stay on the module NC pins. GPIO12 is left open so the
MTDI strap stays low (3.3 V flash). GPIO2 is left open (download mode wants
it low or floating). GPIO15 is the AD5940 chip-select and is pulled up so
the MTDO strap is high.
"""

from skidl import subcircuit

from common import R, nc, shunt, stock


@subcircuit
def mcu(
    gnd,
    v3d,
    en,
    io0,
    tx,
    rx,
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
    i2c_sda,
    i2c_scl,
    lsm_int,
    tmp_alert,
    as_int,
    fsr,
    vbat_adc,
):
    esp = stock(
        "RF_Module",
        "ESP32-WROOM-32E-R2",
        "ESP32-WROOM-32E-N8R2",
        "U1",
        "RF_Module:ESP32-WROOM-32E",
        "ESP32-WROOM-32E-N8R2",
        "8 MB flash + 2 MB PSRAM. Symbol is the R2 pinout (GPIO16 NC). Footprint is the WROOM-32E land.",
    )
    esp["VDD"] += v3d
    esp["GND"] += gnd
    # Bulk plus a local 100 nF on the module 3.3 V pin.
    shunt(v3d, gnd, "10uF", bulk=True)
    shunt(v3d, gnd, "100nF")

    esp["EN"] += en
    esp["IO0"] += io0
    esp["TXD0/IO1"] += tx
    esp["RXD0/IO3"] += rx
    nc(esp, "IO2", "IO12", "NC")

    # GPIO15 strap pull-up. This net is also CS_AD5940.
    r15 = R("10k")
    r15[1] += v3d
    r15[2] += cs_ad
    esp["IO15"] += cs_ad

    esp["IO4"] += cs_ads
    esp["IO5"] += cs_afe
    esp["IO13"] += afe_rdy
    esp["IO14"] += ad_gpio0
    esp["IO17"] += afe_rst
    esp["IO18"] += sck
    esp["IO19"] += miso
    esp["IO23"] += mosi
    esp["IO21"] += i2c_sda
    esp["IO22"] += i2c_scl
    esp["IO25"] += ads_pwdn
    esp["IO26"] += ads_start
    esp["IO27"] += ad_rst
    esp["IO32"] += lsm_int
    esp["IO33"] += tmp_alert
    esp["IO34"] += ads_drdy
    esp["IO35"] += as_int
    esp["SENSOR_VP"] += fsr  # GPIO36, ADC1_CH0
    esp["SENSOR_VN"] += vbat_adc  # GPIO39, ADC1_CH3
