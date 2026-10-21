#!/usr/bin/env python3
# check_corridor.py — V22 plug-grip corridor check (prompt section 5.3)
# run: $KP _work/check_corridor.py vitalq_v2.kicad_pcb
import sys, math
import pcbnew

FN = sys.argv[1] if len(sys.argv) > 1 else 'vitalq_v2.kicad_pcb'
b = pcbnew.LoadBoard(FN)
NM = 1e6

def mm(v): return v / NM

# --- J1 ---
j1 = None
for f in b.Footprints():
    if f.GetReference() == 'J1': j1 = f
assert j1, 'J1 not found'
jx, jy = mm(j1.GetPosition().x), mm(j1.GetPosition().y)
jrot = j1.GetOrientationDegrees()
print('J1 at (%.3f, %.3f) rot %.1f' % (jx, jy, jrot))

# front face: pad-row origin -7.8 => front 3.7 further south
Y_F = jy - 3.7          # expect -11.5
print('receptacle face y_f = %.3f' % Y_F)

# --- gather Edge.Cuts segments ---
edge_segs = []  # (x1,y1,x2,y2)
for dr in b.GetDrawings():
    if dr.GetLayerName() == 'Edge.Cuts':
        shp = dr.GetShapeStr() if hasattr(dr, 'GetShapeStr') else ''
        if hasattr(dr, 'GetStart'):
            edge_segs.append((mm(dr.GetStart().x), mm(dr.GetStart().y), mm(dr.GetEnd().x), mm(dr.GetEnd().y)))

def seg_box(x1, y1, x2, y2):
    return (min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2))

def boxes_hit(a, b2):
    return not (a[2] <= b2[0] or b2[2] <= a[0] or a[3] <= b2[1] or b2[3] <= a[1])

def seg_rect_hit(x1, y1, x2, y2, rect):
    # conservative: bbox overlap + endpoint-inside + crossing edges
    rx0, ry0, rx1, ry1 = rect
    def inside(px, py): return rx0 <= px <= rx1 and ry0 <= py <= ry1
    if inside(x1, y1) or inside(x2, y2): return True
    if not boxes_hit(seg_box(x1, y1, x2, y2), rect): return False
    # segment vs rect edges (proper intersection test)
    def ccw(ax, ay, bx, by, cx, cy): return (cy - ay) * (bx - ax) > (by - ay) * (cx - ax)
    def inter(a, bb, c, d):
        return ccw(a[0], a[1], c[0], c[1], d[0], d[1]) != ccw(bb[0], bb[1], c[0], c[1], d[0], d[1]) and \
               ccw(a[0], a[1], bb[0], bb[1], c[0], c[1]) != ccw(a[0], a[1], bb[0], bb[1], d[0], d[1])
    p1, p2 = (x1, y1), (x2, y2)
    for c1, c2 in (((rx0, ry0), (rx1, ry0)), ((rx1, ry0), (rx1, ry1)), ((rx1, ry1), (rx0, ry1)), ((rx0, ry1), (rx0, ry0))):
        if inter(p1, p2, c1, c2): return True
    return False

# --- gather copper geometry (track/via/pad/zone bboxes) ---
copper = []
for t in b.Tracks():
    try:
        bb = t.GetBoundingBox()
        copper.append(('track/via ' + (t.GetNetname() or '?'),
                       (mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()))))
    except Exception: pass
for f in b.Footprints():
    for p in f.Pads():
        try:
            bb = p.GetBoundingBox()
            copper.append(('pad %s.%s %s' % (f.GetReference(), p.GetNumber(), p.GetNetname() or '?'),
                           (mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()))))
        except Exception: pass
for z in b.Zones():
    try:
        if z.GetIsRuleArea(): continue  # keep-out / rule areas are not copper
        bb = z.GetBoundingBox()
        copper.append(('zone ' + (z.GetNetname() or '?'),
                       (mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()))))
    except Exception: pass

# --- footprint courtyards (fall back to bbox) ---
cy = []
for f in b.Footprints():
    r = None
    try:
        cyps = f.GetCourtyard(pcbnew.F_CrtYd)
        bb = cyps.BBox() if hasattr(cyps, 'BBox') else cyps
        r = (mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()))
    except Exception:
        pass
    if r is None or (r[2] - r[0]) <= 0:
        try:
            bb = f.GetBoundingBox()
            r = (mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()))
        except Exception:
            continue
    cy.append((f.GetReference(), r))
# U1 body per prompt: 2.44..20.56 x, -22.31..3.31 y
U1_BODY = (2.44, -22.31, 20.56, 3.31)

# --- board edge / tab detection ---
# tab edge: the north-south edge segment in the south-west corner region
tab_x = None
for x1, y1, x2, y2 in edge_segs:
    if abs(x1 - x2) < 0.01 and min(y1, y2) < -10.4 and max(y1, y2) > -11 and x1 < 30:
        tab_x = x1  # vertical edge stepping below -10.5
print('edge segs:', len(edge_segs))

# edge straight span along y=-10.5: collect horizontal segs at y=-10.5
span_xs = []
for x1, y1, x2, y2 in edge_segs:
    if abs(y1 - y2) < 0.01 and abs(y1 + 10.5) < 0.01:
        span_xs.append((min(x1, x2), max(x1, x2)))
span_xs.sort()
# merge
merged = []
for a, bb2 in span_xs:
    if merged and a <= merged[-1][1] + 0.01: merged[-1] = (merged[-1][0], max(merged[-1][1], bb2))
    else: merged.append((a, bb2))
print('y=-10.5 edge spans:', ['%.2f..%.2f' % m for m in merged])
cover = next((m for m in merged if m[0] <= 22.8 and m[1] >= 36.2), None)
print('edge straight span 22.8..36.2 @y-10.5:', 'PASS' if cover else 'FAIL')

# --- corridor checks ---
def corridor(half, label):
    cx0, cx1 = jx - half, jx + half
    cy0, cy1 = Y_F - 15.0, Y_F
    rect = (cx0, cy0, cx1, cy1)
    hits = []
    for x1, y1, x2, y2 in edge_segs:
        if seg_rect_hit(x1, y1, x2, y2, rect): hits.append(('Edge.Cuts', x1, y1, x2, y2))
    for name, r in cy:
        if name == 'J1': continue
        if boxes_hit(rect, r): hits.append(('courtyard ' + name, r))
    if boxes_hit(rect, U1_BODY): hits.append(('U1 body', U1_BODY))
    for name, r in copper:
        if boxes_hit(rect, r): hits.append(('copper ' + name, r))
    print('\n== %s grip corridor x %.3f..%.3f, y %.3f..%.3f ==' % (label, cx0, cx1, cy0, cy1))
    if hits:
        for h in hits[:20]: print('  HIT', h)
        print('  -> FAIL (%d hits)' % len(hits))
    else:
        print('  clear -> PASS')
    # margins: west = corridor west edge minus tab/edge obstacle x; east = nearest part east
    return rect, hits

rect12, h12 = corridor(6.2, '12.4mm')
rect83, h83 = corridor(4.15, '8.3mm')

# margins: nearest obstacle (edge seg/courtyard/copper) west & east of corridor, within y band
def obstacles_west_east(rect):
    cx0, cy0, cx1, cy1 = rect
    west = None; east = None
    cand = []
    for x1, y1, x2, y2 in edge_segs: cand.append(('edge', seg_box(x1, y1, x2, y2)))
    for name, r in cy:
        if name != 'J1': cand.append(('cy ' + name, r))
    cand.append(('U1 body', U1_BODY))
    for name, r in copper: cand.append(('cu ' + name, r))
    for name, r in cand:
        # vertical overlap with corridor y-band (include 2mm beyond for side approach)
        if r[3] < cy0 - 2 or r[1] > cy1 + 2: continue
        if r[2] <= cx0:
            d = cx0 - r[2]
            if west is None or d < west[0]: west = (d, name, r)
        if r[0] >= cx1:
            d = r[0] - cx1
            if east is None or d < east[0]: east = (d, name, r)
    return west, east

for rect, label in ((rect12, '12.4mm'), (rect83, '8.3mm')):
    w, e = obstacles_west_east(rect)
    print('%s margin west=%.2fmm (to %s)  east=%s' % (
        label, w[0] if w else 99, w[1] if w else 'none',
        '%.2fmm (to %s)' % (e[0], e[1]) if e else '99+ (none)'))

print('\naxis_to_tab (x22.5)   = %.2f mm' % (jx - 22.5))
print('axis_to_U1  (x20.56)  = %.2f mm' % (jx - 20.56))
# nearest other part: min distance from axis to any footprint body edge
nearest = None
for name, r in cy:
    if name in ('J1',): continue
    # distance from axis point to rect (use courtyard rect)
    dx = max(r[0] - jx, 0, jx - r[2])
    dy = max(r[1] - jy, 0, jy - r[3])
    d = math.hypot(dx, dy)
    if nearest is None or d < nearest[0]: nearest = (d, name)
print('axis to nearest courtyard: %.2f mm (%s)' % (nearest[0], nearest[1]))

ok = (not h12) and (not h83) and cover is not None
w12, e12 = obstacles_west_east(rect12)
margin_ok = (w12 is None or w12[0] >= 0.5) and (e12 is None or e12[0] >= 0.5)
print('\n12.4mm >=0.5mm/side:', 'PASS' if margin_ok else 'CHECK')
print('CORRIDOR OVERALL:', 'PASS' if ok and margin_ok else 'FAIL')
