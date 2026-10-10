"""R2-18: leftovers away from U6/U22/U19 (SUPERVISOR_R2_02 #5): GND R9/R51 island, +3V3 sensor-island stitches (U20/U11, F.Cu island zone),
U18.7/U18.8, TMP117_ALERT, J12 MP, AFE_N_PAD. Each open routed on its own with a 60 s budget; hv_inner respected (outer layers only there)."""
import math
from r2_batch import run
from r2_common import route_open
from r2route import SIGNAL_LAYERS

APP = 'R2-18 leftover A* F/B/In2/In3 (inner only outside hv_inner), 0.4/0.2 vias, 60 s budget'
KEYS = ['R51', 'U20', '+3V3.F.Cu', 'U18', 'TMP117_ALERT', 'J12', 'AFE_N_PAD']


def plan(b, ledger, opens):
    todo = []
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if 'U6' in t or 'U22' in t or 'U19' in t or 'U7' in t or 'C59' in t:
            continue
        if any(k in t for k in KEYS):
            todo.append(it)
    todo.sort(key=lambda it: math.dist((it['items'][0]['pos']['x'], it['items'][0]['pos']['y']), (it['items'][-1]['pos']['x'], it['items'][-1]['pos']['y'])))
    for it in todo:
        t = ' '.join(e['description'] for e in it['items'])
        net = t.split('[')[1].split(']')[0]
        label = net + ' ' + ('/'.join(sorted({w for w in t.replace('[', ' ').split() if w[:1].isupper() and any(c.isdigit() for c in w) and len(w) <= 5})) or 'zone')
        route_open(b, it, label, [(APP + ', margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ', margin 10 mm', 10.0, SIGNAL_LAYERS)], ledger['routes'], small_vias=False)


run('R2-18', 'leftovers_A', plan, next_step='R2-19 C59 move + via; then U6/U22 blocked-net report')
