import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
from shapely.geometry import Point
from shapely.ops import unary_union, nearest_points
from route_geometry import Geometry, LAYERS


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--geometry", default="geometry_refilled.json")
    parser.add_argument("--drc", default="drc_resume_refilled.json")
    args = parser.parse_args()
    geometry = Geometry(args.geometry)
    drc = json.loads(Path(args.drc).read_text())
    cached = {}
    result = []
    lines = []
    for number, violation in enumerate(drc["unconnected_items"], 1):
        net = next((m.group(1) for item in violation["items"] if (m := re.search(r"\[([^]]+)\]", item["description"]))), None)
        if net not in cached:
            cached[net] = geometry.components(net)
        comps, mapping = cached[net]
        endpoints = []
        for item in violation["items"]:
            uid = item["uuid"]
            obj = geometry.items.get(uid)
            pos = [item["pos"]["x"], item["pos"]["y"]]
            comp = comps.get(mapping.get(uid), [])
            conductors = [{"uuid": o["uuid"], "kind": o["kind"], "ref": o.get("ref"), "number": o.get("number"), "layers": list(shapes)} for o, shapes in comp]
            endpoint = {**item, "component": mapping.get(uid), "conductors": conductors}
            if obj:
                endpoint["geometry"] = {k: v for k, v in obj.items() if k not in ("shapes", "polys", "connected")}
            endpoint["nearby_via"] = geometry.nearest_via(net, pos)
            endpoint["plane_access"] = []
            for layer in LAYERS:
                fills = [shape for zone, shape in geometry.zones[layer] if zone["net"] == net]
                if fills:
                    filled = unary_union(fills)
                    p = nearest_points(Point(pos), filled)[1]
                    endpoint["plane_access"].append({"layer": layer, "distance_center_to_fill": Point(pos).distance(p), "nearest_fill": list(p.coords[0])})
            endpoint["via_keepouts"] = list({k["uuid"]: {"name": k["name"], "parent": k["parent"], "layers": k["layers"]} for layer in LAYERS for k, shape in geometry.keepouts[layer] if k["vias"] and Point(pos).buffer(0.15).intersects(shape)}.values())
            endpoints.append(endpoint)
        a, b = endpoints
        ax = [a["pos"]["x"], a["pos"]["y"]]
        bx = [b["pos"]["x"], b["pos"]["y"]]
        if net in ("GND", "+3V3") and any(e["via_keepouts"] and e.get("geometry", {}).get("kind") == "vias" for e in endpoints):
            cause = "Existing via lies in hv_inner fill/via exclusion: refill removes the plane beneath it; missing surface connection to a legal plane stitch."
            cls = "PLANE"
        elif net in ("GND", "+3V3"):
            cause = "Separate same-net copper/plane components after refill; explicit stitch or surface bridge is missing."
            cls = "PLANE"
        elif any(e.get("geometry", {}).get("kind") == "vias" for e in endpoints):
            cause = "Added BGA via reaches only the source component; no routed connection to the destination component."
            cls = "HARD"
        elif any(e.get("geometry", {}).get("ref") in ("U6", "U7", "U22") for e in endpoints):
            cause = "Unescaped BGA pad or incomplete fanout; no conducting route between the listed components."
            cls = "HARD"
        elif net == "unconnected-(J12-PadMP)":
            cause = "Two shield/mounting pads share a net name but no copper connection; preserve netlist, do not assume an NC waiver."
            cls = "MEDIUM"
        else:
            cause = "Incomplete route between distinct copper components; existing stubs do not meet on a common layer."
            cls = "MEDIUM"
        direct = {str(layer): geometry.blockers(net, layer, ax, bx) for layer in LAYERS}
        record = {"id": number, "net": net, "class": cls, "cause": cause, "reported_position_distance_mm": math.dist(ax, bx), "endpoints": endpoints, "direct_blockers": direct}
        result.append(record)
        lines.append(f"{number:02} {net} [{cls}] {math.dist(ax, bx):.3f} mm — {cause}")
        for label, endpoint in zip("AB", endpoints):
            via = endpoint["nearby_via"]
            lines.append(f"  {label}: {endpoint['description']} @ {endpoint['pos']}; component size {len(endpoint['conductors'])}; nearest legal via candidate {via}")
            if endpoint["plane_access"]:
                lines.append("    plane access " + json.dumps(endpoint["plane_access"]))
            if endpoint["via_keepouts"]:
                lines.append("    via keepouts " + json.dumps(endpoint["via_keepouts"]))
        lines.append("  direct blockers " + "; ".join(f"L{l}: {len(v['copper'])} foreign solids, {len(v['keepouts'])} keepouts, edge={v['edge']}" for l, v in direct.items()))
        print(lines[-(1 + sum(1 + bool(e['plane_access']) + bool(e['via_keepouts']) for e in endpoints) + 1)], flush=True)
    issues = []
    for violation in drc["violations"]:
        if violation["type"] == "items_not_allowed":
            for item in violation["items"]:
                obj = geometry.items.get(item["uuid"])
                issues.append({"drc": violation, "geometry": {k: v for k, v in obj.items() if k not in ("shapes", "polys", "connected")} if obj else None})
    output = {"source_sha256": geometry.raw["sha256"], "drc": args.drc, "unconnected": result, "keepout_violations": issues}
    Path("analysis_refilled.json").write_text(json.dumps(output, indent=2) + "\n")
    Path("analysis_refilled.txt").write_text("\n".join(lines) + "\n")
    print("Open items by net:", dict(Counter(r["net"] for r in result)))
    print("Keepout entries by kind:", dict(Counter((r["geometry"] or {}).get("kind") for r in issues)))
    print("Unique keepout items:", len({i["geometry"]["uuid"] for i in issues if i["geometry"]}))


if __name__ == "__main__":
    main()
