"""3.3 V I2C sensors plus the AS7341 behind a PCA9306.

Bus A (3.3 V, pull-ups to 3V3_S):
  MLX90632  0x3A  ADDR = GND
  TMP117    0x48  ADD0 = GND
  LSM6DSV80X 0x6A  SDO/TA0 = GND, mode-1 I2C (SDx = SCx = GND, CS high)
  BME280    0x76  SDO = GND, CSB high

Bus B (1.8 V): AS7341 0x39, fixed. INT is level-shifted with a BSS138
because GPIO35 is an ESP32 input and the AS7341 INT pin is 1.8 V open-drain.
"""

from skidl import Net, subcircuit

from common import R, local_part, nc, pwr_flag, shunt, stock


@subcircuit
def i2c_sensors(gnd, v3s, v1v8, sda, scl, lsm_int, tmp_alert, as_int):
    r_sda = R("4.7k")
    r_scl = R("4.7k")
    r_sda[1] += v3s
    r_sda[2] += sda
    r_scl[1] += v3s
    r_scl[2] += scl

    sda18 = Net("I2C_SDA_1V8")
    scl18 = Net("I2C_SCL_1V8")
    r_sda18 = R("4.7k")
    r_scl18 = R("4.7k")
    r_sda18[1] += v1v8
    r_sda18[2] += sda18
    r_scl18[1] += v1v8
    r_scl18[2] += scl18

    # VREF2 to 3.3 V through 200 ohm. EN tied to the VREF2 pin.
    xlat = stock(
        "Interface",
        "PCA9306DC",
        "PCA9306DC",
        "U16",
        "Package_SO:VSSOP-8_2.3x2mm_P0.5mm",
        "PCA9306DCUR",
        "1.8 V to 3.3 V I2C translator. VREF1 = 1.8 V, EN tied to VREF2.",
    )
    xlat["GND"] += gnd
    xlat["VREF1"] += v1v8
    xlat["SCL1"] += scl18
    xlat["SDA1"] += sda18
    xlat["SCL2"] += scl
    xlat["SDA2"] += sda
    vref2 = Net("PCA9306_VREF2")
    r_vref = R("200")
    r_vref[1] += v3s
    r_vref[2] += vref2
    xlat["VREF2"] += vref2
    xlat["EN"] += vref2
    # VREF2 is a power-input pin behind the 200 ohm resistor.
    pwr_flag(vref2)

    tmp = local_part(
        "TMP117",
        "TMP117",
        "U5",
        "Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm",
        "TMP117AIDRVR",
        "Contact temperature. ADD0 = GND selects 0x48. Exposed pad is pin 7, grounded.",
    )
    tmp["V+"] += v3s
    tmp["GND"] += gnd
    tmp["ADD0"] += gnd
    tmp["SCL"] += scl
    tmp["SDA"] += sda
    tmp["ALERT"] += tmp_alert
    r_al = R("10k")
    r_al[1] += v3s
    r_al[2] += tmp_alert
    shunt(v3s, gnd, "100nF")

    as7 = stock(
        "Sensor_Optical",
        "AS7341DLG",
        "AS7341-DLGM",
        "U6",
        "Package_LGA:AMS_OLGA-8_2x3.1mm_P0.8mm",
        "AS7341-DLGM",
        "Spectral sensor, 1.8 V, I2C address 0x39. Official KiCad symbol, DS000504 p. 6.",
    )
    as7["VDD"] += v1v8
    as7["GND"] += gnd
    as7["PGND"] += gnd
    as7["SCL"] += scl18
    as7["SDA"] += sda18
    shunt(v1v8, gnd, "100nF")
    nc(as7, "GPIO")
    # Optional LED on LDR. Current is set by the AS7341 register, so no series resistor.
    led = stock(
        "Device",
        "LED",
        "AS7341_LED",
        "D7",
        "LED_SMD:LED_0603_1608Metric",
        "generic 0603",
        "AS7341 LDR LED. Assumption: generic 0603, anode on 1.8 V, cathode on LDR.",
    )
    led["A"] += v1v8
    led["K"] += as7["LDR"]

    int18 = Net("AS7341_INT_1V8")
    as7["INT"] += int18
    r_int18 = R("10k")
    r_int18[1] += v1v8
    r_int18[2] += int18
    # BSS138 source-follower style open-drain shifter: gate at 1.8 V.
    q = stock(
        "Transistor_FET",
        "BSS138",
        "BSS138",
        "Q2",
        "Package_TO_SOT_SMD:SOT-23",
        "BSS138",
        "AS7341 INT 1.8 V to ESP32 GPIO35 (3.3 V, input only).",
    )
    q["G"] += v1v8
    q["S"] += int18
    q["D"] += as_int
    r_int33 = R("10k")
    r_int33[1] += v3s
    r_int33[2] += as_int
    # GPIO35 is input-only and the shifter is passive. Flag marks the bias for ERC.
    pwr_flag(as_int)

    mlx = local_part(
        "MLX90632",
        "MLX90632SLD-DCB-000-RE",
        "U7",
        "vitalq:MLX90632",
        "MLX90632SLD-DCB-000-RE",
        "IR thermopile. ADDR = GND selects 0x3A. 100 nF on VDD is an assumption; Fig 27a says the cap is within 10 mm and does not print the value.",
    )
    mlx["VDD"] += v3s
    mlx["GND"] += gnd
    mlx["ADDR"] += gnd
    mlx["SDA"] += sda
    mlx["SCL"] += scl
    shunt(v3s, gnd, "100nF")

    bme = stock(
        "Sensor",
        "BME280",
        "BME280",
        "U8",
        "Package_LGA:Bosch_LGA-8_2.5x2.5mm_P0.65mm_ClockwisePinNumbering",
        "BME280",
        "Humidity / pressure. CSB high and SDO low select I2C address 0x76.",
    )
    bme["VDD"] += v3s
    bme["VDDIO"] += v3s
    bme["GND"] += gnd
    bme["CSB"] += v3s
    bme["SDO"] += gnd
    bme["SDI"] += sda
    bme["SCK"] += scl
    shunt(v3s, gnd, "100nF")
    shunt(v3s, gnd, "100nF")

    imu = local_part(
        "LSM6DSV80X",
        "LSM6DSV80XTR",
        "U9",
        "vitalq:LSM6DSV80X",
        "LSM6DSV80XTR",
        "IMU, mode-1 I2C, address 0x6A. Pins 10 and 11 are soldered and left electrically open, per the datasheet.",
    )
    imu["VDD"] += v3s
    imu["VDDIO"] += v3s
    imu["GND"] += gnd
    imu["SDO_TA0"] += gnd
    imu["SDx"] += gnd
    imu["SCx"] += gnd
    imu["CS"] += v3s
    imu["SCL"] += scl
    imu["SDA"] += sda
    imu["INT1"] += lsm_int
    shunt(v3s, gnd, "100nF")
    shunt(v3s, gnd, "100nF")
    nc(imu, "INT2", "NC")
