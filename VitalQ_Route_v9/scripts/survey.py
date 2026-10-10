import json
import math
import sys
from shapely.geometry import Point
from shapely.ops import nearest_points
from geometry import Geometry, LAYERS
from session import ROOT, save_json

g = Geometry(sys.argv[1])
result = {'layers': g.raw['layers'], 'oversized': [], 'led_plane_tracks': [], 'local_pads': [], 'u7_via_sites': []}
for v in g.raw['vias']:
    if abs(v['diameter'] - 0.6) > 0.000001:
        continue
    allowed = g.via_allowed(v['net'], ignore=(v['uuid'],))
    point = Point(v['xy'])
    nearest = nearest_points(point, allowed)[1]
    connected = [g.items[u] for u in v['connected'] if u in g.items]
    record = {k: v[k] for k in ('uuid', 'net', 'xy', 'diameter', 'drill')}
    record.update(legal_resized=allowed.covers(point), nearest_legal=list(nearest.coords[0]), distance=nearest.distance(point), connected=[{k: i[k] for k in ('uuid', 'net', 'kind', 'layer', 'a', 'b', 'width', 'xy', 'ref', 'number') if k in i} for i in connected])
    result['oversized'].append(record)
for t in g.raw['tracks']:
    if t['net'] == 'LED2_K' and t['layer'] == 4:
        result['led_plane_tracks'].append(t)
for ref in ['R9', 'R51', 'C59', 'U18', 'U19', 'U11', 'U20', 'J12']:
    result['local_pads'].extend([{k: p[k] for k in ('uuid', 'ref', 'number', 'net', 'xy', 'size', 'layers')} for p in g.raw['pads'] if p['ref'] == ref])
for num in ['B3', 'B4', 'B5', 'B6', 'B7', 'C7', 'D2', 'D7', 'E7', 'F2', 'F3', 'F5', 'F7']:
    p = g.pad('U7', num)
    result['u7_via_sites'].append({'pad': num, 'net': p['net'], 'xy': p['xy'], 'legal': g.via_allowed(p['net'], .25, .15).covers(Point(p['xy']))})
save_json(ROOT / 'survey_B0.json', result)
print(json.dumps({k: v for k, v in result.items() if k != 'local_pads'}, indent=2), flush=True)
