#!/usr/bin/env python3
# fix8.py — VBUS spec compliance: inner segs 0.5->0.6, second via on each pad feed.
import re, uuid

fn = 'vitalq_v2.kicad_pcb'
txt = open(fn).read()

def segpat(x1, y1, x2, y2):
    a = re.escape('(start %s %s)' % (x1, y1))
    b = re.escape('(end %s %s)' % (x2, y2))
    return re.compile(r'\(segment\s+\(start %s %s\)\s+\(end %s %s\)\s+\(width ([0-9.]+)\)' % (x1, y1, x2, y2))

# --- 1. bump In3 VBUS segs 0.5 -> 0.6 ---
bumps = [
    ('26.63', '0.8', '28.3', '-1.2'),
    ('28.3', '-1.2', '29.74', '-1'),
    ('27.55', '-4.9', '29.5', '-4.2'),
    ('31.3', '-5.1', '31.5', '-6.6'),
    ('31.95', '-4.4', '31.3', '-5.1'),
]
n = 0
for x1, y1, x2, y2 in bumps:
    pat = re.compile(r'(\(segment\s+\(start %s %s\)\s+\(end %s %s\)\s+\(width )([0-9.]+)(\))' % (
        re.escape(x1), re.escape(y1), re.escape(x2), re.escape(y2)))
    txt, k = pat.subn(lambda m: m.group(1) + '0.6' + m.group(3), txt, count=1)
    assert k == 1, 'seg not found: %s,%s-%s,%s' % (x1, y1, x2, y2)
    n += k
print('width bumps:', n)

# --- 2. add vias + segs ---
def U(): return str(uuid.uuid4())

def via(x, y, net):
    return '\t(via\n\t\t(at %s %s)\n\t\t(size 0.4004)\n\t\t(drill 0.2002)\n\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n' % (x, y, net, U())

def seg(x1, y1, x2, y2, w, layer, net):
    return '\t(segment\n\t\t(start %s %s)\n\t\t(end %s %s)\n\t\t(width %s)\n\t\t(layer "%s")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n' % (x1, y1, x2, y2, w, layer, net, U())

adds = ''.join([
    # pad 27.05 feed: second via at (27.3,-4.35)
    via('27.3', '-4.35', 'VBUS'),
    seg('27.05', '-4.35', '27.3', '-4.35', '0.5', 'F.Cu', 'VBUS'),
    seg('27.3', '-4.35', '29.5', '-4.2', '0.6', 'In3.Cu', 'VBUS'),
    # pad 31.95 feed: second via at (32.2,-4.35)
    via('32.2', '-4.35', 'VBUS'),
    seg('31.95', '-4.5', '32.2', '-4.35', '0.5', 'F.Cu', 'VBUS'),
    seg('32.2', '-4.35', '31.3', '-5.1', '0.6', 'In3.Cu', 'VBUS'),
])
# append before final closing paren of file
assert txt.rstrip().endswith(')')
idx = txt.rstrip().rfind(')')
txt = txt[:idx] + adds + txt[idx:]
open(fn, 'w').write(txt)
print('added 2 vias + 4 segs')
