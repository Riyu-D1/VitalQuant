import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import pcbnew
from checkpoint import checkpoint, ROOT, CLI
from audit_checkpoint import snapshot

COSMETIC = {"lib_footprint_issues", "lib_footprint_mismatch", "silk_edge_clearance", "silk_over_copper", "silk_overlap", "text_height", "text_thickness"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def counts(report):
    return {"unconnected":len(report["unconnected_items"]), "real":sum(v["type"] not in COSMETIC for v in report["violations"]), "total":len(report["violations"]), "types":dict(Counter(v["type"] for v in report["violations"]))}


def signature(v):
    return (v["type"], v["description"], tuple(sorted((i["description"], round(i["pos"]["x"],5), round(i["pos"]["y"],5)) for i in v["items"])))


def point(xy):
    return pcbnew.VECTOR2I(round(xy[0]*1e6),round(xy[1]*1e6))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("plan",type=Path)
    parser.add_argument("source",type=Path)
    parser.add_argument("baseline_drc",type=Path)
    parser.add_argument("tag")
    parser.add_argument("--accept",action="store_true")
    parser.add_argument("--max-repairs",type=int,default=5)
    args=parser.parse_args()
    plan=json.loads(args.plan.read_text())
    plan["repairs"]=plan["repairs"][:args.max_repairs]
    source=args.source.resolve()
    if sha(source)!=plan["geometry_sha256"]:
        raise ValueError("Plan does not match source board hash")
    if not plan["repairs"]:
        raise ValueError("Empty plan")
    current=ROOT/"vitalq_v2.kicad_pcb"
    current_hash=sha(current)
    backup=checkpoint("pre_"+args.tag)
    candidate=checkpoint("candidate_"+args.tag,source=source.parent)
    candidate_board=candidate/"vitalq_v2.kicad_pcb"
    board=pcbnew.LoadBoard(str(candidate_board))
    before=snapshot(board)
    tracks={t.m_Uuid.AsString():t for t in board.GetTracks()}
    moved=[]
    sites=[]
    for repair in plan["repairs"]:
        via=tracks[repair["remove_via"]]
        if via.Type()!=pcbnew.PCB_VIA_T or via.GetNetname()!=repair["net"]:
            raise ValueError("Via identity mismatch")
        if math.dist([via.GetPosition().x/1e6,via.GetPosition().y/1e6],repair["old_xy"])>1e-6:
            raise ValueError("Via position mismatch")
        if math.dist(repair["path"][0],repair["old_xy"])>1e-6:
            raise ValueError("Path must preserve original via junction")
        netcode=via.GetNetCode()
        target=repair["new_xy"]
        duplicate=next((s for s in sites if math.dist(s["xy"],target)<1e-5 and s["net"]==repair["net"]),None)
        if duplicate:
            board.Remove(via)
        else:
            for site in sites:
                if math.dist(site["xy"],target)<0.2+(site["drill"]+repair["drill"])/2-1e-6:
                    raise ValueError("New via-via hole spacing failure")
            via.SetPosition(point(target))
            via.SetWidth(round(repair["diameter"]*1e6))
            via.SetDrill(round(repair["drill"]*1e6))
            sites.append({"xy":target,"net":repair["net"],"drill":repair["drill"]})
        for a,b in zip(repair["path"],repair["path"][1:]):
            track=pcbnew.PCB_TRACK(board)
            track.SetStart(point(a));track.SetEnd(point(b))
            track.SetLayer(repair["layer"]);track.SetWidth(round(repair["width"]*1e6));track.SetNetCode(netcode)
            board.Add(track)
        moved.append(repair)
    after=snapshot(board)
    if before["footprints"]!=after["footprints"] or before["edges"]!=after["edges"] or before["zones"]!=after["zones"]:
        raise ValueError("Forbidden placement/pad/outline/zone change")
    board.Save(str(candidate_board))
    report_path=candidate/"drc.json"
    subprocess.run([CLI,"pcb","drc","--refill-zones","--save-board","--all-track-errors","--schematic-parity","--format","json","-o",str(report_path),str(candidate_board)],check=True)
    baseline=json.loads(args.baseline_drc.read_text())
    result=json.loads(report_path.read_text())
    bc,ac=counts(baseline),counts(result)
    old=Counter(signature(v) for v in baseline["violations"] if v["type"] not in COSMETIC)
    new=Counter(signature(v) for v in result["violations"] if v["type"] not in COSMETIC)
    new_checks=list((new-old).elements())
    parity=lambda r: Counter((v["type"],v["description"]) for v in r["schematic_parity"])
    acceptable=ac["unconnected"]<bc["unconnected"] and ac["real"]<=bc["real"] and not new_checks and parity(result)==parity(baseline)
    info={"date":datetime.now().astimezone().isoformat(),"tag":args.tag,"backup":str(backup.relative_to(ROOT)),"candidate":str(candidate.relative_to(ROOT)),"before":bc,"after":ac,"new_real_checks":new_checks,"eligible_for_acceptance":acceptable,"accepted":False,"changes":moved}
    if args.accept and acceptable:
        if sha(current)!=current_hash:
            raise ValueError("Working board changed during candidate verification; not overwriting")
        final=snapshot(pcbnew.LoadBoard(str(candidate_board)))
        if final["footprints"]!=before["footprints"] or final["edges"]!=before["edges"]:
            raise ValueError("Refill changed a protected invariant")
        shutil.copy2(candidate_board,current)
        shutil.copy2(report_path,ROOT/("drc_"+args.tag+"_refilled.json"))
        info["accepted"]=True
    (ROOT/("batch_"+args.tag+".json")).write_text(json.dumps(info,indent=2)+"\n")
    print(json.dumps({k:v for k,v in info.items() if k!="changes"},indent=2),flush=True)
    with (ROOT/"PROGRESS.md").open("a") as log:
        log.write(f"\n- {info['date']} **{args.tag}** {plan['net']}: {bc['unconnected']} → {ac['unconnected']} refilled opens; {bc['real']} → {ac['real']} real violations; {ac['total']} total. {'ACCEPTED' if info['accepted'] else 'NOT ACCEPTED'}. {len(moved)} original vias relocated/deduplicated, no original track segments removed, no nudges. Exact changes and DRC comparison: `batch_{args.tag}.json`; backup `{info['backup']}`.\n")


if __name__=="__main__":
    main()
