"""R2 multi-layer grid router with exact shapely validation (JLC 0.2 mm hole rules)."""
import heapq
import math
import uuid as uuidlib
import numpy as np
import shapely
from shapely.geometry import Point, LineString, box
from shapely.ops import unary_union, nearest_points
from shapely.strtree import STRtree
from scipy.ndimage import distance_transform_edt
from geometry import Geometry, LAYERS, WIDTH, CLEARANCE, MARGIN, disk

BGA = ('U6', 'U7', 'U22')
SIGNAL_LAYERS = (0, 2, 6, 8)
SIGNAL_LAYERS_8 = (0, 2, 6, 8, 12)
LAYER_COST = {8: 1.0, 6: 1.15, 2: 1.3, 0: 1.5, 10: 2.5, 12: 1.0}
VIA_COST = 0.9
BIG = (0.4, 0.2)
SMALL = (0.25, 0.15)


class Board(Geometry):
    def _shapes(self, item, kind):
        if kind == 'tracks':
            return {item['layer']: LineString([item['a'], item['b']]).buffer(item['width'] / 2, quad_segs=32)}
        return {l: disk(item['xy'], item['diameter'] / 2) for l in item['layers']}

    def _rebuild(self, layers):
        for l in layers:
            self.trees[l] = STRtree([s for _, s in self.solids[l]])
        self.cache = {}

    def add_items(self, tracks=(), vias=()):
        touched = set()
        for kind, records in (('tracks', tracks), ('vias', vias)):
            for rec in records:
                rec.setdefault('uuid', 'new-' + str(uuidlib.uuid4()))
                if kind == 'vias':
                    rec.setdefault('layers', list(LAYERS))
                item = dict(rec, kind=kind)
                item['shapes'] = self._shapes(item, kind)
                self.items[item['uuid']] = item
                self.raw[kind].append(rec)
                for l, s in item['shapes'].items():
                    self.solids[l].append((item, s))
                    touched.add(l)
                if kind == 'vias':
                    self.drills.append((item, disk(item['xy'], item['drill'] / 2)))
        self._rebuild(touched)

    def remove_items(self, uuids):
        uuids = set(uuids)
        for kind in ('tracks', 'vias'):
            self.raw[kind] = [r for r in self.raw[kind] if r['uuid'] not in uuids]
        for u in uuids:
            self.items.pop(u, None)
        for l in LAYERS:
            self.solids[l] = [(i, s) for i, s in self.solids[l] if i['uuid'] not in uuids]
        self.drills = [(i, s) for i, s in self.drills if i['uuid'] not in uuids]
        self._rebuild(LAYERS)

    def via_allowed_local(self, net, bounds, size=BIG, ignore=(), forbid_pads=True):
        d, h = size
        region = box(*bounds)
        near = region.buffer(1)
        radius = max(CLEARANCE + d / 2, h / 2 + 0.2) + MARGIN
        blocks = []
        for l in LAYERS:
            for idx in self.trees[l].query(near):
                item, shape = self.solids[l][idx]
                if item['uuid'] in ignore:
                    continue
                if item['net'] != net:
                    blocks.append(shape.buffer(radius, quad_segs=16))
                elif forbid_pads and item['kind'] == 'pads' and l in (0, 2):
                    blocks.append(shape.buffer(d / 2 + 0.05, quad_segs=16))
        blocks.extend(s.buffer(0.2 + (d if i['kind'] == 'pads' else h) / 2 + MARGIN, quad_segs=16) for i, s in self.drills if i['uuid'] not in ignore and s.intersects(near))
        for l in LAYERS:
            for item, shape in self.keepouts[l]:
                if item['vias'] and shape.intersects(near):
                    blocks.append(shape.buffer(d / 2 + MARGIN, quad_segs=16))
        allowed = self.outline.buffer(-(0.3 + d / 2 + MARGIN)).intersection(region).difference(unary_union(blocks))
        shapely.prepare(allowed)
        return allowed

    def via_issues(self, net, xy, diameter=.4, drill=.2, ignore=()):
        return super().via_issues(net, xy, diameter, drill, ignore)

    def via_issues_exact(self, net, xy, d, h, ignore=(), tol=1e-6, ignore_keepouts=()):
        """JLC check with analytic via/track distances: copper>=0.1016, hole-to-any-foreign-copper>=0.2, hole-hole>=0.2."""
        c = Point(xy)
        need = max(CLEARANCE + d / 2, 0.2 + h / 2)
        hits = []
        for l in LAYERS:
            for idx in self.trees[l].query(c.buffer(need + 0.3)):
                it, sh = self.solids[l][idx]
                if it['uuid'] in ignore:
                    continue
                if it['kind'] == 'vias':
                    dist = math.dist(xy, it['xy'])
                    if it['net'] != net and dist - it['diameter'] / 2 < need - tol:
                        hits.append({'kind': 'vias', 'net': it['net'], 'uuid': it['uuid'], 'gap_hole_to_copper': dist - it['diameter'] / 2 - h / 2})
                    if dist - it['drill'] / 2 - h / 2 < 0.2 - tol:
                        hits.append({'kind': 'hole_pair', 'uuid': it['uuid'], 'gap': dist - it['drill'] / 2 - h / 2})
                    continue
                if it['net'] == net:
                    continue
                if it['kind'] == 'tracks':
                    dist = LineString([it['a'], it['b']]).distance(c) - it['width'] / 2
                else:
                    dist = sh.distance(c)
                if dist < need - tol:
                    hits.append({'kind': it['kind'], 'net': it['net'], 'uuid': it['uuid'], 'layer': l, 'gap_hole_to_copper': dist - h / 2})
        for it, sh in self.drills:
            if it['kind'] == 'pads' and it['uuid'] not in ignore and sh.distance(c) - d / 2 < 0.2 - tol:
                hits.append({'kind': 'pad_hole', 'uuid': it['uuid'], 'gap_hole_to_via_copper': sh.distance(c) - d / 2})
            elif it['kind'] == 'vias' and it['uuid'] not in ignore and it['net'] != net and math.dist(it['xy'], xy) - it['drill'] / 2 - d / 2 < 0.2 - tol:
                hits.append({'kind': 'via_hole_to_copper', 'uuid': it['uuid']})
        for l in LAYERS:
            for area, sh in self.keepouts[l]:
                if area['vias'] and area['uuid'] not in ignore_keepouts and sh.distance(c) < d / 2 + MARGIN:
                    hits.append({'kind': 'keepout', 'name': area['name'], 'uuid': area['uuid']})
        if not self.outline.buffer(-(0.3 + d / 2)).covers(c):
            hits.append({'kind': 'edge'})
        return hits

    def comp_shapes(self, entries, layers, bounds=None):
        clip = box(*bounds) if bounds else None
        out = {}
        for l in layers:
            shapes = [sh[l] for _, sh in entries if l in sh and (clip is None or sh[l].intersects(clip))]
            u = unary_union(shapes) if shapes else shapely.Polygon()
            out[l] = u.intersection(clip) if clip is not None and not u.is_empty else u
        return out


def _simplify(points, layer_allowed):
    pts = [points[0]]
    for p in points[1:]:
        if math.dist(pts[-1], p) > 1e-9:
            pts.append(p)
    if len(pts) <= 2:
        return pts if len(pts) == 2 and layer_allowed.covers(LineString(pts)) else (pts if len(pts) < 2 else None)
    out = [pts[0]]
    i = 0
    while i < len(pts) - 1:
        j = len(pts) - 1
        lo = i + 1
        best = lo
        step = 1
        k = i + 1
        while k <= j:
            if layer_allowed.covers(LineString([pts[i], pts[k]])):
                best = k
                k += step
                step *= 2
            else:
                break
        hi = min(k, j)
        for m in range(hi, best, -1):
            if layer_allowed.covers(LineString([pts[i], pts[m]])):
                best = m
                break
        if not layer_allowed.covers(LineString([pts[i], pts[best]])):
            return None
        out.append(pts[best])
        i = best
    return out


def route(b, net, src_entries, dst_entries, bounds, *a, **k):
    """Retry wrapper: a via site the grid mask accepted but the exact JLC check refuses is banned and the search re-run (max 6)."""
    banned = list(k.pop('via_ban', []))
    for _ in range(7):
        res, err = _route_once(b, net, src_entries, dst_entries, bounds, *a, via_ban=banned, **k)
        if res or not err or 'failed exact check' not in err:
            return res, err
        xy = err.split('[')[1].split(']')[0].split(',')
        banned.append((float(xy[0]), float(xy[1])))
    return res, err


def _route_once(b, net, src_entries, dst_entries, bounds, layers=SIGNAL_LAYERS, res=0.05, width=WIDTH, align=(0.0, 0.0), plane=True, max_expand=1_500_000, ignore=(), small_via_pads=None, log=print, escape_mask=None, escape_track_mask=None, time_budget=60.0, via_ban=()):
    import time as _time
    t_start = _time.monotonic()
    bx0, by0, bx1, by1 = bounds
    ox, oy = align
    x0 = ox + math.floor((bx0 - ox) / res) * res
    y0 = oy + math.floor((by0 - oy) / res) * res
    xs = np.arange(x0, bx1 + res / 2, res)
    ys = np.arange(y0, by1 + res / 2, res)
    xx, yy = np.meshgrid(xs, ys)
    H, W = xx.shape
    nl = len(layers)
    allowed = [b.allowed_local(net, l, bounds, ignore=ignore, width=width) for l in layers]
    free = np.array([shapely.contains_xy(a.buffer(-0.004), xx, yy) for a in allowed])
    vias_ok = shapely.contains_xy(b.via_allowed_local(net, bounds, BIG, ignore=ignore), xx, yy)
    for bx, by in via_ban:
        vias_ok &= (xx - bx) ** 2 + (yy - by) ** 2 > 0.3 ** 2
    small_cells = {}
    if small_via_pads:
        sallowed = b.via_allowed_local(net, bounds, SMALL, ignore=ignore, forbid_pads=False)
        for p in small_via_pads:
            cx, cy = int(round((p['xy'][0] - x0) / res)), int(round((p['xy'][1] - y0) / res))
            if 0 <= cx < W and 0 <= cy < H and abs(xs[cx] - p['xy'][0]) < 1e-6 and abs(ys[cy] - p['xy'][1]) < 1e-6 and sallowed.covers(Point(p['xy'])):
                small_cells[(cy, cx)] = p
    src = b.comp_shapes(src_entries, LAYERS, bounds)
    dst = b.comp_shapes(dst_entries or [], LAYERS, bounds)
    starts = np.zeros((nl + 1, H, W), bool)
    goals = np.zeros((nl + 1, H, W), bool)
    for i, l in enumerate(layers):
        if not src[l].is_empty:
            starts[i] = shapely.contains_xy(src[l], xx, yy) & free[i]
        if not dst[l].is_empty:
            goals[i] = shapely.contains_xy(dst[l], xx, yy) & free[i]
    if plane:
        for l in (4, 10, 14):
            if not dst[l].is_empty:
                goals[nl] |= shapely.contains_xy(dst[l].buffer(-0.35), xx, yy) & vias_ok
            if not src[l].is_empty:
                starts[nl] |= shapely.contains_xy(src[l].buffer(-0.35), xx, yy) & vias_ok
    thin = [sh[l] for e, sh in (dst_entries or []) if e['kind'] == 'tracks' for l in (4, 10, 14) if l in sh]
    if plane and thin:
        goals[nl] |= shapely.contains_xy(unary_union(thin).buffer(0.03).intersection(box(*bounds)), xx, yy) & vias_ok
    if escape_mask is not None:
        goals[nl] |= shapely.contains_xy(escape_mask, xx, yy) & vias_ok
    if escape_track_mask is not None:
        em = shapely.contains_xy(escape_track_mask, xx, yy)
        for i in range(nl):
            goals[i] |= em & free[i]
    for (cy, cx), p in small_cells.items():
        owner_src = any(p['uuid'] == e['uuid'] for e, _ in src_entries)
        (starts if owner_src else goals)[nl, cy, cx] = True
    if not starts.any() or not goals.any():
        return None, 'no legal start/goal cells (sealed endpoint)'
    viacell = vias_ok.copy()
    for (cy, cx) in small_cells:
        viacell[cy, cx] = True
    heur = distance_transform_edt(~goals.any(axis=0)) * res
    INF = np.inf
    cost = np.full(starts.shape, INF)
    parent = {}
    q = []
    for li, y, x in zip(*np.nonzero(starts)):
        cost[li, y, x] = 0.0
        heapq.heappush(q, (heur[y, x], 0.0, int(li), int(y), int(x)))
    nbrs = ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1))
    found = None
    expanded = 0
    lc = [LAYER_COST[l] for l in layers]
    while q and expanded < max_expand:
        f, c, li, y, x = heapq.heappop(q)
        if c > cost[li, y, x] + 1e-9:
            continue
        expanded += 1
        if expanded % 2000 == 0 and _time.monotonic() - t_start > time_budget:
            return None, f'budget exceeded ({time_budget:.0f} s, expanded {expanded})'
        if goals[li, y, x]:
            found = (li, y, x)
            break
        if li < nl:
            for dx, dy in nbrs:
                nx, ny = x + dx, y + dy
                if not (0 <= nx < W and 0 <= ny < H and free[li, ny, nx]):
                    continue
                if dx and dy and not (free[li, y, nx] and free[li, ny, x]):
                    continue
                nc = c + res * lc[li] * (1.41421356 if dx and dy else 1.0)
                if nc < cost[li, ny, nx] - 1e-9:
                    cost[li, ny, nx] = nc
                    parent[(li, ny, nx)] = (li, y, x)
                    heapq.heappush(q, (nc + heur[ny, nx], nc, li, ny, nx))
        if viacell[y, x]:
            for nli in range(nl + 1):
                if nli == li:
                    continue
                if nli < nl and not free[nli, y, x]:
                    continue
                if nli == nl and not goals[nl, y, x]:
                    continue
                nc = c + VIA_COST
                if nc < cost[nli, y, x] - 1e-9:
                    cost[nli, y, x] = nc
                    parent[(nli, y, x)] = (li, y, x)
                    heapq.heappush(q, (nc + heur[y, x], nc, nli, y, x))
    if found is None:
        return None, f'no path (expanded {expanded} cells)' if expanded < max_expand else f'node cap {max_expand} reached'
    cells = [found]
    while cells[-1] in parent:
        cells.append(parent[cells[-1]])
    cells.reverse()
    runs, vias = [], []
    cur, curl = [], None
    for li, y, x in cells:
        pt = (round(float(xs[x]), 6), round(float(ys[y]), 6))
        if li != curl:
            if curl is not None:
                if cur:
                    runs.append((curl, cur))
                vias.append(pt)
            cur, curl = [pt], li
        else:
            cur.append(pt)
    if cur:
        runs.append((curl, cur))
    if cells[0][0] == nl:
        vias.insert(0, (round(float(xs[cells[0][2]]), 6), round(float(ys[cells[0][1]]), 6)))
    tracks = []
    for li, pts in runs:
        if li == nl or len(pts) < 2:
            continue
        simple = _simplify(pts, allowed[li])
        if simple is None:
            return None, 'exact segment validation failed'
        tracks += [{'net': net, 'layer': layers[li], 'a': list(a), 'b': list(bb), 'width': width} for a, bb in zip(simple, simple[1:]) if math.dist(a, bb) > 1e-6]
    out_vias = []
    for v in vias:
        if any(math.dist(v, o['xy']) < 1e-6 for o in out_vias):
            continue
        cy, cx = int(round((v[1] - y0) / res)), int(round((v[0] - x0) / res))
        pad = small_cells.get((cy, cx))
        d, h = SMALL if pad else BIG
        out_vias.append({'net': net, 'xy': list(v), 'diameter': d, 'drill': h, 'pofv': bool(pad), 'pad': (pad['ref'] + '.' + pad['number']) if pad else None})
    for i, v in enumerate(out_vias):
        for o in out_vias[i + 1:]:
            if math.dist(v['xy'], o['xy']) - v['drill'] / 2 - o['drill'] / 2 < 0.2 + MARGIN:
                return None, 'planned vias violate hole-to-hole'
        if b.via_issues_exact(net, v['xy'], v['diameter'], v['drill'], ignore=ignore):
            return None, f"via site {v['xy']} failed exact check"
    return {'net': net, 'tracks': tracks, 'vias': out_vias, 'expanded': expanded, 'cost': float(cost[found])}, None


def components_for(b, net, endpoint_uuids):
    comps, ids = b.components(net)
    keys = []
    for u in endpoint_uuids:
        keys.append(ids.get(u))
    return comps, keys


def window(src_entries, dst_entries, margin, outline_bounds, cap=None):
    s = unary_union([sh for _, d in src_entries for sh in d.values()])
    if cap is not None:
        dshapes = [sh for _, d in dst_entries for sh in d.values() if sh.intersects(cap)]
    else:
        dshapes = [sh for _, d in dst_entries for sh in d.values()]
    t = unary_union(dshapes).intersection(cap) if cap is not None else unary_union(dshapes)
    p, q = nearest_points(s, t)
    sb = s.bounds
    x0 = min(sb[0], q.x) - margin
    y0 = min(sb[1], q.y) - margin
    x1 = max(sb[2], q.x) + margin
    y1 = max(sb[3], q.y) + margin
    ob = outline_bounds
    return [max(x0, ob[0]), max(y0, ob[1]), min(x1, ob[2]), min(y1, ob[3])], math.dist((p.x, p.y), (q.x, q.y))
