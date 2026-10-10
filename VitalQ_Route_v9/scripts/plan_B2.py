import copy
import json
import math
from pathlib import Path
import uuid
from shapely.geometry import Point, LineString
from shapely.ops import unary_union, nearest_points
from geometry import Geometry, disk, LAYERS
from session import ROOT, backup, save_json

state = json.loads((ROOT / 'state.json').read_text())
assert state['streak'] < 2
history = [ROOT / 'PROGRESS.md', ROOT.parent / 'VitalQ_Route_v8' / 'PROGRESS.md', ROOT.parent / 'VitalQ_Route_v8' / 'REPORT.md']
for p in history:
    p.read_text()
checkpoint = backup('B2_local_repairs')
plan = {'batch': 'B2', 'backup': str(checkpoint), 'source_geometry': state['current_geometry'], 'history_reviewed': [str(p) for p in history], 'tracks': [], 'vias': [], 'remove': [], 'attempts': [], 'blocked': [], 'nets_done': [], 'summary': 'Local repairs only; no component moves, no rip-up'}
save_json(ROOT / 'plan_B2.json', plan)
g = Geometry(state['current_geometry'])


def attempt(net, approach, number):
    for p in history:
        p.read_text()
    plan['attempts'].append({'net': net, 'attempt': number, 'approach': approach, 'status': 'started'})
    save_json(ROOT / 'plan_B2.json', plan)


def add(net, tracks, vias, label):
    global g
    for t in tracks:
        t['uuid'] = str(uuid.uuid4())
    for v in vias:
        v['uuid'] = str(uuid.uuid4())
        contacts = []
        for layer in (0, 2):
            for index in g.trees[layer].query(disk(v['xy'], v['diameter'] / 2), predicate='intersects'):
                item, _ = g.solids[layer][index]
                if item['kind'] == 'pads':
                    contacts.append(item['ref'] + '.' + item['number'])
        v.update(layers=list(LAYERS), filled=bool(contacts), capped=bool(contacts), pofv=bool(contacts), pads=sorted(set(contacts)))
    plan['tracks'].extend(tracks)
    plan['vias'].extend(vias)
    plan['nets_done'].append(label)
    raw = copy.copy(g.raw)
    raw['tracks'] = g.raw['tracks'] + tracks
    raw['vias'] = g.raw['vias'] + vias
    g = Geometry(raw)
    save_json(ROOT / 'plan_B2.json', plan)
    print('Candidate:', label, flush=True)


def blocked(net, label, reason):
    plan['blocked'].append({'net': net, 'connection': label, 'reason': reason})
    print('Blocked:', label, reason, flush=True)


attempt('GND', 'F-only connections from each HV-stranded GND branch to existing grounded copper, avoiding I2C_SCL_1V8/TX5_EN; no new via', 1)
for start, end, bounds, label in [(g.pad('R9', 2)['xy'], g.pad('R51', 2)['xy'], [2.5, 60, 9, 64.15], 'R9.2 to R51.2'), ([19.451354, 40.423699], [18.398446, 39.860258], [17.5, 39.1, 20.7, 44], 'U19/D30 GND to existing outside-HV via')]:
    path = g.path('GND', 0, start, end, bounds)
    if path:
        add('GND', g.track_records('GND', 0, path), [], label)
    else:
        blocked('GND', label, 'No continuous F-only detour in the specified local window; original copper retained')
plan['attempts'][-1]['status'] = 'evaluated'

attempt('+3V3', 'C59 outside-HV stitch at 14.85,39.85; U18 independent north-side stitches; explicit sensor bridge site 20.05,52.2 on both outer layers', 2)
jobs = [('C59', 1, 0, [14.85, 39.85], [14.2, 39.2, 16.2, 41.8]), ('U18', 7, 0, [26.65, 40.15], [25.9, 39.3, 27.4, 41.4]), ('U18', 8, 0, None, [24.3, 39.2, 26.2, 41.3]), ('U20', 5, 0, [20.05, 52.2], [18, 49.6, 23.2, 52.75]), ('U11', 5, 2, [20.05, 52.2], [18, 49.6, 23.2, 52.75])]
for ref, num, layer, site, bounds in jobs:
    pad = g.pad(ref, num)
    if site is None:
        choices = sorted([[25.05 + dx * .1, 39.65 + dy * .1] for dx in range(-5, 6) for dy in range(-4, 5)], key=lambda xy: math.dist(xy, pad['xy']))
        site = next((xy for xy in choices if not g.via_issues('+3V3', xy)), None)
    existing = next((v for v in g.raw['vias'] if site and v['net'] == '+3V3' and math.dist(v['xy'], site) < .00001), None)
    issues = g.via_issues('+3V3', site, ignore=(existing['uuid'],) if existing else ()) if site else ['No legal via site in the independent north-side search window']
    if issues:
        blocked('+3V3', f'{ref}.{num}', json.dumps(issues, separators=(',', ':')))
        continue
    path = g.path('+3V3', layer, pad['xy'], site, bounds)
    if path:
        add('+3V3', g.track_records('+3V3', layer, path), [] if existing else [{'net': '+3V3', 'xy': site, 'diameter': .4, 'drill': .2}], f'{ref}.{num} to +3V3 plane stitch {site}')
    else:
        blocked('+3V3', f'{ref}.{num}', f'No legal layer {layer} pad-to-stitch path within slot/copper clearance; site {site}')
plan['attempts'][-1]['status'] = 'evaluated'

attempt('TMP117_ALERT', 'Forced bridge via at 19.5,52.2, B connection from U11.3 and F-only connection to existing R65.2 branch', 1)
net = 'TMP117_ALERT'
site = [19.5, 52.2]
issues = g.via_issues(net, site)
if not issues:
    first = g.path(net, 2, g.pad('U11', 3)['xy'], site, [18, 49.7, 23.2, 53])
    comps, ids = g.components(net)
    source = comps[ids[g.pad('R65', 2)['uuid']]]
    target = unary_union([shape[0] for item, shape in source if 0 in shape])
    dest = list(nearest_points(Point(site), target)[1].coords[0])
    second = g.path(net, 0, site, dest, [10.5, 31.5, 23.3, 54]) if first else None
    if first and second:
        add(net, g.track_records(net, 2, first) + g.track_records(net, 0, second), [{'net': net, 'xy': site, 'diameter': .4, 'drill': .2}], 'TMP117_ALERT bridge')
    else:
        blocked(net, 'U11.3 bridge', 'Forced bridge-site route has no complete legal B/F path')
else:
    blocked(net, 'U11.3 bridge', json.dumps(issues, separators=(',', ':')))
plan['attempts'][-1]['status'] = 'evaluated'

net = 'unconnected-(J12-PadMP)'
attempt(net, 'Exact MP pad-center POFV pair with In2 link instead of shorting B.Cu straight line', 1)
pads = [p for p in g.raw['pads'] if p['ref'] == 'J12' and p['number'] == 'MP']
sites = [p['xy'] for p in pads]
issues = [g.via_issues(net, xy) for xy in sites]
if not any(issues):
    path = g.path(net, 6, sites[0], sites[1], [-1.5, 46, 2, 59])
    if path:
        add(net, g.track_records(net, 6, path), [{'net': net, 'xy': xy, 'diameter': .4, 'drill': .2} for xy in sites], 'J12 MP pair')
    else:
        blocked(net, 'MP pair', 'No In2 corridor between exact MP via sites within x=-1.5..2')
else:
    blocked(net, 'MP pair', json.dumps(issues, separators=(',', ':')))
plan['attempts'][-1]['status'] = 'evaluated'
save_json(ROOT / 'plan_B2.json', plan)
print(json.dumps({'tracks': len(plan['tracks']), 'vias': len(plan['vias']), 'connections': plan['nets_done'], 'blocked': plan['blocked']}, indent=2), flush=True)
