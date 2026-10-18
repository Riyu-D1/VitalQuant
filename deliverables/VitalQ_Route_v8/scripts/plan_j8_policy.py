import argparse
from datetime import datetime
import copy
import json
from pathlib import Path
import shapely
from shapely.geometry import Point
from shapely.ops import unary_union
from route_geometry import Geometry, polyset

parser=argparse.ArgumentParser()
parser.add_argument("geometry")
args=parser.parse_args()
raw=json.loads(Path(args.geometry).read_text())
audit=json.loads(Path("audit_resume.json").read_text())
original=audit["changes_from_raw"]["footprint_changes"]["J8"]["zones"]["before"][0]
original_polys=[{"outer":ring,"holes":[]} for ring in original["outline"]]
for area in raw["keepouts"]:
    if area["parent"]=="J8":
        area["outline"]=original_polys
base=Geometry(raw)
area=polyset(original_polys)
balls=[]
for pad in raw["pads"]:
    if pad["ref"]!="U6" or not area.intersects(Point(pad["xy"]).buffer(0.15)):
        continue
    allowed=base.obstacles(pad["net"],2,margin=0)
    region=next((p for p in shapely.get_parts(allowed) if p.covers(Point(pad["xy"]))),None)
    ordinary_via_sites=base.via_allowed(pad["net"])
    alternative=region is not None and not region.intersection(ordinary_via_sites).is_empty
    existing_access=[]
    if region is not None:
        for via in raw["vias"]:
            if via["net"]==pad["net"] and not area.intersects(Point(via["xy"]).buffer(via["diameter"]/2)) and region.covers(Point(via["xy"])):
                existing_access.append(via["uuid"])
    eligible=region is not None and not alternative and not existing_access and not pad["net"].startswith("unconnected-")
    balls.append({"ball":pad["number"],"net":pad["net"],"xy":pad["xy"],"pad_uuid":pad["uuid"],"pad_polygons":pad["polys"]["2"],"eligible":eligible,
                  "surface_component_area_mm2":None if region is None else region.area,"ordinary_through_via_reachable":alternative,"existing_legal_same_net_vias_reachable":existing_access,
                  "required_process":"JLC resin-filled and copper-capped planar via-in-pad (POFV); no open or tented-only via"})
    print(pad["number"],pad["net"],"POFV exception eligible",eligible,"ordinary escape",alternative,"existing accesses",len(existing_access),flush=True)
policy={"date":datetime.now().astimezone().isoformat(),"approval":"User explicitly permits only filled/copper-capped planar U6 ball via-in-pad inside J8 when no other escape exists; all other original keepout geometry must be restored.","source_sha256":raw["sha256"],"original_outline":original_polys,"balls":balls}
Path("j8_pofv_policy.json").write_text(json.dumps(policy,indent=2)+"\n")
