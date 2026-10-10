"""R2-17c (sub-batch 1, revised): POFV + J8 notches ONLY at the U6 sites that are already clear (B2 PD2_INP, D2 AFE_BG, E2 MISO_AFE).
No rip-up, no routing. Purpose: DRC-verify the per-via J8 notch mechanism. The three vias are expected dangling until R2-17d routes them."""
import json
from r2_batch import run
from r2route import SMALL
import r2_u6 as U

SITES = ['B2', 'D2', 'E2']


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    j8 = [k['uuid'] for k in b.raw['keepouts'] if k['parent'] == 'J8']
    for num in SITES:
        p = u6[num]
        hits = b.via_issues_exact(p['net'], p['xy'], *SMALL, ignore_keepouts=j8)
        if hits:
            ledger['issues'].append({'site': num, 'hits': hits})
            print('SITE BLOCKED', num, hits, flush=True)
            continue
        b.add_items([], [{'net': p['net'], 'xy': list(p['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U6.' + num, 'expected_dangling': True}])
    print('placed', SITES, flush=True)


def allowance(d):
    plan = json.loads((d / 'plan.json').read_text())
    ids = {v['board_uuid'] for v in plan['vias'] if v.get('expected_dangling')}
    rep = json.loads((d / 'drc.json').read_text())
    return sum(1 for v in rep['violations'] if v['type'] == 'via_dangling' and any(i['uuid'] in ids for i in v['items']))


run('R2-17c', 'U6_POFV_notch_check_B2_D2_E2', plan, j8={'original_outline': U.J8_RECT}, real_allowance_fn=allowance, next_step='R2-17d route B2/E2/D2 escapes')
