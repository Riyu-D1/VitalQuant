import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import pcbnew

LAYERS = [pcbnew.F_Cu, pcbnew.B_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu, pcbnew.In4_Cu]


def xy(p):
    return [p.x / 1e6, p.y / 1e6]


def ring(chain):
    return [xy(chain.CPoint(i)) for i in range(chain.PointCount())]


def polygons(poly):
    return [{"outer": ring(poly.Outline(i)), "holes": [ring(poly.Hole(i, j)) for j in range(poly.HoleCount(i))]} for i in range(poly.OutlineCount())]


def uid(item):
    return item.m_Uuid.AsString()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("board", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    board.BuildConnectivity()
    conn = board.GetConnectivity()
    data = {"source": str(args.board), "sha256": hashlib.sha256(args.board.read_bytes()).hexdigest(),
            "date": datetime.now().astimezone().isoformat(), "layers": {l: board.GetLayerName(l) for l in LAYERS},
            "pads": [], "tracks": [], "vias": [], "zones": [], "keepouts": [], "footprints": []}
    for fp in board.GetFootprints():
        data["footprints"].append({"ref": fp.GetReference(), "position": xy(fp.GetPosition()), "rotation": fp.GetOrientationDegrees(), "layer": fp.GetLayer(), "locked": fp.IsLocked()})
        for p in fp.Pads():
            layers = [l for l in LAYERS if p.IsOnLayer(l)]
            data["pads"].append({"uuid": uid(p), "ref": fp.GetReference(), "number": p.GetNumber(), "net": p.GetNetname(),
                                 "xy": xy(p.GetPosition()), "size": xy(p.GetSize()), "rotation": p.GetOrientationDegrees(),
                                 "attribute": p.GetAttribute(), "drill": xy(p.GetDrillSize()), "layers": layers,
                                 "polys": {l: polygons(p.GetEffectivePolygon(l)) for l in layers},
                                 "connected": [uid(t) for t in conn.GetConnectedItems(p)]})
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            data["vias"].append({"uuid": uid(t), "net": t.GetNetname(), "xy": xy(t.GetPosition()),
                                 "diameter": t.GetWidth(pcbnew.F_Cu) / 1e6, "drill": t.GetDrillValue() / 1e6,
                                 "layers": [l for l in LAYERS if t.IsOnLayer(l)],
                                 "filled": t.GetFillingMode() == pcbnew.FILLING_MODE_FILLED, "capped": t.GetCappingMode() == pcbnew.CAPPING_MODE_CAPPED,
                                 "connected": [uid(c) for c in conn.GetConnectedItems(t)]})
        else:
            if t.Type() != pcbnew.PCB_TRACE_T:
                raise ValueError("Unsupported track geometry: " + str(t.Type()))
            data["tracks"].append({"uuid": uid(t), "net": t.GetNetname(), "layer": t.GetLayer(), "a": xy(t.GetStart()), "b": xy(t.GetEnd()), "width": t.GetWidth() / 1e6,
                                   "connected": [uid(c) for c in conn.GetConnectedItems(t)]})
    allzones = [(z, None) for z in board.Zones()] + [(z, fp.GetReference()) for fp in board.GetFootprints() for z in fp.Zones()]
    for z, parent in allzones:
        record = {"uuid": uid(z), "name": z.GetZoneName(), "parent": parent, "net": z.GetNetname(),
                  "layers": [l for l in LAYERS if z.IsOnLayer(l)], "outline": polygons(z.Outline())}
        if z.GetIsRuleArea():
            record.update(tracks=z.GetDoNotAllowTracks(), vias=z.GetDoNotAllowVias(), fills=z.GetDoNotAllowZoneFills())
            data["keepouts"].append(record)
        else:
            record.update(fill={l: polygons(z.GetFilledPolysList(l)) for l in record["layers"]},
                          thermal_gap=z.GetThermalReliefGap() / 1e6, thermal_spoke=z.GetThermalReliefSpokeWidth() / 1e6,
                          connection=z.GetPadConnection(), min_island_area=z.GetMinIslandArea() / 1e12,
                          island_removal=z.GetIslandRemovalMode())
            data["zones"].append(record)
    outline = pcbnew.SHAPE_POLY_SET()
    if not board.GetBoardPolygonOutlines(outline, False):
        raise ValueError("Board outline is invalid")
    data["outline"] = polygons(outline)
    policy = Path(__file__).resolve().parents[1] / "j8_pofv_policy.json"
    if policy.exists():
        data["j8_pofv_policy"] = json.loads(policy.read_text())
    args.output.write_text(json.dumps(data, separators=(",", ":")))
    print({k: len(data[k]) for k in ("pads", "tracks", "vias", "zones", "keepouts", "outline")})


if __name__ == "__main__":
    main()
