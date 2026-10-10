import json
from geometry import Geometry
from session import ROOT, save_json

g = Geometry(ROOT / 'geometry_B0.json.gz')
result = {'led_ground_layer': [{k: t[k] for k in ('uuid', 'layer', 'a', 'b', 'width')} for t in g.raw['tracks'] if t['net'] == 'LED2_K' and t['layer'] == 4], 'u7': [], 'local_vias': []}
for num in ['B3', 'B4', 'B5', 'B6', 'B7', 'C7', 'D2', 'D7', 'E7', 'F2', 'F3', 'F5', 'F7']:
    p = g.pad('U7', num)
    result['u7'].append({'pad': num, 'net': p['net'], 'xy': p['xy'], 'issues': g.via_issues(p['net'], p['xy'], .25, .15)})
for xy in [[14.90, 39.95], [26.65, 40.15], [20.05, 52.2]]:
    result['local_vias'].append({'xy': xy, 'issues': g.via_issues('+3V3', xy)})
save_json(ROOT / 'local_inspection.json', result)
print(json.dumps(result, indent=2), flush=True)
