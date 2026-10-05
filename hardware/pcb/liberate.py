#!/usr/bin/env python3
"""Surgical liberator for sealed-pocket open pads.

For each net in the netlist: find pad groups not yet tied to the net's
copper island. For each such pad, flood the free space; if the flood dies
enclosed, census the boundary — count blocking TRACKS/VIAS per foreign net
(pads, walls, keepouts are never ripped). If <=MAXRIPS choke items explain
the seal, delete exactly those items (recording their nets for re-heal),
retry the BFS, emit the path, then re-heal every harmed net.

This KiCad build's SWIG proxies die unpredictably after board mutation,
so ALL board enumeration happens once at load: pad groups, net classes and
a track snapshot (lane_rip-proven: item proxies survive for Remove()).
Live copper state comes from obs's own seg/via buckets, which ra.emit and
the rip-rebuild keep current — GetTracks()/GetFootprints() are never
called again after the initial index.

usage: liberate.py <board> <netlist-file> [--max-rips N]   (default 4)
saves in place after each net. Prints one status line per net.
"""
import sys
import time
sys.path.insert(0, '.')
import pcbnew
import route_all as ra
import route_hv as rh
from route_flood import bfs, DIRS8
from collections import deque, Counter

IN, NETFILE = sys.argv[1], sys.argv[2]
MAXRIPS = 4
for i, a in enumerate(sys.argv[3:], 3):
    if a.startswith('--max-rips'):
        MAXRIPS = int(a.split('=')[1] if '=' in a else sys.argv[i + 1])

nets = [l.strip() for l in open(NETFILE) if l.strip()]
import route_hv_all as rha
HV_PROTECT = {n for n, s, f in rha.PLANS}     # electrode routes never ripped
# In4 doubles as an escape-routing layer under BGA fields (zone refill yields
# clearance around tracks there — documented waiver).
if pcbnew.In4_Cu not in ra.ROUTE_LAYERS:
    ra.ROUTE_LAYERS = tuple(list(ra.ROUTE_LAYERS) + [pcbnew.In4_Cu])
import route_flood as _rf
_rf.__dict__  # route_flood reads ra.ROUTE_LAYERS dynamically — patched above
b = pcbnew.LoadBoard(IN)
obs = ra.Obstacles(b)
guard = rh.SlotGuard(obs, b)
guard.wrap(obs)
GRID = ra.GRID

# ---- one-shot snapshots (never touch board containers again) ----------
ALL_TRACKS = list(b.GetTracks())          # proxies used ONLY for Remove()
NETOBJ = {n.GetNetname(): n for n in b.GetNetsByName().values()}
GROUPS = {}                               # net -> [(rect, cells)]
for fp in b.GetFootprints():
    try:
        pads = list(fp.Pads())
    except Exception:
        continue
    for p in pads:
        try:
            nm = str(p.GetNetname())
            bb = p.GetBoundingBox()
            rect = (bb.GetLeft() / 1e6, bb.GetTop() / 1e6,
                    bb.GetRight() / 1e6, bb.GetBottom() / 1e6)
            cells = set()
            for l in p.GetLayerSet().Seq():
                if l not in obs.buckets:
                    continue
                for gx in range(int(rect[0] / GRID), int(rect[2] / GRID) + 1):
                    for gy in range(int(rect[1] / GRID),
                                    int(rect[3] / GRID) + 1):
                        cells.add((gx, gy, l))
            if cells:
                GROUPS.setdefault(nm, []).append((rect, cells))
        except Exception:
            continue


def own_cells(net):
    """Copper cells of `net` straight from the live obs index."""
    out = set()
    for ly, buckets in obs.seg_buckets.items():
        for bl in buckets.values():
            for x0, y0, x1, y1, w, n in bl:
                if n != net:
                    continue
                sp = ra.simplify([(x0, y0, ly), (x1, y1, ly)])
                out.update(ra.raster_cells(sp))
    for bl in obs.via_buckets.values():
        for x, y, r, n in bl:
            if n != net:
                continue
            cx, cy = x / GRID, y / GRID
            rr = int(r / GRID) + 1
            for dx in range(-rr, rr + 1):
                for dy in range(-rr, rr + 1):
                    for ly in ra.ROUTE_LAYERS:
                        out.add((int(cx) + dx, int(cy) + dy, ly))
    out |= MYVIAS.get(net, set())
    return out


def census_cell(x, y, layer, my_net, my_cls, hw):
    """Like obs.cell_blocked but returns the blocking item identities."""
    out = []
    in_bga = any(x0 <= x <= x1 and y0 <= y <= y1
                 for x0, y0, x1, y1 in obs.bga_rects)
    for x0, y0, x1, y1, onet, tag in obs._cand_rects(x, y, layer):
        if onet == my_net or onet in obs.dead:
            continue
        clr = 0.05 if tag == 'keepout' else obs.pair_clearance(
            my_cls, obs._cls.get(onet, 'Default'), in_bga)
        if x0 - hw - clr <= x <= x1 + hw + clr and \
           y0 - hw - clr <= y <= y1 + hw + clr:
            out.append(('pad' if tag != 'keepout' else 'keepout',
                        onet or tag, (x0, y0, x1, y1)))
    for x0, y0, x1, y1 in obs._cand_walls(x, y):
        if x0 - hw <= x <= x1 + hw and y0 - hw <= y <= y1 + hw:
            out.append(('wall', 'wall', (x0, y0, x1, y1)))
    for sx0, sy0, sx1, sy1, sw, snet in obs._cand_segs(x, y, layer):
        if snet == my_net or snet in obs.dead:
            continue
        rr = sw + obs.pair_clearance(my_cls, obs._cls.get(snet, 'Default'),
                                     in_bga) + hw
        if ra.seg_dist(sx0, sy0, sx1, sy1, x, y) <= rr:
            out.append(('seg', snet, (sx0, sy0, sx1, sy1)))
    for vx, vy, vr, vnet in obs._cand_vias(x, y):
        if vnet == my_net or vnet in obs.dead:
            continue
        rr = vr + obs.pair_clearance(my_cls, obs._cls.get(vnet, 'Default'),
                                     in_bga) + hw
        if (vx - x) ** 2 + (vy - y) ** 2 <= rr * rr:
            out.append(('via', vnet, (vx, vy)))
    return out


def frontier_choke(seed, my_net, my_cls, hw, cap=120000):
    """Flood free space from seed; tally foreign choke items on frontier."""
    seen = set(seed)
    q = deque(seed)
    tally = Counter()
    n = 0

    def choke(nx, ny, nl):
        for it in census_cell(nx, ny, nl, my_net, my_cls, hw):
            tally[it] += 1

    while q and n < cap:
        n += 1
        gx, gy, ly = q.popleft()
        x, y = gx * GRID, gy * GRID
        for dx, dy in DIRS8:
            nc = (gx + dx, gy + dy, ly)
            if nc in seen:
                continue
            nx, ny = x + dx * GRID, y + dy * GRID
            if obs.cell_blocked(nx, ny, ly, my_net, my_cls, hw):
                choke(nx, ny, ly)
                continue
            if dx and dy and (
                    obs.cell_blocked(nx, y, ly, my_net, my_cls, hw)
                    or obs.cell_blocked(x, ny, ly, my_net, my_cls, hw)):
                choke(nx, ny, ly)
                continue
            seen.add(nc)
            q.append(nc)
        if obs.own_via_at(x, y, my_net) or obs.via_fits(x, y, my_net, my_cls):
            for oly in ra.ROUTE_LAYERS:
                if oly == ly:
                    continue
                nc = (gx, gy, oly)
                if nc in seen or obs.cell_blocked(
                        x, y, oly, my_net, my_cls, hw):
                    if nc not in seen:
                        choke(x, y, oly)
                    continue
                seen.add(nc)
                q.append(nc)
        else:
            for oly in ra.ROUTE_LAYERS:
                choke(x, y, oly)
    return tally, seen


DELETED = set()           # id()s of removed items — never Remove twice
RIPLOG = open(IN + '.rips', 'a')          # merge replays these centrally
PATCH_R = (0.7, 1.2, 2.0)                 # escalating patch-rip radii (mm)


def delete_item(kind, geom):
    """Remove the board item matching a blocker identity. Returns net."""
    match = None
    for t in ALL_TRACKS:
        if id(t) in DELETED:
            continue
        try:
            nm0 = None
            try:
                nm0 = str(t.GetNetname())
            except Exception:
                continue
            if nm0 in HV_PROTECT:
                continue
            if kind == 'via' and t.Type() == pcbnew.PCB_VIA_T:
                p = t.GetPosition()
                if abs(p.x / 1e6 - geom[0]) < 0.02 \
                        and abs(p.y / 1e6 - geom[1]) < 0.02:
                    match = t
                    break
            elif kind == 'seg' and t.Type() != pcbnew.PCB_VIA_T:
                s, e = t.GetStart(), t.GetEnd()
                if (abs(s.x / 1e6 - geom[0]) < 0.02
                        and abs(s.y / 1e6 - geom[1]) < 0.02
                        and abs(e.x / 1e6 - geom[2]) < 0.02
                        and abs(e.y / 1e6 - geom[3]) < 0.02):
                    match = t
                    break
        except Exception:
            continue
    if match is None:
        return None
    try:
        nm = str(match.GetNetname())
        b.Remove(match)
    except Exception:
        return None
    DELETED.add(id(match))
    RIPLOG.write(f'{kind} {nm} {" ".join(f"{v:.4f}" for v in geom)}\n')
    RIPLOG.flush()
    return nm


MYVIAS = {}      # net -> set of cells contributed by emitted blind microvias


def microvia_escape(net, rect, cells):
    """Pad is pocketed: try a blind microvia touching the pad that drops the
    net onto the adjacent inner layer (B->In4 / F->In1) where the flood is
    free. Returns True if a via was emitted."""
    pad_lys = {l for _, _, l in cells}
    spans = []
    if pcbnew.B_Cu in pad_lys:
        spans.append((pcbnew.B_Cu, pcbnew.In4_Cu))
    if pcbnew.F_Cu in pad_lys:
        spans.append((pcbnew.F_Cu, pcbnew.In1_Cu))
    if not spans:
        return False
    import math as _m
    cx = (rect[0] + rect[2]) / 2
    cy = (rect[1] + rect[3]) / 2
    pw2 = (rect[2] - rect[0]) / 2 + 0.10     # via ring may overlap pad edge
    ph2 = (rect[3] - rect[1]) / 2 + 0.10
    VR, VD = 0.15, 0.10                     # 0.3mm pad / 0.1mm laser drill
    cands = [(0.0, 0.0)]
    for d in (0.10, 0.2, 0.28):
        for ang in range(0, 360, 45):
            cands.append((d * _m.cos(_m.radians(ang)),
                          d * _m.sin(_m.radians(ang))))
    netobj = NETOBJ.get(net) or b.FindNet(net)
    for tly, ily in spans:
        for dx, dy in cands:
            x, y = cx + dx, cy + dy
            # via ring must still kiss the pad on the pad layer
            if not (rect[0] - VR - 0.02 <= x <= rect[2] + VR + 0.02
                    and rect[1] - VR - 0.02 <= y <= rect[3] + VR + 0.02):
                continue
            if any(obs.cell_blocked(x, y, ly, net, cls_of(net), VR)
                   for ly in (tly, ily)):
                continue
            v = pcbnew.PCB_VIA(b)
            v.SetPosition(pcbnew.VECTOR2I(ra.mm(x), ra.mm(y)))
            v.SetWidth(ra.mm(VR * 2))
            v.SetDrill(ra.mm(VD))
            v.SetNet(netobj)
            v.SetViaType(pcbnew.VIATYPE_MICROVIA)
            v.SetLayerPair(tly, ily)
            b.Add(v)
            # index as copper on exactly its two span layers
            for ly in (tly, ily):
                obs._put_rect(ly, (x - VR, y - VR, x + VR, y + VR, net, 'vip'))
            gx, gy = int(x / GRID), int(y / GRID)
            rr = int(VR / GRID) + 1
            got = MYVIAS.setdefault(net, set())
            for ddx in range(-rr, rr + 1):
                for ddy in range(-rr, rr + 1):
                    got.add((gx + ddx, gy + ddy, tly))
                    got.add((gx + ddx, gy + ddy, ily))
            print(f'  {net}: microvia {tly}->{ily} @({x:.2f},{y:.2f})',
                  flush=True)
            return True
    return False


def cls_of(net):
    return obs._cls.get(net, 'Default')


def patch_rip(my_net, rect, radius):
    """Remove every foreign seg/via whose centre lies within `radius` mm of
    the pad rect centre. Returns set of harmed net names."""
    cx = (rect[0] + rect[2]) / 2
    cy = (rect[1] + rect[3]) / 2
    harmed = set()
    for t in ALL_TRACKS:
        if id(t) in DELETED:
            continue
        try:
            if t.Type() == pcbnew.PCB_VIA_T:
                p = t.GetPosition()
                px, py = p.x / 1e6, p.y / 1e6
            else:
                s, e = t.GetStart(), t.GetEnd()
                px, py = (s.x + e.x) / 2 / 1e6, (s.y + e.y) / 2 / 1e6
            if abs(px - cx) > radius or abs(py - cy) > radius:
                continue
            nm = str(t.GetNetname())
            if nm == my_net or nm in HV_PROTECT:
                continue
            b.Remove(t)
            DELETED.add(id(t))
            harmed.add(nm)
            if t.Type() == pcbnew.PCB_VIA_T:
                RIPLOG.write(f'via {nm} {px:.4f} {py:.4f}\n')
            else:
                RIPLOG.write(
                    f'seg {nm} {s.x/1e6:.4f} {s.y/1e6:.4f} '
                    f'{e.x/1e6:.4f} {e.y/1e6:.4f}\n')
        except Exception:
            continue
    RIPLOG.flush()
    return harmed


def hop_route(net, cls, groups):
    """Worker-style island hop loop. Returns (hops, unconnected_left)."""
    hops = 0
    for _ in range(24):
        oc = own_cells(net)
        un = [(r, c) for r, c in groups if not (c & oc)]
        if not un:
            break
        seed = set(oc)
        for r, c in groups:
            if c & oc:
                seed |= c
        if not seed:
            seed = set(groups[0][1])
        goalset = set().union(*(c for _, c in un))
        path, _ = bfs(obs, seed, goalset, net, cls, 0.125, cap=350000)
        hw2 = 0.125
        if not path:
            hw2 = 0.05
            path, _ = bfs(obs, seed, goalset, net, cls, hw2, cap=350000)
        if not path:
            break
        sp = ra.simplify(path)
        if ra.path_clear(obs, sp, net, cls, hw2) and \
                not rh.path_wall_violation(guard, sp, hw2):
            ra.emit(b, obs, sp, net, hw2 * 2)
            hops += 1
        else:
            break
    oc = own_cells(net)
    return hops, sum(1 for r, c in groups if not (c & oc))


# ---------------------------------------------------------------- main

queue = list(nets)
seen_rips = Counter()          # foreign net -> times we ripped its copper
for net in queue:
    cls = obs._cls.get(net, 'Default')
    groups = GROUPS.get(net, [])
    if len(groups) < 2:
        print(f'{net}: skip (<2 pads)', flush=True)
        continue
    t0 = time.time()
    hops = 0
    ripped_here = []
    status = None
    _patch_tries = 0
    _via_tries = 0
    for _round in range(6):
        oc = own_cells(net)
        un = [(r, c) for r, c in groups if not (c & oc)]
        if not un:
            break
        # diagnose the first unconnected pad BEFORE any heavy BFS:
        # its flood either reaches own copper (path exists — go hop) or
        # dies enclosed in a pocket (census the choke, rip, retry).
        tally, reach = frontier_choke(un[0][1], net, cls, 0.05)
        saturated = len(reach) >= 100000
        if (reach & oc) or saturated:
            h, left = hop_route(net, cls, groups)
            hops += h
            continue
        # sealed pocket — first try a blind microvia escape (non-destructive)
        if _via_tries < 2 and microvia_escape(net, un[0][0], un[0][1]):
            _via_tries += 1
            continue
        # sealed pocket — census the boundary
        rippable = [(k, c) for k, c in tally.most_common()
                    if k[0] in ('seg', 'via')]
        hard = [k for k in tally if k[0] in ('pad', 'wall', 'keepout')]
        harmed = set()
        if not rippable or len(rippable) > MAXRIPS:
            # entombed — escalate to a radial patch rip around the pad
            radius = PATCH_R[min(_patch_tries, len(PATCH_R) - 1)]
            if _patch_tries < len(PATCH_R):
                harmed = patch_rip(net, un[0][0], radius)
                print(f'  {net}: patch-rip r={radius}mm cleared '
                      f'{len(harmed)} foreign nets', flush=True)
                _patch_tries += 1
                for hn in harmed:
                    seen_rips[hn] += 1
                    ripped_here.append(hn)
            else:
                status = (f'HARD-SEALED chokes={len(rippable)} '
                          f'hard={len(hard)} reach={len(reach)}')
                break
        else:
            for k, _c in rippable[:MAXRIPS]:
                if seen_rips[k[1]] >= 2:
                    continue
                nm = delete_item(k[0], k[2])
                if nm:
                    harmed.add(nm)
                    seen_rips[nm] += 1
                    ripped_here.append(nm)
            if not harmed:
                status = 'STUCK (rip-match-fail)'
                break
        obs = ra.Obstacles(b)          # ripped items leave the index
        guard = rh.SlotGuard(obs, b)
        guard.wrap(obs)
        obs.dead = set(harmed) | {net}
        for hn in sorted(harmed):      # re-heal victims while their stale
            hg = GROUPS.get(hn, [])    # index entries are still dead
            if len(hg) < 2:
                continue
            hh, hl = hop_route(hn, obs._cls.get(hn, 'Default'), hg)
            print(f'  heal {hn}: hops={hh} left={hl}', flush=True)
            if hl and hn not in queue:
                queue.append(hn)
        obs.dead = set()
    oc = own_cells(net)
    left = sum(1 for r, c in groups if not (c & oc))
    st = 'DONE' if left == 0 else f'OPEN-{left}pads'
    extra = f' [{status}]' if status and left else ''
    print(f'{net}: {st} hops={hops} ripped={len(ripped_here)} '
          f'({time.time()-t0:.0f}s){extra}', flush=True)
    pcbnew.SaveBoard(IN, b)
print('LIBERATE-DONE', flush=True)
