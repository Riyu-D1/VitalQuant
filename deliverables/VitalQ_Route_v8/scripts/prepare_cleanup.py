import json
from collections import Counter
from pathlib import Path
import pcbnew
from audit_checkpoint import track_data
from apply_plane_batch import sha

board=pcbnew.LoadBoard("vitalq_v2.kicad_pcb")
report=json.loads(Path("drc_M1_refilled.json").read_text())
tracks={t.m_Uuid.AsString():t for t in board.GetTracks()}
history=json.loads(Path("audit_resume.json").read_text())["changes_from_raw"]["removed_copper"]
used=Counter(t["net"] for t in history if t["kind"]=="track")
plan={"geometry_sha256":sha(Path("vitalq_v2.kicad_pcb")),"removals":[],"deferred":[],"prior_original_segment_removals":dict(used)}
seen=set()
for violation in report["violations"]:
    if violation["type"] not in ("track_dangling","via_dangling"):
        continue
    for item in violation["items"]:
        uid=item["uuid"]
        if uid in seen or uid not in tracks:
            continue
        seen.add(uid)
        track=tracks[uid];data=track_data(track)
        if data["kind"]=="track" and used[data["net"]]>=10:
            plan["deferred"].append({"uuid":uid,"reason":"Cumulative local segment-removal limit","geometry":data})
            continue
        if data["kind"]=="track":
            used[data["net"]]+=1
        plan["removals"].append({"uuid":uid,"reason":violation["type"],"geometry":data})
        print(violation["type"],data["net"],data["start"],uid)
plan["cumulative_segment_removals_if_accepted"]=dict(used)
Path("plan_C1_dangling.json").write_text(json.dumps(plan,indent=2)+"\n")
footprints=[]
for fp in board.GetFootprints():
    bb=fp.GetBoundingBox(False,False)
    footprints.append({"ref":fp.GetReference(),"layer":fp.GetLayer(),"bbox":[bb.GetX()/1e6,bb.GetY()/1e6,(bb.GetX()+bb.GetWidth())/1e6,(bb.GetY()+bb.GetHeight())/1e6],"position":[fp.GetPosition().x/1e6,fp.GetPosition().y/1e6]})
Path("mechanical_geometry.json").write_text(json.dumps({"source_sha256":plan["geometry_sha256"],"footprints":footprints},indent=2)+"\n")
print("Cleanup candidates",len(plan["removals"]),"deferred",len(plan["deferred"]),"cumulative segment removals",dict(used))
