"""R2-17d: route the clear U6 escapes from the R2-17c POFVs: B2 PD2_INP (B1 east), E2 MISO_AFE (E1 east), D2 AFE_BG (C2->C1 east on In4, then F to C15)."""
import json, math
from r2_batch import run
from r2_rip import rip
import r2_u6 as U

APP = 'R2-17d corridor-constrained escape from J8-notched POFV (#04 topology), 60 s budget, margin 6 mm'


def plan(b, ledger, opens):
    u6 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U6'}
    U.add_guard(b)
    for ball in ('D2', 'B2', 'E2'):
        v = U.ball_via(b, u6, ball)
        if v is None:
            continue
        lay = U.CH[ball][2]
        r = U.route_ball(b, ledger, ball, v, [(APP + f' ({ball}, layers {lay})', 6.0, lay)])
        print(ball, v['net'], 'ok' if r else 'FAILED', flush=True)
        if not r:
            comps, ids = b.components(v['net'])
            comp = comps[ids[v['uuid']]]
            if not any(e['kind'] == 'pads' and math.dist(e['xy'], v['xy']) > 0.01 for e, _ in comp):
                rip(b, ledger, [v['uuid']], 'rollback U6.' + ball + ' POFV (escape failed)')
    U.drop_guard(b)


run('R2-17d', 'U6_B2_E2_D2_escapes', plan, j8={'original_outline': U.J8_RECT}, next_step='R2-17e C-channel B3/D4 + E3/E4/C4 after PD_INM in-array removal')
