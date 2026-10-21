#!/usr/bin/env python3
"""Round 8 automated pin cross-check.
Leg A: every 'pin N -> net X' row in every pin_audit file vs live PCB pad nets.
Leg B: claimed pin NAME vs schematic symbol pin name (DS-derived) at that pin number.
"""
import pcbnew, re, os, sys, json

ROOT = os.path.dirname(os.path.abspath(__file__))
BOARD = os.path.join(ROOT, '..', 'vitalq_v2.kicad_pcb')
SYM = os.path.join(ROOT, '..', 'vitalq.kicad_sym')

b = pcbnew.LoadBoard(os.path.abspath(BOARD))
fpmap = {}   # ref -> {padnum -> netname}
for fp in b.GetFootprints():
    fpmap[fp.GetReference()] = {p.GetNumber(): p.GetNetname() for p in fp.Pads()}

# symbol pin name map from vitalq.kicad_sym: symbol name -> {pinnum -> pinname}
symtxt = open(SYM).read()
libpins = {}
for m in re.finditer(r'\(symbol "([^"]+)"(.*?)(?=\n\t\t\(symbol|\Z)', symtxt, re.S):
    name, body = m.group(1), m.group(2)
    pins = {}
    for pm in re.finditer(r'\(pin \w+ \w+ \(at [^)]+\) \(length [^)]+\)[^)]*\(name "([^"]*)"[^)]*\(number "([^"]*)"', body):
        pins[pm.group(2)] = pm.group(1)
    for pm in re.finditer(r'\(pin [^)]*?\(name "([^"]*)"[^)]*?\(number "([^"]*)"', body):
        pins.setdefault(pm.group(2), pm.group(1))
    if pins:
        libpins[name] = pins

# ref -> sch symbol name (from board footprint 'value' is part name; better: sch instance)
sch = open(os.path.join(ROOT, '..', 'vitalq_v2.kicad_sch')).read()
reflib = {}
for m in re.finditer(r'\(symbol\s*\n\s*\(lib_id "vitalq:([^"]+)"\)(.*?)\(instances', sch, re.S):
    lib, body = m.group(1), m.group(2)
    rm = re.search(r'\(property "Reference" "([^"]+)"', body)
    if rm:
        reflib[rm.group(1)] = lib

NETOK = re.compile(r'^(unconnected|—|-|nc)$', re.I)

def norm_net(t):
    t = t.strip().strip('`').strip()
    return t

rows_checked = 0
problems = []
for fn in sorted(os.listdir(ROOT.replace('pin_audit_r8', 'pin_audit'))) + \
          sorted(os.listdir(ROOT.replace('pin_audit_r8', 'pin_audit_r6'))) + \
          sorted(os.listdir(ROOT.replace('pin_audit_r8', 'pin_audit_r7'))):
    pass

def scan(folder):
    global rows_checked
    for fn in sorted(os.listdir(folder)):
        if not (fn.startswith('U') and fn.endswith('.md')):
            continue
        ref = fn.split('_')[0].split('.')[0]
        if ref not in fpmap:
            continue
        pads = fpmap[ref]
        libname = reflib.get(ref)
        lib = libpins.get(libname, {}) if libname else {}
        txt = open(os.path.join(folder, fn)).read()
        for ln, line in enumerate(txt.splitlines(), 1):
            if not line.strip().startswith('|'):
                continue
            cells = [c.strip() for c in line.strip().strip('|').split('|')]
            if len(cells) < 3 or cells[0] in ('Pin', '-----', ':---') or cells[0].startswith('---'):
                continue
            pinfield = cells[0]
            for pin in re.findall(r'[A-Z]?\d+|EP', pinfield):
                if pin not in pads:
                    continue
                claimed_net = None
                for c in cells[1:]:
                    t = norm_net(c)
                    if t in pads.values() or re.match(r'^unconnected', t):
                        claimed_net = t
                if claimed_net is None:
                    continue
                rows_checked += 1
                actual = pads[pin]
                ok_net = (claimed_net == actual) or \
                         (claimed_net.startswith('unconnected') and actual.startswith('unconnected'))
                if not ok_net:
                    problems.append((folder, fn, ln, ref, pin, claimed_net, actual))
                # leg B: pin name check — the 'Function'/'Pad' cell
                namecell = cells[1] if len(cells) > 1 else ''
                nm = re.sub(r'_\d+$', '', namecell).replace('~{', '').replace('}', '').strip('` ')
                libnm = lib.get(pin)
                if libnm and nm and nm not in ('—', '-', 'nc') and libnm not in ('—',''):
                    ln2 = libnm.replace('~{', '').replace('}', '')
                    if nm.upper().rstrip('_0123456789') != ln2.upper().rstrip('_0123456789') and \
                       nm.upper() != ln2.upper():
                        problems.append((folder, fn, ln, ref, pin, 'name:' + nm, 'lib:' + libnm))

for fld in ['pin_audit', 'pin_audit_r6', 'pin_audit_r7']:
    scan(os.path.join(ROOT, '..', fld))
print('rows checked:', rows_checked)
print('problems:', len(problems))
for p in problems:
    print(' ', p[1], 'line', p[2], p[3] + '.' + p[4], 'claimed', p[5], 'actual', p[6])
json.dump([{'file': p[1], 'line': p[2], 'pin': p[3] + '.' + p[4], 'claimed': p[5], 'actual': p[6]} for p in problems],
          open(os.path.join(ROOT, 'crosscheck_report.json'), 'w'), indent=1)
