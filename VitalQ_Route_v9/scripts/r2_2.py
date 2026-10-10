import json
import math
from shapely.geometry import LineString, box
from r2 import ROOT, begin, candidate_dir, verify, finish, save_json, record_attempt, state
from r2_common import kicad_job, stage_dir, extract, opens, route_open
from r2route import Board, SIGNAL_LAYERS
from session import drc

BATCH = 'R2-2'
checkpoint, s = begin(BATCH, 'U18_nudge_HV_parts')
d = candidate_dir(BATCH)
ledger = {'batch': BATCH, 'backup': str(checkpoint), 'moves': [], 'routes': [], 'issues': []}

# Stage 1: U18 nudge (dx=-0.31 mm), shift attached pad-end track points, refill for analysis
st = stage_dir(d, 'stage1')
move = {'ref': 'U18', 'from': [27.209594, 44.367033], 'to': [26.899594, 44.367033], 'reason': 'Centre pin2/pin3 gap on U22 column x=26.9 for C1/D1 via-in-pad (PROMPT_R2 R2-2)'}
kicad_job({'input': str(ROOT / 'vitalq_v2.kicad_pcb'), 'output': str(st / 'vitalq_v2.kicad_pcb'), 'moves': [move], 'refill': True, 'log': str(st / 'move_log.json')}, st / 'job.json')
moves = json.loads((st / 'move_log.json').read_text())
ledger['moves'] = moves
extract(st / 'vitalq_v2.kicad_pcb', st / 'geometry.json.gz')
b = Board(st / 'geometry.json.gz')

# Validate shifted U18 tracks exactly
for t in moves[0]['shifted_track_endpoints']:
    bounds = [min(t['a'][0], t['b'][0]) - 1, min(t['a'][1], t['b'][1]) - 1, max(t['a'][0], t['b'][0]) + 1, max(t['a'][1], t['b'][1]) + 1]
    item = b.items[t['uuid']]
    ok = b.allowed_local(t['net'], t['layer'], bounds, ignore=(t['uuid'],), width=item['width']).covers(LineString([t['a'], t['b']]))
    if not ok:
        ledger['issues'].append({'shifted_track': t, 'problem': 'shifted segment violates clearance'})
via = next(v for v in b.raw['vias'] if v['net'] == '+3V3' and math.dist(v['xy'], [27.56, 48.92]) < 0.05)
ledger['via_27.56_48.92_issues'] = b.via_issues('+3V3', via['xy'], via['diameter'], via['drill'], ignore=(via['uuid'],))
print('shifted-track issues', ledger['issues'], 'via issues', ledger['via_27.56_48.92_issues'], flush=True)

items = opens(s['current_drc'])


def find(net_pred, desc_pred):
    out = []
    for it in items:
        descs = ' '.join(e['description'] for e in it['items'])
        if net_pred(descs) and desc_pred(it):
            out.append(it)
    return out


targets = []
c59 = find(lambda t: '[+3V3]' in t and 'C59' in t, lambda it: True)
u18 = find(lambda t: '[+3V3]' in t and 'U18' in t, lambda it: True)
gnd = find(lambda t: '[GND]' in t, lambda it: any(abs(e['pos']['x'] - 19.451354) < 0.01 for e in it['items']))
for net in ('ADS1292_PWDN', 'IR_GATE', 'AD5940_RESET'):
    targets.append((net, find(lambda t, n=net: '[' + n + ']' in t, lambda it: True)))
plan_tracks, plan_vias = [], []

# C59.1: prompt-specified site first
if c59:
    p = b.pad('C59', 1)
    site = [14.90, 39.95]
    n = record_attempt('+3V3:C59.1', 'prompt site 0.4/0.2 via @(14.90,39.95) + F track C59.1 -> (14.90, C59.1.y) -> via')
    issues = b.via_issues('+3V3', site)
    path = [p['xy'], [14.90, p['xy'][1]], site]
    tracks = [{'net': '+3V3', 'layer': 0, 'a': a, 'b': c, 'width': 0.1016} for a, c in zip(path, path[1:])]
    bad = [t for t in tracks if not b.allowed_local('+3V3', 0, [13.5, 38.9, 16.5, 41.8]).covers(LineString([t['a'], t['b']]))]
    if not issues and not bad:
        vias = [{'net': '+3V3', 'xy': site, 'diameter': 0.4, 'drill': 0.2, 'pofv': False}]
        b.add_items(tracks, vias)
        ledger['routes'].append({'connection': 'C59.1', 'attempt': n, 'status': 'routed', 'approach': 'prompt site'})
    else:
        ledger['routes'].append({'connection': 'C59.1', 'attempt': n, 'status': 'failed', 'reason': {'via_issues': issues, 'track_clearance_failures': len(bad)}})
        print('C59 prompt site failed', issues, len(bad), flush=True)
        route_open(b, c59[0], 'C59.1', [('A* F/B/In2/In3 router, 0.4/0.2 via outside hv_inner, margin 2.5 mm', 2.5, SIGNAL_LAYERS)], ledger['routes'])
for it in u18:
    route_open(b, it, 'U18.8', [('A* F/B/In2/In3 router after U18 nudge, pin8 out of hv_inner, margin 2.5 mm', 2.5, SIGNAL_LAYERS)], ledger['routes'])
for it in gnd:
    route_open(b, it, 'GND U19.16/D30.2 island', [('A* F/B/In2/In3 router, F/B out of hv_inner then legal via/plane, margin 2.5 mm', 2.5, SIGNAL_LAYERS)], ledger['routes'])
for net, its in targets:
    for it in its:
        route_open(b, it, net + ' U19', [('A* F/B/In2/In3 router, outer layers out of hv_inner, via outside, margin 3 mm', 3.0, SIGNAL_LAYERS), ('A* F/B/In2/In3 router, wide detour margin 8 mm', 8.0, SIGNAL_LAYERS)], ledger['routes'])

new_tracks = [r for r in b.raw['tracks'] if str(r['uuid']).startswith('new-')]
new_vias = [r for r in b.raw['vias'] if str(r['uuid']).startswith('new-')]
plan = {'tracks': new_tracks, 'vias': new_vias}
save_json(d / 'plan.json', plan)
save_json(d / 'ledger.json', ledger)
kicad_job({'input': str(st / 'vitalq_v2.kicad_pcb'), 'output': str(d / 'vitalq_v2.kicad_pcb'), 'plan': str(d / 'plan.json'), 'log': str(d / 'apply_log.json')}, d / 'job.json')
drc(d / 'vitalq_v2.kicad_pcb', d / 'drc.json')
verify(d, moved=('U18',))
save_json(d / 'applied.json', {'batch': BATCH, 'ledger': 'ledger.json', 'plan': 'plan.json'})
done = [r['connection'] for r in ledger['routes'] if r['status'] == 'routed']
failed = [r['connection'] + ' (' + str(r.get('reason')) + ')' for r in ledger['routes'] if r['status'] == 'failed']
finish(BATCH, d, ', '.join(done) + '; U18 moved (26.899594,44.367033)', f'U18 dx=-0.31; {len(new_tracks)} tracks, {len(new_vias)} vias; failed attempts: ' + '; '.join(failed), 'R2-3 via-in-pad fan-out U6/U22/U7', extra_ok=not ledger['issues'])
