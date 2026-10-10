"""R2-7: U7 via-in-pad retry (SUPERVISOR_R2_02 priority 2): free the E6 site for SWEAT_WE, route ripped nets first."""
import math
from shapely.geometry import box
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect, trim_stubs
from r2_common import route_entries, open_comps
from r2route import route, SIGNAL_LAYERS, SMALL
import r2_6

APP = 'R2-7 U7 POFV retry: E6 GND via replaced by F ball-to-ball GND link (frees enclosed SWEAT_WE escape via E6/F6/G6), ripped nets routed before fan-out, then A* margin 3 mm'
APPROACHES = [(APP, 3.0, SIGNAL_LAYERS), (APP + ', wide detour margin 8 mm', 8.0, SIGNAL_LAYERS)]
WINDOW = r2_6.WINDOW


def plan(b, ledger, opens):
    u7 = {p['number']: p for p in b.raw['pads'] if p['ref'] == 'U7'}
    align = (u7['A1']['xy'][0] % 0.05, u7['A1']['xy'][1] % 0.05)
    # normalise U7 vias (same as R2-6) except E6, which is removed
    e6 = next(v for v in b.raw['vias'] if v['net'] == 'GND' and math.dist(v['xy'], u7['E6']['xy']) < 0.06)
    rip(b, ledger, [e6['uuid']], 'E6 GND via-in-pad removed: it seals SWEAT_WE (C5); E6 ball re-linked on F to E5/D5')
    r2_6.normalise(b, ledger, u7)
    # GND F link E6 -> E5 -> D5 (same-net ball-to-ball on F)
    pts = [u7['E6']['xy'], u7['E5']['xy'], u7['D5']['xy']]
    tracks = [{'net': 'GND', 'layer': 0, 'a': list(a), 'b': list(c), 'width': 0.1016} for a, c in zip(pts, pts[1:])]
    from shapely.geometry import LineString
    ok = all(b.allowed_local('GND', 0, [36.5, 10.3, 37.9, 11.5]).covers(LineString([t['a'], t['b']])) for t in tracks)
    if not ok:
        ledger['issues'].append('E6-E5-D5 GND F link not legal')
        return
    b.add_items(tracks, [])
    blockers = r2_6.blockers(b, ledger, u7)
    for u, why in blockers.items():
        rip(b, ledger, [u], 'PROMPT_R2 R2-3/SUPERVISOR_R2_02: ' + why)
    print('ripped', sorted({r['net'] for r in ledger['removed']}), flush=True)
    placed = r2_6.place(b, ledger, u7)
    ripped = {r['net'] for r in ledger['removed']} - {'GND'}
    prune_orphans(b, ledger, ripped)
    reconnect(b, ledger, ripped, WINDOW, [(APP + ' (ripped net first)', 3.0, SIGNAL_LAYERS)])
    fan = r2_6.fanout(b, placed, align)
    r2_6.route_opens(b, ledger, opens, align, APPROACHES)
    reconnect(b, ledger, ripped | {'GND'}, WINDOW, [(APP + ' (ripped net second pass)', 5.0, SIGNAL_LAYERS)])
    r2_6.rollback(b, ledger, placed, fan)
    trim_stubs(b, ledger, ripped | {'GND'} | {v['net'] for v in placed.values()}, WINDOW)


run('R2-7', 'U7_via_in_pad_retry', plan, next_step='R2-8 U22 via-in-pad + U18 nudge')
