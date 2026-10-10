"""Incremental 8-layer open routing (SUPERVISOR: no regional rip; 5-10 nets per batch; accept if opens drop).
usage: r2_inc.py BATCH TAG key1,key2,...   (key = substring that must appear in the open's endpoint text)"""
import math
import sys
from r2 import STATE, save_json, state
from r2_batch import run
from r2_common import route_open
from r2route import SIGNAL_LAYERS_8
import os
if os.environ.get('WITH_IN4'):
    SIGNAL_LAYERS_8 = SIGNAL_LAYERS_8 + (10,)

BATCH, TAG, KEYS = sys.argv[1], sys.argv[2], sys.argv[3].split(',')
APP = BATCH + ' incremental 8L A* (F/B/In2/In3/In5' + ('/In4 local' if os.environ.get('WITH_IN4') else '') + '), no rip-up, 60 s budget'
s = state()
for _k in list(s['attempts']):
    if os.environ.get('WITH_IN4') and any(_k.startswith(x.strip('[]') + ':') for x in KEYS):
        s.setdefault('attempts_reset_' + BATCH, {})[_k] = s['attempts'][_k]; s['attempts'][_k] = []
save_json(STATE, s)
if not s.get('attempts_reset_8L'):
    s['attempts_reset_8L'] = {k: v for k, v in s['attempts'].items() if v}
    s['attempts'] = {}
    s['note_8L'] = 'SUPERVISOR: new 8-layer board -> remaining opens are routed one at a time; counters restarted (old counts archived in attempts_reset_8L)'
    save_json(STATE, s)


def plan(b, ledger, opens):
    todo = [it for it in opens if any(k in ' '.join(e['description'] for e in it['items']) for k in KEYS)]
    todo.sort(key=lambda it: math.dist((it['items'][0]['pos']['x'], it['items'][0]['pos']['y']), (it['items'][-1]['pos']['x'], it['items'][-1]['pos']['y'])))
    print('routing', len(todo), 'opens', flush=True)
    for it in todo:
        t = ' '.join(e['description'] for e in it['items'])
        net = t.split('[')[1].split(']')[0]
        refs = sorted({w for w in t.replace('[', ' ').split() if w[:1].isupper() and any(c.isdigit() for c in w) and len(w) <= 5})
        label = net + ' ' + '/'.join(refs)
        r = route_open(b, it, label, [(APP + ', margin 6 mm', 6.0, SIGNAL_LAYERS_8), (APP + ', margin 14 mm', 14.0, SIGNAL_LAYERS_8), (APP + ', margin 25 mm', 25.0, SIGNAL_LAYERS_8)], ledger['routes'], small_vias=True)
        print('OPEN', label, 'ok' if r else 'FAILED', flush=True)


run(BATCH, TAG, plan, adopt_if_fewer_opens=True, next_step='next incremental batch')
