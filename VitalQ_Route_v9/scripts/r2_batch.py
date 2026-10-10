"""Generic R2 batch: optional moves (with stub detach) -> refilled stage DRC -> routing -> apply -> DRC -> audit -> decision."""
import json
from r2 import ROOT, begin, candidate_dir, verify, finish, save_json
from r2_common import kicad_job, stage_dir, extract
from r2route import Board
from session import drc


def run(batch, tag, plan_fn, moves=(), moved=(), next_step='', ripup=None, base=None, extra_check=None, j8=None, real_allowance_fn=None, adopt_if_fewer_opens=False, stackup8=False):
    checkpoint, s = begin(batch, tag)
    if base:
        import shutil
        from r2 import STATE
        shutil.copy2(base / 'vitalq_v2.kicad_pcb', ROOT / 'vitalq_v2.kicad_pcb')
        s['current_geometry'] = str(base / 'geometry.json.gz')
        s['current_drc'] = str(base / 'drc.json')
        s.setdefault('base_adoptions', []).append({'batch': batch, 'base': str(base), 'backup_before': str(checkpoint)})
        save_json(STATE, s)
    d = candidate_dir(batch)
    ledger = {'batch': batch, 'backup': str(checkpoint), 'moves': [], 'removed': [], 'routes': [], 'issues': []}
    st = stage_dir(d, 'stage1')
    job = {'input': str(ROOT / 'vitalq_v2.kicad_pcb'), 'output': str(st / 'vitalq_v2.kicad_pcb'), 'moves': list(moves), 'refill': False, 'log': str(st / 'ops_log.json'), 'stackup8': stackup8}
    if ripup:
        save_json(st / 'ripup.json', {'remove': ripup})
        job['plan'] = str(st / 'ripup.json')
    kicad_job(job, st / 'job.json')
    ops = json.loads((st / 'ops_log.json').read_text())
    ledger['moves'] = [o for o in ops if 'ref' in o]
    for o in ledger['moves']:
        ledger['removed'] += o.get('detached', [])
    if ripup:
        ledger['removed'] += ripup
    drc(st / 'vitalq_v2.kicad_pcb', st / 'drc.json')
    extract(st / 'vitalq_v2.kicad_pcb', st / 'geometry.json.gz')
    b = Board(st / 'geometry.json.gz')
    stage_opens = json.loads((st / 'drc.json').read_text())['unconnected_items']
    plan_fn(b, ledger, stage_opens)
    plan = {'tracks': [r for r in b.raw['tracks'] if str(r['uuid']).startswith('new-')], 'vias': [r for r in b.raw['vias'] if str(r['uuid']).startswith('new-')], 'remove': [{'uuid': u} for u in dict.fromkeys(ledger.get('late_remove', []))], 'modify_vias': ledger.get('modify_vias', []), 'modify_tracks': ledger.get('modify_tracks', [])}
    save_json(d / 'plan.json', plan)
    save_json(d / 'ledger.json', ledger)
    job2 = {'input': str(st / 'vitalq_v2.kicad_pcb'), 'output': str(d / 'vitalq_v2.kicad_pcb'), 'plan': str(d / 'plan.json'), 'log': str(d / 'apply_log.json')}
    if j8:
        job2['j8_notch'] = j8
    kicad_job(job2, d / 'job.json')
    if j8:
        save_json(d / 'j8_permitted_vias_R2.json', json.loads((d / 'apply_log.json').read_text())[-1])
    drc(d / 'vitalq_v2.kicad_pcb', d / 'drc.json')
    from r2_repair import repair
    from r2 import state as _state
    if not repair(d, _state()['current_drc'], ledger):
        ledger['issues'].append('new dangling items remain after in-batch repair')
    save_json(d / 'ledger.json', ledger)
    verify(d, moved=tuple(moved), allow_j8=bool(j8), stackup8=stackup8)
    save_json(d / 'applied.json', {'batch': batch, 'ledger': 'ledger.json', 'plan': 'plan.json'})
    done = [r['connection'] for r in ledger['routes'] if r['status'] == 'routed']
    failed = sorted({r['connection'] for r in ledger['routes'] if r['status'] == 'failed'} - set(done))
    moves_txt = '; '.join(f"{m['ref']} {m['from']}->{m['to']} rot {m['rotation']}" for m in ledger['moves'])
    notes = f"{len(plan['tracks'])} tracks, {len(plan['vias'])} vias added, {len(ledger['removed'])} segments/vias removed; moves: {moves_txt or 'none'}; unrouted: {', '.join(failed) or 'none'}"
    ok = not ledger['issues']
    if extra_check:
        verdict = extra_check(d)
        ledger['extra_check'] = verdict
        save_json(d / 'ledger.json', ledger)
        ok = ok and verdict['pass']
        notes += '; extra check: ' + verdict['text']
    if base:
        notes = f'base adopted from {base.name} (supervisor); ' + notes
    allowance = real_allowance_fn(d) if real_allowance_fn else 0
    if allowance:
        notes += f'; real allowance {allowance} (expected dangling POFV, consumed by next sub-batch)'
    return finish(batch, d, ', '.join(done) or '', notes, next_step, extra_ok=ok, real_allowance=allowance, adopt_if_fewer_opens=adopt_if_fewer_opens)
