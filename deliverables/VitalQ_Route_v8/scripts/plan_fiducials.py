import argparse
import json
from pathlib import Path
import shapely
from shapely.geometry import Point,box
from shapely.ops import unary_union,nearest_points
from route_geometry import Geometry,polyset

parser=argparse.ArgumentParser()
parser.add_argument("geometry")
args=parser.parse_args()
g=Geometry(args.geometry)
mechanical=json.loads(Path("mechanical_geometry.json").read_text())
footprints={f["ref"]:f for f in g.raw["footprints"]}
for fp in mechanical["footprints"]:
    if fp["position"]!=footprints[fp["ref"]]["position"]:
        raise ValueError("Mechanical envelope data is stale")
areas=[]
for item in g.raw["keepouts"]:
    areas.append(polyset(item["outline"]).buffer(1.1))
free={}
for layer in (0,2):
    copper=unary_union([shape for item,shape in g.solids[layer] if not (item["kind"]=="pads" and item["ref"].startswith("FID"))]+[shape for _,shape in g.zones[layer]])
    bodies=[box(*f["bbox"]).buffer(1.3) for f in mechanical["footprints"] if f["layer"]==layer and not f["ref"].startswith("FID")]
    free[layer]=g.outline.buffer(-1.3).difference(unary_union([copper.buffer(1.1)]+bodies+areas)).buffer(-0.01)
    print("Layer",layer,"eligible fiducial center area",free[layer].area,flush=True)
minx,miny,maxx,maxy=g.outline.bounds
choices={"FID1":(minx+1.3,miny+1.3),"FID2":(maxx-1.3,miny+1.3),"FID3":(maxx-1.3,maxy-1.3),"FID5":(minx+1.3,maxy-1.3),"FID4":(minx+1.3,miny+1.3),"FID6":(maxx-1.3,maxy-1.3)}
moves=[]
for ref,target in choices.items():
    fp=footprints[ref];layer=fp["layer"]
    if free[layer].is_empty:
        raise ValueError("No clear site for "+ref)
    p=nearest_points(Point(target),free[layer])[1]
    site=[round(p.x,6),round(p.y,6)]
    if not free[layer].buffer(0.000002).covers(Point(site)):
        raise ValueError("Rounded fiducial site leaves the legal region")
    moves.append({"ref":ref,"layer":layer,"before":fp["position"],"after":site,"translation_mm":Point(fp["position"]).distance(Point(site)),"pad_diameter_mm":1.0,"mask_aperture_diameter_mm":2.0,"copper_optical_clearance_radius_mm":1.1,"body_avoidance_margin_mm":1.3,"silk_reference_action":"Hide reference field to preserve the clear fiducial aperture and avoid new edge-silk artifacts; identity remains unchanged"})
    free[layer]=free[layer].difference(Point(site).buffer(5))
    print(ref,fp["position"],"->",site,"layer",layer,flush=True)
Path("plan_M2_fiducials.json").write_text(json.dumps({"geometry_sha256":g.raw["sha256"],"approval":"User explicitly approved FID1-FID6 relocation beyond 1mm with clear-site checks and logs","moves":moves},indent=2)+"\n")
