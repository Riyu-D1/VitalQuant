import json
import math
from pathlib import Path
from shapely.geometry import Point
from geometry import Geometry
from session import ROOT, backup, save_json

assert not (ROOT / 'state.json').exists(), 'B1 is a one-time batch and must not be replayed'
history = {str(p): p.read_text() for p in [ROOT / 'PROGRESS.md', ROOT.parent / 'VitalQ_Route_v8' / 'PROGRESS.md', ROOT.parent / 'VitalQ_Route_v8' / 'REPORT.md']}
checkpoint = backup('B1_default_artifacts')
plan = {'batch': 'B1', 'backup': str(checkpoint), 'source_geometry': 'geometry_B0.json.gz', 'history_reviewed': list(history), 'modify_vias': [], 'tracks': [], 'vias': [], 'remove': [], 'attempts': [{'net': n, 'attempt': 1, 'approach': 'Reduce only the five user-default vias at exact checked coordinates; LED2_K additionally replaces its 10.130743mm In1 segment with a single In2 detour and two legal through-via sites', 'status': 'started'} for n in ['IOVDD', 'LED2_K', '+3V3']]}
save_json(ROOT / 'plan_B1.json', plan)
g = Geometry(ROOT / 'geometry_B0.json.gz')
for v in g.raw['vias']:
    if abs(v['diameter'] - .6) < .000001:
        assert not g.via_issues(v['net'], v['xy'], ignore=(v['uuid'],)), v
        plan['modify_vias'].append({'uuid': v['uuid'], 'net': v['net'], 'from': {k: v[k] for k in ('xy', 'diameter', 'drill')}, 'xy': v['xy'], 'diameter': .4, 'drill': .2, 'reason': 'Replace user default-rule oversize via at the existing legal site'})
segment = g.items['94820769-c7cc-43a1-a955-c79294772607']
ends = []
for endpoint in [segment['a'], segment['b']]:
    offsets = sorted([(dx * .1, dy * .1) for dx in range(-6, 7) for dy in range(-6, 7)], key=lambda d: math.hypot(*d))
    site = None
    for dx, dy in offsets:
        xy = [round(endpoint[0] + dx, 6), round(endpoint[1] + dy, 6)]
        if g.via_issues('LED2_K', xy):
            continue
        bounds = [min(xy[0], endpoint[0]) - .5, min(xy[1], endpoint[1]) - .5, max(xy[0], endpoint[0]) + .5, max(xy[1], endpoint[1]) + .5]
        allowed = g.allowed_local('LED2_K', 4, bounds)
        from shapely.geometry import LineString
        if allowed.covers(LineString([endpoint, xy])):
            site = xy
            break
    ends.append(site)
if all(ends):
    path = g.path('LED2_K', 6, ends[0], ends[1], [17, 43.6, 32, 47.2])
else:
    path = None
if path:
    plan['remove'].append({k: v for k, v in segment.items() if k not in ('shapes', 'connected', 'kind')})
    plan['tracks'].extend(g.track_records('LED2_K', 6, path))
    for endpoint, xy in zip([segment['a'], segment['b']], ends):
        plan['tracks'].extend(g.track_records('LED2_K', 4, [endpoint, xy]))
        plan['vias'].append({'net': 'LED2_K', 'xy': xy, 'diameter': .4, 'drill': .2})
    outcome = 'In2 replacement candidate generated; full In1 chain remains separate pending work'
else:
    outcome = 'BLOCKED approach 1: no complete In2 detour in y43.6..47.2 using the nearest legal endpoint stitches; original In1 segment retained'
plan['led_detour'] = {'sites': ends, 'result': outcome}
for attempt in plan['attempts']:
    attempt['status'] = 'candidate'
    if attempt['net'] == 'LED2_K':
        attempt['routing_outcome'] = outcome
save_json(ROOT / 'plan_B1.json', plan)
print(json.dumps({'via_resizes': len(plan['modify_vias']), 'new_tracks': len(plan['tracks']), 'new_vias': len(plan['vias']), 'led_detour': plan['led_detour']}, indent=2), flush=True)
