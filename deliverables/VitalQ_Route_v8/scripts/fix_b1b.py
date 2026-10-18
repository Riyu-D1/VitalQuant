import pcbnew

NM = 1e6
board = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')

nets = {ni.GetNetname(): ni.GetNetCode() for ni in board.GetNetInfo().NetsByName().values()}

def add_track(x1, y1, x2, y2, w, layer, netname):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I(int(x1*NM), int(y1*NM)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2*NM), int(y2*NM)))
    t.SetWidth(int(w*NM)); t.SetLayer(layer)
    t.SetNetCode(nets[netname])
    board.Add(t)

kill = []
for t in board.GetTracks():
    if t.Type() == pcbnew.PCB_VIA_T or t.GetNetname() != 'AFE4900_RESETZ':
        continue
    s, e = t.GetStart(), t.GetEnd()
    sx, sy, ex, ey = s.x/NM, s.y/NM, e.x/NM, e.y/NM
    # diagonal ending at the bend
    if abs(ex-26.791) < 0.01 and abs(ey-50.933) < 0.01:
        t.SetEnd(pcbnew.VECTOR2I(int(26.86*NM), int(50.95*NM)))
        print('diag end moved')
    # my two dogleg segments off that bend
    if abs(sx-26.791) < 0.01 and abs(sy-50.933) < 0.01:
        kill.append(t)
    if abs(sx-26.86) < 0.01 and abs(sy-51.0) < 0.01 and abs(ex-26.86) < 0.01 and abs(ey-51.335) < 0.01:
        kill.append(t)
for t in kill:
    board.Remove(t)
add_track(26.86, 50.95, 26.86, 51.335, 0.1017, pcbnew.B_Cu, 'AFE4900_RESETZ')

board.Save('vitalq_v2.kicad_pcb')
print('done')
