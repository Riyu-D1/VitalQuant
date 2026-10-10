from pathlib import Path
import sys
import numpy as np
import shapely
from shapely.geometry import Point, LineString, box
from shapely.ops import unary_union

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'VitalQ_Route_v8' / 'scripts'))
import route_geometry as _rg
_rg.LAYERS = (0, 2, 4, 6, 8, 10, 12, 14)
from route_geometry import Geometry as ReferenceGeometry, LAYERS, WIDTH, CLEARANCE, disk
from surface_route import route_surface

MARGIN = 0.002


class Geometry(ReferenceGeometry):
    def via_allowed(self, net, diameter=0.4, drill=0.2, ignore=()):
        key = (net, 'v9_via', diameter, drill, tuple(ignore))
        if key in self.cache:
            return self.cache[key]
        solids = {}
        for layer in LAYERS:
            for item, shape in self.solids[layer]:
                if item['net'] != net and item['uuid'] not in ignore:
                    solids.setdefault(item['uuid'], []).append(shape)
        radius = max(CLEARANCE + diameter / 2, drill / 2 + 0.2)
        blocks = [unary_union(shapes).buffer(radius + MARGIN, quad_segs=32) for shapes in solids.values()]
        blocks.extend(shape.buffer(0.2 + drill / 2 + MARGIN, quad_segs=32) for item, shape in self.drills if item['uuid'] not in ignore)
        seen = set()
        for layer in LAYERS:
            for item, shape in self.keepouts[layer]:
                if item['vias'] and item['uuid'] not in seen:
                    blocks.append(shape.buffer(diameter / 2 + MARGIN, quad_segs=32))
                    seen.add(item['uuid'])
        allowed = self.outline.buffer(-(0.3 + diameter / 2 + MARGIN)).difference(unary_union(blocks))
        shapely.prepare(allowed)
        self.cache[key] = allowed
        return allowed

    def pad(self, ref, number):
        return next(p for p in self.raw['pads'] if p['ref'] == ref and p['number'] == str(number))

    def via_issues(self, net, xy, diameter=.4, drill=.2, ignore=()):
        center = Point(xy)
        radius = max(CLEARANCE + diameter / 2, drill / 2 + .2) + MARGIN
        hits = {}
        for layer in LAYERS:
            for idx in self.trees[layer].query(center.buffer(radius)):
                item, shape = self.solids[layer][idx]
                if item['net'] != net and item['uuid'] not in ignore and center.distance(shape) < radius:
                    hits[item['uuid']] = {'kind': item['kind'], 'net': item['net'], 'uuid': item['uuid'], 'layer': layer, 'gap_from_hole': center.distance(shape) - drill / 2}
            for area, shape in self.keepouts[layer]:
                if area['vias'] and center.distance(shape) < diameter / 2 + MARGIN:
                    hits[area['uuid']] = {'kind': 'keepout', 'name': area['name'], 'uuid': area['uuid']}
        for item, shape in self.drills:
            if item['uuid'] not in ignore and center.distance(shape) < (diameter if item['kind'] == 'pads' else drill) / 2 + .2 + MARGIN:
                hits['hole:' + item['uuid']] = {'kind': 'drill', 'uuid': item['uuid'], 'gap': center.distance(shape) - drill / 2}
        if not self.outline.buffer(-(.3 + diameter / 2 + MARGIN)).covers(center):
            hits['edge'] = {'kind': 'edge'}
        return list(hits.values())

    def allowed_local(self, net, layer, bounds, ignore=(), width=WIDTH):
        region = box(*bounds)
        near = region.buffer(1)
        radius = width / 2
        blocks = []
        for idx in self.trees[layer].query(near):
            item, shape = self.solids[layer][idx]
            if item['net'] != net and item['uuid'] not in ignore:
                blocks.append(shape.buffer(CLEARANCE + radius + MARGIN, quad_segs=32))
        blocks.extend(s.buffer(max(CLEARANCE, .2 if i['kind'] == 'vias' else .3) + radius + MARGIN, quad_segs=32) for i, s in self.drills if i['net'] != net and i['uuid'] not in ignore and s.intersects(near))
        blocks.extend(s.buffer(radius + MARGIN, quad_segs=32) for i, s in self.keepouts[layer] if i['tracks'] and s.intersects(near))
        allowed = self.outline.buffer(-(.3 + radius + MARGIN)).intersection(region).difference(unary_union(blocks))
        shapely.prepare(allowed)
        return allowed

    def path(self, net, layer, start, finish, bounds, ignore=(), resolution=0.04):
        allowed = self.allowed_local(net, layer, bounds, ignore=ignore)
        if allowed.covers(LineString([start, finish])):
            return [start, finish]
        path = route_surface(allowed, [start], disk(finish, 0.065), bounds, resolution=resolution)
        if path and allowed.covers(LineString([path[-1], finish])):
            return path + [finish]
        return None

    def track_records(self, net, layer, path, width=WIDTH):
        return [{'net': net, 'layer': layer, 'a': a, 'b': b, 'width': width} for a, b in zip(path, path[1:]) if np.linalg.norm(np.array(a) - np.array(b)) > 0.000002]
