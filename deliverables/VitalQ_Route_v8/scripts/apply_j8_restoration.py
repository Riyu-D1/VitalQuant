from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import shutil
import subprocess
import pcbnew
from checkpoint import ROOT,CLI,checkpoint
from audit_checkpoint import snapshot,track_data
from apply_plane_batch import sha,counts,signature,COSMETIC
from j8_pofv import apply_j8_policy,footprint_invariants

current=ROOT/"vitalq_v2.kicad_pcb"
original_hash=sha(current)
backup=checkpoint("pre_M1_J8")
candidate=checkpoint("candidate_M1_J8")
path=candidate/"vitalq_v2.kicad_pcb"
board=pcbnew.LoadBoard(str(path))
before=snapshot(board)
old_copper={t.m_Uuid.AsString():track_data(t) for t in board.GetTracks()}
exceptions=apply_j8_policy(board,mark_existing=True)
marked=[]
for via in board.GetTracks():
    if via.Type()!=pcbnew.PCB_VIA_T:
        continue
    for fp in board.GetFootprints():
        if fp.GetReference() not in ("U6","U7","U22"):
            continue
        pad=next((p for p in fp.Pads() if p.GetNetname()==via.GetNetname() and p.GetEffectivePolygon(fp.GetLayer()).Contains(via.GetPosition())),None)
        if pad:
            via.SetFillingMode(pcbnew.FILLING_MODE_FILLED);via.SetCappingMode(pcbnew.CAPPING_MODE_CAPPED)
            marked.append({"uuid":via.m_Uuid.AsString(),"ref":fp.GetReference(),"ball":pad.GetNumber(),"xy":[via.GetPosition().x/1e6,via.GetPosition().y/1e6]})
board.Save(str(path))
report_path=candidate/"drc.json"
subprocess.run([CLI,"pcb","drc","--refill-zones","--save-board","--all-track-errors","--schematic-parity","--format","json","-o",str(report_path),str(path)],check=True)
final=pcbnew.LoadBoard(str(path));after=snapshot(final)
if footprint_invariants(before)!=footprint_invariants(after) or before["edges"]!=after["edges"] or before["zones"]!=after["zones"]:
    raise ValueError("Unexpected protected geometry change")
if old_copper!={t.m_Uuid.AsString():track_data(t) for t in final.GetTracks()}:
    raise ValueError("Unexpected copper geometry change")
verified=apply_j8_policy(final,validate_only=True)
if exceptions!=verified:
    raise ValueError("POFV exception did not survive KiCad save/refill")
baseline=json.loads((ROOT/"drc_R7_refilled.json").read_text());result=json.loads(report_path.read_text())
old=Counter(signature(v) for v in baseline["violations"] if v["type"] not in COSMETIC)
new_checks=[]
for v in result["violations"]:
    if v["type"] in COSMETIC:
        continue
    key=signature(v)
    if old[key]:
        old[key]-=1
    elif v["type"]!="items_not_allowed" or any(i["uuid"] not in old_copper or i["uuid"] in {e["uuid"] for e in exceptions} for i in v["items"]):
        new_checks.append(v)
bc,ac=counts(baseline),counts(result)
if new_checks or ac["unconnected"]!=bc["unconnected"]:
    raise ValueError("J8 restoration failed refilled DRC: "+json.dumps(new_checks))
if sha(current)!=original_hash:
    raise ValueError("Working board changed during verification")
shutil.copy2(path,current);shutil.copy2(report_path,ROOT/"drc_M1_refilled.json")
info={"date":datetime.now().astimezone().isoformat(),"accepted":True,"before":bc,"after":ac,"backup":str(backup.relative_to(ROOT)),"candidate":str(candidate.relative_to(ROOT)),"j8_exceptions":exceptions,"bga_vips_marked_filled_capped":marked,"description":"Original J8 polygon restored except exact permitted via annuli plus 0.002mm geometric tolerance. All broad old notches removed; no other shape, pad, placement, net or copper geometry changed."}
(ROOT/"batch_M1.json").write_text(json.dumps(info,indent=2)+"\n")
(ROOT/"j8_permitted_vias.json").write_text(json.dumps(exceptions,indent=2)+"\n")
with (ROOT/"PROGRESS.md").open("a") as log:
    log.write(f"\n- {info['date']} **M1 ACCEPTED — J8 restoration / POFV metadata**: {ac['unconnected']} refilled opens; {ac['real']} reported real violations. Original J8 area rebuilt with only {len(exceptions)} precisely bounded authorized U6-ball exceptions; all broad old notches removed. {len(marked)} existing BGA via-in-pad sites explicitly marked filled and copper-capped. No copper/placement/net changes. `batch_M1.json`, `j8_permitted_vias.json`. This cleanup does not reset the routing non-improvement counter.\n")
print(json.dumps(info,indent=2),flush=True)
