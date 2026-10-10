from r2_common import route_open, open_comps
from r2route import SIGNAL_LAYERS


def endpoints_text(it):
    return ' '.join(e['description'] for e in it['items'])


def route_all(b, ledger, opens, select, approaches, label_fn=None, order_key=None):
    chosen = [it for it in opens if select(it)]
    if order_key:
        chosen.sort(key=order_key)
    for it in chosen:
        label = label_fn(it) if label_fn else endpoints_text(it)[:80]
        route_open(b, it, label, approaches, ledger['routes'])


def span(it):
    a, c = it['items'][0]['pos'], it['items'][-1]['pos']
    return abs(a['x'] - c['x']) + abs(a['y'] - c['y'])
