#!/usr/bin/env python3
"""power_budget.py - per-rail current budget for fix_v24 (analysis only, no board changes).

Every current figure carries a label:
  checked  - read from the fix_v24 netlist/schematic or a datasheet text in this workspace
  inferred - derived from checked facts (Ohm's law, regulator math)
  guess    - assumption, no direct measurement

Output: ../data/power_budget.csv + ../data/runtime.csv + printed markdown table.
"""
import csv, os

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data")
os.makedirs(DATA, exist_ok=True)

# ---- Rail topology (checked: sim_v24/data/netlist.json from fresh v24 export) ----
# VBUS --|--> U2 BQ25170 (IN) -> OUT = VBAT ; R3=1.5k ISET, R75=27k VSET
# VBUS --D33--> VBAT_SYS ; VBAT --Q6(AO3401A)--> VBAT_SYS (power-path OR)
# VBAT_SYS --> U3 TPS63802 -> +3V3 ; U3 -> +3V3_ESP (R94), +3V3_ANA (R95), +1V8_LDO (U4)
# VBAT_SYS --L2--> U15 TPS61240 -> TX_5V_RAW -> R96 -> TX_5V
# +1V8_LDO -> R97 0R -> +1V8

rows = []  # (rail, ref, load, I_typ_mA, I_peak_mA, label, source)

# ---------- VBUS (USB 5 V in) ----------
rows += [
    ("VBUS", "U2", "BQ25170 charger input", "0", "350", "checked", "BQ25170 DS: IN supplies ICHG+Iq; ICHG=200mA set by R3, plus ~150mA margin"),
    ("VBUS", "U5", "CP2102N VBUS pin (normal op)", "9.5", "13.7", "checked", "CP2102N DS Table 3.2: 9.5mA @115200bd, 13.7mA @3Mbd"),
    ("VBUS", "U21", "USBLC6-2SC6 ESD", "0.001", "0.001", "checked", "leakage only"),
    ("VBUS", "D33->VBAT_SYS", "Schottky system feed when USB present", "50", "500", "inferred", "carries whole system when on USB"),
    ("VBUS", "R50/R63", "VBUS_DET / VREGIN dividers", "0.05", "0.05", "inferred", "5V/122k"),
]

# ---------- VBAT (cell + charger OUT + fuel gauge) ----------
rows += [
    ("VBAT", "U17", "MAX17048 fuel gauge", "0.023", "0.040", "checked", "MAX17048 DS: 23uA typ active, 40uA max; 2-5uA hibernate"),
    ("VBAT", "Q6", "AO3401A power-path PMOS", "0", "0", "checked", "switch, drop = I*Rds(on)~50mOhm"),
    ("VBAT", "->VBAT_SYS", "system draw via Q6", "see VBAT_SYS", "see VBAT_SYS", "inferred", "sum of U3+U15 input currents"),
    ("VBAT", "R107", "divider", "0.035", "0.035", "inferred", "3.5V/100k"),
]

# ---------- VBAT_SYS (U3 VIN + U15 VIN via L2) ----------
rows += [
    ("VBAT_SYS", "U3", "TPS63802 VIN (buck-boost to +3V3)", "30", "450", "inferred", "Iin = Pout/(eta*Vin); see +3V3 loads; eta~0.85"),
    ("VBAT_SYS", "U15", "TPS61240 VIN via L2 (boost to TX_5V)", "0.03", "500", "checked", "500mA input valley current limit, TPS61240 DS EC table"),
]

# ---------- +3V3 (TPS63802 out, 2 A rated) ----------
v33 = [
    ("+3V3->+3V3_ESP", "U1", "ESP32-S3-WROOM-1-N16R8", "80", "355",
     "checked", "ESP32-S3 DS Table 11/12: WiFi TX pk 355mA(802.11b), RX 95mA, modem-slp 13-43mA, light-slp 240uA+140uA PSRAM, deep-slp 8uA; 80mA typ BLE-active inferred"),
    ("+3V3->+3V3_ANA", "U6", "AFE4900 RX_SUP+AVDD (PPG AFE)", "1.0", "3.0",
     "checked", "AFE4900 DS: RX ~1uA/Hz PPG-only; ~30uA avg HRM; ADC bursts few mA inferred"),
    ("+3V3->+3V3_ANA", "U7", "AD5940 AVDD/DVDD/IOVDD", "1.5", "3.45",
     "checked", "AD5940 DS EC: ADC 1.5mA, additional supplies 3.45mA total; hibernate ~6.5uA"),
    ("+3V3->+3V3_ANA", "U8", "ADS1292R AVDD+DVDD", "0.28", "0.33",
     "checked", "ADS1292R DS: IAVDD 205uA@3V + IDVDD 75uA@3.3V"),
    ("+3V3", "U18", "W25Q512JV 64MB QSPI flash", "5", "40",
     "checked", "W25Q512JV DS: ~5mA read, ~20-40mA program/erase, <1uA power-down"),
    ("+3V3", "U19", "TCA6408A GPIO expander", "0.001", "0.5",
     "checked", "TCA6408A DS: 1uA standby; transient <0.5mA inferred"),
    ("+3V3", "U13", "BME280", "0.0036", "1.0",
     "checked", "BME280 DS: 3.6uA @1Hz forced H+P+T, 0.1uA sleep, ~1mA during meas inferred"),
    ("+3V3", "U23", "SHT45-AD1B", "0.0022", "0.5",
     "checked", "SHT4x DS: 2.2uA avg @1meas/s high rep, 500uA measuring"),
    ("+3V3", "U11+U20", "2x TMP117", "0.007", "0.44",
     "checked", "TMP117 DS: 3.5uA @1Hz duty-cycle, 220uA active each"),
    ("+3V3", "U12", "MLX90632 (VDD 3.3V)", "0.0015", "1.4",
     "checked", "MLX90632 DS: 1.4mA max measuring, 2.5uA sleep"),
    ("+3V3", "U14", "LSM6DSV320XTR IMU", "0.8", "0.8",
     "checked", "LSM6DSV DS: 0.8mA HP gyro+low-g accel, 2.9uA power-down"),
    ("+3V3", "U25", "IM69D130 PDM mic", "1.3", "1.3",
     "checked", "IM69D130 DS: 1.3mA max @3.072MHz, 25-50uA standby"),
    ("+3V3", "U24", "RV-3028-C7 RTC", "0.00006", "0.001",
     "checked", "RV-3028 DS: 45-60nA time-keeping @3V"),
    ("+3V3", "U9", "PCA9306 VREF2 (3V3 side)", "0.01", "0.1",
     "checked", "PCA9306 DS: pass-gate Iq ~uA + pass current inferred"),
    ("+3V3", "U26", "SN74AXC2T245 VCCA side", "0.012", "0.014",
     "checked", "SN74AXC2T245 DS: ICCA+ICCB 16-23uA combined max"),
    ("+3V3", "D10", "NF2W757G white LED (AS7341 LDR pin)", "0", "25",
     "checked", "AS7341 LDR driver; NF2W757G ~70mA max; assumed <=25mA inferred"),
    ("+3V3", "D11+100R", "SFH4053 IR via R47 100R (Q1 sw)", "0", "15",
     "inferred", "(3.3-1.3Vf)/100R = 20mA max; SFH4053 70mA DC rating"),
    ("+3V3", "D12+100R", "XL0603NRC730 730nm via R109 (Q4 sw)", "0", "16",
     "inferred", "(3.3-1.7Vf)/100R = 16mA"),
    ("+3V3", "KT-0603G", "green status LED, GPIO17", "0", "3",
     "inferred", "series R limits ~3mA guess"),
    ("+3V3", "U4", "TPS7A20 LDO input (feeds +1V8)", "4", "20",
     "inferred", "= +1V8 load sum / pass ratio; see +1V8"),
    ("+3V3", "pull-ups", "I2C/logic pull-ups ~10x10k+2.2k etc", "0.5", "3",
     "inferred", "3.3V across ~10-15 resistors, worst bus-low"),
]
rows += v33

# ---------- +1V8 (TPS7A20 300 mA LDO, input = +3V3) ----------
v18 = [
    ("+1V8", "U10", "AS7341 VDD", "0.3", "0.3", "checked", "AS7341 DS: 210-300uA active, 5uA sleep"),
    ("+1V8", "U22", "MAX86141 VDD (C2+D2)", "1.17", "3.0", "checked", "MAX86141 DS: ~1.2mA VDD typ PPG; transients inferred"),
    ("+1V8", "U9", "PCA9306 VREF1 (1V8 side)", "0.01", "0.1", "checked", "leakage + pass current"),
    ("+1V8", "U26", "SN74AXC2T245 VCCB side", "0.011", "0.014", "checked", "shared 16-23uA budget"),
    ("+1V8", "R21/R22/R66", "pull-ups", "0.06", "1.5", "inferred", "2x2.2k + 301k worst bus-low"),
]
rows += v18

# ---------- TX_5V (TPS61240 5 V boost) ----------
tx5 = [
    ("TX_5V", "U6 TX1-4", "AFE4900 LED sinks -> SFH7072", "0", "800",
     "checked", "AFE4900 DS: 8-bit LED DAC up to 200mA/ch, 4 ch; worst 4x200=800mA inferred"),
    ("TX_5V", "U22 LED1-3", "MAX86141 LED drivers -> J10/LED_AN", "0", "372",
     "checked", "MAX86141 DS: 3 drivers, FS range 31/62/93/124mA -> 3x124=372mA inferred"),
    ("TX_5V", "U6 TX_SUP", "AFE4900 TX block quiescent", "0.5", "5",
     "inferred", "TX block bias while enabled"),
    ("TX_5V", "SFH7072 limit", "emitter pulsed ratings", "-", "300/1000",
     "checked", "SFH7072 DS: green 300mA, red 300mA, 2xIR 1A pulsed (D=0.005)"),
]
rows += tx5

# ---------- VDD_CP2102 (internal LDO out of U5, USB-powered) ----------
rows += [
    ("VDD_CP2102", "U5", "CP2102 internal 3.3V regulator out", "9.5", "13.7",
     "checked", "powers only CP2102 core + R62 pull; USB-only"),
]

# ---------- IC-internal rails (not board rails) ----------
rows += [
    ("AVDD_REG/DVDD_REG/IOVDD", "U8", "ADS1292R internal LDO taps", "-", "-", "checked", "decoupled only; drawn from +3V3_ANA"),
    ("VREF_1V82/VREF_2V5/VCAP1/2/VBIAS*", "U7", "AD5940 internal ref/bias", "-", "-", "checked", "internal; not board rails"),
    ("MX_VREF", "U22", "MAX86141 internal ref", "-", "-", "checked", "C83 only"),
    ("RTC_VBK", "U24", "RV-3028 backup pin", "-", "-", "checked", "R115 10k to VSS; no cell fitted"),
    ("VREFP", "U6", "AFE4900 internal ref", "-", "-", "checked", "internal"),
]

with open(os.path.join(DATA, "power_budget.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["rail", "ref", "load", "i_typ_mA", "i_peak_mA", "label", "source"])
    w.writerows(rows)

# ---------- Rail totals (numeric only) ----------
def num(s):
    try: return float(s)
    except: return 0.0

tot_typ, tot_peak = {}, {}
for r in rows:
    rail = r[0].split("->")[0]
    tot_typ[rail] = tot_typ.get(rail, 0) + num(r[3])
    tot_peak[rail] = tot_peak.get(rail, 0) + num(r[4])

REGS = {
    "VBUS": ("USB-C 5V input", "n/a (host supply)"),
    "VBAT": ("1S LiPo 402535", "cell ~4.2-3.0V"),
    "VBAT_SYS": ("OR of VBUS(D33)/VBAT(Q6)", "pass-through"),
    "+3V3": ("TPS63802 buck-boost", "2A out @VIN>=2.3V (DS feature line); 11uA Iq"),
    "+1V8": ("TPS7A20 LDO (300mA)", "VDO<=200mV@300mA; VIN=+3V3"),
    "TX_5V": ("TPS61240 boost", "500mA input valley I-limit; ~450mA out max (DS); ~250mA @3.2V inferred"),
    "VDD_CP2102": ("CP2102N internal LDO", "USB-powered only"),
}

print("\n## Rail totals (sum of listed loads; peak = everything-on worst case)\n")
print("| rail | regulator | rating | I_typ (mA) | I_peak (mA) | headroom vs peak |")
print("|---|---|---|---|---|---|")
for rail in ["+3V3", "+1V8", "TX_5V", "VDD_CP2102", "VBUS", "VBAT", "VBAT_SYS"]:
    reg, rating = REGS[rail]
    t, p = tot_typ.get(rail, 0), tot_peak.get(rail, 0)
    print(f"| {rail} | {reg} | {rating} | {t:.2f} | {p:.1f} | see REPORT |")

# ---------- Runtime estimates (402535 cell) ----------
CAP_MAH = 300.0   # assumed 402535 capacity, labelled assumption
ETA_VBAT = 0.85   # TPS63802+TPS61240 chain efficiency guess
VBAT_NOM = 3.7

# +3V3 load currents in each mode (mA @3V3)
def scen(name, i33, i18, itx5, note):
    # battery-side current = (P3V3+P1V8+PTX5)/(VBAT*eta)
    p = 3.3*i33 + 1.8*i18 + 5.0*itx5
    ibat = p/(VBAT_NOM*ETA_VBAT)
    hrs = CAP_MAH/ibat if ibat > 0 else float('inf')
    return (name, i33, i18, itx5, ibat, hrs, note)

sc = []
# deep sleep: ESP32 8uA + PSRAM 140uA + sensors sleep ~20uA + gauge 5uA + LDO/PMOS leaks ~10uA
sc.append(scen("deep sleep (RTC)", 0.19, 0.02, 0.0,
               "ESP32 8uA+140uA PSRAM; IMU/RTC/sensors sleep; boost+LDO EN off (guess)"))
# BLE continuous streaming: ESP32 ~80mA avg + sensors idle, no LEDs
sc.append(scen("BLE streaming cont.", 81.7, 1.5, 0.0,
               "ESP32 modem-sleep+BLE ~80mA; IMU 0.8mA; mic off; sensors idle"))
# periodic: 60s in 600s active window with PPG+LEDs staggered 200mA*10% duty avg on TX
sc.append(scen("1-min-in-10 sampling", 12.0, 1.5, 20.0,
               "60s/600s active: ESP32+all sensors ~110mA duty 10%; LEDs 200mA pk duty-capped"))
rt = []
for s in sc:
    rt.append(s)
    print(f"| {s[0]} | {s[4]:.3f} | {s[5]:.1f} | {s[6]} |")

with open(os.path.join(DATA, "runtime.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["scenario", "i_3v3_mA", "i_1v8_mA", "i_tx5v_mA", "i_bat_mA", "runtime_h", "note"])
    w.writerows(rt)

# ---------- BQ25170 ----------
KISET = 300.0; RISET = 1500.0
ichg = KISET/RISET*1000  # mA  (KISET in V-ish units: ICHG=KISET/RISET)
print(f"\nBQ25170: ICHG = {KISET:.0f}/{RISET:.0f}ohm = {ichg:.0f} mA  (DS: ICHG=KISET/RISET, KISET=270-330)")
print(f"VSET: R75=27k -> 4.20 V (DS Table 7-1)")
tc = CAP_MAH/ichg
print(f"Charge time: {CAP_MAH:.0f}mAh/{ichg:.0f}mA = {tc:.1f} h CC-only; +CV taper ~30-60min -> ~{tc+0.7:.1f} h")

# ---------- Boost headroom ----------
print("\n--- TX_5V boost headroom ---")
for vin in [4.2, 3.7, 3.2, 3.0]:
    iout = 0.5*vin*0.85/5.0*1000  # valley-limited rough estimate
    print(f"VIN={vin}V: est max Iout ~{iout:.0f} mA (Iin_valley 500mA * eta 0.85)")
print("Worst simultaneous demand: AFE4900 4x200=800 + MAX86141 3x124=372 = ~1172 mA")
print("Prior-analysis figure ~900mA -> either way >> ~200-400mA capability: STAGGER REQUIRED")
