from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import pcbnew
from audit_checkpoint import snapshot,difference,digest
from j8_pofv import apply_j8_policy

root=Path(__file__).resolve().parents[1]
base=pcbnew.LoadBoard(str(root/"backups/20261008_195914_resume_unmodified/vitalq_v2.kicad_pcb"))
board=pcbnew.LoadBoard(str(root/"vitalq_v2.kicad_pcb"))
a,b=snapshot(base),snapshot(board)
delta=difference(a,b)
fids={"FID1","FID2","FID3","FID4","FID5","FID6"}
for ref,old in a["footprints"].items():
    new=b["footprints"][ref]
    if ref in fids:
        old=json.loads(json.dumps(old));new=json.loads(json.dumps(new))
        old.pop("position");new.pop("position")
        for p in old["pads"]+new["pads"]:
            p.pop("position")
    if ref=="J8":
        old=dict(old);new=dict(new);old.pop("zones");new.pop("zones")
    if old!=new:
        raise ValueError("Unauthorized footprint invariant change: "+ref)
if a["edges"]!=b["edges"] or a["zones"]!=b["zones"] or board.GetCopperLayerCount()!=6:
    raise ValueError("Outline, board zones, or layer count changed")
initial=json.loads((root/"audit_resume.json").read_text())["sha256"]
hashes={p:digest(root/p) for p in initial}
for p in ("quilter_raw/vitalq_v2.kicad_pcb","vitalq_v2.kicad_sch","vitalq_v2.kicad_pro"):
    if hashes[p]!=initial[p]:
        raise ValueError("Immutable input changed: "+p)
exceptions=apply_j8_policy(board,validate_only=True)
vias=[t for t in board.GetTracks() if t.Type()==pcbnew.PCB_VIA_T]
if any(v.GetViaType()!=pcbnew.VIATYPE_THROUGH for v in vias):
    raise ValueError("Non-through via found")
vips=[]
for fp in board.GetFootprints():
    for pad in fp.Pads():
        if pad.GetAttribute()!=pcbnew.PAD_ATTRIB_SMD:
            continue
        polygon=pad.GetEffectivePolygon(fp.GetLayer())
        for via in vias:
            if via.GetNetCode()==pad.GetNetCode() and polygon.Contains(via.GetPosition()):
                vips.append({"uuid":via.m_Uuid.AsString(),"ref":fp.GetReference(),"pad":pad.GetNumber(),"net":via.GetNetname(),"xy":[via.GetPosition().x/1e6,via.GetPosition().y/1e6],"filled":via.GetFillingMode()==pcbnew.FILLING_MODE_FILLED,"capped":via.GetCappingMode()==pcbnew.CAPPING_MODE_CAPPED})
result={"date":datetime.now().astimezone().isoformat(),"kicad":pcbnew.GetBuildVersion(),"sha256":hashes,"protected_invariants_passed":True,"outline_and_board_zones_unchanged":True,"footprints":b["counts"]["footprints"],"pads":b["counts"]["pads"],"copper_layers":6,"counts":b["counts"],"changes_since_restart":delta,"j8_permitted_vias":exceptions,"via_in_pad_process_audit":vips,"through_vias_only":True,"via_dimensions_mm":dict(Counter(f"{v.GetWidth(pcbnew.F_Cu)/1e6:.6f}/{v.GetDrillValue()/1e6:.6f}" for v in vias)),"surface_finish":"ENIG" if '(copper_finish "ENIG")' in (root/"vitalq_v2.kicad_pcb").read_text() else "OTHER"}
(root/"audit_final.json").write_text(json.dumps(result,indent=2)+"\n")
print("Protected invariants PASS",result["counts"],"J8 exceptions",len(exceptions),"via-in-pad without explicit filled/capped flags",sum(not (v["filled"] and v["capped"]) for v in vips),flush=True)
