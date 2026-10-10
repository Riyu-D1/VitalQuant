import json
import sys
from shapely.geometry import Point, box
from geometry import Geometry
from r2 import state

g = Geometry(state()['current_geometry'])
fp = {f['ref']: f for f in g.raw['footprints']}
out = {}
for ref in ('U18', 'C59', 'U19', 'D30'):
    out[ref] = {'fp': fp[ref], 'pads': []}
    for p in g.raw['pads']:
        if p['ref'] != ref:
            continue
        shape = p['shapes'] if 'shapes' in p else None
        item = g.items[p['uuid']]
        if not item['shapes']:
            continue
        sh = list(item['shapes'].values())[0]
        att = []
        for layer in item['shapes']:
            for idx in g.trees[layer].query(item['shapes'][layer], predicate='intersects'):
                other, _ = g.solids[layer][idx]
                if other['uuid'] != p['uuid'] and other['net'] == p['net'] and other['kind'] != 'pads':
                    att.append({k: other.get(k) for k in ('uuid', 'kind', 'layer', 'a', 'b', 'xy', 'width', 'diameter')})
        out[ref]['pads'].append({'num': p['number'], 'net': p['net'], 'xy': p['xy'], 'size': p['size'], 'bounds': [round(v, 4) for v in sh.bounds], 'attached': att})
for a in g.raw['keepouts']:
    if a['name'] == 'hv_inner':
        from route_geometry import polyset
        b = polyset(a['outline']).bounds
        if b[0] < 30 and b[2] > 10 and b[1] < 45 and b[3] > 38:
            out.setdefault('hv_inner_near', []).append([round(v, 3) for v in b])
print(json.dumps(out, indent=1))
