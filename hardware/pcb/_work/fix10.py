#!/usr/bin/env python3
# fix10.py — revert via2b (east column 2nd via): no legal placement exists.
import re

fn = 'vitalq_v2.kicad_pcb'
txt = open(fn).read()
rips = [
    r'\t\(via\n\t\t\(at 30\.9 -4\.9\)\n\t\t\(size 0\.4004\)\n\t\t\(drill 0\.2002\)\n\t\t\(layers "F\.Cu" "B\.Cu"\)\n\t\t\(net "VBUS"\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n',
    r'\t\(segment\n\t\t\(start 31\.95 -4\.5\)\n\t\t\(end 30\.9 -4\.9\)\n\t\t\(width 0\.5\)\n\t\t\(layer "F\.Cu"\)\n\t\t\(net "VBUS"\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n',
    r'\t\(segment\n\t\t\(start 30\.9 -4\.9\)\n\t\t\(end 31\.3 -5\.1\)\n\t\t\(width 0\.6\)\n\t\t\(layer "In3\.Cu"\)\n\t\t\(net "VBUS"\)\n\t\t\(uuid "[^"]+"\)\n\t\)\n',
]
for pat in rips:
    txt, k = re.subn(pat, '', txt, count=1)
    assert k == 1, 'rip miss: %s' % pat[:60]
open(fn, 'w').write(txt)
print('reverted via2b: 3 items')
