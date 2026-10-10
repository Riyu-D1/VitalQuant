"""R2-20: C59 move (+0.75,-1.00) (supervisor-listed alternative; counters for the C59 connections reset because the part position differs) per SUPERVISOR_R2_01 (1.03 mm authorised exception), drag-with-tracks, fix local hits, C59.1 -> +3V3 plane via outside hv_inner."""
import math
from shapely.geometry import LineString
from r2_batch import run
from r2_rip import conflicts, rip, prune_orphans, reconnect
from r2_common import route_entries, open_comps
from r2route import SIGNAL_LAYERS

MOVES = [{'ref': 'C59', 'from': [15.647664, 40.509838], 'to': [16.397664, 39.509838], 'reason': 'SUPERVISOR_R2_01 listed alternative (+0.75,-1.00): avoids the NIR_AN via/track hit of (+0.25,-1.00) (R2-19); C59.1 out of hv_inner for a legal +3V3 plane via'}]
REGION = [13.5, 37.5, 19.0, 42.0]
APP = 'R2-20 C59 drag (+0.75,-1.00), local hit rip, A* to +3V3 plane via outside hv_inner, 60 s budget'


def plan(b, ledger, opens):
    seeds = {}
    for m in ledger['moves']:
        for t in m.get('shifted_track_endpoints', []):
            it = b.items[t['uuid']]
            if t.get('kind') == 'via':
                if b.via_issues_exact(t['net'], t['xy'], it['diameter'], it['drill'], ignore=(t['uuid'],)):
                    seeds[t['uuid']] = 'dragged C59 via clearance/keepout hit'
                continue
            bounds = [min(t['a'][0], t['b'][0]) - 1, min(t['a'][1], t['b'][1]) - 1, max(t['a'][0], t['b'][0]) + 1, max(t['a'][1], t['b'][1]) + 1]
            if not b.allowed_local(t['net'], t['layer'], bounds, ignore=(t['uuid'],), width=it['width']).covers(LineString([t['a'], t['b']])):
                seeds[t['uuid']] = 'dragged C59 segment violates clearance'
    for u, info in conflicts(b, ('C59',)).items():
        seeds[u] = 'foreign copper hit by moved C59 pad ' + info['against']
    rip(b, ledger, list(seeds), 'R2-19 local fix after C59 move')
    print('ripped', [(r['net'], r.get('kind')) for r in ledger['removed']], flush=True)
    nets = {r['net'] for r in ledger['removed']} | {'+3V3', 'GND'}
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, REGION, [(APP + ', margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ', margin 7 mm', 7.0, SIGNAL_LAYERS)])


import json as _j
from r2 import state as _st, STATE as _S, save_json as _sj
_s = _st(); _r = {k: v for k, v in _s['attempts'].items() if k in ('+3V3:+3V3 C59', 'GND:GND C59')}
_s.setdefault('attempts_reset_R2_20', {}).update(_r)
for _k in _r: _s['attempts'][_k] = []
_sj(_S, _s)
run('R2-20', 'C59_move_alt', plan, moves=MOVES, moved=('C59',), next_step='R2-20 sensor-island +3V3/TMP117 bridge; U6/U22 regional plan')
