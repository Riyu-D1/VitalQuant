import pcbnew, math
board = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
NM=1e6
def mm(v): return v/NM
for refwant in ['U6','U7','U22','U16','U11','U20','U5','U19','J5','J8','J12']:
    for fp in board.GetFootprints():
        if fp.GetReference()==refwant:
            pos=fp.GetPosition()
            print(f"\n=== {refwant} side={'B' if fp.GetSide() else 'F'} at ({mm(pos.x):.2f},{mm(pos.y):.2f}) ===")
            for p in fp.Pads():
                pp=p.GetPosition(); sz=p.GetSize()
                print(f"  {p.GetNumber():>3} net={p.GetNetname():<22} pos=({mm(pp.x):7.3f},{mm(pp.y):7.3f}) size=({mm(sz.x):.3f}x{mm(sz.y):.3f}) drill={mm(p.GetDrillSize().x):.2f} shape={p.GetShape()}")
            break
# board outline
print("\n=== EDGE.CUTS ===")
for dr in board.GetDrawings():
    if dr.GetLayerName()=='Edge.Cuts':
        try:
            s,e=dr.GetStart(),dr.GetEnd()
            print(f"  line ({mm(s.x):.2f},{mm(s.y):.2f})->({mm(e.x):.2f},{mm(e.y):.2f})")
        except: pass
