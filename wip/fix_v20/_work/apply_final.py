#!/usr/bin/env python3
"""fix_v18 final board build: additive power-path (Q6 AO3401A, D33 1N5819WS,
R123 100k) + east Edge.Cuts tab + verified In3 VBUS route.
Run from fix_v18 dir on a pristine copy of fix_v16's kicad_pcb."""
import re, sys, uuid, math
sys.path.insert(0, '_work')
from pcb import blocks

T = open('vitalq_v2.kicad_pcb').read()
NEW = []
def U(): return str(uuid.uuid4())

def grline(a, b):
    return (f'\t(gr_line\n\t\t(start {a[0]} {a[1]})\n\t\t(end {b[0]} {b[1]})\n'
            f'\t\t(stroke\n\t\t\t(width 0.1)\n\t\t\t(type default)\n\t\t)\n'
            f'\t\t(layer "Edge.Cuts")\n\t\t(uuid "{U()}")\n\t)')

def seg(a, b, w, layer, net):
    return (f'\t(segment\n\t\t(start {a[0]} {a[1]})\n\t\t(end {b[0]} {b[1]})\n'
            f'\t\t(width {w})\n\t\t(layer "{layer}")\n\t\t(net "{net}")\n\t\t(uuid "{U()}")\n\t)')

def via(x, y, net):
    return (f'\t(via\n\t\t(at {x} {y})\n\t\t(size 0.4004)\n\t\t(drill 0.2002)\n'
            f'\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "{net}")\n\t\t(uuid "{U()}")\n\t)')

def lcsc_prop(v):
    return (f'\t\t(property "LCSC" "{v}"\n\t\t\t(at 0 0 0)\n\t\t\t(layer "F.Fab")\n'
            f'\t\t\t(hide yes)\n\t\t\t(uuid "{U()}")\n\t\t\t(effects\n'
            f'\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n')

def lcsc_prop1(v):
    return (f'\t(property "LCSC" "{v}"\n\t\t(at 0 0 0)\n\t\t(layer "F.Fab")\n'
            f'\t\t(hide yes)\n\t\t(uuid "{U()}")\n\t\t(effects\n'
            f'\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t)\n\t)\n')

def set_lcsc(fpblock, val):
    if '"LCSC"' in fpblock:
        return re.sub(r'(property "LCSC" )"[^"]*"', rf'\g<1>"{val}"', fpblock)
    # insert before first attr/pad/fp_ line (handle 1- or 2-tab indents)
    for tok in ('\n\t\t(attr', '\n\t\t(fp_', '\n\t\t(pad', '\n\t\t(model',
                '\n\t(attr', '\n\t(pad'):
        ai = fpblock.find(tok)
        if ai >= 0:
            prop = lcsc_prop(val) if tok.startswith('\n\t\t') else lcsc_prop1(val)
            return fpblock[:ai+1] + prop + fpblock[ai+1:]
    raise RuntimeError('no insertion point')

# ---------- 1. Edge.Cuts tab (final: y20..29.9, x48->53.6) ----------
old = '(start 48 -10.5)\n\t\t(end 48 64.5)'
assert T.count(old) == 1
T = T.replace(old, '(start 48 -10.5)\n\t\t(end 48 20)')
NEW += [grline((48, 29.9), (48, 64.5)),
        grline((48, 20), (53.6, 20)),
        grline((53.6, 20), (53.6, 29.9)),
        grline((53.6, 29.9), (48, 29.9))]

# ---------- 2. R93 -> dnp ----------
for fj, fk, fb in blocks(T, 'footprint'):
    if '"Reference" "R93"' in fb:
        i = fb.find('(attr smd)')
        assert i > 0
        fb2 = fb[:i] + '(attr smd dnp)' + fb[i+len('(attr smd)'):]
        T = T[:fj] + fb2 + T[fk:]
        break

# ---------- 3. New footprints ----------
def reuuid(text):
    return re.sub(r'\(uuid "[^"]*"\)', lambda m: f'(uuid "{U()}")', text)

t17 = open('../fix_v17/vitalq_v2.kicad_pcb').read()
def get_fp(t, ref):
    for j, k, b in blocks(t, 'footprint'):
        if f'"Reference" "{ref}"' in b:
            return t[j:k]
    raise KeyError(ref)

# --- Q6 SOT-23 @ (50.5,22.9) rot270: pad1 G=VBUS, pad2 S=VBAT_SYS, pad3 D=VBAT ---
q6 = get_fp(t17, 'Q6')
q6 = reuuid(q6)
q6 = q6.replace('(footprint "SOT-23"', '(footprint "Package_TO_SOT_SMD:SOT-23"')
q6 = q6.replace('(at 50.3 27.3)', '(at 50.5 22.9 270)')
q6 = q6.replace('''(pad "2" smd roundrect
			(at -0.9375 0.95)
			(size 1.475 0.6)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "VBAT")''', '''(pad "2" smd roundrect
			(at -0.9375 0.95)
			(size 1.475 0.6)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "VBAT_SYS")''')
q6 = q6.replace('''(pad "3" smd roundrect
			(at 0.9375 0)
			(size 1.475 0.6)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "VBAT_SYS")''', '''(pad "3" smd roundrect
			(at 0.9375 0)
			(size 1.475 0.6)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "VBAT")''')
q6 = set_lcsc(q6, 'C15127')
assert '(net "VBAT_SYS")' in q6 and '(net "VBAT")' in q6 and '(net "VBUS")' in q6
NEW.append(q6)

# --- D33 SOD-323 @ (50.3,26.7) rot0: pad1(K)=VBAT_SYS west, pad2(A)=VBUS east ---
mod = open('/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/'
           'Diode_SMD.pretty/D_SOD-323.kicad_mod').read()
mod = re.sub(r'\t\((version|generator|generator_version)[^\n]*\n', '', mod)
mod = mod.replace('(footprint "D_SOD-323"', '(footprint "Diode_SMD:D_SOD-323"')
mod = mod.replace('\t(layer "F.Cu")', '\t(layer "F.Cu")\n\t(uuid "PLACEHOLDER")'
                  '\n\t(at 50.3 26.7 0)', 1)
mod = mod.replace('(property "Reference" "REF**"', '(property "Reference" "D33"')
mod = mod.replace('(property "Value" "D_SOD-323"', '(property "Value" "1N5819WS"')
mod = mod.replace('''(pad "1" smd roundrect
		(at -1.05 0)
		(size 0.6 0.45)
		(layers "F.Cu" "F.Mask" "F.Paste")
		(roundrect_rratio 0.25)
		(uuid''', '''(pad "1" smd roundrect
		(at -1.05 0)
		(size 0.6 0.45)
		(layers "F.Cu" "F.Mask" "F.Paste")
		(roundrect_rratio 0.25)
		(net "VBAT_SYS")
		(uuid''')
mod = mod.replace('''(pad "2" smd roundrect
		(at 1.05 0)
		(size 0.6 0.45)
		(layers "F.Cu" "F.Mask" "F.Paste")
		(roundrect_rratio 0.25)
		(uuid''', '''(pad "2" smd roundrect
		(at 1.05 0)
		(size 0.6 0.45)
		(layers "F.Cu" "F.Mask" "F.Paste")
		(roundrect_rratio 0.25)
		(net "VBUS")
		(uuid''')
mod = reuuid(mod)
mod = set_lcsc(mod, 'C191023')
assert '"Reference" "D33"' in mod and '"Value" "1N5819WS"' in mod
NEW.append(mod)

# --- R123 R_0402 @ (50.5,28.9) rot0: pad1=GND west, pad2=VBUS east ---
r123 = get_fp(t17, 'R123')
r123 = reuuid(r123)
r123 = r123.replace('(footprint "R_0402_1005Metric"', '(footprint "Resistor_SMD:R_0402_1005Metric"')
r123 = r123.replace('(at 51.5 21)', '(at 50.5 28.9)')
r123 = r123.replace('''(pad "1" smd roundrect
			(at -0.51 0)
			(size 0.54 0.64)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "VBUS")''', '''(pad "1" smd roundrect
			(at -0.51 0)
			(size 0.54 0.64)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "GND")''')
r123 = r123.replace('''(pad "2" smd roundrect
			(at 0.51 0)
			(size 0.54 0.64)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "GND")''', '''(pad "2" smd roundrect
			(at 0.51 0)
			(size 0.54 0.64)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "VBUS")''')
r123 = set_lcsc(r123, 'C25741')
assert '(net "GND")' in r123 and '(net "VBUS")' in r123
NEW.append(r123)

# ---------- 4. Vias ----------
for x, y, n in [(53.0, 26.8, 'VBUS'), (52.4, 25.4, 'VBAT'), (48.9, 26.0, 'VBAT_SYS'),
                (49.6, 29.0, 'GND'), (46.25, 28.75, 'GND'), (45.7, 22.53, 'VBAT'),
                (46.5, 26.75, 'VBAT_SYS'), (27.93, -2.23, 'VBUS')]:
    NEW.append(via(x, y, n))

# ---------- 5. Segments ----------
# VBUS In3 verified route: tap (27.93,-2.23) -> via (53.0,26.8)
VBUS_RT = [(27.93,-2.23),(29.74,-1.0),(38.94,8.2),(38.94,9.1),(38.74,9.35),
           (38.74,9.7),(38.84,9.9),(39.39,10.45),(40.29,10.5),(42.0,11.6),
           (43.5,11.7),(44.3,11.9),(44.3,20.9),(52.9,22.2),(52.9,26.6),(53.0,26.8)]
for a, b in zip(VBUS_RT, VBUS_RT[1:]):
    NEW.append(seg(a, b, 0.3, 'In3.Cu', 'VBUS'))
NEW += [
    seg((45.7, 22.53), (52.4, 25.4), 0.3, 'In3.Cu', 'VBAT'),
    seg((46.5, 26.75), (48.9, 26.0), 0.3, 'In5.Cu', 'VBAT_SYS'),
    seg((46.25, 28.75), (49.6, 29.0), 0.3, 'In5.Cu', 'GND'),
    seg((51.45, 21.9625), (53.0, 23.0), 0.3, 'F.Cu', 'VBUS'),
    seg((53.0, 23.0), (53.0, 28.9), 0.3, 'F.Cu', 'VBUS'),
    seg((51.35, 26.7), (53.0, 26.7), 0.3, 'F.Cu', 'VBUS'),
    seg((51.01, 28.9), (53.0, 28.9), 0.3, 'F.Cu', 'VBUS'),
    seg((50.5, 23.8375), (52.4, 25.4), 0.3, 'F.Cu', 'VBAT'),
    seg((49.55, 21.9625), (48.9, 26.0), 0.3, 'F.Cu', 'VBAT_SYS'),
    seg((49.25, 26.7), (48.9, 26.0), 0.3, 'F.Cu', 'VBAT_SYS'),
    seg((49.99, 28.9), (49.6, 29.0), 0.3, 'F.Cu', 'GND'),
]

# ---------- write ----------
tail = T.rstrip()
assert tail.endswith(')')
NEW = ['\t' + n if not n.startswith('\t') else n for n in NEW]
T = tail[:-1] + '\n' + '\n'.join(NEW) + '\n)\n'
open('vitalq_v2.kicad_pcb', 'w').write(T)
print("board written:", len(NEW), "new objects")
