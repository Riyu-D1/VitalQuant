#!/usr/bin/env python3
"""Extract exact geometry from vitalq_v2.kicad_pcb using pcbnew.
Run with KiCad's bundled python:
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 extract_geometry.py
Outputs geometry.json (mm, board coordinates).
"""
import json
import re
import pcbnew

PCB = "/tmp/v22_board.kicad_pcb"
OUT = "geometry.json"

NM = 1e-6  # nm -> mm

def v2mm(p):
    return [round(p.x * NM, 5), round(p.y * NM, 5)]

board = pcbnew.LoadBoard(PCB)
bbox = board.GetBoardEdgesBoundingBox()

# ---- real stackup thickness from the .kicad_pcb setup section ----
def stackup_thickness(path):
    """Sum every stackup layer thickness (copper + dielectric + mask)."""
    txt = open(path).read()
    m = re.search(r'\(stackup(.*?)\(copper_finish', txt, re.S)
    if not m:
        return None
    # every (thickness N) inside the stackup block is a layer thickness
    total = sum(float(t) for t in re.findall(r'\(thickness\s+([0-9.]+)\)', m.group(1)))
    return round(total, 4)

stk = stackup_thickness(PCB)

data = {
    "board_bbox_mm": {
        "x": round(bbox.GetX() * NM, 5),
        "y": round(bbox.GetY() * NM, 5),
        "w": round(bbox.GetWidth() * NM, 5),
        "h": round(bbox.GetHeight() * NM, 5),
    },
    "thickness_mm": stk or 1.6,
    "thickness_nominal_mm": 1.6,
    "thickness_max_mm": 1.6,
    "outline": [],          # Edge.Cuts segments
    "holes": [],            # drilled holes (NPTH + PTH)
    "footprints": [],
    "zones": [],
    "keepouts": [],
}

# ---- board outline (Edge.Cuts drawings) ----
for d in board.Drawings():
    if d.GetLayerName() != "Edge.Cuts":
        continue
    sh = d.GetShapeStr() if hasattr(d, "GetShapeStr") else str(d.GetShape())
    seg = {"type": d.ShowShape(), "layer": "Edge.Cuts"}
    try:
        seg["start"] = v2mm(d.GetStart())
        seg["end"] = v2mm(d.GetEnd())
    except Exception:
        pass
    try:
        seg["center"] = v2mm(d.GetCenter())
        seg["radius"] = round(d.GetRadius() * NM, 5)
    except Exception:
        pass
    # bezier/arc control points
    for attr, key in (("GetBezierC1", "c1"), ("GetBezierC2", "c2"), ("GetArcMid", "mid")):
        try:
            seg[key] = v2mm(getattr(d, attr)())
        except Exception:
            pass
    data["outline"].append(seg)

# ---- footprints ----
for fp in board.Footprints():
    ref = fp.GetReference()
    val = fp.GetValue()
    pos = v2mm(fp.GetPosition())
    rot = fp.GetOrientation().AsDegrees()
    side = "F" if fp.GetLayer() == pcbnew.F_Cu else "B"
    # courtyard bbox in board coords
    try:
        crty = fp.GetCourtyard(pcbnew.F_CrtYd if side == "F" else pcbnew.B_CrtYd)
        poly = crty.Outline(0) if crty.OutlineCount() else None
        crtyd_pts = [v2mm(poly.CPoint(i)) for i in range(poly.PointCount())] if poly else []
    except Exception:
        crtyd_pts = []
    # fp bounding box including courtyard
    fb = fp.GetBoundingBox(False, False)
    entry = {
        "ref": ref, "value": val,
        "fp": str(fp.GetFPID().GetLibItemName()),
        "pos": pos, "rot_deg": round(rot, 3), "side": side,
        "bbox": {"x": round(fb.GetX() * NM, 5), "y": round(fb.GetY() * NM, 5),
                 "w": round(fb.GetWidth() * NM, 5), "h": round(fb.GetHeight() * NM, 5)},
        "courtyard": crtyd_pts,
        "pads": [],
        "models": [],
        "through_hole": fp.HasThroughHolePads() if hasattr(fp, "HasThroughHolePads") else None,
        "excluded_from_bom": getattr(fp, "IsExcludedFromBOM", lambda: False)(),
    }
    for pad in fp.Pads():
        p = {"num": pad.GetNumber(),
             "name": pad.GetName() or pad.GetNumber(),
             "pos": v2mm(pad.GetPosition()),
             "size": [round(pad.GetSize().x * NM, 5), round(pad.GetSize().y * NM, 5)],
             "shape": str(pad.GetShape()),
             "type": str(pad.GetAttribute()),
             "rot_deg": round(pad.GetOrientation().AsDegrees(), 3),
             "on_front": pad.IsOnLayer(pcbnew.F_Cu),
             "on_back": pad.IsOnLayer(pcbnew.B_Cu)}
        if pad.GetAttribute() in (pcbnew.PAD_ATTRIB_NPTH, pcbnew.PAD_ATTRIB_PTH):
            p["drill"] = [round(pad.GetDrillSize().x * NM, 5),
                          round(pad.GetDrillSize().y * NM, 5)]
            p["drill_shape"] = str(pad.GetDrillShape())
            data["holes"].append({"ref": ref, "pad": p["num"],
                                  "pos": p["pos"], "drill": p["drill"],
                                  "shape": p["drill_shape"],
                                  "npth": pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH})
        entry["pads"].append(p)
    # 3d models
    try:
        for m in fp.Models():
            entry["models"].append({"file": m.m_Filename,
                                    "offset": [round(m.m_Offset.x, 4), round(m.m_Offset.y, 4), round(m.m_Offset.z, 4)],
                                    "scale": [m.m_Scale.x, m.m_Scale.y, m.m_Scale.z],
                                    "rot": [m.m_Rotation.x, m.m_Rotation.y, m.m_Rotation.z]})
    except Exception as e:
        entry["models_err"] = str(e)
    data["footprints"].append(entry)

# ---- zones / keepouts ----
for i in range(board.GetAreaCount()):
    z = board.GetArea(i)
    zn = {"net": z.GetNetname(), "layers": [z.GetLayerName(l) if False else "" for l in []],
          "is_keepout": z.GetIsRuleArea(), "name": z.GetZoneName() if hasattr(z, "GetZoneName") else ""}
    try:
        zn["rule_area_type"] = str(z.GetAreaType()) if z.GetIsRuleArea() else None
    except Exception:
        pass
    out = z.Outline()
    polys = []
    for oi in range(out.OutlineCount()):
        o = out.Outline(oi)
        polys.append([v2mm(o.CPoint(j)) for j in range(o.PointCount())])
    zn["polygons"] = polys
    zn["on_F"] = z.IsOnLayer(pcbnew.F_Cu)
    zn["on_B"] = z.IsOnLayer(pcbnew.B_Cu)
    zn["on_EdgeCuts"] = z.IsOnLayer(pcbnew.Edge_Cuts)
    if z.GetIsRuleArea():
        data["keepouts"].append(zn)
    else:
        data["zones"].append(zn)

with open(OUT, "w") as f:
    json.dump(data, f, indent=1)

print("bbox", data["board_bbox_mm"])
print("footprints", len(data["footprints"]))
print("holes", len(data["holes"]))
print("outline segs", len(data["outline"]))
print("keepouts", len(data["keepouts"]))
