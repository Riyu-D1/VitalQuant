#!/usr/bin/env python3
"""Fab-gate electrical adequacy computations for fix_v19 @ b9002188.

Inputs:
  reports/06_tool_output/01_netlist_pinmap_fixed.csv  (pad->net, board coords)
  reports/06_tool_output/BOM.csv                      (ref->value)
  reports/06_tool_output/BOM_JLC.csv                  (ref->MPN)

Outputs:
  reports/06_tool_output/decap_distances.csv
  reports/04_rails.csv
"""
import csv, math, os, sys

BASE = os.path.join(os.path.dirname(__file__), "..")
PINMAP = os.path.join(BASE, "06_tool_output/01_netlist_pinmap_fixed.csv")
BOM    = os.path.join(BASE, "06_tool_output/BOM.csv")
BOMJ   = os.path.join(BASE, "06_tool_output/BOM_JLC.csv")

pads = list(csv.DictReader(open(PINMAP)))

def xy(p): return (float(p['x_mm']), float(p['y_mm']))

# ---- BOM maps ---------------------------------------------------------
val = {}          # ref -> value string
for r in csv.DictReader(open(BOM)):
    refs = [x.strip() for x in r['Refs'].split(',')]
    for x in refs:
        val[x] = r['Value']

mpn = {}          # ref -> manufacturer PN
for row in csv.reader(open(BOMJ)):
    if not row or row[0] == 'Comment':
        continue
    refs = [x.strip() for x in row[1].split(',')]
    for x in refs:
        mpn[x] = row[6] if len(row) > 6 else ''

def dielectric(ref):
    m = mpn.get(ref, '')
    if 'CL05A' in m or 'CL10A' in m or 'R61A' in m or 'R60J' in m: return 'X5R'
    if 'CL05B' in m or 'X7R' in m or '0402B' in m: return 'X7R/C0G?'
    if 'CC0402KRX7R' in m: return 'X7R'
    return '?'

def dist(a, b):
    ax, ay = xy(a); bx, by = xy(b)
    return math.hypot(ax - bx, ay - by)

# ---- build indexes ----------------------------------------------------
by_ref = {}
for p in pads:
    by_ref.setdefault(p['ref'], []).append(p)

cap_pads = [p for p in pads if p['ref'].startswith('C') and p['net']]

def nearest_cap(ppad, net):
    best = None
    for c in cap_pads:
        if c['net'] != net:
            continue
        d = dist(ppad, c)
        if best is None or d < best[0]:
            best = (d, c)
    return best

def cap_other_net(cref, selfpad):
    for p in by_ref[cref]:
        if p['pad_number'] != selfpad:
            return p['net']
    return ''

# ---- pin roles --------------------------------------------------------
# (ref, pad) -> (pin_name, pin_kind); power pins only
ROLES = {
 ('U1','3'):  ('VDD33','vdd'),
 ('U2','1'):  ('IN','vin'), ('U2','8'): ('OUT','vbat_out'),
 ('U3','1'):  ('EN','en_vin'), ('U3','10'): ('VIN','vin'), ('U3','6'): ('VOUT','vout'),
 ('U4','1'):  ('IN','vin'), ('U4','3'): ('EN','en'), ('U4','5'): ('OUT','vout'),
 ('U5','1'):  ('VDD','vdd'), ('U5','2'): ('VDD','vdd'), ('U5','6'): ('VREGIN?VDD','vdd'),
 ('U5','7'):  ('VREGIN','vbus'), ('U5','8'): ('VBUS','sense'), ('U5','23'): ('VDD','vdd'), ('U5','27'): ('VDD','vdd'),
 ('U6','A4'): ('RX_SUP?','vdd'), ('U6','C1'): ('SUP','vdd'), ('U6','C4'): ('SUP','vdd'), ('U6','F5'): ('TX_SUP','vdd5'),
 ('U7','A4'): ('AVDD/DVDD','vdd'), ('U7','F2'): ('AVDD/DVDD','vdd'), ('U7','F3'): ('AVDD/DVDD','vdd'),
 ('U7','G2'): ('IOVDD','vdd'), ('U7','D8'): ('AVDD_REG','ldo_out'), ('U7','G3'): ('DVDD_REG','ldo_out'),
 ('U7','A5'): ('VREF_1V82','ref'), ('U7','D7'): ('VREF_2V5','ref'), ('U7','C7'): ('VBIAS0','ref'),
 ('U7','D1'): ('VBIAS_CAP','ref'), ('U7','B7'): ('VZERO0','ref'),
 ('U8','9'):  ('VREFP','ref'), ('U8','11'): ('VCAP1','bypass'), ('U8','27'): ('VCAP2','bypass'),
 ('U8','12'): ('AVDD','vdd'), ('U8','14'): ('CLKSEL','digital'), ('U8','23'): ('DVDD','vdd'),
 ('U9','2'):  ('VREF1','vdd'), ('U9','7'): ('VREF2','vdd'), ('U9','8'): ('EN','en'),
 ('U10','1'): ('VDD','vdd'),
 ('U11','5'): ('V+','vdd'),
 ('U12','2'): ('VDD','vdd'),
 ('U13','6'): ('VDDIO','vdd'), ('U13','8'): ('VDD','vdd'), ('U13','2'): ('CSB','digital'),
 ('U14','5'): ('VDDIO','vdd'), ('U14','8'): ('VDD','vdd'), ('U14','12'): ('CS','digital'),
 ('U15','A1'): ('VIN','vin'), ('U15','B2'): ('VOUT','vout'), ('U15','C2'): ('VOUT','vout'), ('U15','C1'): ('EN','en'),
 ('U16','4'): ('LED_ANODE','vdd5'), ('U16','5'): ('LED_ANODE','vdd5'), ('U16','7'): ('LED_ANODE','vdd5'), ('U16','11'): ('LED_ANODE','vdd5'),
 ('U17','2'): ('VDD','vbat'), ('U17','3'): ('VDD','vbat'),
 ('U18','3'): ('WP#/IO2','pullup'), ('U18','7'): ('HOLD#/IO3','pullup'), ('U18','8'): ('VCC','vdd'),
 ('U19','14'): ('VCCP','vdd'), ('U19','15'): ('VCCI','vdd'),
 ('U20','4'): ('ADD0=V+','vdd'), ('U20','5'): ('V+','vdd'),
 ('U22','A1'): ('VLED','vdd5'), ('U22','C2'): ('VDD','vdd'), ('U22','D2'): ('VDD','vdd'),
 ('U23','3'): ('VDD','vdd'),
 ('U24','6'): ('VBACKUP','vbackup'), ('U24','7'): ('VDD','vdd'),
 ('U25','2'): ('VDD','vdd'),
 ('U26','6'): ('VCCB','vdd'), ('U26','7'): ('VCCA','vdd'), ('U26','10'): ('DIR1','digital'),
}

# datasheet rule for specific pins -> cite string
RULES = {
 ('U2','1'):  'BQ25170: bypass IN >=1uF, placed close to IC',
 ('U2','8'):  'BQ25170: bypass OUT >=1uF, placed close to IC',
 ('U3','10'): 'TPS63802: 10uF X5R/X7R as close as possible to VIN/PGND',
 ('U3','6'):  'TPS63802: output cap as close as possible; CO,eff>=7uF',
 ('U4','1'):  'TPS7A20: input cap >=nominal, as close to IN/GND as possible',
 ('U4','5'):  'TPS7A20: >=1uF ceramic, as close to OUT/GND as possible',
 ('U5','1'):  'CP2102N: 4.7uF+0.1uF per power pin, as close as possible',
 ('U5','6'):  'CP2102N: 4.7uF+0.1uF per power pin, as close as possible',
 ('U5','23'): 'CP2102N: 4.7uF+0.1uF per power pin, as close as possible',
 ('U9','7'):  'PCA9306: 100pF filter cap on VREF2 recommended, as close as possible -> NONE FITTED',
 ('U10','1'): 'AS7341: decouple VDD 100nF (typical)',
 ('U11','5'): 'TMP117: 0.1uF as close as possible to V+',
 ('U12','2'): 'MLX90632: bypass cap close to VDD',
 ('U13','6'): 'BME280: 100nF each at VDDIO+VDD (Bosch)',
 ('U13','8'): 'BME280: 100nF each at VDDIO+VDD (Bosch)',
 ('U14','5'): 'LSM6DSV80X: C2=100nF as near as possible to VDDIO',
 ('U14','8'): 'LSM6DSV80X: C1=100nF as near as possible to VDD',
 ('U15','A1'):'TPS61240: >=2.2uF ceramic as close as possible to VIN',
 ('U15','B2'):'TPS61240: COUT=4.7uF ref design, close to VOUT',
 ('U17','2'): 'MAX17048: 100nF close to VDD (typical)',
 ('U17','3'): 'MAX17048: 100nF close to VDD (typical)',
 ('U18','8'): 'W25Q512: 100nF close to VCC (typical)',
 ('U19','14'):'TCA6408: bypass cap on supply (typical)',
 ('U19','15'):'TCA6408: bypass cap on supply (typical)',
 ('U23','3'): 'SHT45: 100nF between VDD/VSS (Sensirion)',
 ('U24','7'): 'RV-3028: decoupling at VDD (typical)',
 ('U25','2'): 'IM69D130: 100nF at VDD (typical)',
 ('U1','3'):  'ESP32-S3: 100nF close to VDD + bulk >=10uF (Espressif HW guideline)',
 ('U8','12'): 'ADS1292R: AVDD bypass close to pin (typical)',
 ('U8','23'): 'ADS1292R: DVDD bypass close to pin (typical)',
 ('U6','A4'): 'AFE4900: RX_SUP bypass close to pin (TI)',
 ('U6','C1'): 'AFE4900: supply bypass close to pin (TI)',
 ('U6','C4'): 'AFE4900: supply bypass close to pin (TI)',
 ('U6','F5'): 'AFE4900: TX_SUP bypass close to pin (TI)',
 ('U22','C2'):'MAX86141: VDD bypass close to pin (Maxim)',
 ('U22','D2'):'MAX86141: VDD bypass close to pin (Maxim)',
 ('U22','A1'):'MAX86141: VLED bypass close to pin (Maxim)',
 ('U26','6'): 'AXC2T245: 100nF each VCC pin close (TI)',
 ('U26','7'): 'AXC2T245: 100nF each VCC pin close (TI)',
}

rows = []
for (ref, padn), (pname, kind) in sorted(ROLES.items()):
    pp = [p for p in by_ref.get(ref, []) if p['pad_number'] == padn]
    if not pp:
        rows.append([ref, padn, pname, kind, 'PAD_NOT_FOUND', '', '', '', '', '', '', '', '', ''])
        continue
    p = pp[0]
    net = p['net']
    bc = nearest_cap(p, net)
    if bc is None:
        rows.append([ref, padn, pname, kind, net, '', '', '', '', '', '', '', 'NO_CAP_ON_NET', ''])
        continue
    d, cp = bc
    cref = cp['ref']
    other = cap_other_net(cref, cp['pad_number'])
    flag = ''
    supply_kind = kind in ('vdd','vdd5','vbat','vin','vout','ref','bypass','ldo_out')
    if supply_kind and d > 10.0:
        flag = '>10MM'
    elif other != 'GND':
        flag = f'CAP_OTHER_PAD={other}'
    rule = RULES.get((ref, padn), '')
    rows.append([ref, padn, pname, kind, net, cref, val.get(cref,'?'), mpn.get(cref,'?'),
                 dielectric(cref), f"{d:.2f}", f"{cp['x_mm']},{cp['y_mm']}", other, flag, rule])

with open(os.path.join(BASE, '06_tool_output/decap_distances.csv'), 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['ic_ref','pad','pin_name','pin_kind','net','nearest_cap','cap_value','cap_mpn','cap_dielectric',
                'dist_mm','cap_pad_xy','cap_other_pad_net','flag','datasheet_rule'])
    w.writerows(rows)
print(f"wrote {len(rows)} decap rows")

# ====================================================================
# Rail map
# ====================================================================
POWER_NETS = {
 'VBUS':      dict(nom=5.0,  lo=4.75, hi=5.25, src='J1 USB-C (external), fused to U2.IN/D33/Q6.gate'),
 'VBAT':      dict(nom=None, lo=3.0,  hi=4.20, src='J2 LiPo / U2 BQ25170 OUT pin8 (VREG=4.20V via R75=27k)'),
 'VBAT_SYS':  dict(nom=None, lo=3.0,  hi=5.00, src='D33 1N5819 (VBUS, USB) OR Q6 AO3401 (VBAT); R93 0R DNP'),
 '+3V3':      dict(nom=3.30, lo=3.234,hi=3.366,src='U3 TPS63802 pin6 VOUT (VFB=500mV, R23/R24=560k/100k, +/-1%)'),
 '+3V3_ESP':  dict(nom=3.30, lo=3.234,hi=3.366,src='+3V3 via R94 0R 0603'),
 '+3V3_ANA':  dict(nom=3.30, lo=3.234,hi=3.366,src='+3V3 via R95 0R 0402 (no filtering)'),
 '+1V8_LDO':  dict(nom=1.80, lo=1.773,hi=1.827,src='U4 TPS7A2018 pin5 OUT (+/-1.5%)'),
 '+1V8':      dict(nom=1.80, lo=1.773,hi=1.827,src='+1V8_LDO via R97 0R'),
 'TX_5V_RAW': dict(nom=5.00, lo=4.90, hi=5.10, src='U15 TPS61240 B2/C2 VOUT (5.0V +/-2%)'),
 'TX_5V':     dict(nom=5.00, lo=4.90, hi=5.10, src='TX_5V_RAW via R96 0R'),
 'VDD_CP2102':dict(nom=3.30, lo=3.0,  hi=3.6,  src='U5 CP2102N internal LDO (VREGIN=VBUS 5V)'),
 'LED_AN':    dict(nom=5.00, lo=4.90, hi=5.10, src='TX_5V via R114 0R'),
 'LED_A':     dict(nom=None, lo=0,    hi=3.3,  src='U1 GPIO21 STATUS_LED via R88 150R'),
 'RTC_VBK':   dict(nom=0.0,  lo=0,    hi=0,    src='R115 10k to GND (backup disabled)'),
 'VREF2':     dict(nom=3.30, lo=3.234,hi=3.366,src='+3V3 via R18 200k (PCA9306 EN/VREF2)'),
 'IOVDD':     dict(nom=3.30, lo=3.234,hi=3.366,src='+3V3_ANA via R12 10R'),
 'AVDD_REG':  dict(nom=1.80, lo=None, hi=None, src='U7 AD5940 internal LDO (out)'),
 'DVDD_REG':  dict(nom=1.80, lo=None, hi=None, src='U7 AD5940 internal LDO (out)'),
 'VREF_1V82': dict(nom=1.82, lo=None, hi=None, src='U7 AD5940 internal ref (out)'),
 'VREF_2V5':  dict(nom=2.50, lo=None, hi=None, src='U7 AD5940 internal ref (out)'),
 'VZERO0':    dict(nom=None, lo=None, hi=None, src='U7 AD5940 internal (out)'),
 'VBIAS0':    dict(nom=None, lo=None, hi=None, src='U7 AD5940 internal (out)'),
 'VBIAS_CAP': dict(nom=None, lo=None, hi=None, src='U7 AD5940 internal (out)'),
 'VREFP':     dict(nom=2.42, lo=None, hi=None, src='U8 ADS1292R internal ref (out), C30=10uF'),
 'VCAP1':     dict(nom=None, lo=None, hi=None, src='U8 ADS1292R internal (bypass C32=1uF)'),
 'VCAP2':     dict(nom=None, lo=None, hi=None, src='U8 ADS1292R internal (bypass C33=1uF)'),
 'MX_VREF':   dict(nom=None, lo=None, hi=None, src='U22 MAX86141 internal ref (out)'),
 'CP_VBUS':   dict(nom=3.41, lo=3.24, hi=3.58, src='VBUS via R63/R64 22.1k/47.5k divider'),
 'VBUS_DET':  dict(nom=3.00, lo=2.85, hi=3.15, src='VBUS via R50/R51 100k/150k divider'),
 'GND':       dict(nom=0.0,  lo=0,    hi=0,    src='reference'),
}

# datasheet spec per load pin: (ref,pad) or ref -> (label,Vmin,Vmax_op,absmax)
LOAD_SPEC = {
 ('U1','3'):  ('ESP32-S3-MINI VDD33',       3.0, 3.6, 3.6),
 ('U2','1'):  ('BQ25170 IN',                3.0, 6.6, 7.0),
 ('U3','1'):  ('TPS63802 EN(=VIN)',         1.3, 5.5, 6.0),
 ('U3','10'): ('TPS63802 VIN',              1.3, 5.5, 6.0),
 ('U4','1'):  ('TPS7A2018 IN',              1.6, 6.0, 6.5),
 ('U4','3'):  ('TPS7A2018 EN',              0,   6.0, 6.5),
 ('U5','1'):  ('CP2102N VDD',               3.0, 3.6, 4.2),
 ('U5','2'):  ('CP2102N VDD',               3.0, 3.6, 4.2),
 ('U5','6'):  ('CP2102N VDD',               3.0, 3.6, 4.2),
 ('U5','23'): ('CP2102N VDD',               3.0, 3.6, 4.2),
 ('U5','27'): ('CP2102N VDD',               3.0, 3.6, 4.2),
 ('U5','7'):  ('CP2102N VREGIN',            3.6, 5.25, 5.8),
 ('U5','8'):  ('CP2102N VBUS sense (VIH=VIO-0.6=2.7V)', 0, 5.25, 5.8),
 ('U6','A4'): ('AFE4900 RX_SUP/IO',         2.0, 3.6, None),
 ('U6','C1'): ('AFE4900 supply',            2.0, 3.6, None),
 ('U6','C4'): ('AFE4900 supply',            2.0, 3.6, None),
 ('U6','F5'): ('AFE4900 TX_SUP',            3.0, 5.25, None),
 ('U7','A4'): ('AD5940 AVDD/DVDD',          2.8, 3.6, 3.9),
 ('U7','F2'): ('AD5940 AVDD/DVDD',          2.8, 3.6, 3.9),
 ('U7','F3'): ('AD5940 AVDD/DVDD',          2.8, 3.6, 3.9),
 ('U7','G2'): ('AD5940 IOVDD (2.8-3.6 opt)',2.8, 3.6, 3.9),
 ('U8','9'):  ('ADS1292R VREFP (2.42V int)',0,  None, None),
 ('U8','11'): ('ADS1292R VCAP1 bypass',     0,  None, None),
 ('U8','27'): ('ADS1292R VCAP2 bypass',     0,  None, None),
 ('U8','12'): ('ADS1292R AVDD',             2.7, 3.6, 5.5),
 ('U8','14'): ('ADS1292R CLKSEL (=AVDD)',   0,   3.6, 3.9),
 ('U8','23'): ('ADS1292R DVDD',             1.65,3.6, 3.9),
 ('U9','2'):  ('PCA9306 VREF1',             1.2, 5.5, 7.0),
 ('U9','7'):  ('PCA9306 VREF2',             0,   5.5, 7.0),
 ('U9','8'):  ('PCA9306 EN',                0,   5.5, 7.0),
 ('U10','1'): ('AS7341 VDD',                1.7, 2.0, 2.2),
 ('U11','5'): ('TMP117 V+',                 1.7, 5.5, 6.0),
 ('U12','2'): ('MLX90632 VDD',              3.0, 3.6, 5.0),
 ('U13','2'): ('BME280 CSB (=VDDIO)',       0,   3.6, None),
 ('U13','6'): ('BME280 VDDIO',              1.2, 3.6, None),
 ('U13','8'): ('BME280 VDD',                1.71,3.6, None),
 ('U14','5'): ('LSM6DSV80X VDDIO',          1.62,3.6, None),
 ('U14','8'): ('LSM6DSV80X VDD',            1.71,3.6, None),
 ('U14','12'):('LSM6DSV80X CS (=VDDIO)',    0,   3.6, None),
 ('U15','A1'):('TPS61240 VIN',              2.3, 5.5, None),
 ('U15','B2'):('TPS61240 VOUT',             0,   5.5, None),
 ('U15','C2'):('TPS61240 VOUT',             0,   5.5, None),
 ('U15','C1'):('TPS61240 EN',               0,   5.5, None),
 ('U16','4'): ('SFH7072 LED anode',         None,None,None),
 ('U16','5'): ('SFH7072 LED anode',         None,None,None),
 ('U16','7'): ('SFH7072 LED anode',         None,None,None),
 ('U16','11'):('SFH7072 LED anode',         None,None,None),
 ('U17','2'): ('MAX17048 VDD/CELL',         2.5, 4.5, None),
 ('U17','3'): ('MAX17048 VDD/CELL',         2.5, 4.5, None),
 ('U18','3'): ('W25Q512 WP# pullup',        0,   3.6, 4.6),
 ('U18','7'): ('W25Q512 HOLD# pullup',      0,   3.6, 4.6),
 ('U18','8'): ('W25Q512 VCC',               2.7, 3.6, 4.6),
 ('U19','14'):('TCA6408A VCCP',             1.65,5.5, 6.5),
 ('U19','15'):('TCA6408A VCCI',             1.65,5.5, 6.5),
 ('U20','4'): ('TMP117 ADD0=V+',            0,   5.5, 6.0),
 ('U20','5'): ('TMP117 V+',                 1.7, 5.5, 6.0),
 ('U22','A1'):('MAX86141 VLED',             3.1, 5.5, 6.0),
 ('U22','C2'):('MAX86141 VDD',              1.7, 2.0, 2.2),
 ('U22','D2'):('MAX86141 VDD',              1.7, 2.0, 2.2),
 ('U23','3'): ('SHT45 VDD',                 2.15,3.6, None),
 ('U24','6'): ('RV-3028 VBACKUP (to VSS ok)',0,  5.5, None),
 ('U24','7'): ('RV-3028 VDD',               1.2, 5.5, None),
 ('U25','2'): ('IM69D130 VDD',              1.62,3.6, None),
 ('U26','6'): ('SN74AXC2T245 VCCB',         0.65,3.6, 4.2),
 ('U26','7'): ('SN74AXC2T245 VCCA',         0.65,3.6, 4.2),
 ('U26','10'):('SN74AXC2T245 DIR1(=VCCA)',  0,   3.6, 4.2),
}

rail_rows = []
for net in sorted(POWER_NETS):
    spec = POWER_NETS[net]
    pads_on = [p for p in pads if p['net'] == net]
    # load pins = only pads that are actual power pins per ROLES
    loads = [(p['ref'], p['pad_number']) for p in pads_on
             if (p['ref'], p['pad_number']) in ROLES]
    n_non_ic = sum(1 for p in pads_on if not p['ref'].startswith('U'))
    if not loads:
        loads = [('', '')]
    for (lref, lpad) in loads:
        role = ROLES.get((lref, lpad), ('', ''))[0]
        ds = LOAD_SPEC.get((lref, lpad), LOAD_SPEC.get(lref, ('',None,None,None)))
        pin_nom = spec['nom']
        margin_lo = margin_hi = ''
        flag = ''
        if pin_nom is not None and ds[1] is not None:
            margin_lo = f"{pin_nom - ds[1]:.2f}"
            if ds[2] is not None:
                margin_hi = f"{ds[2] - pin_nom:.2f}"
            if spec['hi'] is not None and ds[3] is not None and spec['hi'] > ds[3]:
                flag = f"ABOVE_ABSMAX rail_hi={spec['hi']} > absmax={ds[3]}"
            elif spec['hi'] is not None and ds[2] is not None and spec['hi'] > ds[2]:
                flag = f"ABOVE_OPMAX rail_hi={spec['hi']} > Vmax_op={ds[2]}"
        rail_rows.append([net, pin_nom if pin_nom is not None else 'int',
                          spec['lo'], spec['hi'], spec['src'],
                          lref, lpad, role, ds[0], ds[1], ds[2], ds[3],
                          margin_lo, margin_hi, flag, len(pads_on), n_non_ic])

with open(os.path.join(BASE, '04_rails.csv'), 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['net','nominal_V','rail_lo_V','rail_hi_V','source',
                'load_ref','load_pad','pin_name','load_device_spec',
                'spec_Vmin','spec_Vmax','spec_absmax','margin_min_V','margin_max_V',
                'flag','pads_on_net','non_ic_pads_on_net'])
    w.writerows(rail_rows)
print(f"wrote {len(rail_rows)} rail rows")
