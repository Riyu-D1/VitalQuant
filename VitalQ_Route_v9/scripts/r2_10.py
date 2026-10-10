"""R2-10 = second rerun (R2-9 walk bug: join not recognised) of approved R2-8 (R2-8 voided: tool error, repair stage saved DRC with default rules): R2-7 candidate as base; delete orphaned old SWEAT_WE In2 piece, its via, and the B stub down to the new join. Nothing else."""
import math
from shapely.geometry import Point, LineString
from r2 import ROOT
from r2_batch import run
from r2_rip import rip
from session import summary

BASE = ROOT / 'candidates' / 'R2-7'  # plan.json of R2-7 identifies the new SWEAT_WE join
IN2 = ['8a3dcc2b', '32e5d5e3', 'b244665f', 'f401ef24']
VIA = '7491b032'


def full(b, pre):
    return next(i for i in b.items if i.startswith(pre))


def plan(b, ledger, opens):
    seeds = [full(b, p) for p in IN2]
    via = b.items[full(b, VIA)]
    assert all(b.items[u]['net'] == 'SWEAT_WE' and b.items[u]['layer'] == 6 for u in seeds) and via['net'] == 'SWEAT_WE'
    import json as _j
    joiners = {t['board_uuid'] for t in _j.loads((BASE / 'plan.json').read_text())['tracks'] + _j.loads((BASE / 'plan.json').read_text())['vias'] if t['net'] == 'SWEAT_WE'}
    assert joiners and all(u in b.items for u in joiners), 'R2-7 SWEAT_WE route items missing'
    removed = set(seeds) | {via['uuid']}
    cur = via['xy']
    walk = []
    while True:
        nxt = [t for t in b.raw['tracks'] if t['net'] == 'SWEAT_WE' and t['layer'] == 2 and t['uuid'] not in removed and (math.dist(t['a'], cur) < 1e-4 or math.dist(t['b'], cur) < 1e-4)]
        if len(nxt) != 1:
            ledger['issues'].append(f'walk hit a junction/end ({len(nxt)}) before the R2-7 join; aborting')
            return
        t = nxt[0]
        far = t['b'] if math.dist(t['a'], cur) < 1e-4 else t['a']
        shape = b.items[t['uuid']]['shapes'][2]
        land = []
        for l in (2,):
            for idx in b.trees[l].query(shape):
                o, sh = b.solids[l][idx]
                if o['uuid'] not in joiners:
                    continue
                if o['kind'] == 'tracks':
                    land += [e for e in (o['a'], o['b']) if shape.distance(Point(e)) < 1e-3]
                elif o['kind'] == 'vias' and sh.intersects(shape):
                    land.append(o['xy'])
        if land:
            seg = LineString([far, cur])
            cut = max(land, key=lambda x: seg.project(Point(x)))
            proj = seg.interpolate(seg.project(Point(cut)))
            nb = [round(proj.x, 6), round(proj.y, 6)]
            removed.add(t['uuid'])
            walk.append(t['uuid'])
            if math.dist(nb, far) > 1e-6:
                b.add_items([{'net': 'SWEAT_WE', 'layer': 2, 'a': list(far), 'b': nb, 'width': t['width']}], [])
            ledger['join'] = {'segment': t['uuid'], 'shortened_to': nb}
            break
        removed.add(t['uuid'])
        walk.append(t['uuid'])
        cur = far
        if any(o['net'] == 'SWEAT_WE' and o['kind'] in ('pads', 'vias') and sh.distance(Point(cur)) < 1e-3 for idx in b.trees[2].query(Point(cur).buffer(0.01)) for o, sh in [b.solids[2][idx]]):
            ledger['issues'].append('walk reached a pad/via before the R2-7 join; aborting')
            return
        if len(walk) > 30:
            ledger['issues'].append('walk exceeded 30 segments without meeting the R2-7 join; aborting')
            return
    ledger['b_stub_walk'] = walk
    print('B stub segments removed', len(walk), 'join', ledger.get('join'), flush=True)
    rip(b, ledger, list(removed), 'R2-8 (approved): orphaned old SWEAT_WE In2 piece / via / B stub after R2-7 reroute')


def extra(d):
    s = summary(d / 'drc.json')
    t, v = s['types'].get('track_dangling', 0), s['types'].get('via_dangling', 0)
    ok = s['unconnected'] <= 30 and s['real'] <= 239 and t <= 2 and v <= 1
    return {'pass': ok, 'text': f"approved gate unconnected {s['unconnected']}<=30, real {s['real']}<=239, track_dangling {t}<=2, via_dangling {v}<=1 -> {'PASS' if ok else 'FAIL'}"}


run('R2-10', 'SWEAT_WE_orphan_cleanup_rerun2', plan, extra_check=extra, next_step='R2-11 U22 via-in-pad + U18 dx -0.31 nudge with authorised local rip-up')
