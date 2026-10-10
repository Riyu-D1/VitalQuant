"""DRC cleanup: copper inside keepout rule areas (items_not_allowed). Rip every track/via that violates a keepout (rule areas are never edited)
and re-route those connections with the router (which honours all keepouts). Batch: python r2_ko.py BATCH [max_nets]."""
import sys
from collections import Counter
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2route import SIGNAL_LAYERS_8 as SL

BATCH = sys.argv[1]
MAXN = int(sys.argv[2]) if len(sys.argv) > 2 else 12
EXCL = set(sys.argv[3].split(',')) if len(sys.argv) > 3 else set()
ONLY = set(sys.argv[4].split(',')) if len(sys.argv) > 4 else None
APP = BATCH + ' keepout cleanup: rip copper inside rule areas, re-route around them'
PLANES = {'GND', '+3V3', 'VBAT_SYS'}


def plan(b, ledger, opens):
    hits = {}
    for l, areas in b.keepouts.items():
        for area, poly in areas:
            for idx in b.trees[l].query(poly):
                it, sh = b.solids[l][idx]
                if it['kind'] == 'pads' or not sh.intersects(poly):
                    continue
                if (it['kind'] == 'tracks' and area['tracks']) or (it['kind'] == 'vias' and area['vias']):
                    hits[it['uuid']] = it['net']
    per_net = Counter(hits.values())
    chosen = [n for n, _ in sorted(per_net.items(), key=lambda kv: kv[1]) if n not in PLANES and n and n not in EXCL and (ONLY is None or n in ONLY)][:MAXN]
    seeds = [u for u, n in hits.items() if n in chosen]
    print('keepout hits', len(hits), 'nets', len(per_net), 'chosen', chosen, 'items', len(seeds), flush=True)
    rip(b, ledger, seeds, APP)
    prune_orphans(b, ledger, set(chosen))
    reconnect(b, ledger, set(chosen), [-3, -11, 49, 65], [(APP + ', margin 4 mm', 4.0, SL), (APP + ', margin 10 mm', 10.0, SL)])


run(BATCH, 'keepout_cleanup', plan, next_step='next cleanup')
