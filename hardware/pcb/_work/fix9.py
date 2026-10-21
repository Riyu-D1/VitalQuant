#!/usr/bin/env python3
# fix9.py — relocate the 2 fix8 VBUS vias off the pad/NPTH collisions.
import re, uuid

fn = 'vitalq_v2.kicad_pcb'
txt = open(fn).read()

def U(): return str(uuid.uuid4())

# --- rip fix8's additions ---
rips = [
    # via2 at 27.3,-4.35 and its 2 segs
    r'\t\(via\n\t\t\(at 27\.3 -4\.35\)\n\t\t\(size 0\.4004\)\n\t\t\(drill 0\.2002\)\n\t\t\(layers "F\.Cu" "B\.Cu"\)\n\t\t\(net "VBUS"\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n',
    r'\t\(segment\n\t\t\(start 27\.05 -4\.35\)\n\t\t\(end 27\.3 -4\.35\)\n\t\t\(width 0\.5\)\n\t\t\(layer "F\.Cu"\)\n\t\t\(net "VBUS"\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n',
    r'\t\(segment\n\t\t\(start 27\.3 -4\.35\)\n\t\t\(end 29\.5 -4\.2\)\n\t\t\(width 0\.6\)\n\t\t\(layer "In3\.Cu"\)\n\t\t\(net "VBUS"\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n',
    # via2 at 32.2,-4.35 and its 2 segs
    r'\t\(via\n\t\t\(at 32\.2 -4\.35\)\n\t\t\(size 0\.4004\)\n\t\t\(drill 0\.2002\)\n\t\t\(layers "F\.Cu" "B\.Cu"\)\n\t\t\(net "VBUS"\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n',
    r'\t\(segment\n\t\t\(start 31\.95 -4\.5\)\n\t\t\(end 32\.2 -4\.35\)\n\t\t\(width 0\.5\)\n\t\t\(layer "F\.Cu"\)\n\t\t\(net "VBUS"\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n',
    r'\t\(segment\n\t\t\(start 32\.2 -4\.35\)\n\t\t\(end 31\.3 -5\.1\)\n\t\t\(width 0\.6\)\n\t\t\(layer "In3\.Cu"\)\n\t\t\(net "VBUS"\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n',
]
for pat in rips:
    txt, k = re.subn(pat, '', txt, count=1)
    assert k == 1, 'rip miss: %s' % pat[:60]
print('ripped %d fix8 items' % len(rips))

def via(x, y, net):
    return '\t(via\n\t\t(at %s %s)\n\t\t(size 0.4004)\n\t\t(drill 0.2002)\n\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n' % (x, y, net, U())

def seg(x1, y1, x2, y2, w, layer, net):
    return '\t(segment\n\t\t(start %s %s)\n\t\t(end %s %s)\n\t\t(width %s)\n\t\t(layer "%s")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n' % (x1, y1, x2, y2, w, layer, net, U())

adds = ''.join([
    via('27.15', '-4.35', 'VBUS'),
    seg('27.05', '-4.35', '27.15', '-4.35', '0.5', 'F.Cu', 'VBUS'),
    seg('27.15', '-4.35', '29.5', '-4.2', '0.6', 'In3.Cu', 'VBUS'),
    via('30.9', '-4.9', 'VBUS'),
    seg('31.95', '-4.5', '30.9', '-4.9', '0.5', 'F.Cu', 'VBUS'),
    seg('30.9', '-4.9', '31.3', '-5.1', '0.6', 'In3.Cu', 'VBUS'),
])
assert txt.rstrip().endswith(')')
idx = txt.rstrip().rfind(')')
txt = txt[:idx] + adds + txt[idx:]
open(fn, 'w').write(txt)
print('added relocated 2 vias + 4 segs')
