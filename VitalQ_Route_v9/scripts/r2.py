import json
from datetime import datetime
from pathlib import Path
import shutil
import subprocess
from session import ROOT, COSMETIC, backup, save_json, log, summary

STATE = ROOT / 'state_R2.json'
KICAD_PY = '/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3'
GEO_PY = str(ROOT.parent / 'VitalQ_Route_v8' / '.venv' / 'bin' / 'python')
HISTORY = [ROOT / 'PROGRESS.md', ROOT / 'REPORT.md', ROOT.parent / 'VitalQ_Route_v8' / 'PROGRESS.md', ROOT.parent / 'VitalQ_Route_v8' / 'REPORT.md']


def state():
    if STATE.exists():
        return json.loads(STATE.read_text())
    return {'phase': 'R2', 'streak': 0, 'batches': [], 'attempts': {}, 'current_geometry': str(ROOT / 'geometry_final.json.gz'), 'current_drc': str(ROOT / 'drc_final.json')}


def begin(batch, tag):
    s = state()
    assert not any(b['id'] == batch for b in s['batches']), 'batch already finalized'
    for p in HISTORY:
        p.read_text()
    save_json(STATE, s)
    return backup(batch + '_' + tag), s


def record_attempt(net, approach):
    s = state()
    tries = s['attempts'].setdefault(net, [])
    if len(tries) >= 3 or approach in [t['approach'] for t in tries]:
        s.setdefault('blocked', {})[net] = 'attempt limit (3) reached or approach already tried'
        save_json(STATE, s)
        return 0
    tries.append({'approach': approach, 'time': datetime.now().astimezone().isoformat()})
    save_json(STATE, s)
    return len(tries)


def candidate_dir(batch):
    d = ROOT / 'candidates' / batch
    d.mkdir(parents=True, exist_ok=False)
    for ext in ('kicad_pro', 'kicad_dru', 'kicad_sch', 'kicad_prl'):
        shutil.copy2(ROOT / ('vitalq_v2.' + ext), d / ('vitalq_v2.' + ext))
    return d


def faults(report):
    return {(v['type'], tuple(sorted(i['uuid'] for i in v.get('items', [])))) for v in report['violations'] if v['type'] not in COSMETIC and v['type'] != 'items_not_allowed'}


def verify(d, allow_zone_changes=False, moved=(), allow_j8=False, stackup8=False):
    subprocess.run([KICAD_PY, '-B', str(ROOT / 'scripts' / 'extract.py'), str(d / 'vitalq_v2.kicad_pcb'), str(d / 'geometry.json.gz')], check=True)
    command = [GEO_PY, '-B', str(ROOT / 'scripts' / 'audit.py'), state()['current_geometry'], str(d / 'geometry.json.gz'), str(d / 'audit.json'), '--moved=' + ','.join(moved)]
    if allow_zone_changes:
        command.append('--allow-zone-changes')
    if allow_j8:
        command.append('--allow-j8')
    if stackup8:
        command += ['--stackup8', '--allow-zone-changes']
    subprocess.run(command, check=True, env={'PYTHONDONTWRITEBYTECODE': '1', 'PATH': '/usr/bin:/bin'})


def finish(batch, d, nets_done, notes, next_step, extra_ok=True, real_allowance=0, adopt_if_fewer_opens=False):
    s = state()
    old_report = json.loads(Path(s['current_drc']).read_text())
    new_report = json.loads((d / 'drc.json').read_text())
    old, new = summary(s['current_drc']), summary(d / 'drc.json')
    audit = json.loads((d / 'audit.json').read_text())
    new_faults = sorted(faults(new_report) - faults(old_report))
    accepted = extra_ok and audit['pass'] and new['unconnected'] <= old['unconnected'] and new['real'] - real_allowance <= old['real']
    if adopt_if_fewer_opens and not accepted:
        inv_ok = all(audit['invariants'].values()) and not audit['new_keepout_hits'] and not audit['new_hole_pairs']
        if inv_ok and new['unconnected'] < old['unconnected']:
            accepted = True
            notes += '; ADOPTED under supervisor regional-rip rule (fewer opens; remaining faults fixed next batch)'
    if accepted:
        shutil.copy2(d / 'vitalq_v2.kicad_pcb', ROOT / 'vitalq_v2.kicad_pcb')
        s['current_geometry'] = str(d / 'geometry.json.gz')
        s['current_drc'] = str(d / 'drc.json')
    result = new if accepted else old
    improving = result['unconnected'] < old['unconnected'] or result['real'] < old['real']
    s['streak'] = 0 if improving else s['streak'] + 1
    rec = {'id': batch, 'accepted': accepted, 'before': old, 'candidate': new, 'current': result, 'new_native_faults': [list(f) for f in new_faults], 'audit_pass': audit['pass'], 'streak': s['streak'], 'notes': notes}
    s['batches'].append(rec)
    save_json(d / 'decision.json', rec)
    save_json(STATE, s)
    status = ('ACCEPTED' if accepted else 'REJECTED, board unchanged') + f"; streak={s['streak']}; {notes}"
    if s['streak'] >= 2:
        status += '; streak>=2: standing order (supervisor) = change approach and continue'
    log(batch, (nets_done if accepted else '') or 'no completed nets', result, status)
    print(f"{batch}: {'ACCEPTED' if accepted else 'REJECTED'} (streak {s['streak']}); new native faults {len(new_faults)}; audit {'pass' if audit['pass'] else 'FAIL'}")
    print(f"unconnected {old['unconnected']} -> {result['unconnected']} (candidate {new['unconnected']}); real {old['real']} -> {result['real']} (candidate {new['real']})")
    print('next: ' + next_step, flush=True)
    return rec
