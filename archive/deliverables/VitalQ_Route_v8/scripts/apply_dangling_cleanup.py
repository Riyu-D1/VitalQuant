import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import shutil
import subprocess
import sys
import pcbnew
from checkpoint import ROOT,CLI,checkpoint
from audit_checkpoint import snapshot,track_data
from apply_plane_batch import sha,counts,signature,COSMETIC
from apply_signal_batch import open_counts
from j8_pofv import apply_j8_policy

parser=argparse.ArgumentParser()
parser.add_argument("plan",type=Path)
parser.add_argument("baseline",type=Path)
parser.add_argument("before_geometry",type=Path)
parser.add_argument("tag")
args=parser.parse_args()
plan=json.loads(args.plan.read_text())
current=ROOT/"vitalq_v2.kicad_pcb"
current_hash=sha(current)
if current_hash!=plan["geometry_sha256"]:
    raise ValueError("Cleanup plan is stale")
backup=checkpoint("pre_"+args.tag)
candidate=checkpoint("candidate_"+args.tag)
path=candidate/"vitalq_v2.kicad_pcb"
board=pcbnew.LoadBoard(str(path));before=snapshot(board)
tracks={t.m_Uuid.AsString():t for t in board.GetTracks()}
for item in plan["removals"]:
    track=tracks[item["uuid"]]
    if track_data(track)!=item["geometry"]:
        raise ValueError("Cleanup geometry mismatch")
    board.Remove(track)
board.Save(str(path))
report_path=candidate/"drc.json"
subprocess.run([CLI,"pcb","drc","--refill-zones","--save-board","--all-track-errors","--schematic-parity","--format","json","-o",str(report_path),str(path)],check=True)
final=pcbnew.LoadBoard(str(path));after=snapshot(final)
if before["footprints"]!=after["footprints"] or before["edges"]!=after["edges"] or before["zones"]!=after["zones"]:
    raise ValueError("Cleanup changed protected objects")
apply_j8_policy(final,validate_only=True)
geometry_path=candidate/"geometry.json"
subprocess.run([sys.executable,str(ROOT/"scripts/extract_exact.py"),str(path),str(geometry_path)],check=True)
connectivity_path=candidate/"pad_connectivity.json"
verification=subprocess.run([str(ROOT/".venv/bin/python"),str(ROOT/"scripts/verify_pad_connectivity.py"),str(args.before_geometry),str(geometry_path),str(connectivity_path)])
baseline=json.loads(args.baseline.read_text());result=json.loads(report_path.read_text())
bc,ac=counts(baseline),counts(result)
bo,ao=open_counts(baseline),open_counts(result)
regressions={n:c-bo[n] for n,c in ao.items() if c>bo[n]}
old=Counter(signature(v) for v in baseline["violations"] if v["type"] not in COSMETIC)
new_faults=[];new_dangling=[]
for v in result["violations"]:
    if v["type"] in COSMETIC:
        continue
    key=signature(v)
    if old[key]:
        old[key]-=1
    elif v["type"] in ("track_dangling","via_dangling"):
        new_dangling.append(v)
    elif v["type"]!="items_not_allowed":
        new_faults.append(v)
old_dangling=sum(bc["types"].get(t,0) for t in ("track_dangling","via_dangling"))
new_dangling_count=sum(ac["types"].get(t,0) for t in ("track_dangling","via_dangling"))
parity=lambda r:Counter((v["type"],v["description"]) for v in r["schematic_parity"])
accepted=not verification.returncode and not regressions and not new_faults and new_dangling_count<old_dangling and parity(baseline)==parity(result)
info={"date":datetime.now().astimezone().isoformat(),"tag":args.tag,"accepted":accepted,"before":bc,"after":ac,"backup":str(backup.relative_to(ROOT)),"candidate":str(candidate.relative_to(ROOT)),"open_regressions":regressions,"new_real_faults":new_faults,"newly_exposed_dangling":new_dangling,"pad_connectivity_passed":verification.returncode==0,"removals":plan["removals"],"cumulative_segment_removals":plan["cumulative_segment_removals_if_accepted"]}
if accepted:
    if sha(current)!=current_hash:
        raise ValueError("Working board changed during cleanup verification")
    shutil.copy2(path,current);shutil.copy2(report_path,ROOT/("drc_"+args.tag+"_refilled.json"));shutil.copy2(geometry_path,ROOT/("geometry_"+args.tag+"_refilled.json"))
    (ROOT/"cleanup_removal_ledger.json").write_text(json.dumps(info["cumulative_segment_removals"],indent=2)+"\n")
(ROOT/("batch_"+args.tag+".json")).write_text(json.dumps(info,indent=2)+"\n")
with (ROOT/"PROGRESS.md").open("a") as log:
    log.write(f"\n- {info['date']} **{args.tag} non-routing cleanup {'ACCEPTED' if accepted else 'REJECTED'}**: {bc['unconnected']} → {ac['unconnected']} refilled opens; {bc['real']} → {ac['real']} reported real violations; dangling checks {old_dangling} → {new_dangling_count}. Removed {len(plan['removals'])} DRC-identified unused copper items; no copper added. Full pad-component connectivity preservation: {verification.returncode==0}. Remaining/newly exposed leaves stay logged; local per-net rip-up limits enforced. `batch_{args.tag}.json`. Routing remains stopped.\n")
print(json.dumps({k:v for k,v in info.items() if k not in ("removals","newly_exposed_dangling")},indent=2),flush=True)
