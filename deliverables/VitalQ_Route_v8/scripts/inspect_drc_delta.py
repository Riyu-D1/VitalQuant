import json
from pathlib import Path
from collections import Counter
import pcbnew
from audit_checkpoint import track_data

root=Path(__file__).resolve().parents[1]
info=json.loads((root/"batch_R4.json").read_text())
base=pcbnew.LoadBoard(str(root/"backups/20261008_195914_resume_refilled/vitalq_v2.kicad_pcb"))
new=pcbnew.LoadBoard(str(root/info["candidate"]/"vitalq_v2.kicad_pcb"))
bm={t.m_Uuid.AsString():track_data(t) for t in base.GetTracks()}
nm={t.m_Uuid.AsString():track_data(t) for t in new.GetTracks()}
a=json.loads((root/"drc_resume_refilled.json").read_text())
b=json.loads((root/info["candidate"]/"drc.json").read_text())
key=lambda v:(v["type"],tuple(i["uuid"] for i in v["items"]))
old=Counter(key(v) for v in a["violations"])
remaining=old.copy()
for v in b["violations"]:
    k=key(v)
    if remaining[k]:
        remaining[k]-=1
    elif v["type"] not in ("lib_footprint_issues","lib_footprint_mismatch"):
        print("NEW CHECK",v)
        for item in v["items"]:
            uid=item["uuid"]
            print("EXACT COPPER UNCHANGED", bm.get(uid)==nm.get(uid), "BEFORE",bm.get(uid),"AFTER",nm.get(uid))
print("Pre-existing track geometry changed:",[(uid,bm[uid],nm.get(uid)) for uid in bm if bm[uid]!=nm.get(uid)])
