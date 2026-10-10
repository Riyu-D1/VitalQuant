"""R2-51 (one attempt, U19/C59 cluster, no part move): U19.7 IR_GATE POFV (pad is outside hv_inner) after ripping the three tracks on its site;
U19.3/.4 and C59.1 exit on F through the hv band (F copper is allowed there, vias are not) after ripping the F copper of ESP_EN/TX5_EN/GND/
SWEAT_WE/VBUS_DET in the U19-north channel and the C59 area; vias only outside hv_inner; long runs prefer In5; ripped nets re-routed in-batch."""
from shapely.geometry import LineString, box
import r2route
from r2_batch import run
from r2_common import route_open
from r2_rip import rip, prune_orphans, reconnect
from r2route import SMALL, SIGNAL_LAYERS_8 as SL
from r2 import STATE, save_json, state

APP = 'R2-51 U19/C59 cluster: local F rip in hv band, POFV U19.7, In5 long runs'
CHANNEL = box(15.6, 39.6, 18.3, 42.2)
C59A = box(14.6, 39.4, 16.8, 41.6)
RIPNETS = {'ESP_EN', 'TX5_EN', 'GND', 'SWEAT_WE', 'VBUS_DET'}
r2route.LAYER_COST[12] = 0.7
s = state()
for k in list(s['attempts']):
    if k.split(':')[0] in ('ADS1292_PWDN', 'AD5940_RESET', 'IR_GATE', '+3V3', 'ESP_EN', 'TX5_EN', 'GND', 'SWEAT_WE', 'VBUS_DET', 'BIOZ_SP_PAD', 'CHG_STAT', 'EXP_INT'):
        s.setdefault('attempts_reset_R2_51', {})[k] = s['attempts'][k]
        s['attempts'][k] = []
save_json(STATE, s)


def plan(b, ledger, opens):
    p7 = b.pad('U19', '7')
    seeds = {h['uuid'] for h in b.via_issues_exact(p7['net'], p7['xy'], *SMALL) if h['kind'] == 'tracks'}
    for t in b.raw['tracks']:
        if t['layer'] == 0 and t['net'] in RIPNETS and LineString([t['a'], t['b']]).intersects(CHANNEL.union(C59A)):
            seeds.add(t['uuid'])
    nets = {b.items[u]['net'] for u in seeds}
    rip(b, ledger, list(seeds), APP)
    prune_orphans(b, ledger, nets - {'GND'})
    print('ripped', len(seeds), sorted(nets), flush=True)
    if not b.via_issues_exact(p7['net'], p7['xy'], *SMALL):
        b.add_items([], [{'net': p7['net'], 'xy': list(p7['xy']), 'diameter': SMALL[0], 'drill': SMALL[1], 'pofv': True, 'pad': 'U19.7'}])
        print('POFV U19.7', flush=True)
    for it in opens:
        t = ' '.join(e['description'] for e in it['items'])
        if any(k in t for k in ('of U19', 'of C59')):
            r = route_open(b, it, t.split('[')[1].split(']')[0] + ' R2-51', [(APP + ', margin 30 mm', 30.0, SL)], ledger['routes'], small_vias=True)
            print('OPEN', t[:70], 'ok' if r else 'FAILED', flush=True)
    reconnect(b, ledger, nets, [12.0, 36.5, 21.5, 46.5], [(APP + ' (ripped), margin 5 mm', 5.0, SL), (APP + ' (ripped), margin 12 mm', 12.0, SL)])


run('R2-51', 'U19_C59_local_rip', plan, adopt_if_fewer_opens=True, next_step='I2C_SDA U11, U6 E2/B3, U22 C5')
