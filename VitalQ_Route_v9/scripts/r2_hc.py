"""DRC cleanup batch: hole_clearance (track/pad vs via hole or NPTH) — rip the offending TRACK segment(s) and re-route the connection
with the JLC hole rules; never accept a new open (batch acceptance: unconnected not rising, real falling)."""
import json
import sys
from r2 import state
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2route import SIGNAL_LAYERS_8 as SL

BATCH = sys.argv[1] if len(sys.argv) > 1 else 'R2-40'
APP = BATCH + ' hole_clearance cleanup: rip offending track, re-route with JLC 0.2 mm hole rules'


def plan(b, ledger, opens):
    rep = json.loads(open(state()['current_drc']).read())
    seeds, boxes = set(), []
    for v in rep['violations']:
        if v['type'] != 'hole_clearance':
            continue
        for i in v['items']:
            it = b.items.get(i['uuid'])
            if it and it['kind'] == 'tracks' and it['net'] not in ('GND', '+3V3', 'VBAT_SYS'):
                seeds.add(i['uuid'])
                boxes.append((i['pos']['x'], i['pos']['y']))
    print('hole_clearance tracks', len(seeds), flush=True)
    nets = {b.items[u]['net'] for u in seeds}
    rip(b, ledger, list(seeds), APP)
    prune_orphans(b, ledger, nets)
    xs = [p[0] for p in boxes]
    ys = [p[1] for p in boxes]
    reconnect(b, ledger, nets, [min(xs) - 3, min(ys) - 3, max(xs) + 3, max(ys) + 3], [(APP + ', margin 4 mm', 4.0, SL), (APP + ', margin 10 mm', 10.0, SL)])


run(BATCH, 'hole_clearance_cleanup', plan, next_step='next cleanup')
