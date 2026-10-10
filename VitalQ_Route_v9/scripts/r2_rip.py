"""Local rip-up and component-based reconnection helpers for R2 batches."""
import math
from shapely.geometry import Point, box, LineString
from shapely.ops import unary_union, nearest_points
from r2_common import route_entries
from geometry import CLEARANCE, LAYERS as LAYERS_ALL

EPS = 0.002


def conflicts(b, refs, layers=(0, 2)):
    """Foreign tracks/vias violating clearance (copper 0.1016, via hole 0.2) to the pads of refs."""
    out = {}
    for p in b.raw['pads']:
        if p['ref'] not in refs:
            continue
        item = b.items[p['uuid']]
        for l, shape in item['shapes'].items():
            if l not in layers:
                continue
            for idx in b.trees[l].query(shape.buffer(0.35)):
                other, osh = b.solids[l][idx]
                if other['kind'] == 'pads' or other['net'] == p['net']:
                    continue
                gap = shape.distance(osh)
                bad = gap < CLEARANCE - 1e-6
                if other['kind'] == 'vias':
                    bad |= Point(other['xy']).distance(shape) - other['drill'] / 2 < 0.2 - 1e-6
                if bad:
                    out[other['uuid']] = {'uuid': other['uuid'], 'net': other['net'], 'kind': other['kind'], 'against': p['ref'] + '.' + p['number'], 'gap': gap}
    return out


def _touch(a, b_):
    return math.dist(a, b_) < EPS


def _record(b, u):
    it = b.items[u]
    rec = {'uuid': u, 'net': it['net'], 'kind': it['kind']}
    for k in ('layer', 'a', 'b', 'width', 'xy', 'diameter', 'drill'):
        if k in it:
            rec[k] = it[k]
    return rec


def chain_extend(b, seeds):
    """Extend removal along simple (non-branching, pad-free) chains so no dangling stubs remain."""
    removed = set(seeds)
    frontier = list(seeds)
    while frontier:
        u = frontier.pop()
        it = b.items[u]
        if it['kind'] == 'tracks':
            ends = [(it['layer'], it['a']), (it['layer'], it['b'])]
        else:
            ends = [(l, it['xy']) for l in it['layers']]
        for layer, pt in ends:
            p = Point(pt)
            stop = False
            touching = []
            for idx in b.trees[layer].query(p.buffer(0.3)):
                o, sh = b.solids[layer][idx]
                if o['net'] != it['net'] or o['uuid'] in removed:
                    continue
                if o['kind'] == 'pads' and sh.distance(p) < EPS:
                    stop = True
                elif o['kind'] == 'vias' and sh.distance(p) < EPS:
                    touching.append(o)
                elif o['kind'] == 'tracks' and (_touch(o['a'], pt) or _touch(o['b'], pt)):
                    touching.append(o)
                elif o['kind'] == 'tracks' and sh.distance(p) < EPS:
                    stop = True
            if stop or len(touching) != 1:
                continue
            o = touching[0]
            if o['kind'] == 'vias':
                others = 0
                for l in o['layers']:
                    for idx in b.trees[l].query(Point(o['xy']).buffer(o['diameter'] / 2)):
                        t, sh = b.solids[l][idx]
                        if t['net'] == o['net'] and t['uuid'] not in removed and t['uuid'] != o['uuid'] and t['kind'] != 'vias' and sh.distance(Point(o['xy'])) < o['diameter'] / 2:
                            others += 1
                    for z, zs in b.zones[l]:
                        if z['net'] == o['net'] and zs.distance(Point(o['xy'])) < o['diameter'] / 2 + 0.05:
                            others += 1
                if others:
                    continue
            removed.add(o['uuid'])
            frontier.append(o['uuid'])
    return removed


def rip(b, ledger, uuids, reason, extend=False):
    uuids = chain_extend(b, uuids) if extend else set(uuids)
    recs = [dict(_record(b, u), reason=reason) for u in uuids if u in b.items]
    real = [r['uuid'] for r in recs if not str(r['uuid']).startswith('new-')]
    ledger.setdefault('late_remove', []).extend(real)
    ledger['removed'].extend(recs)
    b.remove_items(uuids)
    return recs


def prune_orphans(b, ledger, nets):
    for net in nets:
        comps, _ = b.components(net)
        for entries in comps.values():
            if any(e['kind'] in ('pads', 'zones') for e, _ in entries):
                continue
            uu = [e['uuid'] for e, _ in entries]
            rip(b, ledger, uu, 'orphan after rip-up/detach (no pad, no plane)')


def reconnect(b, ledger, nets, region, approaches, label_prefix=''):
    """Route every pad/plane-bearing component of each net that touches region to the net's main component."""
    reg = box(*region)
    jobs = []
    for net in sorted(nets):
        comps, _ = b.components(net)
        live = [c for c in comps.values() if any(e['kind'] in ('pads', 'zones') for e, _ in c)]
        if len(live) < 2:
            continue
        main = max(live, key=lambda c: (any(e['kind'] == 'zones' for e, _ in c), len(c)))
        for c in live:
            if c is main:
                continue
            shapes = [s for _, d in c for s in d.values()]
            cutpts = [Point(r[k]) for r in ledger.get('removed', []) if r['net'] == net for k in ('a', 'b', 'xy') if k in r]
            if not any(s.intersects(reg) for s in shapes) and not any(s.distance(cp) < 0.05 for s in shapes for cp in cutpts):
                continue
            refs = sorted({e['ref'] for e, _ in c if e['kind'] == 'pads'})
            near = unary_union(shapes)
            zone = near.envelope.buffer(10)
            close = [s for _, d in main for s in d.values() if s.intersects(zone)]
            jobs.append((near.distance(unary_union(close)) if close else 99.0, net, c, refs))
    jobs.sort(key=lambda j: j[0])
    seen = {}
    for gap, net, c, refs in jobs:
        comps, ids = b.components(net)
        anchor = next(e['uuid'] for e, _ in c if e['kind'] == 'pads')
        src = comps[ids[anchor]]
        live = [x for x in comps.values() if any(e['kind'] in ('pads', 'zones') for e, _ in x)]
        main = max(live, key=lambda x: (any(e['kind'] == 'zones' for e, _ in x), len(x)))
        if main is src:
            continue
        label = label_prefix + net + ' ' + '/'.join(refs)
        seen[label] = seen.get(label, 0) + 1
        if seen[label] > 1:
            label += '#' + str(seen[label])
        route_entries(b, net, src, main, label, approaches, ledger['routes'])


def _anchored(b, net, pt, exclude):
    p = Point(pt)
    for l in LAYERS_ALL:
        for idx in b.trees[l].query(p.buffer(0.01)):
            o, sh = b.solids[l][idx]
            if o['net'] == net and o['uuid'] not in exclude and sh.distance(p) < EPS:
                return True
        for z, zs in b.zones[l]:
            if z['net'] == net and l in (0, 2) and zs.distance(p) < EPS:
                return True
    return False


def trim_stubs(b, ledger, nets, region):
    """Remove (or shorten) dangling track stubs left by cuts, never removing copper something else lands on."""
    reg = box(*region).buffer(3)
    cuts = []
    for r in ledger['removed']:
        cuts += [r[k] for k in ('a', 'b', 'xy') if k in r]
    changed = True
    while changed:
        changed = False
        for t in [r for r in b.raw['tracks'] if r['net'] in nets]:
            if t['uuid'] not in b.items or not LineString([t['a'], t['b']]).intersects(reg):
                continue
            for free, other in ((t['a'], t['b']), (t['b'], t['a'])):
                if _anchored(b, t['net'], free, {t['uuid']}) or not (str(t['uuid']).startswith('new-') or any(math.dist(free, c) < 0.01 for c in cuts)):
                    continue
                shape = b.items[t['uuid']]['shapes'][t['layer']]
                landings = []
                for l in LAYERS_ALL:
                    for idx in b.trees[l].query(shape):
                        o, sh = b.solids[l][idx]
                        if o['uuid'] == t['uuid'] or o['net'] != t['net']:
                            continue
                        if o['kind'] == 'tracks' and l == t['layer']:
                            for e in (o['a'], o['b']):
                                if shape.distance(Point(e)) < EPS:
                                    landings.append(e)
                        elif o['kind'] in ('vias', 'pads') and sh.intersects(shape):
                            landings.append(list(nearest_points(sh, Point(free))[0].coords[0]))
                seg = LineString([other, free])
                far = [x for x in landings if math.dist(x, other) > EPS]
                if not landings:
                    rip(b, ledger, [t['uuid']], 'dangling stub trim')
                elif far:
                    cut = max(far, key=lambda x: seg.project(Point(x)))
                    proj = seg.interpolate(seg.project(Point(cut)))
                    newb = [round(proj.x, 6), round(proj.y, 6)]
                    if math.dist(newb, free) < EPS:
                        continue
                    rec = {'net': t['net'], 'layer': t['layer'], 'a': list(other), 'b': newb, 'width': t['width']}
                    rip(b, ledger, [t['uuid']], 'dangling stub shortened')
                    if math.dist(rec['a'], rec['b']) > EPS:
                        b.add_items([rec], [])
                else:
                    rip(b, ledger, [t['uuid']], 'dangling stub trim')
                changed = True
                break


def push(b, ledger, uuids, dx, dy, reason):
    """Translate a cluster of foreign track segments by (dx,dy); neighbours sharing a moved vertex get that endpoint moved.
    Returns the uuids that could not be pushed legally (caller rips them)."""
    S = {u for u in uuids if u in b.items and b.items[u]['kind'] == 'tracks'}
    if not S:
        return set(uuids)
    verts = []
    for u in S:
        verts += [tuple(b.items[u]['a']), tuple(b.items[u]['b'])]
    def moved(pt):
        return any(math.dist(pt, v) < 1e-4 for v in verts)
    for v in verts:
        for idx in b.trees[0].query(Point(v).buffer(0.01)) if b.items[next(iter(S))]['layer'] == 0 else []:
            pass
    layer = {b.items[u]['layer'] for u in S}
    net = {b.items[u]['net'] for u in S}
    blocked = False
    for v in verts:
        for o in b.raw['vias'] + [p for p in b.raw['pads']]:
            if o.get('net') in net and math.dist(o['xy'], v) < 0.15:
                blocked = True
    if blocked or len(layer) != 1:
        return set(uuids)
    layer = layer.pop()
    N = [t for t in b.raw['tracks'] if t['uuid'] not in S and t['net'] in net and t['layer'] == layer and (moved(tuple(t['a'])) or moved(tuple(t['b'])))]
    new = []
    for u in S:
        t = b.items[u]
        new.append(dict({k: t[k] for k in ('uuid', 'net', 'layer', 'width')}, a=[t['a'][0] + dx, t['a'][1] + dy], b=[t['b'][0] + dx, t['b'][1] + dy]))
    for t in N:
        nt = dict({k: t[k] for k in ('uuid', 'net', 'layer', 'width')}, a=list(t['a']), b=list(t['b']))
        for k in ('a', 'b'):
            if moved(tuple(t[k])):
                nt[k] = [t[k][0] + dx, t[k][1] + dy]
        new.append(nt)
    ids = [t['uuid'] for t in new]
    olds = [dict(b.items[u]) for u in ids]
    b.remove_items(ids)
    ok = True
    for t in new:
        bounds = [min(t['a'][0], t['b'][0]) - 1, min(t['a'][1], t['b'][1]) - 1, max(t['a'][0], t['b'][0]) + 1, max(t['a'][1], t['b'][1]) + 1]
        if not b.allowed_local(t['net'], t['layer'], bounds, width=t['width']).covers(LineString([t['a'], t['b']])):
            ok = False
            break
    if not ok:
        b.add_items([{k: o[k] for k in ('uuid', 'net', 'layer', 'width', 'a', 'b')} for o in olds], [])
        return set(uuids)
    b.add_items(new, [])
    ledger.setdefault('modify_tracks', []).extend({'uuid': t['uuid'], 'a': t['a'], 'b': t['b']} for t in new)
    ledger.setdefault('pushed', []).append({'reason': reason, 'dx': dx, 'dy': dy, 'segments': [{'uuid': o['uuid'], 'net': o['net'], 'from': [o['a'], o['b']]} for o in olds]})
    return set()
