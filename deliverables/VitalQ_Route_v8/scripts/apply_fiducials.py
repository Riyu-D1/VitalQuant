import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import shutil
import subprocess
import pcbnew
from checkpoint import ROOT,CLI,checkpoint
from audit_checkpoint import snapshot,track_data
from apply_plane_batch import sha,counts,signature,COSMETIC,point
from apply_signal_batch import open_counts
from j8_pofv import apply_j8_policy

parser=argparse.ArgumentParser()
parser.add_argument("baseline",type=Path)
args=parser.parse_args()
plan=json.loads((ROOT/"plan_M2_fiducials.json").read_text())
current=ROOT/"vitalq_v2.kicad_pcb"
current_hash=sha(current)
if current_hash!=plan["geometry_sha256"]:
    raise ValueError("Fiducial placement plan is stale")
backup=checkpoint("pre_M2_FIDs")
candidate=checkpoint("candidate_M2_FIDs")
path=candidate/"vitalq_v2.kicad_pcb"
board=pcbnew.LoadBoard(str(path));before=snapshot(board)
footprints={fp.GetReference():fp for fp in board.GetFootprints()}
if {m["ref"] for m in plan["moves"]}!={"FID1","FID2","FID3","FID4","FID5","FID6"}:
    raise ValueError("Unexpected relocation scope")
for move in plan["moves"]:
    fp=footprints[move["ref"]]
    if fp.IsLocked() or [fp.GetPosition().x/1e6,fp.GetPosition().y/1e6]!=move["before"]:
        raise ValueError("Fiducial changed or is locked")
    fp.SetPosition(point(move["after"]));fp.Reference().SetVisible(False)
board.Save(str(path))
text=path.read_text()
if text.count('(copper_finish "Lead-Free")')!=1:
    raise ValueError("Unexpected legacy surface-finish metadata")
path.write_text(text.replace('(copper_finish "Lead-Free")','(copper_finish "ENIG")'))
report_path=candidate/"drc.json"
subprocess.run([CLI,"pcb","drc","--refill-zones","--save-board","--all-track-errors","--schematic-parity","--format","json","-o",str(report_path),str(path)],check=True)
final=pcbnew.LoadBoard(str(path));after=snapshot(final)
for ref,data in before["footprints"].items():
    if ref.startswith("FID"):
        for key in ("rotation","layer","locked","value","zones"):
            if data[key]!=after["footprints"][ref][key]:
                raise ValueError("Fiducial invariant changed: "+key)
        oldpad={k:v for k,v in data["pads"][0].items() if k!="position"}
        newpad={k:v for k,v in after["footprints"][ref]["pads"][0].items() if k!="position"}
        if oldpad!=newpad:
            raise ValueError("Fiducial pad/net changed")
    elif data!=after["footprints"][ref]:
        raise ValueError("Unauthorized footprint change: "+ref)
if before["edges"]!=after["edges"] or before["tracks"]!=after["tracks"] or before["zones"]!=after["zones"]:
    raise ValueError("Fiducial relocation changed unrelated geometry")
apply_j8_policy(final,validate_only=True)
baseline=json.loads(args.baseline.read_text());result=json.loads(report_path.read_text())
old=Counter(signature(v) for v in baseline["violations"] if v["type"] not in COSMETIC)
new_faults=[]
for v in result["violations"]:
    if v["type"] in COSMETIC:
        continue
    key=signature(v)
    if old[key]:
        old[key]-=1
    elif v["type"]!="items_not_allowed":
        new_faults.append(v)
bc,ac=counts(baseline),counts(result)
parity=lambda r:Counter((v["type"],v["description"]) for v in r["schematic_parity"])
if new_faults or open_counts(baseline)!=open_counts(result) or parity(baseline)!=parity(result):
    raise ValueError("Fiducial relocation regressed refilled DRC: "+json.dumps(new_faults))
if sha(current)!=current_hash:
    raise ValueError("Working board changed during verification")
shutil.copy2(path,current);shutil.copy2(report_path,ROOT/"drc_M2_refilled.json")
info={"date":datetime.now().astimezone().isoformat(),"accepted":True,"before":bc,"after":ac,"backup":str(backup.relative_to(ROOT)),"candidate":str(candidate.relative_to(ROOT)),"moves":plan["moves"],"surface_finish":{"before":"Lead-Free","after":"ENIG","reason":"Current JLCPCB capability requires ENIG for the existing 0.20–0.25mm BGA pads; metadata only, no stackup dimensions or copper changed"}}
(ROOT/"batch_M2.json").write_text(json.dumps(info,indent=2)+"\n")
with (ROOT/"PROGRESS.md").open("a") as log:
    log.write(f"\n- {info['date']} **M2 ACCEPTED — authorized FID1–FID6 relocation**: {ac['unconnected']} refilled opens, {ac['real']} reported real violations. Six 1mm fiducials / 2mm mask apertures moved to optically and mechanically clear on-board sites; reference fields hidden, identifiers/net assignments retained. All other footprints, routing, outline, zones and parity unchanged. Exact six translations in `batch_M2.json`. Surface-finish metadata corrected from Lead-Free to ENIG for JLC's 0.20–0.25mm BGA requirement; stackup dimensions unchanged. Routing remains stopped.\n")
print(json.dumps(info,indent=2),flush=True)
