#!/usr/bin/env python3
"""Route ONE open HV net. Loads fresh, floods, rips the dominant HV
cager if needed (recording it to a queue file for the driver), routes,
saves. Process-per-net avoids pcbnew proxy lifetime bugs.

usage: emit_one.py <board> <pro> <net> <ripped_queue.txt>
exit 0=emitted-or-connected 2=still-open 3=caged(ripped, rerun both)
"""
import sys, time, shutil, re, ast
sys.path.insert(0, '.')
import pcbnew
import route_all as ra
import route_hv as rh
import route_hv_all as rha
from route_flood import pad_cells, bfs, DIRS8
from collections import deque, Counter

IN, PRO, NET, RIPQ = sys.argv[1:5]

src = open('emit_hv.py').read()
DEAD = ast.literal_eval(
    re.search(r"DEAD = (\{.*?\n\})", src, re.S).group(1))

b = pcbnew.LoadBoard(IN)
assert ra.class_of(b, 'ECG1_PAD') == 'HV_ELECTRODE'
obs = ra.Obstacles(b)
guard = rh.SlotGuard(obs, b)
guard.wrap(obs)
rha.sibling_wrap(b, obs, guard)
obs.dead |= DEAD | set(l.strip() for l in open(RIPQ) if l.strip())

PLANS = {n: (s, f) for n, s, f in rha.PLANS}
HV = set(PLANS)
(sref, sname), planf = PLANS[NET]
cls = ra.class_of(b, NET)
_, (gref, gname) = planf()


def save():
    pcbnew.SaveBoard(IN, b)
    dst = IN.replace('.kicad_pcb', '.kicad_pro')
    if dst != PRO:
        shutil.copy(PRO, dst)


def cage_census(seed, hw):
    seen = set(seed)
    q = deque(seed)
    while q:
        ux, uy, ul = q.popleft()
        x, y = ux * ra.GRID, uy * ra.GRID
        for dx, dy in DIRS8:
            v = (ux + dx, uy + dy, ul)
            if v in seen:
                continue
            fx, fy = x + dx * ra.GRID, y + dy * ra.GRID
            if not (3 < fx < 43 and 3 < fy < 67):
                continue
            if obs.cell_blocked(fx, fy, ul, NET, cls, hw):
                continue
            if dx and dy and (obs.cell_blocked(fx, y, ul, NET, cls, hw)
                              or obs.cell_blocked(x, fy, ul, NET, cls, hw)):
                continue
            seen.add(v)
            q.append(v)
        if obs.own_via_at(x, y, NET) or obs.via_fits(x, y, NET, cls):
            for oly in ra.ROUTE_LAYERS:
                v = (ux, uy, oly)
                if oly != ul and v not in seen and not obs.cell_blocked(
                        x, y, oly, NET, cls, hw):
                    seen.add(v)
                    q.append(v)
    cnt = Counter()
    for cx, cy, cl in seen:
        for dx, dy in DIRS8:
            x, y = (cx + dx) * ra.GRID, (cy + dy) * ra.GRID
            for sx0, sy0, sx1, sy1, sw, sn in obs._cand_segs(x, y, cl):
                if sn != NET and ra.seg_dist(sx0, sy0, sx1, sy1, x, y) < 0.8:
                    cnt[sn] += 1
            for vx, vy, vr, vn in obs._cand_vias(x, y):
                if vn != NET and (vx - x) ** 2 + (vy - y) ** 2 < 1.4:
                    cnt[vn] += 1
    return seen, cnt


def try_route(hw):
    seed = pad_cells(b, sref, sname, NET, cls, obs, hw)
    goals = pad_cells(b, gref, gname, NET, cls, obs, hw)
    t0 = time.time()
    path, ncell = bfs(obs, seed, goals, NET, cls, hw, cap=1500000)
    if not path:
        return None
    sp = ra.simplify(path)
    if not ra.path_clear(obs, sp, NET, cls, hw):
        return None
    if rh.path_wall_violation(guard, sp, hw):
        return None
    ra.emit(b, obs, sp, NET, hw * 2)
    return sp


for hw in (0.125, 0.06, 0.045):
    sp = try_route(hw)
    if sp:
        print(f'[{NET}] EMITTED {len(sp)}segs hw={hw}', flush=True)
        save()
        sys.exit(0)
    # caged? census the pocket at this width — but only rip as the
    # LAST resort, after the narrowest width also fails
    if hw == 0.045:
        seed = pad_cells(b, sref, sname, NET, cls, obs, hw)
        seen, blk = cage_census(seed, hw)
        hv_blk = [n for n, c in blk.most_common()
                  if n in HV and n not in DEAD]
        if hv_blk:
            sib = hv_blk[0]
            n = 0
            for t in list(b.GetTracks()):
                if t.GetNetname() == sib:
                    b.Remove(t)
                    n += 1
            open(RIPQ, 'a').write(sib + '\n')
            print(f'[{NET}] caged by {sib} ({n} ripped)', flush=True)
            save()
            sys.exit(3)
        print(f'[{NET}] pocket {len(seen)}c, sealed', flush=True)

sys.exit(2)
