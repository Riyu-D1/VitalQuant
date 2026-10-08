import argparse
import json
from pathlib import Path
from shapely.geometry import Point, box
from shapely.ops import unary_union
from route_geometry import Geometry, WIDTH, LAYERS
from surface_route import route_surface


def anchors(item):
    return [item["a"], item["b"]] if item["kind"] == "tracks" else [item["xy"]]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("net")
    parser.add_argument("output")
    parser.add_argument("--geometry", default="geometry_refilled.json")
    parser.add_argument("--only-planned")
    args = parser.parse_args()
    selected = {r["remove_via"] for r in json.loads(Path(args.only_planned).read_text())["repairs"]} if args.only_planned else None
    g = Geometry(args.geometry)
    comps, _ = g.components(args.net)
    largest = max(comps, key=lambda c: len(comps[c]))
    destination = comps[largest]
    filled = unary_union([shape for obj, shapes in destination if obj["kind"] == "zones" for layer, shape in shapes.items() if layer in (4,8,10)])
    plan = {"geometry_sha256": g.raw["sha256"], "net": args.net, "approach": "Relocate isolated forbidden plane vias using short surface tracks to surviving main-plane fill", "repairs": [], "failed": []}
    for comp, entries in comps.items():
        if comp == largest:
            continue
        vias = [obj for obj, shapes in entries if obj["kind"] == "vias"]
        if not vias:
            continue
        if len(vias) != 1:
            plan["failed"].append({"component": comp, "reason": "Multiple vias need separate connectivity audit", "vias": [v["uuid"] for v in vias]})
            continue
        via = vias[0]
        if selected is not None and via["uuid"] not in selected:
            continue
        connected_layers = sorted({layer for obj, shapes in entries if obj["kind"] != "vias" for layer in shapes})
        print(args.net, "island", comp, "via", via["xy"], "items", len(entries), "layers", connected_layers, flush=True)
        if any(layer not in (0,2) for layer in connected_layers):
            plan["failed"].append({"component": comp, "via": via["uuid"], "reason": "Existing inner-layer attachment must be preserved with a separate local reroute"})
            continue
        allowed_via = g.via_allowed(args.net, diameter=0.4, drill=0.2, ignore=(via["uuid"],))
        goal = allowed_via.intersection(filled.buffer(-0.08))
        center = via["xy"]
        bounds = (center[0]-4,center[1]-4,center[0]+4,center[1]+4)
        best = None
        for layer in connected_layers:
            starts = [via["xy"]]
            free = g.obstacles(args.net, layer, ignore=(via["uuid"],))
            path = route_surface(free, starts, goal, bounds)
            if path:
                distance = sum(Point(a).distance(Point(b)) for a, b in zip(path,path[1:]))
                if best is None or distance < best["length_mm"]:
                    best = {"layer":layer, "path":path, "length_mm":distance}
        if best is None:
            plan["failed"].append({"component":comp, "via":via["uuid"], "reason":"No surface path to a legal through-via within the 4mm local search window"})
            continue
        if len(connected_layers) != 1:
            plan["failed"].append({"component":comp, "via":via["uuid"], "reason":"Via carries both outer layers; a single-layer stitch is insufficient"})
            continue
        repair = {"component":comp, "net":args.net, "remove_via":via["uuid"], "old_xy":center,
                  "new_xy":best["path"][-1], "diameter":0.4, "drill":0.2, "width":WIDTH, **best}
        plan["repairs"].append(repair)
        print("  planned", repair["new_xy"], "layer", repair["layer"], "length", repair["length_mm"], flush=True)
    Path(args.output).write_text(json.dumps(plan,indent=2)+"\n")
    print("Planned", len(plan["repairs"]), "failed", len(plan["failed"]), flush=True)


if __name__ == "__main__":
    main()
