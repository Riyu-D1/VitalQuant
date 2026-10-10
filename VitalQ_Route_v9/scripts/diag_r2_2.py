"""Read-only diagnosis of R2-2 router failures (no board changes, not an attempt)."""
import json
from shapely.geometry import Point, box
from r2route import Board
from r2 import ROOT, save_json

b = Board(ROOT / 'candidates/R2-2/stage1/geometry.json.gz')
out = {}
for ref, num, layer in [('U19', 3, 0), ('U19', 4, 0), ('U19', 7, 0), ('C59', 1, 0), ('U18', 8, 0)]:
    p = b.pad(ref, num)
    bounds = [p['xy'][0] - 4, p['xy'][1] - 4, p['xy'][0] + 4, p['xy'][1] + 4]
    rec = {'net': p['net'], 'xy': p['xy']}
    for l in (0, 2):
        allowed = b.allowed_local(p['net'], l, bounds)
        geoms = getattr(allowed, 'geoms', [allowed])
        comp = next((g for g in geoms if g.distance(Point(p['xy'])) < 0.02), None)
        if comp is None:
            rec[f'layer{l}'] = 'pad centre not in free space'
            continue
        region = comp
        vias = b.via_allowed_local(p['net'], bounds)
        frontier = region.buffer(0.2).difference(region)
        blockers = {}
        for idx in b.trees[l].query(frontier):
            it, sh = b.solids[l][idx]
            if it['net'] != p['net'] and sh.distance(region) < 0.2:
                blockers[it['uuid']] = {'kind': it['kind'], 'net': it['net'], 'ref': it.get('ref'), 'number': it.get('number')}
        rec[f'layer{l}'] = {'reachable_area_mm2': round(region.area, 3), 'bounds': [round(v, 3) for v in region.bounds], 'via_sites_in_region_mm2': round(region.intersection(vias).area, 4), 'frontier_blockers': list(blockers.values())[:25]}
    out[f'{ref}.{num}'] = rec
save_json(ROOT / 'candidates/R2-2/diagnosis.json', out)
print(json.dumps(out, indent=1))
