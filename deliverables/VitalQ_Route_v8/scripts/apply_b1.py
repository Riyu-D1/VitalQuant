import pcbnew, sys

NM = 1e6
board = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
L = {'F': pcbnew.F_Cu, 'B': pcbnew.B_Cu, 'L3': pcbnew.In2_Cu}

def find_track(x1, y1, x2, y2, tol=0.02):
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T: continue
        s, e = t.GetStart(), t.GetEnd()
        if (abs(s.x/NM-x1) < tol and abs(s.y/NM-y1) < tol and
            abs(e.x/NM-x2) < tol and abs(e.y/NM-y2) < tol) or \
           (abs(e.x/NM-x1) < tol and abs(e.y/NM-y1) < tol and
            abs(s.x/NM-x2) < tol and abs(s.y/NM-y2) < tol):
            return t
    return None

def add_track(x1, y1, x2, y2, w, layer, netname):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I(int(x1*NM), int(y1*NM)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*NM), int(y2*NM)))
    t.SetWidth(int(w*NM))
    t.SetLayer(layer)
    t.SetNetCode(board.FindNet(netname).GetNetCode())
    board.Add(t)

def add_via(x, y, d, dr, netname):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pcbnew.VECTOR2I(int(x*NM), int(y*NM)))
    v.SetWidth(int(d*NM))
    v.SetDrill(int(dr*NM))
    v.SetNetCode(board.FindNet(netname).GetNetCode())
    board.Add(v)

# 1) ESP_TX L3 jog: remove (24.84,53.51)->(24.84,49.93); add jog path
t = find_track(24.838, 53.506, 24.838, 49.931)
assert t is not None, 'ESP_TX mid segment not found'
board.Remove(t)
path = [(24.838,53.506),(24.84,51.45),(25.26,51.15),(25.26,50.7),(24.84,50.25),(24.838,49.931)]
for a, b in zip(path, path[1:]):
    add_track(a[0], a[1], b[0], b[1], 0.1017, L['L3'], 'ESP_TX')

# 2) AFE_INP L3 jog: remove (26.36,54.6)->(26.36,50.31) and (26.36,50.31)->(26.97,49.71)
for seg in [(26.361,54.602,26.361,50.314),(26.361,50.314,26.97,49.71)]:
    t = find_track(*seg)
    assert t is not None, f'AFE_INP seg {seg} not found'
    board.Remove(t)
path = [(26.361,54.602),(26.36,51.4),(26.95,51.2),(26.95,50.0),(26.97,49.71)]
for a, b in zip(path, path[1:]):
    add_track(a[0], a[1], b[0], b[1], 0.1017, L['L3'], 'AFE_INP')

# 3) via-in-pad GND 0.3/0.15
for x, y in [(25.3,50.2),(24.9,51.0),(26.5,51.0),
             (37.7859,11.4808),(37.3859,11.0808),(36.9859,10.6808)]:
    add_via(x, y, 0.3, 0.15, 'GND')

# 4) stitching vias 0.4/0.2 + F tracks
add_via(25.70, 52.90, 0.4, 0.2, 'GND')
add_track(25.18, 52.565, 25.70, 52.90, 0.1016, L['F'], 'GND')
add_via(38.40, 15.85, 0.4, 0.2, 'GND')
add_track(37.8699, 15.7945, 38.40, 15.85, 0.1016, L['F'], 'GND')

# 5) GND stub bridge
add_track(37.386, 10.681, 37.786, 10.681, 0.1016, L['F'], 'GND')

board.Save('vitalq_v2.kicad_pcb')
print('B1 applied: 2 jogs, 6 via-in-pad, 2 stitch vias, 1 stub bridge')
