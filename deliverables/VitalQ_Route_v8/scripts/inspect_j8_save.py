import json
from pathlib import Path
import pcbnew
from extract_exact import polygons
from j8_pofv import apply_j8_policy,polygon

path=Path("backups/20261008_204459_candidate_M1_J8/vitalq_v2.kicad_pcb")
board=pcbnew.LoadBoard(str(path))
zone=next(z for f in board.GetFootprints() if f.GetReference()=="J8" for z in f.Zones())
saved=polygons(zone.Outline())
apply_j8_policy(board)
expected=polygons(zone.Outline())
a,b=polygon(saved),polygon(expected)
extra=polygon(saved);extra.BooleanSubtract(b)
missing=polygon(expected);missing.BooleanSubtract(a)
result={"saved":saved,"expected":expected,"extra":polygons(extra),"missing":polygons(missing)}
Path("j8_save_geometry_audit.json").write_text(json.dumps(result,indent=2)+"\n")
for name,polys in result.items():
    print(name,"outlines",len(polys),"rings",[(len(p["outer"]),[len(h) for h in p["holes"]]) for p in polys])
