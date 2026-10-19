#!/bin/bash
PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
KC=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
cd /Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20
CUR=_work/stage10.kicad_pcb
for i in 11 12 13 14; do
  $KC pcb drc --severity-all -o _work/drc_s$i.rpt $CUR 2>/dev/null | tail -2
  $PY _work/repair4.py $CUR _work/s${i}d.kicad_pcb _work/drc_s$i.rpt 2>/dev/null | tail -2
  PHASE=2 $PY _work/repair4.py _work/s${i}d.kicad_pcb $CUR _work/drc_s$i.rpt 2>/dev/null | tail -3
done
$KC pcb drc --severity-all -o _work/drc_final.rpt $CUR 2>/dev/null | tail -2
