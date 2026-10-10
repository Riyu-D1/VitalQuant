"""R2-67 (supervisor single-net #3): C59 rotated 180 in place (detach). Pad 1 (+3V3) moves to y 40.03 next to the old C59.2 GND via site;
a POFV 0.25/0.15 at (15.20, 39.90) (pad edge, outside hv_inner) drops to the In4 +3V3 plane; C59.2 (GND) re-attached on F."""
from r2_batch import run
from r2_rip import reconnect
from r2route import SMALL, SIGNAL_LAYERS_8 as SL
from r2 import STATE, save_json, state

MOVES = [{'ref': 'C59', 'from': [15.647664, 40.509838], 'to': [15.647664, 40.509838], 'rotate_to': 270.0, 'detach': True, 'reason': 'supervisor #3: rotate C59 180 so pad 1 (+3V3) faces a via site outside hv_inner'}]
s = state()
for k in list(s['attempts']):
    if k.split(':')[0] in ('+3V3', 'GND'):
        s.setdefault('attempts_reset_R2_67', {})[k] = s['attempts'][k]
        s['attempts'][k] = []
save_json(STATE, s)


def plan(b, ledger, opens):
    p1 = b.pad('C59', '1')
    print('C59.1 now at', p1['xy'], flush=True)
    site = (15.2, 39.9) if p1['xy'][1] < 40.5 else None
    if site:
        h = b.via_issues_exact('+3V3', site, *SMALL)
        print('site hits', sorted({x.get('net', x.get('name')) for x in h}, key=str), flush=True)
        if not h:
            b.add_items([], [{'net': '+3V3', 'xy': list(site), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'C59.1'}])
    reconnect(b, ledger, {'GND', '+3V3'}, [14.0, 38.5, 17.5, 42.0], [('R2-67 C59 rotated, re-attach pad nets, margin 3 mm', 3.0, SL), ('R2-67 C59 rotated, margin 6 mm', 6.0, SL)])


run('R2-67', 'C59_rot180_In4_via', plan, moves=MOVES, moved=('C59',), adopt_if_fewer_opens=True, next_step='stop at 00:14')
