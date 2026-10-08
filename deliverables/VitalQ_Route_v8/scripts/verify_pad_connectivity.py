import argparse
import json
from pathlib import Path
from route_geometry import Geometry

parser=argparse.ArgumentParser()
parser.add_argument("before")
parser.add_argument("after")
parser.add_argument("output")
args=parser.parse_args()
a,b=Geometry(args.before),Geometry(args.after)
oldpads={(p["uuid"],p["net"]) for p in a.raw["pads"]}
newpads={(p["uuid"],p["net"]) for p in b.raw["pads"]}
if oldpads!=newpads:
    raise ValueError("Pad/net identities changed")
failures=[];checked=0
for net in sorted({p["net"] for p in a.raw["pads"]}):
    before,_=a.components(net)
    _,after_ids=b.components(net)
    for entries in before.values():
        pads=[obj["uuid"] for obj,_ in entries if obj["kind"]=="pads"]
        if not pads:
            continue
        checked+=1
        roots={after_ids.get(uid) for uid in pads}
        if None in roots or len(roots)>1:
            failures.append({"net":net,"pads":pads,"after_components":list(roots)})
result={"before_sha256":a.raw["sha256"],"after_sha256":b.raw["sha256"],"pad_identities_unchanged":True,"pad_components_checked":checked,"regressions":failures,"passed":not failures}
Path(args.output).write_text(json.dumps(result,indent=2)+"\n")
print("Pad connectivity components checked",checked,"regressions",len(failures),flush=True)
if failures:
    raise SystemExit(1)
