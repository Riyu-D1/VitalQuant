"""R2-58 cleanup: delete exactly the track segments KiCad reports as track_dangling (one end unconnected) and any via left with <2 layers used."""
import json
from r2 import state
from r2_batch import run
from r2_rip import rip


def plan(b, ledger, opens):
    rep = json.loads(open(state()['current_drc']).read())
    ids = [i['uuid'] for v in rep['violations'] if v['type'] in ('track_dangling', 'via_dangling') for i in v['items'] if i['uuid'] in b.items]
    print('dangling', len(ids), [(b.items[u]['net'], b.items[u]['kind']) for u in ids], flush=True)
    rip(b, ledger, ids, 'R2-58 cleanup: dangling item reported by refilled DRC')


import sys as _s
run(_s.argv[1] if len(_s.argv) > 1 else 'R2-58', 'dangling_delete', plan, next_step='J8 courtyard / hole clearance')
