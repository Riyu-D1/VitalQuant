#!/usr/bin/env python3
"""case_v7 final9: parse the exported netlist + board dump, run pad parity
and the footprint-side audit (skin parts on B.Cu, outward parts on F.Cu)."""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# --- minimal sexpr parser ---
def tokenize(s):
    # kicadsexpr netlists carry no comments; ";" appears inside quoted
    # description strings — never strip it.
    return re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[\w.+\-*/:<>=?#$%&~!@^|]+', s)

def parse(txt):
    toks = tokenize(txt)
    pos = [0]
    def rd():
        t = toks[pos[0]]; pos[0] += 1
        if t == '(':
            out = []
            while toks[pos[0]] != ')':
                out.append(rd())
            pos[0] += 1
            return out
        if t.startswith('"'):
            return t[1:-1]
        return t
    return rd()

def children(node, name):
    return [c for c in node if isinstance(c, list) and c and c[0] == name]

def field(node, name):
    for c in children(node, name):
        return c[1:]
    return []

n = parse(open(os.path.join(HERE, 'netlist', 'vitalq_v2.net')).read())
comps = {}
for c in children(children(n, 'components')[0], 'comp'):
    ref = field(c, 'ref')[0]
    comps[ref] = {'value': (field(c, 'value') or [''])[0],
                  'footprint': (field(c, 'footprint') or [''])[0]}
nets = {}
pin2net = {}
for nn in children(children(n, 'nets')[0], 'net'):
    name = field(nn, 'name')[0]
    nodes = [(field(x, 'ref')[0], field(x, 'pin')[0]) for x in children(nn, 'node')]
    nets[name] = nodes
    for r, p in nodes:
        pin2net[f'{r}.{p}'] = name

pcb = json.load(open(os.path.join(HERE, 'parse', 'pcb.json')))
pad_net = {}
fp_layer = {}
for fp in pcb['footprints']:
    fp_layer[fp['ref']] = fp['layer']
    for p in fp['pads']:
        pad_net[f"{fp['ref']}.{p['num']}"] = p.get('net')

checked = mism = 0
misses = []
for rp, snet in sorted(pin2net.items()):
    if rp not in pad_net:
        misses.append((rp, snet, 'no pad'))
        continue
    pnet = pad_net[rp]
    checked += 1
    if snet.startswith('unconnected-') and pnet in (None, ''):
        continue
    if pnet != snet:
        mism += 1
        misses.append((rp, snet, pnet))
extra = [rp for rp in pad_net if rp not in pin2net and pad_net[rp]]
print(f'PAD PARITY: {checked} pads checked, {mism} mismatches, '
      f'{sum(1 for m in misses if m[2] == "no pad")} sch pins without pad, '
      f'{len(extra)} extra pcb pads')
for m in misses[:40]:
    print('  MISM', m)
print('  extra:', extra[:15])
print(f'NETLIST: {len(comps)} components, {len(nets)} nets, {len(pin2net)} pin-net pairs')

# --- footprint-side audit ---
# skin-facing = patient/electrode/optical side -> B.Cu ; outward/UI -> F.Cu
SKIN = ['J3', 'J5', 'J6', 'J7', 'J11', 'J13',          # electrode pads
        'D10', 'D12', 'U10', 'U11', 'U12', 'U16', 'U22', 'U23',  # optics/sensors
        'J2']                                        # battery leads (B side)
EDGE_TAILS = ['J9', 'J10', 'J12']                    # side-exit connectors
bad = []
for ref in SKIN:
    if fp_layer.get(ref) != 'B.Cu':
        bad.append((ref, fp_layer.get(ref), 'expected B.Cu'))
print('SIDE AUDIT skin parts on B.Cu:',
      'PASS' if not bad else f'FAIL {bad}')
for ref in EDGE_TAILS:
    print(f'  edge connector {ref}: {fp_layer.get(ref)}')
top_ct = sum(1 for l in fp_layer.values() if l == 'F.Cu')
bot_ct = sum(1 for l in fp_layer.values() if l == 'B.Cu')
print(f'  footprint layers: {top_ct} F.Cu / {bot_ct} B.Cu of {len(fp_layer)}')

if mism or bad:
    sys.exit(1)
