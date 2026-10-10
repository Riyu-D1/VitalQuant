"""In-batch repair of dangling items created by the batch (supervisor gate rule, R2 #04)."""
import json
import math
import shutil
from shapely.geometry import Point, LineString
from r2_common import kicad_job, extract
from r2route import Board
from session import drc

TIGHT = 0.005


def dangling_ids(report):
    out = {}
    for v in report['violations']:
        if v['type'] in ('track_dangling', 'via_dangling'):
            for i in v['items']:
                out[i['uuid']] = v['type']
    return out


def strict_anchor(b, item, pt):
    p = Point(pt)
    layers = [item['layer']] if item['kind'] == 'tracks' else item['layers']
    for l in layers:
        for idx in b.trees[l].query(p.buffer(0.3)):
            o, sh = b.solids[l][idx]
            if o['uuid'] == item['uuid'] or o['net'] != item['net']:
                continue
            if o['kind'] == 'tracks':
                if any(math.dist(e, pt) < 1e-4 for e in (o['a'], o['b'])) or LineString([o['a'], o['b']]).distance(p) < o['width'] / 2 - TIGHT:
                    return True
            elif o['kind'] == 'vias':
                if math.dist(o['xy'], pt) < o['diameter'] / 2 - TIGHT:
                    return True
            elif sh.buffer(-TIGHT).contains(p):
                return True
        for z, zs in b.zones[l]:
            if z['net'] == item['net'] and l in (0, 2) and zs.buffer(-TIGHT).contains(p):
                return True
    return False


def fix_plan(b, ids, log):
    remove, add = [], []
    queue = dict(ids)
    seen = set()
    while queue:
        if len(remove) > 40:
            log.append({'action': 'ABORT: cascade exceeded 40 removals; repair stopped (needs manual review)'})
            break
        u, kind = queue.popitem()
        if u in seen:
            continue
        seen.add(u)
        before = set(remove)
        n_add = len(add)
        _fix_one(b, u, remove, add, log)
        if len(add) > n_add:
            b.add_items(add[n_add:], [])
        for r in set(remove) - before:
            it = b.items.get(r)
            if it is None:
                continue
            ends = [it['a'], it['b']] if it['kind'] == 'tracks' else [it['xy']]
            net = it['net']
            b.remove_items([r])
            for t in list(b.raw['tracks']):
                if t['net'] == net and t['uuid'] not in seen and any(min(math.dist(t['a'], e), math.dist(t['b'], e)) < 0.06 for e in ends):
                    item = b.items.get(t['uuid'])
                    if item and not (strict_anchor(b, item, t['a']) and strict_anchor(b, item, t['b'])):
                        queue[t['uuid']] = 'cascade'
    gone = {u for u in remove if str(u).startswith('new-')}
    add = [t for t in add if t.get('uuid') not in gone]
    return {'remove': [{'uuid': u} for u in remove if u not in gone], 'tracks': add}


def _fix_one(b, u, remove, add, log):
    for _ in [0]:
        it = b.items.get(u)
        if it is None:
            continue
        kind = None
        if it['kind'] == 'vias':
            remove.append(u)
            log.append({'uuid': u, 'net': it['net'], 'action': 'removed dangling via', 'xy': it['xy']})
            continue
        if math.dist(it['a'], it['b']) < 1e-6:
            remove.append(u)
            log.append({'uuid': u, 'net': it['net'], 'action': 'removed zero-length track'})
            continue
        anc = [strict_anchor(b, it, it['a']), strict_anchor(b, it, it['b'])]
        if all(anc):
            log.append({'uuid': u, 'net': it['net'], 'action': 'left (both ends anchored geometrically)'})
            continue
        keep = it['a'] if anc[0] else (it['b'] if anc[1] else None)
        free = it['b'] if keep is it['a'] else it['a']
        seg = LineString([keep or it['a'], free])
        shape = it['shapes'][it['layer']]
        landings = []
        for l in ([it['layer']] + [x for x in (0, 2, 4, 6, 8, 10) if x != it['layer']]):
            for idx in b.trees[l].query(shape):
                o, sh = b.solids[l][idx]
                if o['uuid'] == u or o['net'] != it['net']:
                    continue
                if o['kind'] == 'tracks' and l == it['layer']:
                    landings += [e for e in (o['a'], o['b']) if shape.distance(Point(e)) < 1e-3]
                elif o['kind'] == 'vias' and l == it['layer'] and sh.intersects(shape):
                    landings.append(o['xy'])
                elif o['kind'] == 'pads' and l == it['layer'] and sh.intersects(shape):
                    landings.append(list(sh.centroid.coords[0]))
        far = [x for x in landings if keep is None or math.dist(x, keep) > 1e-3]
        remove.append(u)
        if keep is not None and far:
            cut = max(far, key=lambda x: seg.project(Point(x)))
            proj = seg.interpolate(seg.project(Point(cut)))
            nb = [round(proj.x, 6), round(proj.y, 6)]
            if math.dist(nb, keep) > 1e-6:
                add.append({'net': it['net'], 'layer': it['layer'], 'a': list(keep), 'b': nb, 'width': it['width']})
            log.append({'uuid': u, 'net': it['net'], 'action': 'shortened dangling track to last landing', 'new_end': nb})
        else:
            log.append({'uuid': u, 'net': it['net'], 'action': 'removed dangling track', 'a': it['a'], 'b': it['b']})


def repair(d, base_drc, ledger, rounds=6):
    base_ids = dangling_ids(json.loads(open(base_drc).read()))
    try:
        plan = json.loads((d / 'plan.json').read_text())
        base_ids.update({v['board_uuid']: 'expected' for v in plan.get('vias', []) if v.get('expected_dangling') and v.get('board_uuid')})
    except FileNotFoundError:
        pass
    ledger.setdefault('dangling_repair', [])
    for r in range(1, rounds + 1):
        rep = json.loads((d / 'drc.json').read_text())
        new = {u: k for u, k in dangling_ids(rep).items() if u not in base_ids}
        if not new:
            return True
        print(f'dangling repair round {r}: {len(new)} new items', flush=True)
        pre = d / f'pre_repair{r}'
        pre.mkdir()
        for f in ('vitalq_v2.kicad_pcb', 'drc.json', 'vitalq_v2.kicad_pro', 'vitalq_v2.kicad_dru', 'vitalq_v2.kicad_sch'):
            shutil.copy2(d / f, pre / f)
        extract(d / 'vitalq_v2.kicad_pcb', pre / 'geometry.json.gz')
        b = Board(pre / 'geometry.json.gz')
        log = []
        plan = fix_plan(b, new, log)
        ledger['dangling_repair'].append({'round': r, 'items': log})
        (pre / 'plan.json').write_text(json.dumps(plan, indent=1))
        if not plan['remove'] and not plan['tracks']:
            return False
        for e in plan['remove']:
            if not e['uuid'].startswith('new-'):
                ledger.setdefault('removed', []).append(dict(e, reason='in-batch dangling repair'))
        kicad_job({'input': str(pre / 'vitalq_v2.kicad_pcb'), 'output': str(d / 'vitalq_v2.kicad_pcb'), 'plan': str(pre / 'plan.json'), 'log': str(pre / 'apply_log.json')}, pre / 'job.json')
        drc(d / 'vitalq_v2.kicad_pcb', d / 'drc.json')
    rep = json.loads((d / 'drc.json').read_text())
    return not {u for u in dangling_ids(rep) if u not in base_ids}
