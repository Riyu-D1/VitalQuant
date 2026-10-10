"""R2-36 cleanup: remove/shorten every dangling track/via reported by the refilled DRC (pre-existing ones included)."""
import json
from r2_batch import run
from r2_repair import dangling_ids, fix_plan
from r2_rip import rip


def plan(b, ledger, opens):
    from r2 import state
    st = json.loads(open(state()['current_drc']).read())
    ids = dangling_ids(st)
    log = []
    p = fix_plan(b, ids, log)
    ledger['dangling_cleanup'] = log
    only = {x['uuid'] for x in log if x.get('action') in ('removed dangling track', 'removed zero-length track', 'removed dangling via')}
    for e in p['remove']:
        if e['uuid'] in b.items and e['uuid'] in only:
            rip(b, ledger, [e['uuid']], 'cleanup: dangling copper (whole stub removed, no shortening)')
    print('cleanup', log, flush=True)


import sys as _sys
run(_sys.argv[1], 'dangling_cleanup', plan, adopt_if_fewer_opens=True, next_step='SOT-23 rule + fab outputs')
