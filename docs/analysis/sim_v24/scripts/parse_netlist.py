#!/usr/bin/env python3
"""Parse the KiCad s-expr netlist of the board under analysis.

Default input: ../inputs/v24.net — the committed kicad-cli export of the
fix_v24 schematic snapshot (inputs/vitalq_v2.kicad_sch). Regenerate with:
    kicad-cli sch export netlist ../inputs/vitalq_v2.kicad_sch \
        --output ../inputs/v24.net
(or pass --net PATH to parse a different export).

Writes ../results/netlist.json + ../results/all_nets.txt.
"""
import sys, os, json, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sexpr import parse, find_all, child, children, num

HERE = os.path.dirname(os.path.abspath(__file__))
D = lambda *p: os.path.join(HERE, "..", *p)

ap = argparse.ArgumentParser()
ap.add_argument("--net", default=D("inputs", "v24.net"),
                help="kicad-cli netlist export (default: committed inputs/v24.net)")
ap.add_argument("--out", default=D("results"),
                help="output directory (default: ../results)")
args = ap.parse_args()

tree = parse(open(args.net).read())

# components: (comp (ref ..) (value ..) (footprint ..) (fields (field (name x) v)*))
comps = {}
for c in find_all(tree, 'comp'):
    ref = child(c, 'ref')[1]
    val = child(c, 'value')
    fp = child(c, 'footprint')
    fields = {}
    flds = child(c, 'fields')
    if flds:
        for f in children(flds, 'field'):
            nm = child(f, 'name')
            fields[nm[1]] = f[1] if len(f) > 1 else ''
    comps[ref] = dict(value=val[1] if val else '', footprint=fp[1] if fp else '', fields=fields)

nets = {}
for n in find_all(tree, 'net'):
    code = child(n, 'code')[1]
    name = child(n, 'name')[1]
    nodes = []
    for nd in children(n, 'node'):
        r = child(nd, 'ref')[1]
        p = child(nd, 'pin')
        pin = p[1] if p else '?'
        ptype = child(nd, 'pintype')
        nodes.append((r, pin, ptype[1] if ptype else ''))
    nets[name] = nodes

os.makedirs(args.out, exist_ok=True)
with open(os.path.join(args.out, 'all_nets.txt'), 'w') as out:
    for name in sorted(nets):
        out.write(f"{name} | {len(nets[name])} | {' '.join(f'{r}.{p}' for r,p,pt in nets[name])}\n")

with open(os.path.join(args.out, 'netlist.json'), 'w') as f:
    json.dump(dict(comps=comps, nets={k: [(r, p, pt) for r, p, pt in v] for k, v in nets.items()}),
              f, indent=0)
print(len(comps), 'comps,', len(nets), 'nets')
