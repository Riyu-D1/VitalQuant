"""Read-only replay of R2-4 planning to diagnose sealed U19 pins (no attempts recorded, no board writes)."""
import json
import r2_common
r2_common.record_attempt = lambda key, desc: 1
import r2_rip
from shapely.geometry import Point
from r2route import Board
from r2 import ROOT
from r2_4_cfg import TARGET_NETS, REGION, APPROACH

b = Board(ROOT / 'candidates/R2-4/stage1/geometry.json.gz')
ledger = {'removed': [], 'routes': []}
hits = r2_rip.conflicts(b, ('U19', 'C59'))
r2_rip.rip(b, ledger, list(hits), 'diag')
print('conflict seeds', len(hits), 'removed with chains', len(ledger['removed']))
from collections import Counter
print(Counter(r['net'] for r in ledger['removed']))
out = {}
for num in ('2', '3', '4', '10', '13'):
    p = b.pad('U19', num)
    bounds = [p['xy'][0] - 3, p['xy'][1] - 3, p['xy'][0] + 3, p['xy'][1] + 3]
    allowed = b.allowed_local(p['net'], 0, bounds)
    comp = next((g for g in getattr(allowed, 'geoms', [allowed]) if g.distance(Point(p['xy'])) < 0.02), None)
    if comp is None:
        out[num] = 'sealed at pad'
        continue
    vias = b.via_allowed_local(p['net'], bounds)
    fr = comp.buffer(0.25)
    bl = {}
    for l in (0,):
        for idx in b.trees[l].query(fr):
            it, sh = b.solids[l][idx]
            if it['net'] != p['net'] and sh.distance(comp) < 0.2:
                bl[it['uuid']] = (it['kind'], it['net'], it.get('ref'), it.get('number'))
    out[num] = {'net': p['net'], 'area': round(comp.area, 3), 'bounds': [round(v, 2) for v in comp.bounds], 'via_area': round(comp.intersection(vias).area, 4), 'blockers': sorted(set(bl.values()), key=str)}
print(json.dumps(out, indent=1))
