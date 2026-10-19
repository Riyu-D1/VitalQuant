#!/bin/bash
cd /Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20
PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
KC=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
RPT=_work/drc_fix4m.rpt
for i in 1 2 3 4 5 6 7 8; do
  echo "=== cycle $i rpt=$RPT ==="
  $PY _work/fix4_p7.py $RPT 2>/dev/null | grep -E "deduped|routed|FAIL|saved"
  cp vitalq_v2.kicad_pcb _work/ckpt_p7_$i.kicad_pcb
  $PY -c "
import pcbnew
b=pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save('vitalq_v2.kicad_pcb')" 2>/dev/null
  cp ../fix_v19/vitalq_v2.kicad_pro .
  RPT=_work/drc_fix4m_$i.rpt
  $KC pcb drc --output $RPT vitalq_v2.kicad_pcb 2>/dev/null | grep -E "violations|unconnected"
done
