import pcbnew

NM = 1e6
board = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
L = {'F': pcbnew.F_Cu, 'B': pcbnew.B_Cu, 'L3': pcbnew.In2_Cu}

nets = {ni.GetNetname(): ni.GetNetCode() for ni in board.GetNetInfo().NetsByName().values()}

def add_track(x1, y1, x2, y2, w, layer, netname):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I(int(x1*NM), int(y1*NM)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*NM), int(y2*NM)))
    t.SetWidth(int(w*NM)); t.SetLayer(layer)
    t.SetNetCode(nets[netname])
    board.Add(t)

# single pass: collect everything to remove
del_via_xy = [(25.70, 52.90), (38.40, 15.85)]
kill = []
for t in board.GetTracks():
    if t.Type() == pcbnew.PCB_VIA_T:
        p = t.GetPosition()
        if any(abs(p.x/NM-x) < 0.01 and abs(p.y/NM-y) < 0.01 for x, y in del_via_xy):
            kill.append(t)
    else:
        s, e = t.GetStart(), t.GetEnd()
        sx, sy, ex, ey = s.x/NM, s.y/NM, e.x/NM, e.y/NM
        if abs(sx-25.18) < 0.01 and abs(sy-52.565) < 0.01 and abs(ex-25.70) < 0.01 and abs(ey-52.90) < 0.01:
            kill.append(t)
        elif abs(sx-37.8699) < 0.01 and abs(sy-15.7945) < 0.01 and abs(ex-38.40) < 0.01 and abs(ey-15.85) < 0.01:
            kill.append(t)
        elif t.GetNetname() == 'AFE4900_RESETZ' and abs(sx-26.791) < 0.01 and abs(sy-50.933) < 0.01 and abs(ex-26.791) < 0.01 and abs(ey-51.335) < 0.01:
            kill.append(t)
for t in kill:
    board.Remove(t)
print('removed', len(kill))

# re-aimed stitch tracks to existing GND vias
add_track(25.18, 52.565, 25.798257, 52.987001, 0.1016, L['F'], 'GND')
add_track(37.8699, 15.7945, 38.535262, 15.830148, 0.1016, L['F'], 'GND')

# RESETZ dogleg x=26.791 -> x=26.86
for a, b in [((26.791,50.933),(26.86,51.0)),((26.86,51.0),(26.86,51.335)),((26.86,51.335),(26.791,51.335))]:
    add_track(a[0], a[1], b[0], b[1], 0.1017, L['B'], 'AFE4900_RESETZ')

board.Save('vitalq_v2.kicad_pcb')
print('fixes applied')
