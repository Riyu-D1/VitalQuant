"""Incremental 8L routing with a local blocker rip-up fallback (approach change for nets whose plain A* failed):
route the open; if it fails, rip the foreign TRACKS that wall in either endpoint (frontier within 0.25 mm of the endpoint's free pocket),
route the open again, then re-route the ripped nets; if any ripped net cannot be re-routed, roll the whole attempt back.
usage: r2_inc2.py BATCH TAG key1,key2,..."""
import math
import sys
from shapely.geometry import Point
from r2 import STATE, save_json, state
from r2_batch import run
from r2_common import route_open, route_entries, open_comps
from r2_rip import rip, reconnect
from r2route import SIGNAL_LAYERS_8 as SL
import os
if os.environ.get('WITH_IN4'):
    SL = SL + (10,)

BATCH, TAG, KEYS = sys.argv[1], sys.argv[2], sys.argv[3].split(',')
APP = BATCH + ' 8L local-blocker rip fallback' + (' (+In4 local)' if os.environ.get('WITH_IN4') else '')
PLANES = {'GND', '+3V3', 'VBAT_SYS'}
s = state()
for k in list(s['attempts']):
    if any(k.startswith(x.strip('[]') + ':') for x in KEYS):
        s.setdefault('attempts_reset_' + BATCH, {})[k] = s['attempts'][k]
        s['attempts'][k] = []
save_json(STATE, s)


def pocket_blockers(b, net, entries):
    out = set()
    for e, shapes in entries:
        if e['kind'] != 'pads':
            continue
        p = Point(e['xy'])
        for l, sh in shapes.items():
            if l not in SL:
                continue
            bounds = [e['xy'][0] - 2.5, e['xy'][1] - 2.5, e['xy'][0] + 2.5, e['xy'][1] + 2.5]
            al = b.allowed_local(net, l, bounds)
            comp = next((g for g in getattr(al, 'geoms', [al]) if g.distance(p) < 0.06), None)
            if comp is None or comp.area > 6.0:
                continue
            for idx in b.trees[l].query(comp.buffer(0.25)):
                it, osh = b.solids[l][idx]
                if it['kind'] == 'tracks' and it['net'] != net and it['net'] not in PLANES and osh.distance(comp) < 0.2:
                    out.add(it['uuid'])
    return out


def plan(b, ledger, opens):
    todo = [it for it in opens if any(k in ' '.join(e['description'] for e in it['items']) for k in KEYS)]
    for it in todo:
        t = ' '.join(e['description'] for e in it['items'])
        net = t.split('[')[1].split(']')[0]
        label = net + ' inc2' + ('b' if os.environ.get('WITH_IN4') else '')
        r = route_open(b, it, label, [(APP + ' plain A*, margin 12 mm', 12.0, SL)], ledger['routes'], small_vias=True)
        if r:
            print('OPEN', label, 'ok (plain)', flush=True)
            continue
        info = open_comps(b, it)
        if not info or None in info[2]:
            continue
        _, comps, (ka, kb) = info
        seeds = pocket_blockers(b, net, comps[ka]) | pocket_blockers(b, net, comps[kb])
        if not seeds:
            print('OPEN', label, 'FAILED (no pocket blockers found)', flush=True)
            continue
        new_before = {u for u in b.items if str(u).startswith('new-')}
        recs = [dict(b.items[u]) for u in seeds]
        def padcomps(n):
            c2, _ = b.components(n)
            return len([c for c in c2.values() if any(e['kind'] == 'pads' for e, _ in c)])
        before_cnt = {r_['net']: padcomps(r_['net']) for r_ in recs}
        n_removed = len(ledger['removed'])
        late = list(ledger.get('late_remove', []))
        rip(b, ledger, list(seeds), APP + ': walls in ' + net)
        ripped_nets = {r_['net'] for r_ in recs}
        r = route_open(b, it, label, [(APP + ' after local blocker rip, margin 12 mm', 12.0, SL)], ledger['routes'], small_vias=True)
        ok = bool(r)
        if ok:
            reconnect(b, ledger, ripped_nets, [min(x['a'][0] for x in recs) - 1, min(x['a'][1] for x in recs) - 1, max(x['a'][0] for x in recs) + 1, max(x['a'][1] for x in recs) + 1], [(APP + ' (re-route ripped walls), margin 6 mm', 6.0, SL), (APP + ' (re-route ripped walls), margin 14 mm', 14.0, SL)])
            for n in ripped_nets:
                if padcomps(n) > before_cnt[n]:
                    ok = False
        if not ok:
            added = [u for u in b.items if str(u).startswith('new-') and u not in new_before]
            b.remove_items(added)
            b.add_items([{k: v for k, v in x.items() if k not in ('shapes', 'kind')} for x in recs if x['kind'] == 'tracks'], [])
            ledger['removed'] = ledger['removed'][:n_removed]
            ledger['late_remove'] = late
            print('OPEN', label, 'FAILED, rolled back', flush=True)
        else:
            print('OPEN', label, 'ok (after local rip of', len(seeds), 'segments)', flush=True)


run(BATCH, TAG, plan, adopt_if_fewer_opens=True, next_step='next incremental batch')
