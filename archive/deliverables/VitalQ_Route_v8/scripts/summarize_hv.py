import json
from collections import defaultdict
from pathlib import Path

analysis=json.loads(Path("analysis_refilled.json").read_text())
groups=defaultdict(dict)
for violation in analysis["keepout_violations"]:
    obj=violation["geometry"]
    if obj:
        groups[obj["net"]][obj["uuid"]]=obj
result={}
for net,items in sorted(groups.items()):
    values=list(items.values())
    tracks=[i for i in values if i["kind"]=="tracks"]
    vias=[i for i in values if i["kind"]=="vias"]
    result[net]={"track_count":len(tracks),"via_count":len(vias),"items":values}
    print(net,"tracks",len(tracks),"vias",len(vias),"OVER LOCAL LIMIT" if len(tracks)>10 else "")
Path("hv_worklist.json").write_text(json.dumps(result,indent=2)+"\n")
geometry=json.loads(Path("geometry_refilled.json").read_text())
for fp in geometry["footprints"]:
    if fp["ref"].startswith("FID") or fp["ref"] in ("R122","J8","H1","H2"):
        print("mechanical",fp)
