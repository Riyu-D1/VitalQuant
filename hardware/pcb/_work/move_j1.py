#!/usr/bin/env python3
# V22 J1 move: move J1 +3.5mm east, relocate clashing parts, reroute.
import pcbnew, sys, math, json

MM = 1e6
def P(x, y): return pcbnew.VECTOR2I(int(round(x*MM)), int(round(y*MM)))

brd = pcbnew.LoadBoard(sys.argv[1])
log = {'moves': [], 'ripped': [], 'added': [], 'vias_out': []}

def nm(v): return v / MM

# ---------- helpers ----------
def find_fp(ref):
    for fp in brd.GetFootprints():
        if fp.GetReference() == ref:
            return fp
    return None

def move_fp(ref, x, y):
    fp = find_fp(ref)
    old = fp.GetPosition()
    log['moves'].append([ref, round(nm(old.x),3), round(nm(old.y),3), x, y])
    fp.SetPosition(P(x, y))
    return fp

def net_of(name):
    nc = brd.GetNetInfo().GetNetItem(name)
    return nc.GetNetCode() if nc else 0

def tracks_iter():
    out = []
    for t in brd.GetTracks():
        out.append(t)
    return out

def seg_matches(t, x1, y1, x2, y2, netname=None, layer=None, tol=0.03):
    if not isinstance(t, pcbnew.PCB_TRACK): return False
    if isinstance(t, pcbnew.PCB_VIA): return False
    s, e = t.GetStart(), t.GetEnd()
    a = abs(nm(s.x)-x1)<tol and abs(nm(s.y)-y1)<tol and abs(nm(e.x)-x2)<tol and abs(nm(e.y)-y2)<tol
    b = abs(nm(s.x)-x2)<tol and abs(nm(s.y)-y2)<tol and abs(nm(e.x)-x1)<tol and abs(nm(e.y)-y1)<tol
    if not (a or b): return False
    if netname and t.GetNetname() != netname: return False
    if layer is not None and t.GetLayer() != layer: return False
    return True

def rip_seg(x1, y1, x2, y2, netname=None, layer=None):
    for t in tracks_iter():
        if seg_matches(t, x1, y1, x2, y2, netname, layer):
            log['ripped'].append([t.GetNetname(), round(nm(t.GetStart().x),3), round(nm(t.GetStart().y),3), round(nm(t.GetEnd().x),3), round(nm(t.GetEnd().y),3), t.GetLayerName()])
            brd.Remove(t)
            return True
    log['ripped'].append(['MISS', x1, y1, x2, y2, netname])
    return False

def rip_via(x, y, netname=None, tol=0.05):
    for t in tracks_iter():
        if isinstance(t, pcbnew.PCB_VIA):
            p = t.GetPosition()
            if abs(nm(p.x)-x)<tol and abs(nm(p.y)-y)<tol and (netname is None or t.GetNetname()==netname):
                log['ripped'].append(['VIA', t.GetNetname(), round(nm(p.x),3), round(nm(p.y),3)])
                brd.Remove(t); return True
    log['ripped'].append(['MISSVIA', x, y, netname]); return False

L_TOP = brd.GetLayerID('F.Cu')
L3 = brd.GetLayerID('In2.Cu')
L4 = brd.GetLayerID('In3.Cu')
L_BOT = brd.GetLayerID('B.Cu')

def add_track(x1, y1, x2, y2, netname, layer, w):
    t = pcbnew.PCB_TRACK(brd)
    t.SetStart(P(x1,y1)); t.SetEnd(P(x2,y2))
    t.SetWidth(int(round(w*MM)))
    t.SetLayer(layer)
    t.SetNetCode(net_of(netname))
    brd.Add(t)
    log['added'].append([netname, x1, y1, x2, y2, w, brd.GetLayerName(layer)])

def add_poly(pts, netname, layer, w):
    for i in range(len(pts)-1):
        add_track(pts[i][0], pts[i][1], pts[i+1][0], pts[i+1][1], netname, layer, w)

def add_via(x, y, netname, tent=True):
    v = pcbnew.PCB_VIA(brd)
    v.SetPosition(P(x,y))
    v.SetDrill(int(0.2*MM)); v.SetWidth(int(0.45*MM))
    v.SetNetCode(net_of(netname))
    try:
        v.SetIsTented(tent)
    except Exception:
        pass
    brd.Add(v)
    log['added'].append(['VIA', netname, x, y])

# ---------- 1. move footprints ----------
move_fp('J1', 29.5, -7.8)     # rot stays 180
move_fp('R2', 27.3, -1.35)    # rot -90
move_fp('R1', 29.5, -1.35)    # rot -90  (CC1 pull-down, moved near new CC1 pad)
move_fp('TP11', 36.2, -8.47)  # rot -90
move_fp('TP20', 32.0, 1.9)    # rot -90
move_fp('R26', 34.2, -1.8)    # rot 180

# ---------- 2. rip old tracks/vias ----------
RIP = [
 # net, x1,y1,x2,y2, layer('t'/'l3'/'l4'/'b'/None)
 ('GND', 29.25,-3.755, 29.4,-3.936, 't'),
 ('GND', 29.4,-3.936, 29.551,-4.118, 't'),
 ('GND', 29.551,-4.118, 29.817,-3.968, 't'),
 ('GND', 29.817,-3.968, 30.068,-4.319, 't'),
 ('GND', 30.068,-4.319, 30.32,-4.67, 't'),
 # old R2 stubs
 ('USB_CC2', 32.324,-5.452, 32.324,-5.723, 't'),
 ('USB_CC2', 32.324,-5.338, 32.324,-5.452, 't'),
 ('USB_CC2', 32.267,-5.281, 32.324,-5.338, 't'),
 ('USB_CC2', 31.909,-5.281, 32.267,-5.281, 't'),
 ('USB_CC2', 31.663,-5.035, 31.909,-5.281, 't'),
 ('USB_CC2', 31.663,-3.639, 31.663,-5.035, 't'),
 ('USB_CC2', 30.714,-2.69, 31.663,-3.639, 'l3'),
 ('USB_CC2', 24.426,-2.69, 30.714,-2.69, 'l3'),
 ('USB_CC2', 24.426,-2.701, 24.426,-2.69, 't'),
 ('USB_CC2', 24.25,-2.877, 24.426,-2.701, 't'),
 ('USB_CC2', 24.25,-3.03, 24.25,-2.877, 't'),
 ('USB_CC2', 24.25,-3.755, 24.25,-3.03, 't'),
 ('GND', 32.324,-4.703, 32.324,-4.432, 't'),
 ('GND', 32.324,-4.432, 32.324,-4.14, 't'),
 ('GND', 32.324,-4.14, 32.324,-2.796, 't'),
 ('GND', 32.324,-2.796, 32.475,-2.645, 't'),
 ('GND', 34.681,-2.645, 32.475,-2.645, 't'),
 ('GND', 34.715,-2.68, 34.681,-2.645, 't'),
 # VBUS old
 ('VBUS', 23.55,-3.755, 23.55,-4.118, 't'),
 ('VBUS', 23.55,-4.118, 23.55,-4.481, 't'),
 ('VBUS', 23.55,-4.481, 23.688,-4.71, 't'),
 ('VBUS', 23.688,-4.71, 23.881,-4.902, 't'),
 ('VBUS', 23.881,-4.902, 24.359,-4.902, 't'),
 ('VBUS', 24.359,-4.902, 25.353,-5.896, 'b'),
 ('VBUS', 25.353,-5.896, 27.501,-5.896, 'b'),
 ('VBUS', 28.312,-4.71, 28.45,-4.481, 't'),
 ('VBUS', 27.5,-5.7, 28.3,-4.8, 't'),
 ('VBUS', 28.45,-4.118, 28.45,-3.755, 't'),
 ('VBUS', 28.45,-4.481, 28.45,-4.118, 't'),
 ('VBUS', 28.45,-3.755, 28.45,-3.392, 't'),
 ('VBUS', 28.45,-3.392, 28.45,-3.029, 't'),
 ('VBUS', 28.45,-3.029, 28.536,-2.81, 't'),
 ('VBUS', 28.536,-2.81, 26.847,-1.121, 't'),
 # USB_DM old
 ('USB_DM', 25.75,-3.755, 25.75,-3.03, 't'),
 ('USB_DM', 25.75,-3.03, 25.75,-2.684, 't'),
 ('USB_DM', 25.87,-2.564, 25.75,-2.684, 't'),
 ('USB_DM', 25.75,-3.755, 25.602,-4.25, 't'),
 ('USB_DM', 25.602,-4.25, 25.602,-4.48, 't'),
 ('USB_DM', 25.602,-4.48, 25.602,-4.561, 't'),
 ('USB_DM', 25.602,-4.561, 25.593,-4.606, 't'),
 ('USB_DM', 25.593,-4.606, 25.568,-4.644, 't'),
 ('USB_DM', 25.568,-4.644, 23.925,-6.287, 't'),
 ('USB_DM', 23.925,-6.287, 23.886,-6.312, 't'),
 ('USB_DM', 23.886,-6.312, 23.841,-6.321, 't'),
 ('USB_DM', 23.841,-6.321, 23.841,-6.55, 't'),
 ('USB_DM', 23.841,-6.55, 22.4,-6.55, 't'),
 ('USB_DM', 22.4,-6.55, 22.1,-6.32, 't'),
 ('USB_DM', 26.75,-3.755, 26.75,-4.48, 't'),
 ('USB_DM', 26.75,-4.48, 26.75,-4.711, 't'),
 ('USB_DM', 26.75,-4.711, 26.574,-4.887, 't'),
 ('USB_DM', 26.574,-4.887, 25.969,-4.887, 't'),
 ('USB_DM', 25.969,-4.887, 25.75,-4.667, 't'),
 ('USB_DM', 25.75,-4.667, 25.75,-4.48, 't'),
 # USB_DP old
 ('USB_DP', 26.25,-3.755, 26.25,-3.03, 't'),
 ('USB_DP', 26.25,-3.03, 26.25,-2.684, 't'),
 ('USB_DP', 25.25,-3.755, 25.382,-3.261, 't'),
 ('USB_DP', 25.382,-3.261, 25.382,-3.03, 't'),
 ('USB_DP', 25.382,-3.03, 25.382,-1.749, 't'),
 ('USB_DP', 25.382,-1.749, 23.957,-0.324, 't'),
 ('USB_DP', 25.25,-3.755, 25.367,-4.248, 't'),
 ('USB_DP', 25.367,-4.248, 25.367,-4.48, 't'),
 ('USB_DP', 25.367,-4.48, 25.367,-4.512, 't'),
 ('USB_DP', 25.367,-4.512, 23.793,-6.086, 't'),
 ('USB_DP', 23.793,-6.086, 22.9,-6.09, 't'),
 # USB_CC1 old (entire run to old R1.1)
 ('USB_CC1', 27.25,-3.755, 27.25,-4.48, 't'),
 ('USB_CC1', 27.25,-4.48, 27.25,-4.773, 't'),
 ('USB_CC1', 27.25,-4.773, 27.074,-4.949, 't'),
 ('USB_CC1', 27.074,-4.949, 27.074,-6.047, 't'),
 ('USB_CC1', 27.074,-6.047, 27.453,-6.426, 't'),
 ('USB_CC1', 27.453,-6.426, 33.134,-6.426, 't'),
 ('USB_CC1', 33.134,-6.426, 34.481,-7.772, 't'),
 ('USB_CC1', 34.481,-7.772, 40.068,-7.772, 't'),
 ('USB_CC1', 40.068,-7.772, 40.177,-7.881, 't'),
 ('USB_CC1', 40.177,-7.881, 40.177,-7.956, 't'),
 ('USB_CC1', 40.177,-7.956, 40.61,-8.182, 't'),
 ('USB_CC1', 40.61,-8.182, 41.125,-8.697, 't'),
 ('USB_CC1', 41.125,-8.697, 41.403,-8.697, 't'),
 ('USB_CC1', 41.403,-8.697, 41.945,-9.239, 't'),
 ('USB_CC1', 41.945,-9.239, 42.237,-9.239, 't'),
 ('USB_CC1', 42.237,-9.239, 42.558,-9.239, 't'),
 # old R1 GND stub if any near 42.5,-8.2 (net GND) - handled by collision pass
 # IN1P old via/stub/diagonal
 ('IN1P', 34.348,-5.766, 33.736,-5.154, 't'),
 ('IN1P', 33.736,-5.154, 33.736,-4.705, 't'),
 ('IN1P', 33.736,-4.705, 33.736,-4.488, 't'),
 ('IN1P', 33.736,-4.488, 33.736,-4.167, 't'),
 ('IN1P', 29.921,-1.339, 34.348,-5.766, 'l3'),
 ('IN1P', 43.266,-5.766, 34.348,-5.766, 'l3'),
 # +3V3_ANA old R26 branch
 ('+3V3_ANA', 35.027,-4.167, 34.756,-4.167, 't'),
 ('+3V3_ANA', 35.251,-4.167, 35.027,-4.167, 't'),
 ('+3V3_ANA', 35.807,-3.612, 35.251,-4.167, 't'),
 ('+3V3_ANA', 36.018,-3.612, 35.807,-3.612, 't'),
 ('+3V3_ANA', 36.551,-3.08, 36.018,-3.612, 't'),
 ('+3V3_ANA', 36.551,-1.753, 36.551,-3.08, 't'),
 ('+3V3_ANA', 37.046,-1.258, 36.551,-1.753, 't'),
 # ESP_TX tail to old TP11
 ('ESP_TX', 31.516,-6.78, 33.21,-8.474, 't'),
 ('ESP_TX', 33.21,-8.474, 33.503,-8.474, 't'),
 ('ESP_TX', 33.503,-8.474, 34.006,-8.474, 't'),
 # CS_ADS1292 pad stub
 ('CS_ADS1292', 33.714,-1.736, 33.892,-1.558, 't'),
 ('CS_ADS1292', 33.892,-1.558, 34.277,-1.173, 't'),
 # ECG_P top-edge run (collides with new west rear tab)
 ('ECG_P', 43.045,-9.959, 24.808,-9.959, 't'),
 ('ECG_P', 24.808,-9.959, 21.951,-7.102, 't'),
]
LAY = {'t': L_TOP, 'l3': L3, 'l4': L4, 'b': L_BOT}
for net, x1, y1, x2, y2, l in RIP:
    rip_seg(x1, y1, x2, y2, net, LAY[l])

for vx, vy, vn in [
    (24.359,-4.902,'VBUS'), (32.475,-2.645,'GND'), (31.663,-3.639,'USB_CC2'),
    (24.426,-2.69,'USB_CC2'), (34.348,-5.766,'IN1P'),
    (22.1,-6.32,'USB_DM'), (22.9,-6.09,'USB_DP'),
]:
    rip_via(vx, vy, vn)

# ---------- 3. new copper ----------
W = 0.102   # signal width matching existing
W2 = 0.127
WV = 0.4    # VBUS top
WV4 = 0.6   # VBUS inner

# J1 GND pad -> shell tab stubs (top, under body)
add_poly([(26.25,-3.755),(26.1,-3.936),(25.949,-4.118),(25.683,-3.968),(25.432,-4.319),(25.18,-4.67)], 'GND', L_TOP, W)
add_poly([(32.75,-3.755),(32.9,-3.936),(33.051,-4.118),(33.317,-3.968),(33.568,-4.319),(33.82,-4.67)], 'GND', L_TOP, W)

# GND stitch vias around connector
for x,y in [(23.8,-5.5),(23.8,-9.3),(34.9,-4.5),(34.9,-9.0),(24.0,-10.2),(35.0,-10.2)]:
    add_via(x, y, 'GND')

# USB_DM: A7' pad stub + west lane to old vert; B7' via-hop
add_poly([(29.25,-3.755),(29.25,-2.82)], 'USB_DM', L_TOP, W2)
add_poly([(29.25,-2.82),(25.87,-2.82),(25.87,-2.564)], 'USB_DM', L_TOP, W2)
add_poly([(30.25,-3.755),(30.25,-4.95),(30.1,-5.5),(30.0,-5.6)], 'USB_DM', L_TOP, W2)
add_via(30.0,-5.6, 'USB_DM')
add_poly([(30.0,-5.6),(28.0,-4.2),(27.0,-2.95)], 'USB_DM', L3, W)
add_via(27.0,-2.95, 'USB_DM')

# USB_DP: A6' pad stub + west lane to old vert; B6' via-hop
add_poly([(29.75,-3.755),(29.75,-2.45)], 'USB_DP', L_TOP, W2)
add_poly([(29.75,-2.45),(26.4,-2.45),(26.25,-2.6),(26.25,-2.684)], 'USB_DP', L_TOP, W2)
add_poly([(28.75,-3.755),(28.75,-4.95),(29.0,-5.5)], 'USB_DP', L_TOP, W2)
add_via(29.0,-5.5, 'USB_DP')
add_poly([(29.0,-5.5),(27.0,-4.0),(26.8,-2.4)], 'USB_DP', L3, W)
add_via(26.8,-2.4, 'USB_DP')

# USB_CC1: A5' under-body stub -> via -> L3 -> via -> top -> R1.1
add_poly([(30.75,-3.755),(30.75,-4.48),(30.75,-5.0),(30.6,-5.5)], 'USB_CC1', L_TOP, W)
add_via(30.6,-5.5, 'USB_CC1')
add_poly([(30.6,-5.5),(30.3,-1.5)], 'USB_CC1', L3, W)
add_via(30.3,-1.5, 'USB_CC1')
add_poly([(30.3,-1.5),(29.6,-1.65),(29.5,-1.86)], 'USB_CC1', L_TOP, W)

# USB_CC2: B5' under-body stub -> via -> L3 -> via -> top -> R2.1
add_poly([(27.75,-3.755),(27.75,-5.05),(28.6,-5.75),(29.45,-5.95)], 'USB_CC2', L_TOP, W)
add_via(29.45,-5.95, 'USB_CC2')
add_poly([(29.45,-5.95),(28.6,-5.2),(28.35,-1.55)], 'USB_CC2', L3, W)
add_via(28.35,-1.55, 'USB_CC2')
add_poly([(28.35,-1.55),(27.3,-1.86)], 'USB_CC2', L_TOP, W)

# VBUS: west pad -> two under-body vias -> L4 -> via27.93/island
add_poly([(27.05,-3.755),(27.05,-5.15)], 'VBUS', L_TOP, WV)
add_poly([(27.05,-5.15),(27.35,-5.5),(27.501,-5.896)], 'VBUS', L_TOP, WV)
add_poly([(27.05,-5.15),(27.6,-5.25),(28.35,-5.4)], 'VBUS', L_TOP, WV)
add_via(28.35,-5.4, 'VBUS')
add_poly([(27.501,-5.896),(28.35,-5.4)], 'VBUS', L4, WV4)
add_poly([(28.35,-5.4),(28.35,-2.3),(27.93,-2.23)], 'VBUS', L4, WV4)
# east pad -> top lane -> via27.93 + inner via for second path
add_poly([(31.95,-3.755),(31.95,-3.03),(31.95,-2.0),(27.93,-2.0)], 'VBUS', L_TOP, WV)
add_via(31.95,-2.6, 'VBUS')
add_poly([(31.95,-2.6),(29.74,-1.0)], 'VBUS', L4, WV4)
# bridge old island node (26.847,-1.121) into net via new via + L4
add_poly([(26.847,-1.121),(26.8,-1.2)], 'VBUS', L_TOP, WV)
add_via(26.8,-1.2, 'VBUS')
add_poly([(26.8,-1.2),(27.93,-2.23)], 'VBUS', L4, WV4)

# R1.2 / R2.2 GND vias + stubs
add_poly([(29.5,-0.84),(29.9,-0.55),(29.9,-0.35)], 'GND', L_TOP, W)
add_via(29.9,-0.35, 'GND')
add_poly([(27.3,-0.84),(26.7,-0.5),(26.7,-0.3)], 'GND', L_TOP, W)
add_via(26.7,-0.3, 'GND')

# ESP_TX: extend old end to new TP11 pad
add_poly([(31.516,-6.78),(35.0,-7.55),(36.2,-8.47)], 'ESP_TX', L_TOP, W2)

# CS_ADS1292 stub to new TP20
add_poly([(34.277,-1.173),(34.277,0.85),(32.0,0.85),(32.0,1.9)], 'CS_ADS1292', L_TOP, W)

# IN1P: L3 west leg -> new via -> R26.2; new east leg
add_poly([(29.921,-1.339),(32.6,-1.9),(33.0,-2.1)], 'IN1P', L3, W)
add_via(33.0,-2.1, 'IN1P')
add_poly([(33.0,-2.1),(33.4,-1.8),(33.69,-1.8)], 'IN1P', L_TOP, W)
add_poly([(33.0,-2.1),(41.5,-2.1),(43.5,-5.55),(43.266,-5.766)], 'IN1P', L3, W)

# +3V3_ANA to new R26.1
add_poly([(34.71,-1.8),(36.95,-1.85),(37.046,-1.6)], '+3V3_ANA', L_TOP, W)

# ECG_P detour around new west rear tab
add_poly([(43.045,-9.959),(26.6,-9.959),(26.3,-8.45),(24.05,-8.45),(23.8,-8.0),(21.951,-7.102)], 'ECG_P', L_TOP, W)

brd.Save(sys.argv[1])
json.dump(log, open(sys.argv[2],'w'), indent=1)
miss = [r for r in log['ripped'] if r[0] in ('MISS','MISSVIA')]
print('moves:', log['moves'])
print('ripped:', len(log['ripped']), 'misses:', len(miss))
for m in miss: print('  MISS', m)
print('added:', len(log['added']))
