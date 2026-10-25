#!/usr/bin/env python3
"""Apply PROMPT_FIX component edits to fix_v1 copies. v2 - keyed on Reference."""
import re, csv
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PARENT = ROOT.parent
SCH, PCB = ROOT / 'vitalquant_v2.kicad_sch', ROOT / 'vitalquant_v2.kicad_pcb'

# comment(BOM row) -> resolved LCSC / marker
ROW = {
    '4.7u': {  # two groups share comment; split by footprint
        'C_0603_1608Metric': 'C23124', 'C_0402_1005Metric': 'C23733'},
    '1u': {'C_0603_1608Metric': 'C15849', 'C_0402_1005Metric': 'C52923'},
    '22u': 'C23677', '100n': 'C1525', '470n': 'C15115', '10u':
        {'C_0402_1005Metric': 'C15525', 'C_0603_1608Metric': 'C19702'},
    '47n': 'C23692', '4.7n': 'C23691', '2.2u': 'C49240', '2.2n': 'C22299',
    '15n': 'C22314', '1.5n': 'C1528', '47u': 'C23230',
    'TPD1E10B06': 'C48260', 'NF2W757G-F1': 'C17610390',
    'SFH4053': 'C2900307', 'SFH 4735 730nm': 'CONSIGNED',
    '0.47u': 'C668312', '1.0u': 'C337892',
    'CSD13380F3T': 'C2871092', 'BC847BS,115': 'C156292',
    '5.1k': 'C25905', '1.5k': 'C22879', '10k': 'C25744', '1k': 'C11702',
    '10': 'C25513', '1M': 'C25797', '200k': 'C26015', '4.7k': 'C25900',
    '560k': 'C25840', '100k': 'C25741', '10M': 'C17413', '40.2k': 'C17275',
    '51k': 'C146900', '5.11M': 'C283954', '100': 'C25084',
    '22.1k': 'C853138', '47.5k': 'C25896', '301k': 'C6097400',
    '27.0k': 'C25779',
    '0': {'R_0402_1005Metric': 'C17168', 'R_0603_1608Metric': 'C21189',
          'R_2512_6332Metric': 'C18009'},
    'TS-1088': 'C720477',
    'ESP32-S3-MINI-1-N4R2': 'C3013941', 'BQ25170DSGR': 'C5208201',
    'TPS63802DLAR': 'C2845237', 'TPS7A2018PDBVR': 'C963430',
    'CP2102N-A02-GQFN28R': 'C964632', 'AFE4900YZR': 'C2651595',
    'AD5940BCBZ-RL7': 'C650308', 'ADS1292RIRSMT': 'C882777',
    'PCA9306DCUR': 'C33196', 'AS7341-DLGM': 'C2649486',
    'TMP117AIDRVR': 'C699536', 'MLX90632SLD-DCB-100-SP': 'CONSIGNED',
    'BME280': 'C92489', 'LSM6DSV80XTR': 'C46898862',
    'TPS61240YFFR': 'C2070904', 'SFH 7072': 'C2655172',
    'MAX17048G+T10': 'C2682616', 'W25Q512JVEIQ': 'C2986165',
    'TCA6408ARSVR': 'C2649390', 'USBLC6-2SC6': 'C7519',
    'MAX86141ENP+': 'C5328762', 'SHT45-AD1B': 'C6705435',
    'RV-3028-C7': 'C2829066', 'IM69D130': 'C536262',
    'SN74AXC2T245RSWR': 'C1882550', 'TYPE-C-31-M-12': 'C165948',
    'KT-0603G': 'C12624', 'FH12-14S': 'C5378715',
}

VALUE_CHANGE = {'D10': 'JB3030UWT-B-H50EA0000-1Z000001', 'D12': 'XL0603NRC730'}
FOOTPRINT_CHANGE = {'D12': 'LED_SMD:LED_0603_1608Metric'}
DNP_POP = {'J12', 'R61', 'R104', 'R105', 'R107', 'R108'}
EXTRA_LCSC = {'J12': 'C5378715', 'R61': 'C25744', 'R104': 'C17168',
              'R105': 'C17168', 'R107': 'C25741', 'R108': 'C25741', 'J1': 'C165948'}

# ---- ref -> LCSC from parent fab BOM groups ----
ref2lcsc = {}
for r in csv.DictReader(open(PARENT / 'fab/BOM.csv')):
    rule = ROW.get(r['Comment'])
    if rule is None:
        continue
    n = rule[r['Footprint']] if isinstance(rule, dict) else rule
    for ref in re.split(r',\s*', r['Designator'].strip('"')):
        ref2lcsc[ref.strip()] = n
ref2lcsc.update(EXTRA_LCSC)
ref2lcsc['D10'], ref2lcsc['D12'], ref2lcsc['U12'] = 'C17610390', 'CONSIGNED', 'CONSIGNED'

def blocks(t, key):
    out = []; i = 0
    tag = '(' + key
    while True:
        j = t.find(tag, i)
        if j < 0 or not t[j + len(tag):j + len(tag) + 1] in (' ', '\n', '\t'):
            if j < 0:
                break
        d = 0; k = j
        while k < len(t):
            if t[k] == '(':
                d += 1
            elif t[k] == ')':
                d -= 1
                if d == 0:
                    break
            k += 1
        out.append((j, k + 1, t[j:k + 1])); i = k + 1
    return out

sch = SCH.read_text()
# operate only on INSTANCE symbols (those having a "Reference" prop != lib def);
# lib_symbols entries also match '(symbol' but appear inside (lib_symbols ...) span.
ls = re.search(r'\(lib_symbols', sch)
ls_end = 0
if ls:
    d = 0; k = ls.start()
    while k < len(sch):
        if sch[k] == '(':
            d += 1
        elif sch[k] == ')':
            d -= 1
            if d == 0:
                break
        k += 1
    ls_end = k + 1

parts = []
for s, e, b in blocks(sch, 'symbol'):
    if s < ls_end:
        continue
    m = re.search(r'property "Reference" "([^"]+)"', b)
    if not m:
        continue
    ref = m.group(1)
    nb = b
    if ref in VALUE_CHANGE:
        nb = re.sub(r'(property "Value" )"[^"]*"', r'\1"' + VALUE_CHANGE[ref] + '"', nb, 1)
    if ref in FOOTPRINT_CHANGE:
        nb = re.sub(r'(property "Footprint" )"[^"]*"', r'\1"' + FOOTPRINT_CHANGE[ref] + '"', nb, 1)
    if ref in DNP_POP:
        nb = nb.replace('(dnp yes)', '(dnp no)')
    want = ref2lcsc.get(ref)
    if want and want != 'CONSIGNED':
        if re.search(r'property "LCSC" "[^"]*"', nb):
            nb = re.sub(r'(property "LCSC" )"[^"]*"', r'\1"' + want + '"', nb, 1)
        else:
            at = re.search(r'\(at ([\d.\-]+) ([\d.\-]+)', nb)
            ax, ay = (at.group(1), at.group(2)) if at else ('0', '0')
            prop = (f'\n\t\t(property "LCSC" "{want}"\n\t\t\t(at {ax} {ay} 0)\n'
                    '\t\t\t(hide yes)\n\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n'
                    '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)')
            vm = re.search(r'\(property "Value" "[^"]*"', nb)
            if vm:
                d = 0; k = vm.start()
                while k < len(nb):
                    if nb[k] == '(':
                        d += 1
                    elif nb[k] == ')':
                        d -= 1
                        if d == 0:
                            break
                    k += 1
                end = k + 1
            else:
                end = len(nb)
            nb = nb[:end] + prop + nb[end:]
    parts.append((s, e, nb))

out = []; last = 0
for s, e, b in parts:
    out.append(sch[last:s]); out.append(b); last = e
out.append(sch[last:])
sch = ''.join(out)
SCH.write_text(sch)

pcb = PCB.read_text()

def fp_block(t, ref):
    for s, e, b in blocks(t, 'footprint'):
        if f'property "Reference" "{ref}"' in b:
            return s, e, b

s, e, b = fp_block(pcb, 'D12')
nb = b.replace('(footprint "LED_SMD:LED_0402_1005Metric"',
               '(footprint "LED_SMD:LED_0603_1608Metric"', 1)
nb = nb.replace('(at -0.485 0 180)\n\t\t\t(size 0.59 0.64)',
                '(at -0.7875 0 180)\n\t\t\t(size 1.025 0.95)')
nb = nb.replace('(at 0.485 0 180)\n\t\t\t(size 0.59 0.64)',
                '(at 0.7875 0 180)\n\t\t\t(size 1.025 0.95)')
nb = nb.replace('(start -0.93 0.47)\n\t\t\t(end 0.93 -0.47)',
                '(start -1.21 0.76)\n\t\t\t(end 1.21 -0.76)')
nb = nb.replace('(property "Value" "SFH 4735 730nm"', '(property "Value" "XL0603NRC730"')
nb = nb.replace('LED_SMD.3dshapes/LED_0402_1005Metric.step',
                'LED_SMD.3dshapes/LED_0603_1608Metric.step')
assert '1.025' in nb and 'XL0603NRC730' in nb
pcb = pcb[:s] + nb + pcb[e:]

s, e, b = fp_block(pcb, 'D10')
pcb = pcb[:s] + b.replace('(property "Value" "NF2W757G-F1"',
                          '(property "Value" "JB3030UWT-B-H50EA0000-1Z000001"') + pcb[e:]

for ref in DNP_POP:
    s, e, b = fp_block(pcb, ref)
    nb = b.replace('(attr smd dnp)', '(attr smd)')
    assert nb != b, ref
    pcb = pcb[:s] + nb + pcb[e:]

PCB.write_text(pcb)

n_lcsc = sch.count('(property "LCSC"')
print('LCSC properties in schematic:', n_lcsc)
print('unresolved BOM refs:', [r for r in ref2lcsc if ref2lcsc[r] == 'CONSIGNED'])
