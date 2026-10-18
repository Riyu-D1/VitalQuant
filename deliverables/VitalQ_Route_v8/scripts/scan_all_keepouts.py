import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
from route_geometry import Geometry, polyset

parser=argparse.ArgumentParser()
parser.add_argument("geometry")
parser.add_argument("output")
args=parser.parse_args()
g=Geometry(args.geometry)
violations={}
for area in g.raw["keepouts"]:
    polygon=polyset(area["outline"])
    for layer in area["layers"]:
        for index in g.trees[layer].query(polygon,predicate="intersects"):
            item,shape=g.solids[layer][index]
            forbidden=(item["kind"]=="tracks" and area["tracks"]) or (item["kind"]=="vias" and area["vias"])
            if forbidden:
                key=(item["uuid"],area["uuid"])
                violations[key]={"uuid":item["uuid"],"net":item["net"],"kind":item["kind"],"layer":item.get("layer"),"keepout":area["uuid"],"keepout_name":area["name"],"parent":area["parent"],"geometry":{k:v for k,v in item.items() if k not in ("shapes","polys","connected")}}
by_net=defaultdict(dict)
for record in violations.values():
    by_net[record["net"]][record["uuid"]]=record
summary={n:dict(Counter(v["kind"] for v in records.values())) for n,records in by_net.items()}
result={"source_sha256":g.raw["sha256"],"method":"Full geometry intersection scan; no KiCad per-error-code reporting cap; polygon approximation is not a substitute for final KiCad DRC", "item_keepout_pairs":len(violations),"unique_items":len({v["uuid"] for v in violations.values()}),"by_net":summary,"violations":list(violations.values())}
Path(args.output).write_text(json.dumps(result,indent=2)+"\n")
print("Uncapped keepout item/area pairs",result["item_keepout_pairs"],"unique items",result["unique_items"])
for net,counts in sorted(summary.items()):
    print(net,counts,"EXCEEDS 10 ORIGINAL SEGMENTS" if counts.get("tracks",0)>10 else "")
