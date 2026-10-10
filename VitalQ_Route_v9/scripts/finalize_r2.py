"""Final R2 deliverables from the saved working board: refilled DRC -> drc_final_R2.json, renders/R2, fab outputs, REPORT/FAB_NOTES, PROGRESS."""
import json
import shutil
import subprocess
from session import ROOT, drc, summary, backup, log

ck = backup('R2-final')
tmp = ROOT / 'candidates' / 'R2-final'
tmp.mkdir(exist_ok=True)
for f in ('vitalq_v2.kicad_pcb', 'vitalq_v2.kicad_pro', 'vitalq_v2.kicad_dru', 'vitalq_v2.kicad_sch', 'vitalq_v2.kicad_prl'):
    shutil.copy2(ROOT / f, tmp / f)
drc(tmp / 'vitalq_v2.kicad_pcb', tmp / 'drc.json')
shutil.copy2(tmp / 'drc.json', ROOT / 'drc_final_R2.json')
py = str(ROOT.parent / 'VitalQ_Route_v8' / '.venv' / 'bin' / 'python')
for script in ('render_r2.py', 'fab_outputs.py', 'report_r2.py'):
    r = subprocess.run([py, '-B', str(ROOT / 'scripts' / script)], capture_output=True, text=True, env={'PYTHONDONTWRITEBYTECODE': '1', 'PATH': '/usr/bin:/bin'})
    print(script, r.returncode, r.stdout[-400:], r.stderr[-400:])
s = summary(ROOT / 'drc_final_R2.json')
log('R2-final', 'final refilled DRC of the saved working board -> drc_final_R2.json; renders/R2; fab/ (gerbers zip, BOM, CPL, FAB_NOTES.txt/.md); REPORT.md R2 section', s, 'NOT fabrication-ready; backup ' + ck.name)
print(json.dumps(s))
