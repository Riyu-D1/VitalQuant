import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import re
import shutil
import subprocess
import pcbnew
from checkpoint import checkpoint,ROOT,CLI
from audit_checkpoint import snapshot,track_data
from apply_plane_batch import counts,signature,COSMETIC,sha,point
from j8_pofv import apply_j8_policy,footprint_invariants


def open_counts(report):
    out=Counter()
    for v in report["unconnected_items"]:
        net=next((m.group(1) for i in v["items"] if (m:=re.search(r"\[([^]]+)\]",i["description"]))),"unknown")
        out[net]+=1
    return out


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("plan",type=Path)
    parser.add_argument("baseline",type=Path)
    parser.add_argument("tag")
    parser.add_argument("--accept",action="store_true")
    args=parser.parse_args()
    plan=json.loads(args.plan.read_text())
    current=ROOT/"vitalq_v2.kicad_pcb"
    current_hash=sha(current)
    if current_hash!=plan["geometry_sha256"]:
        raise ValueError("Working board changed after planning")
    if not plan["routes"]:
        raise ValueError("No successful geometric routes; no physical batch created")
    rules=json.loads((ROOT/"vitalq_v2.kicad_pro").read_text())["board"]["design_settings"]["rules"]
    if any(route["vias"] for route in plan["routes"]) and rules["min_hole_clearance"]<0.2:
        raise ValueError("JLC hole-to-copper clearance needs an explicit >=0.20mm manufacturing rule before new via batches; current project rules are insufficient")
    backup=checkpoint("pre_"+args.tag)
    folder=checkpoint("candidate_"+args.tag)
    path=folder/"vitalq_v2.kicad_pcb"
    board=pcbnew.LoadBoard(str(path))
    before=snapshot(board)
    old_tracks={t.m_Uuid.AsString():track_data(t) for t in board.GetTracks()}
    nets={n.GetNetname():n.GetNetCode() for n in board.GetNetInfo().NetsByName().values()}
    for route in plan["routes"]:
        for spec in route["tracks"]:
            track=pcbnew.PCB_TRACK(board)
            track.SetStart(point(spec["a"]));track.SetEnd(point(spec["b"]))
            track.SetLayer(spec["layer"]);track.SetWidth(round(spec["width"]*1e6));track.SetNetCode(nets[spec["net"]])
            board.Add(track)
        for spec in route["vias"]:
            via=pcbnew.PCB_VIA(board)
            via.SetPosition(point(spec["xy"]));via.SetWidth(round(spec["diameter"]*1e6));via.SetDrill(round(spec["drill"]*1e6))
            via.SetViaType(pcbnew.VIATYPE_THROUGH);via.SetLayerPair(pcbnew.F_Cu,pcbnew.B_Cu);via.SetNetCode(nets[spec["net"]])
            via.SetFillingMode(pcbnew.FILLING_MODE_FILLED);via.SetCappingMode(pcbnew.CAPPING_MODE_CAPPED)
            board.Add(via)
    exceptions=apply_j8_policy(board)
    board.Save(str(path))
    report_path=folder/"drc.json"
    subprocess.run([CLI,"pcb","drc","--refill-zones","--save-board","--all-track-errors","--schematic-parity","--format","json","-o",str(report_path),str(path)],check=True)
    final=pcbnew.LoadBoard(str(path))
    after=snapshot(final)
    if footprint_invariants(before)!=footprint_invariants(after) or before["edges"]!=after["edges"] or before["zones"]!=after["zones"]:
        raise ValueError("Protected invariant changed")
    if apply_j8_policy(final,validate_only=True)!=exceptions:
        raise ValueError("J8 POFV exception failed saved-board validation")
    final_tracks={t.m_Uuid.AsString():track_data(t) for t in final.GetTracks()}
    if any(final_tracks.get(uid)!=data for uid,data in old_tracks.items()):
        raise ValueError("Existing copper unexpectedly changed")
    baseline=json.loads(args.baseline.read_text());result=json.loads(report_path.read_text())
    old=Counter(signature(v) for v in baseline["violations"] if v["type"] not in COSMETIC)
    new_faults=[];uncovered_old=[]
    for violation in result["violations"]:
        if violation["type"] in COSMETIC:
            continue
        key=signature(violation)
        if old[key]:
            old[key]-=1
        elif violation["type"]=="items_not_allowed" and all(i["uuid"] in old_tracks and i["uuid"] not in {e["uuid"] for e in exceptions} and old_tracks[i["uuid"]]==final_tracks.get(i["uuid"]) for i in violation["items"]):
            uncovered_old.append(violation)
        else:
            new_faults.append(violation)
    before_opens,after_opens=open_counts(baseline),open_counts(result)
    regressions={net:count-before_opens[net] for net,count in after_opens.items() if count>before_opens[net]}
    bc,ac=counts(baseline),counts(result)
    parity=lambda r:Counter((v["type"],v["description"]) for v in r["schematic_parity"])
    acceptable=ac["unconnected"]<bc["unconnected"] and not new_faults and not regressions and parity(baseline)==parity(result)
    info={"date":datetime.now().astimezone().isoformat(),"tag":args.tag,"backup":str(backup.relative_to(ROOT)),"candidate":str(folder.relative_to(ROOT)),"before":bc,"after":ac,"new_real_checks":new_faults,"uncovered_unchanged_keepout_checks":uncovered_old,"open_regressions":regressions,"eligible_for_acceptance":acceptable,"accepted":False,"routes":plan["routes"]}
    if acceptable and args.accept:
        if sha(current)!=current_hash:
            raise ValueError("Working board changed during DRC")
        shutil.copy2(path,current);shutil.copy2(report_path,ROOT/("drc_"+args.tag+"_refilled.json"))
        info["accepted"]=True
        (ROOT/"j8_permitted_vias.json").write_text(json.dumps(exceptions,indent=2)+"\n")
    info["j8_exceptions"]=exceptions
    (ROOT/("batch_"+args.tag+".json")).write_text(json.dumps(info,indent=2)+"\n")
    ledger_path=ROOT/"signal_attempts.json"
    ledger=json.loads(ledger_path.read_text())
    for route in plan["routes"]:
        entry=ledger[route["net"]]
        attempt=entry["attempts"][route["attempt"]-1]
        attempt["status"]="accepted" if info["accepted"] else "failed_DRC"
        if not info["accepted"] and len(entry["attempts"])>=3:
            entry["status"]="BLOCKED"
    ledger_path.write_text(json.dumps(ledger,indent=2)+"\n")
    with (ROOT/"PROGRESS.md").open("a") as log:
        log.write(f"\n- {info['date']} **{args.tag} {'ACCEPTED' if info['accepted'] else 'REJECTED'}**, {', '.join(r['net'] for r in plan['routes'])}: {bc['unconnected']} → {ac['unconnected']} refilled opens; {bc['real']} → {ac['real']} reported real violations (keepout count capped); {ac['total']} total; new real faults {len(new_faults)}; open regressions {regressions}. No original copper removed, no nudges. Detailed paths/vias and checkpoint: `batch_{args.tag}.json`.\n")
    print(json.dumps({k:v for k,v in info.items() if k!="routes"},indent=2),flush=True)


if __name__=="__main__":
    main()
