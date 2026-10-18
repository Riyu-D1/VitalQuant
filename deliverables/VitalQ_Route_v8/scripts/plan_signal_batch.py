import argparse
from datetime import datetime
import json
from pathlib import Path
import uuid
from route_geometry import Geometry, LAYERS
from multilayer_route import route_components

ROOT=Path(__file__).resolve().parents[1]
BLOCKED={"BIOZ_FP","BIOZ_SP","BIOZ_FN","VREF_2V5","VBIAS0","DE0","VZERO0","SPI_MOSI"}


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("geometry")
    parser.add_argument("output")
    parser.add_argument("tag")
    parser.add_argument("nets",nargs="+")
    args=parser.parse_args()
    progress=(ROOT/"PROGRESS.md").read_text()
    if not progress or "Attempt ledger" not in progress:
        raise ValueError("Read and reconcile the attempt ledger first")
    ledger_path=ROOT/"signal_attempts.json"
    ledger=json.loads(ledger_path.read_text()) if ledger_path.exists() else {n:{"attempts":[{"approach":"historical exhausted searches","status":"BLOCKED"}]*3,"status":"BLOCKED"} for n in BLOCKED}
    ledger.setdefault("CP_RX",{"attempts":[{"approach":"historical F-only pad escape","status":"failed"}]})
    g=Geometry(args.geometry)
    source_hash=g.raw["sha256"]
    result={"geometry_sha256":source_hash,"routes":[],"failed":[],"tag":args.tag}
    approach="Exact F/B/L3 route with legal through vias, no rip-up, 5mm endpoint corridor"
    for net in args.nets:
        entry=ledger.setdefault(net,{"attempts":[]})
        if len(entry["attempts"])>=3 or any(a["approach"]==approach for a in entry["attempts"]):
            result["failed"].append({"net":net,"reason":"Attempt budget exhausted or this approach already attempted"})
            continue
        attempt={"date":datetime.now().astimezone().isoformat(),"approach":approach,"status":"in_progress","batch":args.tag}
        entry["attempts"].append(attempt)
        ledger_path.write_text(json.dumps(ledger,indent=2)+"\n")
        with (ROOT/"PROGRESS.md").open("a") as log:
            log.write(f"\n- {attempt['date']} {args.tag} planning: **{net}**, attempt {len(entry['attempts'])}/3: {approach}.\n")
        comps,_=g.components(net)
        viable=[items for items in comps.values() if any(o["kind"]=="pads" for o,_ in items)]
        if len(viable)<2:
            attempt.update(status="not_routed",reason="Fewer than two pad-bearing components; requires explicit dangling-copper analysis")
            result["failed"].append({"net":net,"reason":attempt["reason"]})
        else:
            viable.sort(key=len)
            route,error=route_components(g,net,viable[0],viable[-1])
            if error:
                attempt.update(status="failed",reason=error)
                result["failed"].append({"net":net,"reason":error})
            else:
                route["attempt"]=len(entry["attempts"])
                result["routes"].append(route)
                for track in route["tracks"]:
                    g.raw["tracks"].append(dict(track,uuid="planned-"+str(uuid.uuid4()),connected=[]))
                for via in route["vias"]:
                    g.raw["vias"].append(dict(via,uuid="planned-"+str(uuid.uuid4()),layers=list(LAYERS),connected=[]))
                g=Geometry(g.raw)
                attempt.update(status="candidate",track_count=len(route["tracks"]),via_count=len(route["vias"]))
        if len(entry["attempts"])>=3 and attempt["status"]!="candidate":
            entry["status"]="BLOCKED"
        ledger_path.write_text(json.dumps(ledger,indent=2)+"\n")
        with (ROOT/"PROGRESS.md").open("a") as log:
            log.write(f"- {args.tag} {net} geometric result: {json.dumps(attempt)}. Candidate is not an accepted connection until refilled DRC passes.\n")
        print(net,attempt,flush=True)
        (ROOT/args.output).write_text(json.dumps(result,indent=2)+"\n")
    (ROOT/args.output).write_text(json.dumps(result,indent=2)+"\n")


if __name__=="__main__":
    main()
