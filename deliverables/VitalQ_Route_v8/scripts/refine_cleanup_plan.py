import argparse
from collections import Counter
import json
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument("input",type=Path)
parser.add_argument("output",type=Path)
parser.add_argument("preserve_nets",nargs="+")
args=parser.parse_args()
plan=json.loads(args.input.read_text())
preserve=set(args.preserve_nets)
selected=[]
for item in plan["removals"]:
    if item["geometry"]["net"] in preserve:
        plan["deferred"].append(dict(item,preservation_reason="Previous cleanup candidate exposed an open/pad-connectivity regression on this net; preserve all its candidate copper"))
    else:
        selected.append(item)
plan["removals"]=selected
used=Counter(plan["prior_original_segment_removals"])
used.update(i["geometry"]["net"] for i in selected if i["geometry"]["kind"]=="track")
plan["cumulative_segment_removals_if_accepted"]=dict(used)
args.output.write_text(json.dumps(plan,indent=2)+"\n")
print("Selected",len(selected),"preserved",len(plan["deferred"]),"nets preserved",sorted(preserve))
