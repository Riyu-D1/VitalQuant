import gzip
import json
import math
from pathlib import Path
import numpy as np
import shapely
from shapely import affinity
from shapely.geometry import Point, LineString, Polygon, GeometryCollection
from shapely.ops import unary_union, nearest_points
from shapely.strtree import STRtree

LAYERS = (0, 2, 4, 6, 8, 10)
CLEARANCE = 0.1016
WIDTH = 0.1016
MARGIN = 0.006


def polyset(records):
    pieces = [shapely.make_valid(Polygon(p["outer"], p.get("holes", []))) for p in records if len(p["outer"]) >= 3]
    return unary_union(pieces)


def disk(xy, radius):
    return Point(xy).buffer(radius, quad_segs=32)


class Geometry:
    def __init__(self, path):
        if isinstance(path, dict):
            self.raw = path
        else:
            path = Path(path)
            if not path.exists():
                path = Path(str(path) + ".gz")
            with (gzip.open(path, "rt") if path.suffix == ".gz" else path.open()) as stream:
                self.raw = json.load(stream)
        self.outline = polyset(self.raw["outline"])
        self.items = {}
        self.solids = {l: [] for l in LAYERS}
        self.keepouts = {l: [] for l in LAYERS}
        self.drills = []
        self.zones = {l: [] for l in LAYERS}
        for kind in ("pads", "tracks", "vias"):
            for original in self.raw[kind]:
                item = dict(original, kind=kind)
                uid = item["uuid"]
                self.items[uid] = item
                if kind == "tracks":
                    shapes = {item["layer"]: LineString([item["a"], item["b"]]).buffer(item["width"] / 2, quad_segs=32)}
                elif kind == "vias":
                    shapes = {l: disk(item["xy"], item["diameter"] / 2) for l in item["layers"]}
                    self.drills.append((item, disk(item["xy"], item["drill"] / 2)))
                else:
                    shapes = {int(l): polyset(polys) for l, polys in item["polys"].items()}
                    dx, dy = item["drill"]
                    if max(dx, dy) > 0:
                        radius = min(dx, dy) / 2
                        a = ((max(dx - dy, 0)) / 2, max(dy - dx, 0) / 2)
                        drill = LineString([(-a[0], -a[1]), a]).buffer(radius, quad_segs=32) if max(a) else disk((0, 0), radius)
                        drill = affinity.rotate(drill, -item["rotation"], origin=(0, 0))
                        drill = affinity.translate(drill, *item["xy"])
                        self.drills.append((item, drill))
                item["shapes"] = shapes
                for layer, shape in shapes.items():
                    if not shape.is_empty:
                        self.solids[layer].append((item, shape))
        for original in self.raw["keepouts"]:
            item = dict(original)
            shape = polyset(item["outline"])
            for layer in item["layers"]:
                self.keepouts[layer].append((item, shape))
        for item in self.raw["zones"]:
            for layer, polys in item["fill"].items():
                for i, polygon in enumerate(polys):
                    self.zones[int(layer)].append((dict(item, island=i), polyset([polygon])))
        self.trees = {l: STRtree([s for _, s in self.solids[l]]) for l in LAYERS}
        self.cache = {}

    def obstacles(self, net, layer, radius=WIDTH / 2, ignore=(), dynamic_zones=True, margin=MARGIN):
        key = (net, layer, radius, tuple(ignore), dynamic_zones, margin)
        if key in self.cache:
            return self.cache[key]
        blocks = [s.buffer(CLEARANCE + radius + margin, quad_segs=32) for item, s in self.solids[layer] if item["net"] != net and item["uuid"] not in ignore]
        blocks.extend(s.buffer(max(CLEARANCE, 0.2 if item["kind"] == "vias" else 0.3) + radius + margin, quad_segs=32) for item, s in self.drills if item["net"] != net and item["uuid"] not in ignore)
        blocks.extend(s.buffer(radius + margin, quad_segs=32) for item, s in self.keepouts[layer] if item["tracks"])
        if not dynamic_zones:
            blocks.extend(s.buffer(CLEARANCE + radius + margin, quad_segs=32) for item, s in self.zones[layer] if item["net"] != net)
        obstacle = unary_union(blocks)
        allowed = self.outline.buffer(-(0.3 + radius + margin)).difference(obstacle)
        shapely.prepare(allowed)
        self.cache[key] = allowed
        return allowed

    def via_allowed(self, net, diameter=0.3, drill=0.15, ignore=()):
        key = (net, "via", diameter, drill, tuple(ignore))
        if key in self.cache:
            return self.cache[key]
        solids = {}
        for layer in LAYERS:
            for item, shape in self.solids[layer]:
                if item["net"] != net and item["uuid"] not in ignore:
                    radius = max(CLEARANCE + diameter / 2, drill / 2 + 0.2 if layer not in (0, 2) or item["kind"] == "tracks" else 0)
                    solids.setdefault((item["uuid"], radius), []).append(shape)
        blocks = [unary_union(shapes).buffer(radius + MARGIN, quad_segs=16) for (_, radius), shapes in solids.items()]
        blocks.extend(shape.buffer((0.2 if item["kind"] == "vias" else 0.45) + drill / 2 + MARGIN, quad_segs=16) for item, shape in self.drills if item["uuid"] not in ignore)
        seen = set()
        for layer in LAYERS:
            for item, shape in self.keepouts[layer]:
                if item["vias"] and item["uuid"] not in seen:
                    policy = self.raw.get("j8_pofv_policy")
                    if item.get("parent") == "J8" and policy:
                        forbidden = polyset(policy["original_outline"]).buffer(diameter / 2 + MARGIN, quad_segs=32)
                        if diameter <= 0.300001 and drill <= 0.150001:
                            centers = unary_union([polyset(ball["pad_polygons"]).buffer(-0.01) for ball in policy["balls"] if ball["eligible"] and ball["net"] == net])
                            forbidden = forbidden.difference(centers)
                        blocks.append(forbidden)
                    else:
                        blocks.append(shape.buffer(diameter / 2 + MARGIN, quad_segs=32))
                    seen.add(item["uuid"])
        allowed = self.outline.buffer(-(0.3 + diameter / 2 + MARGIN)).difference(unary_union(blocks))
        shapely.prepare(allowed)
        self.cache[key] = allowed
        return allowed

    def nearest_via(self, net, xy, max_distance=3, ignore=()):
        allowed = self.via_allowed(net, ignore=ignore)
        if allowed.is_empty:
            return None
        origin = Point(xy)
        point = nearest_points(origin, allowed)[1]
        if point.distance(origin) > max_distance:
            return None
        return {"xy": list(point.coords[0]), "distance": origin.distance(point)}

    def components(self, net):
        entries, parents = [], []
        by_uid = {}
        for item in self.items.values():
            if item["net"] != net:
                continue
            index = len(entries)
            entries.append((item, item["shapes"]))
            parents.append(index)
            by_uid[item["uuid"]] = index
        for layer in LAYERS:
            for item, shape in self.zones[layer]:
                if item["net"] == net:
                    index = len(entries)
                    entries.append((dict(item, kind="zones", component_uuid=item["uuid"] + ":" + str(item["island"]) + ":" + str(layer)), {layer: shape}))
                    parents.append(index)

        def root(i):
            while parents[i] != i:
                parents[i] = parents[parents[i]]
                i = parents[i]
            return i

        for layer in LAYERS:
            indexed = [(i, shapes[layer]) for i, (_, shapes) in enumerate(entries) if layer in shapes]
            shapes = [s.buffer(0.000002) for _, s in indexed]
            if not shapes:
                continue
            tree = STRtree(shapes)
            pairs = tree.query(shapes, predicate="intersects")
            for a, b in zip(*pairs):
                ra, rb = root(indexed[a][0]), root(indexed[b][0])
                if ra != rb:
                    parents[ra] = rb
        comps = {}
        for i, entry in enumerate(entries):
            comps.setdefault(root(i), []).append(entry)
        return comps, {uid: root(i) for uid, i in by_uid.items()}

    def blockers(self, net, layer, a, b):
        line = LineString([a, b]).buffer(WIDTH / 2 + MARGIN, quad_segs=16)
        near = self.trees[layer].query(line.buffer(CLEARANCE), predicate="intersects")
        copper = [{"uuid": self.solids[layer][i][0]["uuid"], "net": self.solids[layer][i][0]["net"]} for i in near if self.solids[layer][i][0]["net"] != net]
        keepouts = [{"uuid": item["uuid"], "name": item["name"], "parent": item["parent"]} for item, s in self.keepouts[layer] if item["tracks"] and s.intersects(line)]
        return {"copper": copper, "keepouts": keepouts, "edge": not self.outline.buffer(-0.3).covers(line)}
