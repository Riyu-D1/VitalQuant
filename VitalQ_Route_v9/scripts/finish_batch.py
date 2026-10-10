import argparse
from collections import Counter
import json
from pathlib import Path
import shutil
from session import ROOT, COSMETIC, save_json, log, summary


def faults(report):
    return {(v['type'], tuple(sorted(i['uuid'] for i in v.get('items', [])))) for v in report['violations'] if v['type'] not in COSMETIC and v['type'] != 'items_not_allowed'}


parser = argparse.ArgumentParser()
parser.add_argument('batch')
parser.add_argument('previous')
args = parser.parse_args()
directory = ROOT / 'candidates' / args.batch
plan = json.loads((directory / 'applied.json').read_text())
audit = json.loads((directory / 'audit.json').read_text())
old_report = json.loads(Path(args.previous).read_text())
new_report = json.loads((directory / 'drc.json').read_text())
old, new = summary(args.previous), summary(directory / 'drc.json')
new_faults = sorted(faults(new_report) - faults(old_report))
improved = new['unconnected'] < old['unconnected'] or new['real'] < old['real']
accepted = audit['pass'] and not new_faults and new['unconnected'] <= old['unconnected'] and new['real'] <= old['real'] and (improved or bool(plan.get('modify_vias')))
state_path = ROOT / 'state.json'
state = json.loads(state_path.read_text()) if state_path.exists() else {'streak': 0, 'batches': []}
assert not any(b['id'] == args.batch for b in state['batches']), 'Batch already finalized'
assert state['streak'] < 2, 'Routing stop already reached'
if accepted:
    shutil.copy2(directory / 'vitalq_v2.kicad_pcb', ROOT / 'vitalq_v2.kicad_pcb')
    state['current_geometry'] = str(directory / 'geometry.json.gz')
    state['current_drc'] = str(directory / 'drc.json')
result = new if accepted else old
state['streak'] = 0 if result['unconnected'] < old['unconnected'] else state['streak'] + 1
record = {'id': args.batch, 'accepted': accepted, 'before': old, 'candidate': new, 'current': result, 'new_native_faults': new_faults, 'audit_pass': audit['pass'], 'non_improvement_streak': state['streak']}
state['batches'].append(record)
save_json(directory / 'decision.json', record)
save_json(state_path, state)
notes = plan.get('summary', '')
if 'led_detour' in plan:
    notes += '; ' + plan['led_detour']['result']
if plan.get('blocked'):
    notes += '; BLOCKED: ' + ', '.join(f"{b['net']} ({b['reason']})" for b in plan['blocked'])
status = ('ACCEPTED' if accepted else 'REJECTED, original retained') + '; non-improvement streak=' + str(state['streak']) + '; ' + notes
if state['streak'] >= 2:
    status += '; ROUTING STOPPED: two non-improving batches; remaining routing/cleanup deferred to final report'
log(args.batch, (', '.join(plan.get('nets_done', [])) if accepted else '') or 'no completed nets', result, status)
print(json.dumps(record, indent=2), flush=True)
