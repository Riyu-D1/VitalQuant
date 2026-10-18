import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import pcbnew

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def xy(point):
    return [point.x / 1e6, point.y / 1e6]


def contours(poly):
    return [[[p.x / 1e6, p.y / 1e6] for p in
             [poly.Outline(i).CPoint(j) for j in range(poly.Outline(i).PointCount())]]
            for i in range(poly.OutlineCount())]


def zone_data(zone):
    return {"name": zone.GetZoneName(), "net": zone.GetNetname(),
            "layers": list(zone.GetLayerSet().Seq()),
            "rule_area": zone.GetIsRuleArea(), "tracks_forbidden": zone.GetDoNotAllowTracks(),
            "vias_forbidden": zone.GetDoNotAllowVias(), "outline": contours(zone.Outline())}


def track_data(track):
    record = {"net": track.GetNetname(), "layer": track.GetLayer(),
              "start": xy(track.GetStart()), "end": xy(track.GetEnd())}
    if track.Type() == pcbnew.PCB_VIA_T:
        record.update(kind="via", diameter=track.GetWidth(pcbnew.F_Cu) / 1e6,
                      drill=track.GetDrillValue() / 1e6)
    else:
        record.update(kind="track", width=track.GetWidth() / 1e6)
    return record


def pad_data(pad):
    return {"number": pad.GetNumber(), "net": pad.GetNetname(),
            "position": xy(pad.GetPosition()), "size": xy(pad.GetSize()),
            "drill": xy(pad.GetDrillSize()), "rotation": pad.GetOrientationDegrees(),
            "shape": pad.GetShape(), "layers": list(pad.GetLayerSet().Seq())}


def snapshot(board):
    footprints = {}
    for fp in board.GetFootprints():
        footprints[fp.GetReference()] = {
            "position": xy(fp.GetPosition()), "rotation": fp.GetOrientationDegrees(),
            "layer": fp.GetLayer(), "locked": fp.IsLocked(), "value": fp.GetValue(),
            "pads": sorted([pad_data(p) for p in fp.Pads()], key=lambda p: json.dumps(p, sort_keys=True)),
            "zones": [zone_data(z) for z in fp.Zones()]}
    tracks = [track_data(t) for t in board.GetTracks()]
    return {"footprints": footprints, "tracks": tracks,
            "zones": [zone_data(z) for z in board.Zones()],
            "edges": [{"shape": d.GetShape(), "start": xy(d.GetStart()), "end": xy(d.GetEnd())}
                      for d in board.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts],
            "counts": {"footprints": len(footprints), "pads": sum(len(f["pads"]) for f in footprints.values()),
                       "tracks": sum(t["kind"] == "track" for t in tracks),
                       "vias": sum(t["kind"] == "via" for t in tracks)}}


def difference(a, b):
    changes = {}
    for ref in sorted(a["footprints"].keys() | b["footprints"].keys()):
        old, new = a["footprints"].get(ref), b["footprints"].get(ref)
        if old != new:
            changes[ref] = {key: {"before": old.get(key), "after": new.get(key)}
                            for key in old.keys() | new.keys() if old.get(key) != new.get(key)} if old and new else {"before": old, "after": new}
    old = Counter(json.dumps(t, sort_keys=True) for t in a["tracks"])
    new = Counter(json.dumps(t, sort_keys=True) for t in b["tracks"])
    return {"footprint_changes": changes, "removed_copper": [json.loads(t) for t in (old - new).elements()],
            "added_copper": [json.loads(t) for t in (new - old).elements()],
            "edges_unchanged": a["edges"] == b["edges"],
            "zones_before": a["zones"], "zones_after": b["zones"]}


def report_data(path):
    data = json.loads(path.read_text())
    unconnected = []
    for violation in data.get("unconnected_items", []):
        items = violation["items"]
        nets = sorted(set(net for item in items for net in re.findall(r"\[([^]]+)\]", item["description"])))
        unconnected.append({"nets": nets, "items": items,
                            "reported_position_distance_mm": math.dist(list(items[0]["pos"].values()), list(items[1]["pos"].values())) if len(items) == 2 else None})
    return {"date": data.get("date"), "violations": len(data["violations"]),
            "by_type": dict(Counter(v["type"] for v in data["violations"])),
            "unconnected_count": len(unconnected), "unconnected": unconnected,
            "schematic_parity": data.get("schematic_parity", [])}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="audit_checkpoint.json")
    args = parser.parse_args()
    raw = pcbnew.LoadBoard(str(ROOT / "quilter_raw/vitalq_v2.kicad_pcb"))
    current = pcbnew.LoadBoard(str(ROOT / "vitalq_v2.kicad_pcb"))
    a, b = snapshot(raw), snapshot(current)
    result = {"date": datetime.now().astimezone().isoformat(), "kicad": pcbnew.Version(),
              "sha256": {str(p.relative_to(ROOT)): digest(p) for p in
                         [ROOT / "quilter_raw/vitalq_v2.kicad_pcb", ROOT / "vitalq_v2.kicad_pcb", ROOT / "vitalq_v2.kicad_pro", ROOT / "vitalq_v2.kicad_sch"]},
              "baseline_counts": a["counts"], "current_counts": b["counts"],
              "changes_from_raw": difference(a, b),
              "reports": {p.name: report_data(p) for p in sorted(ROOT.glob("drc_*.json"))}}
    vias = [v for v in b["tracks"] if v["kind"] == "via"]
    pairs = []
    for i, v in enumerate(vias):
        for w in vias[i + 1:]:
            gap = math.dist(v["start"], w["start"]) - (v["drill"] + w["drill"]) / 2
            if gap < 0.2 - 1e-6:
                pairs.append({"a": v, "b": w, "hole_edge_gap_mm": gap})
    result["via_hole_spacing_below_0_2mm"] = pairs
    outline = pcbnew.SHAPE_POLY_SET()
    result["board_outline_valid"] = current.GetBoardPolygonOutlines(outline, False)
    outside = []
    for fp in current.GetFootprints():
        for pad in fp.Pads():
            poly = pad.GetEffectivePolygon(pad.GetLayer())
            points = [poly.Outline(i).CPoint(j) for i in range(poly.OutlineCount()) for j in range(poly.Outline(i).PointCount())]
            bad = [xy(p) for p in points if not outline.Contains(p)]
            if bad:
                outside.append({"ref": fp.GetReference(), "pad": pad_data(pad), "outside_vertices": bad})
    result["pad_copper_vertices_outside_outline"] = outside
    (ROOT / args.output).write_text(json.dumps(result, indent=2) + "\n")
    print("Counts:", result["baseline_counts"], "->", result["current_counts"])
    print("Footprint changes:", list(result["changes_from_raw"]["footprint_changes"]))
    print("Copper removed/added:", len(result["changes_from_raw"]["removed_copper"]), len(result["changes_from_raw"]["added_copper"]))
    print("Via-hole pairs below 0.2mm:", len(pairs))
    print("Outline valid:", result["board_outline_valid"], "Pads with copper outside:", [(p["ref"], p["pad"]["number"]) for p in outside])
    for name, data in result["reports"].items():
        print(name, "unconnected", data["unconnected_count"], "violations", data["violations"], "parity", len(data["schematic_parity"]), data["by_type"])


if __name__ == "__main__":
    main()
