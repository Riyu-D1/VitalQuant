"""R2-24 (SUPERVISOR B): sensor island mirrored pads. Rotate U11 (B, locked sensor, stays in place inside the island) by 180 deg
(180 -> 0) so U11.5 (+3V3) sits under U20.5 (+3V3, same net) and a 0.4/0.2 POFV via-in-pad there stitches U11.5, U20.5, the F.Cu +3V3
island and the In4 +3V3 plane; U11.3 (TMP117_ALERT) gets a 0.5 mm B dog-bone to a 0.4/0.2 via at (19.15,49.25) on free opposite-side
space. All U11 nets re-attached. Found by scripts/u11_search2.py (logs/u11_search2.json)."""
from shapely.geometry import LineString
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2_common import route_entries
from r2route import SIGNAL_LAYERS_8 as SIGNAL_LAYERS
from shapely.geometry import box

MOVES = [{'ref': 'U11', 'from': [20.2, 50.4], 'to': [20.2, 50.4], 'rotate_to': 0.0, 'allow_locked': True, 'detach': True,
          'reason': 'SUPERVISOR (B): U11/U20 mirrored on the sensor island; rotating U11 180 deg in place puts U11.5 under same-net U20.5 so a POFV via-in-pad can stitch +3V3, and frees a dog-bone site for TMP117_ALERT. Stays inside the sensor island/cluster, skin side unchanged.'}]
ISLAND = box(17.0, 46.0, 23.3, 54.0)
APP = 'R2-34 U11 rotated 180 in place; POFV in-pad +3V3 stitch; TMP117 dog-bone via; re-attach U11 nets; 60 s budget'


def plan(b, ledger, opens):
    isl = box(17.2, 46.19, 23.1, 53.6)
    local = [u for u, it in b.items.items() if it['kind'] in ('tracks', 'vias') and it['net'] in ('I2C_SCL', 'I2C_SDA', 'TMP117_ALERT') and any(sh.intersects(isl) for sh in it['shapes'].values())]
    rip(b, ledger, local, 'R2-33: island-local I2C/TMP117 copper re-routed around the rotated U11')
    print('island rip', len(local), flush=True)
    p5, p3 = b.pad('U11', 5), b.pad('U11', 3)
    v5 = {'net': '+3V3', 'xy': list(p5['xy']), 'diameter': 0.4, 'drill': 0.2, 'pofv': True, 'pad': 'U11.5/U20.5'}
    hits = b.via_issues_exact('+3V3', v5['xy'], 0.4, 0.2)
    print('U11.5 POFV', p5['xy'], hits, flush=True)
    vsite = [19.15, 49.25]
    v3 = {'net': 'TMP117_ALERT', 'xy': vsite, 'diameter': 0.4, 'drill': 0.2, 'pofv': False}
    hits3 = b.via_issues_exact('TMP117_ALERT', vsite, 0.4, 0.2)
    stub = {'net': 'TMP117_ALERT', 'layer': 2, 'a': list(p3['xy']), 'b': vsite, 'width': 0.1016}
    stub_ok = b.allowed_local('TMP117_ALERT', 2, [18.0, 48.5, 20.5, 50.5]).covers(LineString([stub['a'], stub['b']]))
    print('U11.3', p3['xy'], 'dog-bone via', hits3, 'stub ok', stub_ok, flush=True)
    if hits or hits3 or not stub_ok:
        ledger['issues'].append({'u11_5': hits, 'u11_3': hits3, 'stub_ok': stub_ok})
        return
    b.add_items([stub], [v5, v3])
    nets = {p['net'] for p in b.raw['pads'] if p['ref'] == 'U11'} | {r['net'] for m in ledger['moves'] for r in m.get('detached', [])}
    nets.discard('')
    prune_orphans(b, ledger, nets)
    reconnect(b, ledger, nets, [16.5, 45.5, 24.0, 54.5], [(APP + ', margin 4 mm', 4.0, SIGNAL_LAYERS), (APP + ', margin 10 mm', 10.0, SIGNAL_LAYERS)])
    # TMP117_ALERT long run to R65 and the F.Cu +3V3 island stitch are covered by reconnect (comps touching the island)


if __name__ == '__main__':
    run('R2-34', 'U11_rotate_island_stitch_v2', plan, moves=MOVES, moved=('U11',), adopt_if_fewer_opens=True, next_step='R2-29 (C) C59 rotations / regional rip x12-20 y36-44')
