from datetime import datetime
import json
from pathlib import Path
import shutil
import subprocess
import pcbnew
from checkpoint import ROOT,CLI,checkpoint
from audit_checkpoint import snapshot
from apply_plane_batch import sha,counts
from apply_signal_batch import open_counts
from j8_pofv import apply_j8_policy

current=ROOT/"vitalq_v2.kicad_pcb"
original_hash=sha(current)
audit=json.loads((ROOT/"audit_final.json").read_text())
required={v["uuid"]:v for v in audit["via_in_pad_process_audit"] if not(v["filled"] and v["capped"])}
if len(required)!=3 or audit["sha256"]["vitalq_v2.kicad_pcb"]!=original_hash:
    raise ValueError("Unexpected process-qualification scope")
backup=checkpoint("pre_M3_POFV")
candidate=checkpoint("candidate_M3_POFV")
path=candidate/"vitalq_v2.kicad_pcb"
board=pcbnew.LoadBoard(str(path));before=snapshot(board)
for via in board.GetTracks():
    if via.m_Uuid.AsString() in required:
        via.SetFillingMode(pcbnew.FILLING_MODE_FILLED);via.SetCappingMode(pcbnew.CAPPING_MODE_CAPPED)
board.Save(str(path))
report_path=candidate/"drc.json"
subprocess.run([CLI,"pcb","drc","--refill-zones","--save-board","--all-track-errors","--schematic-parity","--format","json","-o",str(report_path),str(path)],check=True)
final=pcbnew.LoadBoard(str(path))
if snapshot(final)!=before:
    raise ValueError("Process metadata update changed physical geometry")
for via in final.GetTracks():
    if via.m_Uuid.AsString() in required and (via.GetFillingMode()!=pcbnew.FILLING_MODE_FILLED or via.GetCappingMode()!=pcbnew.CAPPING_MODE_CAPPED):
        raise ValueError("POFV metadata did not survive save/refill")
apply_j8_policy(final,validate_only=True)
baseline=json.loads((ROOT/"drc_final.json").read_text());result=json.loads(report_path.read_text())
if counts(baseline)!=counts(result) or open_counts(baseline)!=open_counts(result) or baseline["schematic_parity"]!=result["schematic_parity"]:
    raise ValueError("Process update unexpectedly changed DRC")
if sha(current)!=original_hash:
    raise ValueError("Working board changed during verification")
shutil.copy2(path,current);shutil.copy2(report_path,ROOT/"drc_M3_refilled.json")
info={"date":datetime.now().astimezone().isoformat(),"accepted":True,"before":counts(baseline),"after":counts(result),"backup":str(backup.relative_to(ROOT)),"candidate":str(candidate.relative_to(ROOT)),"process_updates":list(required.values()),"geometry_unchanged":True,"required_process":"Resin-filled, copper-capped planar via-in-pad; specify explicitly in fabrication order"}
(ROOT/"batch_M3.json").write_text(json.dumps(info,indent=2)+"\n")
with (ROOT/"PROGRESS.md").open("a") as log:
    log.write(f"\n- {info['date']} **M3 ACCEPTED — process metadata only**: existing via-in-pad at R19.1, C59.2 and C89.2 marked filled/copper-capped POFV. No geometry, placement, net, rule or connection changed. Refilled result remains 43 opens / 212 reported real / 298 total / 29 parity. These are outside J8; its two exceptions are unchanged. `batch_M3.json`. Routing stays stopped.\n")
print("M3 accepted: three existing VIPs now explicitly filled/capped; physical geometry and refilled DRC unchanged",flush=True)
