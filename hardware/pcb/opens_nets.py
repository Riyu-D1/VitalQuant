#!/usr/bin/env python3
"""List net names that still have unconnected pads.

A net is "open" when its pads form 2+ disconnected islands through
track/via copper, OR when any pad of the net sits on an island that
contains no track segment/via at all. Pour zones are NEVER counted as
connectivity (the In2 island pours get filled later) — tracks and vias
only.

Primary path uses board.GetConnectivity() with EXCLUDE_ZONES so zone
fills can't bridge islands. On pcbnew versions without GetConnectivity
it falls back to per-net pad adjacency via geometric track-intersection
tests.

stdout: one net name per line (sorted) — feed straight into
route_all.py --only files. stderr: summary (net count, pad count).

Usage: pcbnew-python opens_nets.py <board.kicad_pcb>
"""
import math
import sys
from collections import defaultdict

import pcbnew

MM = 1_000_000
SKIP_NET_PREFIXES = ("unconnected",)          # same filter as route_all.py
EPS = 0.02                                    # mm contact slack

PAD_T = getattr(pcbnew, "PCB_PAD_T", 4)
COPPER_T = frozenset(t for t in (
    getattr(pcbnew, "PCB_TRACE_T", None),
    getattr(pcbnew, "PCB_ARC_T", None),
    getattr(pcbnew, "PCB_VIA_T", None)) if t is not None)


def pad_key(p):
    """Stable identity for a pad across SWIG proxy objects."""
    try:
        return "u:" + p.m_Uuid.AsString()
    except Exception:
        pass
    try:
        return "r:" + p.GetParentFootprint().GetReference() + "." + p.GetName()
    except Exception:
        pos = p.GetPosition()
        return f"p:{pos.x},{pos.y}"


def pads_per_net(board):
    """netname -> [(ref, pad)] for every named pad (same net filter as
    route_all.main)."""
    nets = defaultdict(list)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname() or ""
            if not n or n.startswith(SKIP_NET_PREFIXES):
                continue
            nets[n].append((fp.GetReference(), p))
    return nets


class UF:
    def __init__(self):
        self.parent = {}

    def find(self, x):
        self.parent.setdefault(x, x)
        r = x
        while self.parent[r] != r:
            r = self.parent[r]
        while self.parent[x] != r:
            self.parent[x], x = r, self.parent[x]
        return r

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.parent[ra] = rb

    def groups(self, keys):
        g = defaultdict(list)
        for k in keys:
            g[self.find(k)].append(k)
        return g


def verdict(groups, copper_of, npads):
    """groups: {root: [padkey,...]} islands of a net; copper_of: root->bool
    whether the island contains any track/via. Returns open-pad count if the
    net is open else None."""
    if len(groups) <= 1 and all(copper_of.get(r, False) for r in groups):
        return None
    if len(groups) == 1:                      # one island, but zero copper
        return npads                          # (e.g. stacked pads, no tracks)
    largest = max(len(v) for v in groups.values())
    return npads - largest                    # pads off the main island


# ---------------- path 1: board connectivity (tracks/vias only) -------------

def analyze_conn(board, nets):
    """Island analysis via CONNECTIVITY_DATA. Returns ({net: open_pads},
    saw_link) or None if this pcbnew's API is too old."""
    con = board.GetConnectivity()
    if con is None:
        return None
    try:
        con.Build(board)                      # ensure ratsnest is current
    except Exception:
        pass
    excl = getattr(pcbnew, "EXCLUDE_ZONES", 0)

    have_items = hasattr(con, "GetConnectedItems")

    def items_of(p):
        if have_items:
            try:
                return list(con.GetConnectedItems(p, excl))  # zones excluded
            except TypeError:
                return list(con.GetConnectedItems(p))
        return list(con.GetConnectedPads(p))  # ancient API: pads only

    out = {}
    saw_link = False                          # sanity flag for broken builds
    for n, pl in nets.items():
        if len(pl) < 2:
            continue
        uf = UF()
        copper_pad = {}                       # padkey -> island has copper
        keys = []
        for ref, p in pl:
            k = pad_key(p)
            keys.append(k)
            uf.find(k)
            copper_pad.setdefault(k, False)
            try:
                its = items_of(p)
            except Exception:
                return None                   # API unusable -> fallback
            if len(its) > 1:
                saw_link = True
            if have_items:
                for i in its:
                    t = i.Type()
                    if t in COPPER_T:
                        copper_pad[k] = True
                    elif t == PAD_T:
                        uf.union(k, pad_key(i))   # same-island pad
            else:
                # GetConnectedPads path: no item types available; treat a
                # multi-pad cluster as having copper (island test still works)
                peers = 0
                for q in its:
                    qk = pad_key(q)
                    if qk != k:
                        peers += 1
                        uf.union(k, qk)
                copper_pad[k] = copper_pad[k] or peers > 0
        groups = uf.groups(keys)
        copper_of = {r: any(copper_pad.get(k, False) for k in ks)
                     for r, ks in groups.items()}
        v = verdict(groups, copper_of, len(pl))
        if v is not None:
            out[n] = v
    return out, saw_link


# ---------------- path 2: geometric adjacency fallback ----------------------

def seg_dist(x0, y0, x1, y1, px, py):
    dx, dy = x1 - x0, y1 - y0
    l2 = dx * dx + dy * dy
    if l2 == 0:
        return math.hypot(px - x0, py - y0)
    t = max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / l2))
    return math.hypot(px - (x0 + t * dx), py - (y0 + t * dy))


def _orient(ax, ay, bx, by, cx, cy):
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)


def seg_seg_dist(a, b):
    ax, ay, bx, by = a[0], a[1], a[2], a[3]
    cx, cy, dx, dy = b[0], b[1], b[2], b[3]
    d1 = _orient(cx, cy, dx, dy, ax, ay)
    d2 = _orient(cx, cy, dx, dy, bx, by)
    d3 = _orient(ax, ay, bx, by, cx, cy)
    d4 = _orient(ax, ay, bx, by, dx, dy)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)):
        return 0.0
    return min(seg_dist(cx, cy, dx, dy, ax, ay),
               seg_dist(cx, cy, dx, dy, bx, by),
               seg_dist(ax, ay, bx, by, cx, cy),
               seg_dist(ax, ay, bx, by, dx, dy))


def layer_seq(ls):
    try:
        return list(ls.Seq())
    except Exception:
        return [ly for ly in range(64) if ls.Contains(ly)]


def pad_hits_seg(p, hw, sx0, sy0, sx1, sy1):
    """Precise pad/segment contact: shape HitTest sampled along the segment."""
    n = max(2, int(math.hypot(sx1 - sx0, sy1 - sy0) / 0.08) + 1)
    for i in range(n + 1):
        x = sx0 + (sx1 - sx0) * i / n
        y = sy0 + (sy1 - sy0) * i / n
        try:
            if p.HitTest(pcbnew.VECTOR2I(int(round(x * MM)), int(round(y * MM)))):
                return True
        except Exception:
            return True                     # no HitTest -> trust dist test
    return False


def analyze_geom(board, nets):
    """Union-find over pads + track/via items per net, geometrically."""
    segs = defaultdict(list)                  # net -> [(x0,y0,x1,y1,hw,layer)]
    vias = defaultdict(list)                  # net -> [(x,y,r,layerset)]
    for t in board.GetTracks():
        n = t.GetNetname() or ""
        if not n or n.startswith(SKIP_NET_PREFIXES):
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            try:
                w = t.GetWidth(pcbnew.F_Cu)
            except TypeError:
                w = t.GetWidth()
            vias[n].append((p.x / MM, p.y / MM, w / 2 / MM,
                            set(layer_seq(t.GetLayerSet()))))
        elif t.Type() in COPPER_T:
            s, e = t.GetStart(), t.GetEnd()
            segs[n].append((s.x / MM, s.y / MM, e.x / MM, e.y / MM,
                            t.GetWidth() / 2 / MM, t.GetLayer()))

    out = {}
    for n, pl in nets.items():
        if len(pl) < 2:
            continue
        # pad geometry: (pos, hit radius, layerset, layer-seq set, pad)
        pads = []
        for ref, p in pl:
            pos = p.GetPosition()
            bb = p.GetBoundingBox()
            r = math.hypot(bb.GetWidth(), bb.GetHeight()) / 2 / MM
            ls = p.GetLayerSet()
            pads.append((pos.x / MM, pos.y / MM, r, ls,
                         set(layer_seq(ls)), p))
        items = ([("s", s) for s in segs.get(n, ())] +
                 [("v", v) for v in vias.get(n, ())])
        uf = UF()
        pkeys = [pad_key(p) for _, _, _, _, _, p in pads]
        for k in pkeys:
            uf.find(k)
        ik = lambda j: ("item", j)            # noqa: E731

        # pad <-> item
        for i, (px, py, pr, ls, lset, p) in enumerate(pads):
            for j, (kind, it) in enumerate(items):
                if kind == "s":
                    x0, y0, x1, y1, hw, ly = it
                    if not ls.Contains(ly):
                        continue
                    if seg_dist(x0, y0, x1, y1, px, py) > pr + hw + EPS:
                        continue
                    if pad_hits_seg(p, hw, x0, y0, x1, y1):
                        uf.union(pkeys[i], ik(j))
                else:
                    vx, vy, vr, vset = it
                    if vset and not (vset & lset):
                        continue
                    if math.hypot(vx - px, vy - py) <= pr + vr + EPS:
                        uf.union(pkeys[i], ik(j))

        # pad <-> pad (physically touching pads share an island)
        for i in range(len(pads)):
            ax, ay, ar, _, alset, _ = pads[i]
            for k in range(i + 1, len(pads)):
                bx, by, br, _, blset, _ = pads[k]
                if alset & blset and \
                        math.hypot(ax - bx, ay - by) <= ar + br + EPS:
                    uf.union(pkeys[i], pkeys[k])

        # item <-> item (chain connectivity through touching copper)
        for a in range(len(items)):
            ka, ia = items[a]
            for b in range(a + 1, len(items)):
                kb, ib = items[b]
                touch = False
                if ka == "s" and kb == "s":
                    if ia[5] == ib[5]:
                        touch = seg_seg_dist(ia, ib) <= ia[4] + ib[4] + EPS
                elif ka == "v" and kb == "v":
                    if ia[3] & ib[3]:
                        touch = math.hypot(ia[0] - ib[0],
                                           ia[1] - ib[1]) <= ia[2] + ib[2] + EPS
                else:
                    v, s = (ia, ib) if ka == "v" else (ib, ia)
                    if s[5] in v[3]:
                        touch = seg_dist(s[0], s[1], s[2], s[3],
                                         v[0], v[1]) <= v[2] + s[4] + EPS
                if touch:
                    uf.union(ik(a), ik(b))

        groups = uf.groups(pkeys)
        # island has copper iff any track/via item node shares its root
        copper_of = {r: any(uf.find(ik(j)) == r for j in range(len(items)))
                     for r in groups}
        v = verdict(groups, copper_of, len(pl))
        if v is not None:
            out[n] = v
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    board = pcbnew.LoadBoard(sys.argv[1])
    nets = pads_per_net(board)
    multi = {n: pl for n, pl in nets.items() if len(pl) > 1}
    npads = sum(len(v) for v in multi.values())

    open_map = None
    have_conn = callable(getattr(board, "GetConnectivity", None))
    if have_conn:
        try:
            res = analyze_conn(board, nets)
            if res is not None:
                open_map, saw_link = res
                # guard: a board with routed copper but zero shared clusters
                # means the connectivity query is broken in this build
                if any(board.GetTracks()) and not saw_link:
                    open_map = None
        except Exception:
            open_map = None
    if open_map is None:
        open_map = analyze_geom(board, nets)

    for n in sorted(open_map):
        print(n)
    total_open = sum(open_map.values())
    print(f"{len(open_map)} open net(s), {total_open} unconnected pad(s) "
          f"(of {len(multi)} multi-pad nets / {npads} pads)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
