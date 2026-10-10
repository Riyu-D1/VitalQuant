from r2_batch import run
from r2_common import route_open
from r2route import SIGNAL_LAYERS


def plan(b, ledger, opens):
    for it in opens:
        if any('[GND]' in e['description'] and abs(e['pos']['x'] - 19.451354) < 0.01 for e in it['items']):
            route_open(b, it, 'GND U19.16/D30.2 island', [('R2-3 standalone (supervisor #01 Q5): A* F/B/In2/In3 on unmoved R2-1 board, margin 2.5 mm', 2.5, SIGNAL_LAYERS)], ledger['routes'])


run('R2-3', 'GND_island_U19', plan, next_step='R2-4 U19 dy+1.00 and C59 (+0.25,-1.00) moves with escapes')
