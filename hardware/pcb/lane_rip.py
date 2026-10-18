#!/usr/bin/env python3
"""Rip non-HV copper inside the reserved HV lane boxes (hv_ownlayer /
hv_inner footprints, +margin). Victims get re-routed afterwards by
worker_route.py. Usage: lane_rip.py <in_board> <out_board>"""
import sys
sys.path.insert(0, '.')
import pcbnew
import route_all as ra

infile, outfile = sys.argv[1], sys.argv[2]
b = pcbnew.LoadBoard(infile)

boxes = []
for z in b.Zones():
    if z.GetIsRuleArea() and z.GetZoneName() in ('hv_ownlayer', 'hv_inner'):
        bb = z.GetBoundingBox()
        boxes.append((bb.GetLeft() / 1e6, bb.GetTop() / 1e6,
                      bb.GetRight() / 1e6, bb.GetBottom() / 1e6))


def in_box(x, y):
    return any(x0 - 0.3 <= x <= x1 + 0.3 and y0 - 0.3 <= y <= y1 + 0.3
               for x0, y0, x1, y1 in boxes)


hv = {str(p.GetNetname()) for fp in b.GetFootprints() for p in fp.Pads()
      if ra.class_of(b, p.GetNetname()) == 'HV_ELECTRODE'}
print('protected HV nets:', len(hv))

ripped = set()
n = 0
for t in list(b.GetTracks()):
    net = str(t.GetNetname() or '')
    if not net or net in hv:
        continue
    if t.Type() == pcbnew.PCB_VIA_T:
        p = t.GetPosition()
        kill = in_box(p.x / 1e6, p.y / 1e6)
    else:
        s, e = t.GetStart(), t.GetEnd()
        kill = (in_box(s.x / 1e6, s.y / 1e6) or in_box(e.x / 1e6, e.y / 1e6)
                or in_box((s.x + e.x) / 2e6, (s.y + e.y) / 2e6))
    if kill:
        b.Remove(t)
        ripped.add(net)
        n += 1

pcbnew.SaveBoard(outfile, b)
print(f'ripped {n} items, {len(ripped)} victim nets:')
for v in sorted(ripped):
    print(' ', v)
