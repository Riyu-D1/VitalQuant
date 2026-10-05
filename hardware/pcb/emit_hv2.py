#!/usr/bin/env python3
"""Reshuffle pass for caged HV nets.

When a net's source pad is pocket-sealed by an already-emitted HV sibling,
rip that sibling's tracks (record it), route the caged net, then re-route
the sibling around the new copper. Bounded passes.

usage: emit_hv2.py <board> <pro> [NET,NET ...]   # open-net list
"""
import sys, time, shutil
sys.path.insert(0, '.')
import pcbnew
import route_all as ra
import route_hv as rh
import route_hv_all as rha
from route_flood import pad_cells, bfs, DIRS8
from collections import deque
import re, ast

IN = sys.argv[1]
PRO = sys.argv[2]
ONLY = set(sys.argv[3].split(',')) if len(sys.argv) > 3 else None

src = open('emit_hv.py').read()
DEAD = ast.literal_eval(re.search(r"DEAD = (\{.*?\n\})", src, re.S).group(1))

b = pcbnew.LoadBoard(IN)
assert ra.class_of(b, 'ECG1_PAD') == 'HV_ELECTRODE'
obs = ra.Obstacles(b)
guard = rh.SlotGuard(obs, b)
guard.wrap(obs)
rha.sibling_wrap(b, obs, guard)
obs.dead |= DEAD

PLANS = {n: (s, f) for n, s, f in rha.PLANS}
HV = set(PLANS)


def flood(seed, net, cls, hw, cap=2000000):
    seen = set(seed)
    q = deque(seed)
    while q:
        u = q.popleft()
        ux, uy, ul = u
        x, y = ux * ra.GRID, uy * ra.GRID
        for dx, dy in DIRS8:
            v = (ux + dx, uy + dy, ul)
            if v in seen:
                continue
            fx, fy = x + dx * ra.GRID, y + dy * ra.GRID
            if not (3 < fx < 43 and 3 < fy < 67):
                continue
            if obs.cell_blocked(fx, fy, ul, net, cls, hw):
                continue
            if dx and dy and (
                    obs.cell_blocked(fx, y, ul, net, cls, hw)
                    or obs.cell_blocked(x, fy, ul, net, cls, hw)):
                continue
            seen.add(v)
            q.append(v)
        if obs.own_via_at(x, y, net) or obs.via_fits(x, y, net, cls):
            for oly in ra.ROUTE_LAYERS:
                if oly == ul:
                    continue
                v = (ux, uy, oly)
                if v in seen or obs.cell_blocked(
                        x, y, oly, net, cls, hw):
                    continue
                seen.add(v)
                q.append(v)
    return seen


def cage_blockers(seen, net):
    """Nets whose segs/vias sit adjacent to the reachable pocket."""
    from collections import Counter
    cnt = Counter()
    for cx, cy, cl in seen:
        for dx, dy in DIRS8:
            x, y = (cx + dx) * ra.GRID, (cy + dy) * ra.GRID
            for sx0, sy0, sx1, sy1, sw, sn in obs._cand_segs(x, y, cl):
                if sn != net and ra.seg_dist(sx0, sy0, sx1, sy1, x, y) < 0.8:
                    cnt[sn] += 1
            for vx, vy, vr, vn in obs._cand_vias(x, y):
                if vn != net and (vx - x) ** 2 + (vy - y) ** 2 < 1.4:
                    cnt[vn] += 1
    return cnt


def rip_net(net):
    n = 0
    for t in list(b.GetTracks()):
        if t.GetNetname() == net:
            b.Remove(t)
            n += 1
    return n


CELLS = {}  # net -> (seed, goal) cached before any board mutation


def try_route(net, cls, hw):
    seed, goals = CELLS[net]
    t0 = time.time()
    path, ncell = bfs(obs, seed, goals, net, cls, hw, cap=1500000)
    if not path:
        return None, ncell
    sp = ra.simplify(path)
    if not ra.path_clear(obs, sp, net, cls, hw):
        return None, ncell
    if rh.path_wall_violation(guard, sp, hw):
        return None, ncell
    ra.emit(b, obs, sp, net, hw * 2)
    return sp, ncell


def save():
    pcbnew.SaveBoard(IN, b)
    dst = IN.replace('.kicad_pcb', '.kicad_pro')
    if dst != PRO:
        shutil.copy(PRO, dst)


opens = [n for n in PLANS if ONLY is None or n in ONLY]
ripped_once = set()

# cache pad cells + classes for every net we may touch (opens AND
# potential cager siblings) BEFORE any board mutation — pcbnew proxies
# break after Remove().
for n in PLANS:
    (sr, sn), pf = PLANS[n]
    _, (gr, gn) = pf()
    c = ra.class_of(b, n)
    CELLS[n] = (pad_cells(b, sr, sn, n, c, obs, 0.06),
                pad_cells(b, gr, gn, n, c, obs, 0.06))
CLASSES = {n: ra.class_of(b, n) for n in PLANS}

for p in range(4):
    progress = False
    for net in list(opens):
        cls = CLASSES[net]
        for hw in (0.125, 0.06):
            seed = CELLS[net][0]
            seen = flood(seed, net, cls, hw)
            blk = cage_blockers(seen, net)
            hv_blk = [n for n, c in blk.most_common()
                      if n in HV and n not in ripped_once]
            sp, nc = try_route(net, cls, hw)
            if sp:
                print(f'[{net}] EMITTED {len(sp)}segs hw={hw}',
                      flush=True)
                save()
                opens.remove(net)
                progress = True
                break
            if not hv_blk:
                continue
            # rip the dominant HV cager, retry, queue it for re-route.
            # keep it in obs.dead: its bucket entries are now stale.
            sib = hv_blk[0]
            n = rip_net(sib)
            ripped_once.add(sib)
            obs.dead.add(sib)
            if sib not in opens:
                opens.append(sib)
            print(f'[{net}] caged by {sib} ({n} items ripped), retry',
                  flush=True)
            sp, nc = try_route(net, cls, hw)
            if sp:
                print(f'[{net}] EMITTED {len(sp)}segs after rip {sib}',
                      flush=True)
                save()
                opens.remove(net)
                progress = True
                break
            print(f'[{net}] still blocked after rip {sib}', flush=True)
        else:
            print(f'[{net}] OPEN (pocket {len(seen)}c)', flush=True)
    if not opens or not progress:
        break
    print(f'== reshuffle pass {p} done, {len(opens)} open ==', flush=True)

print(f'== RESHUFFLE DONE, open={sorted(opens)}', flush=True)
save()
