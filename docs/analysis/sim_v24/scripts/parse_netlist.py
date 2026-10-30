#!/usr/bin/env python3
"""Parse v24.net (KiCad s-expr netlist): emit rail->pin map + part values."""
import sys, os, json, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sexpr import parse, find_all, child, children, num

NET = '/Users/deepakdiwan/Downloads/VitalQ_Hand/sim_v24/_work/v24.net'
tree = parse(open(NET).read())

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

out = open('/Users/deepakdiwan/Downloads/VitalQ_Hand/sim_v24/data/all_nets.txt', 'w')
for name in sorted(nets):
    out.write(f"{name} | {len(nets[name])} | {' '.join(f'{r}.{p}' for r,p,pt in nets[name])}\n")
out.close()

json.dump(dict(comps=comps, nets={k: [(r,p,pt) for r,p,pt in v] for k,v in nets.items()}),
          open('/Users/deepakdiwan/Downloads/VitalQ_Hand/sim_v24/data/netlist.json','w'), indent=0)
print(len(comps), 'comps,', len(nets), 'nets')
