#!/usr/bin/env python3
# check_conn.py — V22 connectivity verification (prompt 7.2)
import sys, re
import pcbnew

NM = 1e6
def mm(v): return v / NM

b = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
b0 = pcbnew.LoadBoard('_work/vitalq_v2_pre_move.kicad_pcb')

print('=== 1. J1 pad net map ===')
expected = {
    'A1': 'GND', 'A4': 'VBUS', 'A5': 'USB_CC1', 'A6': 'USB_DP', 'A7': 'USB_DM',
    'A8': 'VBUS', 'A9': 'VBUS', 'A12': 'GND',
    'B1': 'GND', 'B4': 'VBUS', 'B5': 'USB_CC2', 'B6': 'USB_DP', 'B7': 'USB_DM',
    'B8': 'SBU', 'B9': 'VBUS', 'B12': 'GND',
}
for f in b.Footprints():
    if f.GetReference() == 'J1':
        j1 = f
        bad = 0
        for p in f.Pads():
            n = p.GetNumber(); net = p.GetNetname() or '(none)'
            pos = p.GetPosition()
            exp = expected.get(n)
            flag = ''
            if exp is not None:
                if exp == 'SBU':
                    ok = net in ('SBU1', 'SBU2', 'SBU', '(none)', 'Net-(J1-PadB8)')
                else:
                    ok = net == exp
                if not ok:
                    flag = '  <-- MISMATCH exp ' + exp; bad += 1
            print('  pad %-4s net %-22s @ (%.3f,%.3f)%s' % (n, net, mm(pos.x), mm(pos.y), flag))
        print('  mismatches:', bad)

print('\n=== 2. Moved-part nets — connectivity (DRC unconnected was 0; list nets) ===')
for f in b.Footprints():
    if f.GetReference() in ('R2', 'TP11', 'TP20', 'R26'):
        nets = sorted({p.GetNetname() or '?' for p in f.Pads()})
        print('  %-4s @ (%.3f,%.3f) rot %.0f nets: %s' % (
            f.GetReference(), mm(f.GetPosition().x), mm(f.GetPosition().y),
            f.GetOrientationDegrees(), ', '.join(nets)))

print('\n=== 3. Antenna keep-out zone contents ===')
for z in b.Zones():
    if z.GetIsRuleArea():
        bb = z.GetBoundingBox()
        krect = (mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()))
        lname = z.GetLayerName() if hasattr(z, 'GetLayerName') else '?'
        print('  rule area bbox %.2f %.2f %.2f %.2f layer %s' % (*krect, lname))
        inside = []
        for t in b.Tracks():
            tb = t.GetBoundingBox()
            r = (mm(tb.GetLeft()), mm(tb.GetTop()), mm(tb.GetRight()), mm(tb.GetBottom()))
            if not (r[2] < krect[0] or r[0] > krect[2] or r[3] < krect[1] or r[1] > krect[3]):
                inside.append(('track %s' % (t.GetNetname() or '?'), r, t.GetLayerName()))
        for f in b.Footprints():
            for p in f.Pads():
                pb = p.GetBoundingBox()
                r = (mm(pb.GetLeft()), mm(pb.GetTop()), mm(pb.GetRight()), mm(pb.GetBottom()))
                if not (r[2] < krect[0] or r[0] > krect[2] or r[3] < krect[1] or r[1] > krect[3]):
                    inside.append(('pad %s.%s %s' % (f.GetReference(), p.GetNumber(), p.GetNetname()), r, p.GetLayerName() if hasattr(p,'GetLayerName') else '?'))
        for z2 in b.Zones():
            if z2 is z or z2.GetIsRuleArea(): continue
            zb = z2.GetBoundingBox()
            r = (mm(zb.GetLeft()), mm(zb.GetTop()), mm(zb.GetRight()), mm(zb.GetBottom()))
            if not (r[2] < krect[0] or r[0] > krect[2] or r[3] < krect[1] or r[1] > krect[3]):
                inside.append(('zone %s' % (z2.GetNetname() or '?'), r, '?'))
        if inside:
            for it in inside[:30]: print('    INSIDE:', it)
            print('  keep-out NOT empty: %d items' % len(inside))
        else:
            print('  keep-out EMPTY -> PASS')

print('\n=== 4. WROOM (U1) net route diff vs pre-move ===')
u1_nets = set()
for f in b0.Footprints():
    if f.GetReference() == 'U1':
        u1_nets = {p.GetNetname() for p in f.Pads() if p.GetNetname()}
print('  U1 nets: %d' % len(u1_nets))
def net_sig(brd, nets):
    sig = {}
    for t in brd.Tracks():
        n = t.GetNetname()
        if n in nets:
            try:
                if hasattr(t, 'GetStart') and t.GetClass() == 'PCB_VIA' or t.Type() == pcbnew.PCB_VIA_T:
                    k = ('via', round(mm(t.GetPosition().x), 3), round(mm(t.GetPosition().y), 3))
                else:
                    k = ('seg', round(mm(t.GetStart().x), 3), round(mm(t.GetStart().y), 3),
                         round(mm(t.GetEnd().x), 3), round(mm(t.GetEnd().y), 3), t.GetLayerName(), round(mm(t.GetWidth()), 3))
            except Exception:
                continue
            sig.setdefault(n, []).append(k)
    return {n: sorted(v) for n, v in sig.items()}
s0 = net_sig(b0, u1_nets); s1 = net_sig(b, u1_nets)
changed = []
for n in sorted(u1_nets):
    a, c = s0.get(n, []), s1.get(n, [])
    if a != c: changed.append((n, len(a), len(c)))
if changed:
    for n, la, lc in changed: print('  CHANGED %s: %d -> %d items' % (n, la, lc))
else:
    print('  ALL U1-net copper identical -> PASS')
