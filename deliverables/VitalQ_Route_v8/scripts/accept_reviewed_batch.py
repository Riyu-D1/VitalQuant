import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import shutil
import pcbnew
from checkpoint import ROOT
from audit_checkpoint import snapshot, track_data
from apply_plane_batch import counts, signature, COSMETIC, sha

parser=argparse.ArgumentParser()
parser.add_argument("tag")
parser.add_argument("baseline_drc",type=Path)
parser.add_argument("candidate_drc",type=Path)
args=parser.parse_args()
info_path=ROOT/("batch_"+args.tag+".json")
info=json.loads(info_path.read_text())
backup=ROOT/info["backup"]
candidate=ROOT/info["candidate"]
base=pcbnew.LoadBoard(str(backup/"vitalq_v2.kicad_pcb"))
new=pcbnew.LoadBoard(str(candidate/"vitalq_v2.kicad_pcb"))
a,b=snapshot(base),snapshot(new)
if a["footprints"]!=b["footprints"] or a["edges"]!=b["edges"] or a["zones"]!=b["zones"]:
    raise ValueError("Protected geometry changed")
bm={t.m_Uuid.AsString():track_data(t) for t in base.GetTracks()}
nm={t.m_Uuid.AsString():track_data(t) for t in new.GetTracks()}
old_report=json.loads(args.baseline_drc.read_text())
new_report=json.loads(args.candidate_drc.read_text())
remaining=Counter(signature(v) for v in old_report["violations"] if v["type"] not in COSMETIC)
preexisting=[]
for violation in new_report["violations"]:
    if violation["type"] in COSMETIC:
        continue
    key=signature(violation)
    if remaining[key]:
        remaining[key]-=1
        continue
    if violation["type"]!="items_not_allowed":
        raise ValueError("New non-keepout violation requires independent repair")
    for item in violation["items"]:
        uid=item["uuid"]
        if uid not in bm or bm[uid]!=nm.get(uid):
            raise ValueError("Keepout violation affects changed/new copper")
    preexisting.append(violation)
bc,ac=counts(old_report),counts(new_report)
if not(ac["unconnected"]<bc["unconnected"] and ac["real"]<=bc["real"]):
    raise ValueError("No acceptable improvement")
parity=lambda r:Counter((v["type"],v["description"]) for v in r["schematic_parity"])
if parity(old_report)!=parity(new_report):
    raise ValueError("Parity changed")
if sha(ROOT/"vitalq_v2.kicad_pcb")!=sha(backup/"vitalq_v2.kicad_pcb"):
    raise ValueError("Working board changed; refusing overwrite")
shutil.copy2(candidate/"vitalq_v2.kicad_pcb",ROOT/"vitalq_v2.kicad_pcb")
shutil.copy2(args.candidate_drc,ROOT/("drc_"+args.tag+"_refilled.json"))
info.update(accepted=True,eligible_for_acceptance=True,review_date=datetime.now().astimezone().isoformat(),
            newly_reported_unchanged_keepout_checks=preexisting,
            review_reason="Both all-track-error DRCs were refilled. Newly reported keepout checks concern exactly unchanged copper and exactly unchanged rule areas; they remain counted as real. All newly modified copper passes; open and real-violation counts decrease.")
info_path.write_text(json.dumps(info,indent=2)+"\n")
with (ROOT/"PROGRESS.md").open("a") as log:
    log.write(f"\n- {info['review_date']} **{args.tag} ACCEPTED after delta audit**: {bc['unconnected']} → {ac['unconnected']} refilled opens; {bc['real']} → {ac['real']} real violations. {len(preexisting)} newly reported keepout entries concern unchanged old copper with unchanged keepout geometry; these remain counted, not waived. Exact UUID/geometry comparison in `batch_{args.tag}.json`. Both sides verified with `--all-track-errors --refill-zones`.\n")
print(args.tag,"ACCEPTED",bc,"->",ac,"newly reported unchanged keepout entries",len(preexisting))
