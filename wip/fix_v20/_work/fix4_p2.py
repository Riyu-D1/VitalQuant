import pcbnew,math,re
BASE='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/'
board=pcbnew.LoadBoard(BASE+'vitalq_v2.kicad_pcb')
MM=1e6
TOP,L3,L4,L6,BOT=0,6,8,12,2
def add_seg(x1,y1,x2,y2,net,layer,w=0.127):
    if abs(x2-x1)<1e-9 and abs(y2-y1)<1e-9: return
    t=pcbnew.PCB_TRACK(board);t.SetStart(pcbnew.VECTOR2I(int(round(x1*MM)),int(round(y1*MM))))
    t.SetEnd(pcbnew.VECTOR2I(int(round(x2*MM)),int(round(y2*MM))))
    t.SetWidth(int(w*MM));t.SetLayer(layer);t.SetNet(board.FindNet(net));board.Add(t)
def add_via(x,y,net):
    v=pcbnew.PCB_VIA(board);v.SetPosition(pcbnew.VECTOR2I(int(round(x*MM)),int(round(y*MM))))
    v.SetWidth(int(0.4*MM));v.SetDrill(int(0.2*MM));v.SetNet(board.FindNet(net));v.SetLayerPair(0,2);board.Add(v)

# 1. delete stale antenna_keepout (y -10.5..-9.6 under module); keep the new one (y<=-15.2)
for z in list(board.Zones()):
    if z.GetZoneName()=='antenna_keepout':
        o=z.Outline().Outline(0)
        ys=[o.CPoint(i).y/MM for i in range(o.PointCount())]
        if min(ys)>-15.0:
            board.Remove(z);print('removed old antenna_keepout')

# 2. new GND pour covering the tab on Ground Layer 2 + Ground Layer 7 (clone fill settings)
srcgnd=None
for z in board.Zones():
    if not z.GetIsRuleArea() and z.GetNetname()=='GND':
        srcgnd=z;break
for lay in ('Ground Layer 2','Ground Layer 7'):
    zn=pcbnew.ZONE(board)
    zn.SetNet(board.FindNet('GND'))
    ls=pcbnew.LSET();ls.addLayer(board.GetLayerID(lay));zn.SetLayerSet(ls)
    zn.Outline().NewOutline()
    for px,py in [(-1.9,-16.4),(22.3,-16.4),(22.3,-9.8),(-1.9,-9.8)]:
        zn.Outline().Append(int(px*MM),int(py*MM))
    if srcgnd:
        zn.SetFillMode(srcgnd.GetFillMode());zn.SetThermalReliefGap(srcgnd.GetThermalReliefGap())
        zn.SetThermalReliefSpokeWidth(srcgnd.GetThermalReliefSpokeWidth());zn.SetPadConnection(srcgnd.GetPadConnection())
        zn.SetLocalClearance(srcgnd.GetLocalClearance());zn.SetMinThickness(srcgnd.GetMinThickness())
    board.Add(zn)
print('pour zones:')
for z in board.Zones():
    if not z.GetIsRuleArea():
        o=z.Outline().Outline(0)
        xs=[o.CPoint(i).x/MM for i in range(o.PointCount())];ys=[o.CPoint(i).y/MM for i in range(o.PointCount())]
        print(' ',z.GetNetname(),[board.GetLayerName(l) for l in z.GetLayerSet().Seq()],'bbox',round(min(xs),2),round(min(ys),2),round(max(xs),2),round(max(ys),2))
# 3. FID4
for m in board.GetFootprints():
    if m.GetReference()=='FID4':
        m.SetPosition(pcbnew.VECTOR2I(int(-0.4*MM),int(-10.3*MM)));print('FID4 moved')
# 4. bridge via<->stub gaps (Top)
BRIDGES=[('USB_DM',[(22.1,-6.32,23.841,-6.321),(21.02,5.0,21.022,6.433),(22.9173,13.5533,22.0182,12.8378)]),
         ('USB_DP',[(22.9,-6.09,23.793,-6.086),(21.26,4.4,21.258,6.482),(10.4425,12.7799,10.5967,13.3272)]),
         ('IN1P_AC',[(21.6,0.52,21.515,0.531),(21.6,-2.05,21.747,-2.053)]),
         ('ECG_P',[(21.6,-7.1,21.951,-7.102),(15.72,4.4,15.716,4.893)])]
for net,links in BRIDGES:
    for a,b,c,d in links: add_seg(a,b,c,d,net,TOP,0.127)
board.Save(BASE+'vitalq_v2.kicad_pcb')
print('saved')
