#!/usr/bin/env python3
# metrics.py — V22 USB/VBUS/analogue reporting
import math
import pcbnew

NM = 1e6
def mm(v): return v / NM
def dist(a, b): return math.hypot(a[0] - b[0], a[1] - b[1])

b0 = pcbnew.LoadBoard('_work/vitalq_v2_pre_move.kicad_pcb')
b1 = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')

def net_geom(brd, net):
    segs, vias = [], []
    for t in brd.Tracks():
        if t.GetNetname() == net:
            if t.Type() == pcbnew.PCB_VIA_T:
                vias.append((mm(t.GetPosition().x), mm(t.GetPosition().y)))
            else:
                segs.append((mm(t.GetStart().x), mm(t.GetStart().y), mm(t.GetEnd().x), mm(t.GetEnd().y),
                             t.GetLayerName(), mm(t.GetWidth())))
    return segs, vias

def seglen(s): return dist((s[0], s[1]), (s[2], s[3]))

print('================ USB_DP / USB_DM ================')
for net in ('USB_DP', 'USB_DM'):
    for tag, brd in (('before', b0), ('after', b1)):
        segs, vias = net_geom(brd, net)
        L = sum(seglen(s) for s in segs)
        lays = {}
        for s in segs: lays[s[4]] = lays.get(s[4], 0) + seglen(s)
        print('%s %s: len=%.2fmm  vias=%d  segs=%d  per-layer=%s' % (
            net, tag, L, len(vias), len(segs), {k: round(v, 2) for k, v in lays.items()}))
# skew
s_dp, v_dp = net_geom(b1, 'USB_DP'); s_dm, v_dm = net_geom(b1, 'USB_DM')
Ldp = sum(map(seglen, s_dp)); Ldm = sum(map(seglen, s_dm))
s_dp0, v_dp0 = net_geom(b0, 'USB_DP'); s_dm0, v_dm0 = net_geom(b0, 'USB_DM')
Ldp0 = sum(map(seglen, s_dp0)); Ldm0 = sum(map(seglen, s_dm0))
print('skew before=%.3fmm  after=%.3fmm' % (abs(Ldp0 - Ldm0), abs(Ldp - Ldm)))
print('via count DP %d->%d  DM %d->%d' % (len(v_dp0), len(v_dp), len(v_dm0), len(v_dm)))
# pair gap: min distance between DP and DM parallel segments on F.Cu
def pair_gap(brd):
    best = None
    dp = [s for s in net_geom(brd, 'USB_DP')[0] if s[4] == 'Top Layer']
    dm = [s for s in net_geom(brd, 'USB_DM')[0] if s[4] == 'Top Layer']
    for a in dp:
        ax, ay = (a[0]+a[2])/2, (a[1]+a[3])/2
        for c in dm:
            cx, cy = (c[0]+c[2])/2, (c[1]+c[3])/2
            d = dist((ax, ay), (cx, cy))
            if best is None or d < best: best = d
    return best
g = pair_gap(b1)
print('closest DP-DM mid-mid dist after: %.3fmm (center-to-center)' % g)

print('\n--- impedance estimate (edge-coupled microstrip, F.Cu over In1 GND) ---')
# stackup: h=0.0994mm prepreg er 4.1, t=0.035, w=0.129, s=gap-w
w = 0.129; h = 0.0994; er = 4.1; t = 0.035
for s_guess in (0.10, 0.15, 0.20):
    # Hammerstad-Jensen single microstrip
    weff = w + 1.25 * t / math.pi * (1 + math.log(2 * h / t))
    u = weff / h
    er_eff = (er + 1) / 2 + (er - 1) / 2 * (1 / math.sqrt(1 + 12 / u) + 0.04 * (1 - u) ** 2 if u < 1 else 1 / math.sqrt(1 + 12 / u))
    z0 = 60 / math.sqrt(er_eff) * math.log(8 / u + u / 4)
    # odd-mode rough: coupling reduces Z0; use approx k from s/h
    kk = 1 - math.exp(-0.9 * (s_guess / h) ** 0.5) * 0.0  # placeholder
    # simple diff estimate: Zdiff ~= 2*Z0*(1-0.48*exp(-0.96*s/h))  (IPC approx)
    zdiff = 2 * z0 * (1 - 0.48 * math.exp(-0.96 * s_guess / h))
    print('  s=%.2f w=%.3f h=%.4f er=%.1f -> Z0~%.1fR  Zdiff~%.1fR' % (s_guess, w, h, er, z0, zdiff))

print('\n================ VBUS ================')
for tag, brd in (('before', b0), ('after', b1)):
    segs, vias = net_geom(brd, 'VBUS')
    L = sum(seglen(s) for s in segs)
    widths = sorted({s[5] for s in segs})
    lays = {}
    for s in segs: lays[s[4]] = lays.get(s[4], 0) + seglen(s)
    print('VBUS %s: len=%.2fmm vias=%d widths=%s per-layer=%s' % (
        tag, L, len(vias), widths, {k: round(v, 2) for k, v in lays.items()}))

# part positions
pos = {}
for f in b1.Footprints():
    if f.GetReference() in ('J1', 'U21', 'C63', 'U2', 'R1', 'R2', 'C64', 'C8', 'D33', 'Q6', 'U1', 'TP18', 'R26'):
        pos[f.GetReference()] = (mm(f.GetPosition().x), mm(f.GetPosition().y))
print('\nJ1 -> U21 (USB ESD): %.2f mm' % dist(pos['J1'], pos['U21']))
print('J1 -> C63:          %.2f mm' % dist(pos['J1'], pos['C63']))
print('C63 -> U2:          %.2f mm' % dist(pos['C63'], pos['U2']))
print('J1 -> U2:           %.2f mm' % dist(pos['J1'], pos['U2']))
print('J1 -> R1 (CC1 pd):  %.2f mm' % dist(pos['J1'], pos['R1']))
print('J1 -> R2 (CC2 pd):  %.2f mm' % dist(pos['J1'], pos['R2']))
# R1/R2 CC pad to J1 CC pad distance (pad-level)
def pad_pos(brd, ref, net):
    for f in brd.Footprints():
        if f.GetReference() == ref:
            return [(mm(p.GetPosition().x), mm(p.GetPosition().y)) for p in f.Pads() if p.GetNetname() == net]
    return []
for ref, net, jnet in (('R1', 'USB_CC1', 'USB_CC1'), ('R2', 'USB_CC2', 'USB_CC2')):
    rp = pad_pos(b1, ref, net); jp = pad_pos(b1, 'J1', jnet)
    if rp and jp:
        dmin = min(dist(a, c) for a in rp for c in jp)
        print('%s %s pad -> J1 %s pad: %.2f mm' % (ref, net, jnet, dmin))

print('\n================ Analogue (IN1P / +3V3_ANA / moved-part nets) ================')
for net in ('IN1P', '+3V3_ANA', 'CS_ADS1292', 'ESP_TX', 'SPARE_IN', 'ADS1292_DRDY', 'ADS1292_PWDN', 'SPI_MISO'):
    for tag, brd in (('before', b0), ('after', b1)):
        segs, vias = net_geom(brd, net)
        L = sum(seglen(s) for s in segs)
        print('%-14s %s: len=%.2fmm vias=%d segs=%d' % (net, tag, L, len(vias), len(segs)))
# closest digital/USB neighbour to IN1P (after)
print('\n--- IN1P nearest neighbours (after) ---')
inp = net_geom(b1, 'IN1P')[0]
targets = {}
for t in b1.Tracks():
    n = t.GetNetname()
    if n and n != 'IN1P' and t.Type() != pcbnew.PCB_VIA_T:
        targets.setdefault(n, []).append((mm(t.GetStart().x), mm(t.GetStart().y), mm(t.GetEnd().x), mm(t.GetEnd().y)))
def segdist(a, c):
    # point-seg distance sampled
    best = 1e9
    for tt in (0, .25, .5, .75, 1):
        px = a[0] + (a[2] - a[0]) * tt; py = a[1] + (a[3] - a[1]) * tt
        for uu in (0, .25, .5, .75, 1):
            qx = c[0] + (c[2] - c[0]) * uu; qy = c[1] + (c[3] - c[1]) * uu
            best = min(best, dist((px, py), (qx, qy)))
    return best
res = []
for n, lst in targets.items():
    dmin = min(segdist(a, c) for a in inp for c in lst[:200])
    res.append((dmin, n))
for d, n in sorted(res)[:8]:
    print('  IN1P <-%.3fmm-> %s' % (d, n))
