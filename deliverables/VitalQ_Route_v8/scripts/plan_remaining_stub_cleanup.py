import json
from collections import Counter
from pathlib import Path
from route_geometry import Geometry

g=Geometry("geometry_C2_refilled.json")
report=json.loads(Path("drc_C2_refilled.json").read_text())
used=Counter(json.loads(Path("cleanup_removal_ledger.json").read_text()))
plan={"geometry_sha256":g.raw["sha256"],"removals":[],"deferred":[],"prior_original_segment_removals":dict(used)}
comps,ids=g.components("+3V3")
selected=set()
for v in report["violations"]:
    if v["type"]!="track_dangling":
        continue
    for item in v["items"]:
        if item["uuid"] in ids:
            selected.add(ids[item["uuid"]])
for comp in selected:
    items=[obj for obj,_ in comps[comp]]
    pads=[o for o in items if o["kind"]=="pads"]
    tracks=[o for o in items if o["kind"]=="tracks"]
    if len(pads)!=1 or any(o["kind"] in ("vias","zones") for o in items) or used["+3V3"]+len(tracks)>10:
        plan["deferred"].append({"component":comp,"reason":"Not a single-pad dead branch within the local removal budget"})
        continue
    print("Dead +3V3 branch at",pads[0]["ref"],pads[0]["number"],"remove",len(tracks),"tracks; isolated pad retained")
    for track in tracks:
        data={"net":track["net"],"layer":track["layer"],"start":track["a"],"end":track["b"],"kind":"track","width":track["width"]}
        plan["removals"].append({"uuid":track["uuid"],"reason":"Dead tail left after removing a forbidden, nonconducting plane via; component has one retained pad and no other copper access","geometry":data})
    used["+3V3"]+=len(tracks)
plan["cumulative_segment_removals_if_accepted"]=dict(used)
Path("plan_C3_stubs.json").write_text(json.dumps(plan,indent=2)+"\n")
print("Selected",len(plan["removals"]),"track removals")
