from collections import Counter
import json
from pathlib import Path
from shapely.geometry import Point,Polygon
from shapely.strtree import STRtree
from route_geometry import Geometry,LAYERS

g=Geometry("geometry_final_refilled.json")
restart=Geometry("geometry_refilled.json")
outside=[];fids=[]
for item in g.items.values():
    if item["kind"]!="pads":
        continue
    for layer,shape in item["shapes"].items():
        if not g.outline.buffer(0.000002).covers(shape):
            outside.append({"uuid":item["uuid"],"ref":item["ref"],"pad":item["number"],"layer":layer,"xy":item["xy"]})
    if item["ref"].startswith("FID"):
        layer=0 if 0 in item["shapes"] else 2
        center=Point(item["xy"])
        aperture=center.buffer(1)
        nearest=min((center.distance(s) for other,s in g.solids[layer] if other["uuid"]!=item["uuid"]),default=999)
        fids.append({"ref":item["ref"],"layer":layer,"xy":item["xy"],"copper_inside_board":g.outline.covers(item["shapes"][layer]),"mask_aperture_inside_board":g.outline.covers(aperture),"nearest_other_copper_from_center_mm":nearest})
if len(fids)!=6 or not all(f["copper_inside_board"] and f["mask_aperture_inside_board"] and f["nearest_other_copper_from_center_mm"]>=1.09 for f in fids):
    raise ValueError("Fiducial geometry verification failed")
hole_gaps=[]
for i,(a,sa) in enumerate(g.drills):
    for b,sb in g.drills[i+1:]:
        required=0.2 if a["kind"]==b["kind"]=="vias" else 0.45
        if sa.distance(sb)<required-0.000002:
            hole_gaps.append({"a":a["uuid"],"b":b["uuid"],"a_kind":a["kind"],"b_kind":b["kind"],"actual_mm":sa.distance(sb),"screening_requirement_mm":required})
zone_trees={l:STRtree([s for _,s in g.zones[l]]) for l in LAYERS}
small_via_dfm=[]
for via in g.raw["vias"]:
    if via["diameter"]>=0.399:
        continue
    center=Point(via["xy"]);radius=via["drill"]/2
    for layer in LAYERS:
        for index in g.trees[layer].query(center.buffer(radius+0.2)):
            other,shape=g.solids[layer][index]
            if other["net"]==via["net"] or (layer in (0,2) and other["kind"]!="tracks"):
                continue
            gap=center.distance(shape)-radius
            if gap<0.2-0.000002:
                small_via_dfm.append({"via":via["uuid"],"net":via["net"],"xy":via["xy"],"layer":layer,"other":other["uuid"],"other_kind":other["kind"],"gap_mm":gap,"required_mm":0.2,"existed_at_restart":via["uuid"] in restart.items})
        if layer not in (0,2):
            for index in zone_trees[layer].query(center.buffer(radius+0.2)):
                other,shape=g.zones[layer][index]
                gap=center.distance(shape)-radius
                if other["net"]!=via["net"] and gap<0.2-0.000002:
                    small_via_dfm.append({"via":via["uuid"],"net":via["net"],"xy":via["xy"],"layer":layer,"other":other["uuid"],"other_kind":"zone","gap_mm":gap,"required_mm":0.2,"existed_at_restart":via["uuid"] in restart.items})
slots=[]
for polygon in g.raw["outline"]:
    for ring in polygon["holes"]:
        shape=Polygon(ring)
        coords=list(shape.minimum_rotated_rectangle.exterior.coords)
        sides=[Point(coords[i]).distance(Point(coords[i+1])) for i in range(4)]
        slots.append({"bounds":list(shape.bounds),"minimum_rectangle_width_mm":min(sides),"area_mm2":shape.area})
result={"source_sha256":g.raw["sha256"],"outline_valid":g.outline.is_valid,"board_bounds_mm":list(g.outline.bounds),"board_size_mm":[g.outline.bounds[2]-g.outline.bounds[0],g.outline.bounds[3]-g.outline.bounds[1]],"fiducials":fids,"pads_outside_outline":outside,"internal_cutouts":slots,"hole_spacing_screening":hole_gaps,"small_via_hole_to_copper_screening":small_via_dfm,"screening_note":"Additional conservative JLC DFM screen, separate from native KiCad DRC. Via hole-to-track and inner hole-to-foreign-copper use 0.20mm. Mixed/component hole pairs use 0.45mm conservatively. No rules weakened and no DFM findings waived. This is not a complete fabrication approval."}
Path("geometry_audit_final.json").write_text(json.dumps(result,indent=2)+"\n")
print("Outline",result["board_size_mm"],"fiducials verified",len(fids),"outside pad/layer pairs",len(outside),"hole-spacing flags",len(hole_gaps),"small-via DFM flags",len(small_via_dfm),"affected vias",len({v["via"] for v in small_via_dfm}),flush=True)
