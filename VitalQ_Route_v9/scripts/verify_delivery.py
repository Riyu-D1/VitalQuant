from collections import Counter
import hashlib
import json
import struct
import xml.etree.ElementTree as ET
from session import ROOT, summary, save_json


def load(name):
    return json.loads((ROOT / name).read_text())


project = load('vitalq_v2.kicad_pro')
rules = project['board']['design_settings']['rules']
assert rules['min_clearance'] == rules['min_track_width'] == .1016
assert rules['min_hole_clearance'] == rules['min_hole_to_hole'] == .2
assert rules['min_via_diameter'] == .25 and rules['min_via_annular_width'] == .05
assert rules['min_through_hole_diameter'] == .15
assert not rules['allow_blind_buried_vias'] and not rules['allow_microvias']
assert project['board']['design_settings']['via_dimensions'] == [{'diameter': .25, 'drill': .15}, {'diameter': .4, 'drill': .2}]
assert all(n['clearance'] == n['track_width'] == .1016 for n in project['net_settings']['classes'])
result = summary(ROOT / 'drc_final.json')
assert result['unconnected'] == 43 and result['real'] == 253 and result['total'] == 339
assert len(load('blocked_connections_final.json')) == result['unconnected']
assert load('state.json')['streak'] == 2
assert load('audit_final.json')['pass'] and load('design_diff_final.json')['pass']
ledger = load('change_ledger.json')
assert len(ledger['accepted_via_changes']) == 5
assert not any(ledger[k] for k in ('accepted_track_additions', 'accepted_track_removals', 'accepted_via_additions', 'accepted_via_removals', 'component_moves', 'passive_nudges', 'rule_area_edits'))
assert all(v['filled'] and v['capped'] for v in load('geometry_audit_final.json')['via_in_pad'])
progress = (ROOT / 'PROGRESS.md').read_text().splitlines()
for batch in ('B0', 'B1', 'B2', 'F0'):
    assert sum(line.startswith(batch + ' |') for line in progress) == 1
images = {}
for name in ('final_top.png', 'final_bottom.png', 'final_3d.png'):
    data = (ROOT / 'renders' / name).read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    images[name] = struct.unpack('>II', data[16:24])
    assert min(images[name]) >= 900
for name in ('final_top_copper.svg', 'final_bottom_copper.svg'):
    assert ET.parse(ROOT / 'renders' / name).getroot().tag.endswith('svg')
board_hash = hashlib.sha256((ROOT / 'vitalq_v2.kicad_pcb').read_bytes()).hexdigest()
assert load('design_diff_final.json')['sha256']['vitalq_v2.kicad_pcb'] == board_hash
assert board_hash in (ROOT / 'REPORT.md').read_text()
files = ['vitalq_v2.kicad_pcb', 'vitalq_v2.kicad_pro', 'vitalq_v2.kicad_dru', 'vitalq_v2.kicad_sch', 'REPORT.md', 'PLAN.md', 'PROGRESS.md', 'drc_final.json', 'audit_final.json', 'design_diff_final.json', 'change_ledger.json', 'blocked_connections_final.json', 'hv_uncapped_final.json', 'geometry_audit_final.json'] + ['renders/' + n for n in images] + ['renders/final_top_copper.svg', 'renders/final_bottom_copper.svg']
manifest = {'status': 'UNFINISHED_NOT_FOR_FABRICATION', 'drc': result, 'verification': 'PASS: source geometry, retained connectivity, explicit stopped status, rule settings, complete blocker list, exactly-one-line batch records, report hash and render file formats', 'png_dimensions': images, 'files': {n: {'sha256': hashlib.sha256((ROOT / n).read_bytes()).hexdigest(), 'size': (ROOT / n).stat().st_size} for n in files}}
save_json(ROOT / 'delivery_manifest.json', manifest)
print(json.dumps({'verification': manifest['verification'], 'drc': result, 'files_verified': len(files), 'board_sha256': board_hash}, indent=2))
