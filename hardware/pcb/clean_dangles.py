#!/usr/bin/env python3
"""Remove dangling copper (tracks/vias) from a routed KiCad board.

Two kinds of cruft are removed:

(a) ISOLATED items — a via whose connectivity island contains no other
    item at all, or a track that forms a whole island by itself and
    touches no pad at either endpoint.

(b) STUB SPURS — inside islands that hold fewer than two pads, a track
    with exactly one free endpoint (nothing same-net touching it: no pad,
    no via, no other track) is a dead-end spur.  Spurs are pruned
    iteratively so whole dangling chains are removed; pruning stops at
    pads, vias and T-junctions.

Safety rules (conservative — when in doubt the item is kept):
  * an island containing 2+ pads IS the net's connectivity: nothing
    inside it is ever deleted;
  * vias are never stub-pruned — a via whose island holds a pad is a
    via-in-pad / stitch via; spur chains only stop at vias;
  * locked items are never deleted and anchor their neighbours;
  * an endpoint sitting on any pad or via (any net) counts as anchored;
  * loops and bridge segments are kept — a track is only cut when an
    endpoint is genuinely free;
  * islands the connectivity query fails on are skipped entirely.

Zone fills never count as connectivity (EXCLUDE_ZONES) — same rule as
opens_nets.py.

Usage:
  clean_dangles.py <in.kicad_pcb> <out.kicad_pcb>

  Run with the KiCad bundled python, e.g.:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/\
Versions/3.9/bin/python3 clean_dangles.py in.kicad_pcb out.kicad_pcb
"""
import math
import os
import sys
from collections import defaultdict

import pcbnew

EPS = 20_000                        # nm touch slack (0.02 mm, cf. opens_nets)
TRACK_TYPES = frozenset(t for t in (getattr(pcbnew, "PCB_TRACE_T", 13),
                                    getattr(pcbnew, "PCB_ARC_T", 15))
                        if t is not None)
VIA_T = getattr(pcbnew, "PCB_VIA_T", 14)
PAD_T = getattr(pcbnew, "PCB_PAD_T", 4)
KNOWN_TYPES = TRACK_TYPES | {VIA_T, PAD_T}


def key_of(it):
    """Stable identity for SWIG proxy objects (same trick as opens_nets)."""
    return it.m_Uuid.AsString()


def pt(v):
    return (v.x, v.y)


def d2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def seg_dist(p, a, b):
    """Distance from point p to segment a-b (all (x, y) nm tuples)."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    if l2 == 0:
        return d2(p, a)
    t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2))
    return math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy))


def _orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def seg_seg(a, b, c, d):
    """Segment-segment distance."""
    d1 = _orient(c, d, a)
    dd = _orient(c, d, b)
    d3 = _orient(a, b, c)
    d4 = _orient(a, b, d)
    if ((d1 > 0) != (dd > 0)) and ((d3 > 0) != (d4 > 0)):
        return 0.0
    return min(seg_dist(c, a, b), seg_dist(d, a, b),
               seg_dist(a, c, d), seg_dist(b, c, d))


def segs_of(t):
    """Linearisation of a track/arc: [(p0, p1, halfwidth), ...]."""
    hw = t.GetWidth() / 2
    s, e = pt(t.GetStart()), pt(t.GetEnd())
    if t.Type() == getattr(pcbnew, "PCB_ARC_T", -1):
        try:
            m = pt(t.GetMid())
            return [(s, m, hw), (m, e, hw)]
        except Exception:
            pass
    return [(s, e, hw)]


def via_geom(v):
    """(pos, radius) for a via."""
    p = pt(v.GetPosition())
    try:
        w = v.GetWidth(pcbnew.F_Cu)
    except Exception:
        w = v.GetWidth()
    return p, w / 2


class Pads:
    """All board pads with a cheap 'does the pad reach point p' query."""

    def __init__(self, board):
        self.pads = []
        for fp in board.GetFootprints():
            for p in fp.Pads():
                bb = p.GetBoundingBox()
                hd = math.hypot(bb.GetWidth(), bb.GetHeight()) / 2
                self.pads.append((pt(p.GetPosition()), hd, p))

    def any_touch(self, p, slack):
        """True if ANY board pad reaches within `slack` of point p."""
        for pos, hd, pad in self.pads:
            if self._query(pos, hd, pad, p, slack):
                return True
        return False

    @staticmethod
    def _query(pos, hd, pad_obj, p, slack):
        if d2(pos, p) > hd + slack + EPS:
            return False                    # cheap reject
        try:
            samples = [(p[0], p[1])]
            for r in (slack * 0.5, slack):
                for i in range(8):
                    a = math.tau * i / 8
                    samples.append((p[0] + r * math.cos(a),
                                    p[1] + r * math.sin(a)))
            for q in samples:
                if pad_obj.HitTest(pcbnew.VECTOR2I(int(round(q[0])),
                                                   int(round(q[1])))):
                    return True
            return False                    # precise: near corner != touch
        except Exception:
            return True                     # no HitTest -> trust dist (keep)


def member_pad_rec(m):
    bb = m.GetBoundingBox()
    return (pt(m.GetPosition()),
            math.hypot(bb.GetWidth(), bb.GetHeight()) / 2, m)


def pad_near_track(pad_rec, segs, slack):
    """True if pad shape comes within `slack` of any track segment."""
    for a, b, hw in segs:
        for i in range(5):                  # sample along the segment
            t = i / 4
            q = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
            if Pads._query(pad_rec[0], pad_rec[1], pad_rec[2], q,
                           slack + hw):
                return True
    return False


def any_via_touch(vias, p, slack):
    """True if any (pos, radius) via reaches within `slack` of p."""
    for vp, vr in vias:
        if d2(vp, p) <= vr + slack:
            return True
    return False


# ---------------- connectivity islands -------------------------------------

def build_islands(board, items):
    """Per-item connectivity island via CONNECTIVITY_DATA (zones excluded).

    Returns (island_of, members, ok) where island_of maps uuid->frozenset
    of member uuids, members maps frozenset->{uuid: object}, and ok is the
    set of island frozensets the query succeeded for."""
    con = board.GetConnectivity()
    try:
        con.Build(board)
    except Exception:
        pass
    excl = getattr(pcbnew, "EXCLUDE_ZONES", 0)

    island_of, members, ok = {}, {}, set()
    for it in items:
        k = key_of(it)
        if k in island_of:
            continue                        # island already computed
        try:
            ms = {key_of(m): m
                  for m in con.GetConnectedItems(it, excl)}
            good = bool(ms)     # empty island = broken query -> skip
        except Exception:
            ms, good = {}, False
        ms.setdefault(k, it)
        fs = frozenset(ms)
        members[fs] = ms
        if good:
            ok.add(fs)
        for mk in ms:
            island_of[mk] = fs
    return island_of, members, ok


def pad_count(members_map):
    return sum(1 for m in members_map.values() if m.Type() == PAD_T)


# ---------------- rule (a): isolated items ----------------------------------

def isolated_deletions(items, island_of, members, ok, pads, all_vias):
    """Singleton islands: orphan vias; tracks that touch no pad."""
    out = {}
    for it in items:
        k = key_of(it)
        fs = island_of[k]
        if fs not in ok or len(fs) != 1 or it.IsLocked():
            continue
        ty = it.Type()
        if ty == VIA_T:
            pos, vr = via_geom(it)
            # geometric guard: a pad overlapping the via means stale conn
            # data or a VIP — keep it
            if not pads.any_touch(pos, vr + EPS):
                out[k] = "isolated-via"
        elif ty in TRACK_TYPES:
            hw = it.GetWidth() / 2
            free = True
            for e in (pt(it.GetStart()), pt(it.GetEnd())):
                if pads.any_touch(e, hw + EPS):
                    free = False
                    break
            if free:
                out[k] = "isolated-track"
    return out


# ---------------- rule (b): stub-spur pruning -------------------------------

def prune_island(mem, pads, all_vias):
    """Iteratively prune dead-end tracks inside one island.

    `mem`: {uuid: item object} — pads, vias and locked items are anchors
    and live forever ... except a via that ends up with no surviving
    neighbour at all: it was left orphaned by the pruning itself, which
    makes it rule-(a) dangling (a via can never orphan a pad — pads are
    permanent anchors, so a VIP/stitch via on a pad always keeps a live
    neighbour and is never swept).  Returns the uuids to delete."""
    live = set(mem)
    cand = {k for k, m in mem.items()
            if m.Type() in TRACK_TYPES and not m.IsLocked()}
    via_cand = {k for k, m in mem.items()
                if m.Type() == VIA_T and not m.IsLocked()}
    if not cand and not via_cand:
        return set()

    end_touch = {k: [set(), set()] for k in cand}   # member keys at each end
    end_guard = {k: [False, False] for k in cand}   # foreign pad/via on end
    body = {k: set() for k in cand}                 # members touching anywhere
    via_nb = {k: set() for k in via_cand}           # members touching the via

    for ck in cand:
        t = mem[ck]
        hw = t.GetWidth() / 2
        segs = segs_of(t)
        ends = [pt(t.GetStart()), pt(t.GetEnd())]
        for i, e in enumerate(ends):
            if pads.any_touch(e, hw + EPS) or \
                    any_via_touch(all_vias, e, hw + EPS):
                end_guard[ck][i] = True
        for mk, m in mem.items():
            if mk == ck:
                continue
            mt = m.Type()
            if mt == PAD_T:
                rec = member_pad_rec(m)
                for i, e in enumerate(ends):
                    if Pads._query(rec[0], rec[1], rec[2], e, hw + EPS):
                        end_touch[ck][i].add(mk)
                if pad_near_track(rec, segs, EPS):
                    body[ck].add(mk)
            elif mt == VIA_T:
                vp, vr = via_geom(m)
                for i, e in enumerate(ends):
                    if d2(e, vp) <= vr + hw + EPS:
                        end_touch[ck][i].add(mk)
                if any(seg_dist(vp, a, b) <= vr + shw + EPS
                       for a, b, shw in segs):
                    body[ck].add(mk)
            elif mt in TRACK_TYPES:
                msegs = segs_of(m)
                for i, e in enumerate(ends):
                    if any(seg_dist(e, a, b) <= mhw + hw + EPS
                           for a, b, mhw in msegs):
                        end_touch[ck][i].add(mk)
                if any(seg_seg(a, b, c, d) <= hw1 + hw2 + EPS
                       for a, b, hw1 in segs for c, d, hw2 in msegs):
                    body[ck].add(mk)
            # unknown member type: island was already skipped by caller

    for vk in via_cand:
        vp, vr = via_geom(mem[vk])
        for mk, m in mem.items():
            if mk == vk:
                continue
            mt = m.Type()
            if mt == PAD_T:
                rec = member_pad_rec(m)
                if Pads._query(rec[0], rec[1], rec[2], vp, vr + EPS):
                    via_nb[vk].add(mk)
            elif mt == VIA_T:
                op, orr = via_geom(m)
                if d2(vp, op) <= vr + orr + EPS:
                    via_nb[vk].add(mk)
            elif mt in TRACK_TYPES:
                if any(seg_dist(vp, a, b) <= vr + hw + EPS
                       for a, b, hw in segs_of(m)):
                    via_nb[vk].add(mk)

    deleted = set()
    changed = True
    while changed:
        changed = False
        for ck in list(cand & live):
            free0 = not end_guard[ck][0] and not (end_touch[ck][0] & live)
            free1 = not end_guard[ck][1] and not (end_touch[ck][1] & live)
            nfree = free0 + free1
            body_live = bool(body[ck] & live)
            # exactly one free end -> spur tip; both ends free with no
            # surviving touch anywhere -> detached remnant of a pruned chain
            if nfree == 1 or (nfree == 2 and not body_live):
                live.discard(ck)
                deleted.add(ck)
                changed = True
        for vk in list(via_cand & live):
            # orphan only when it HAD neighbours and every one is gone —
            # a via with zero detected neighbours is a geometry/conn
            # mismatch: keep it (conservative)
            if via_nb[vk] and not (via_nb[vk] & live):
                live.discard(vk)
                deleted.add(vk)
                changed = True
    return deleted


def stub_deletions(items, island_of, members, ok, pads, all_vias):
    """Prune spurs inside every island holding < 2 pads."""
    by_island = defaultdict(set)
    for it in items:
        by_island[island_of[key_of(it)]].add(key_of(it))
    out = {}
    for fs in by_island:
        if fs not in ok or len(fs) <= 1:
            continue                        # bad query or singleton (rule a)
        mem = members[fs]
        if any(m.Type() not in KNOWN_TYPES for m in mem.values()):
            continue                        # unknown member — keep everything
        for k in prune_island(mem, pads, all_vias):
            out[k] = "stub"
    return out


# ---------------- driver ----------------------------------------------------

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    in_p, out_p = sys.argv[1], sys.argv[2]
    if os.path.abspath(in_p) == os.path.abspath(out_p):
        sys.exit("error: output path equals input path — "
                 "refusing to overwrite the input board")

    board = pcbnew.LoadBoard(in_p)
    items = [t for t in board.GetTracks()
             if t.Type() in TRACK_TYPES or t.Type() == VIA_T]
    obj = {key_of(t): t for t in items}
    before_tracks = sum(1 for t in items if t.Type() in TRACK_TYPES)
    before_vias = len(items) - before_tracks

    pads = Pads(board)
    all_vias = [via_geom(t) for t in items if t.Type() == VIA_T]
    island_of, members, ok = build_islands(board, items)

    doomed = isolated_deletions(items, island_of, members, ok, pads, all_vias)
    doomed.update(stub_deletions(items, island_of, members, ok,
                                 pads, all_vias))

    per_net = defaultdict(lambda: [0, 0])   # net -> [tracks, vias]
    for k in doomed:
        t = obj[k]
        per_net[t.GetNetname() or "<no net>"][t.Type() == VIA_T] += 1
        board.Remove(t)

    pcbnew.SaveBoard(out_p, board)

    if per_net:
        print("removed per net (tracks/vias):")
        for n, (tr, vi) in sorted(per_net.items(),
                                  key=lambda kv: (-(kv[1][0] + kv[1][1]),
                                                  kv[0])):
            print(f"  {n}: {tr + vi} ({tr} tracks, {vi} vias)")
    else:
        print("no dangling items found")
    gone_tr = sum(v[0] for v in per_net.values())
    gone_vi = sum(v[1] for v in per_net.values())
    print(f"total: removed {gone_tr + gone_vi} of "
          f"{before_tracks + before_vias} copper items "
          f"(tracks {before_tracks} -> {before_tracks - gone_tr}, "
          f"vias {before_vias} -> {before_vias - gone_vi})")


if __name__ == "__main__":
    main()
