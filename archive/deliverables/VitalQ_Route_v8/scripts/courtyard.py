import pcbnew
board=pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
NM=1e6
boxes=[]
for fp in board.GetFootprints():
    bb=None
    try:
        for gd in fp.GraphicalItems():
            ln=gd.GetLayerName()
            if ln in ('F.Courtyard','B.Courtyard'):
                b=gd.GetBoundingBox()
                bb=b if bb is None else bb.Merge(b)
    except Exception as e:
        pass
    if bb: boxes.append((fp.GetReference(),bb.GetX()/NM,bb.GetY()/NM,bb.GetRight()/NM,bb.GetBottom()/NM,'F' if not fp.GetSide() else 'B'))
n=0
for i in range(len(boxes)):
    for j in range(i+1,len(boxes)):
        r1,r2=boxes[i],boxes[j]
        ox=min(r1[3],r2[3])-max(r1[1],r2[1]); oy=min(r1[4],r2[4])-max(r1[2],r2[2])
        if ox>0.005 and oy>0.005:
            n+=1; print(f"  {r1[0]}({r1[5]}) x {r2[0]}({r2[5]}) overlap {ox:.2f}x{oy:.2f}mm")
print('overlaps:',n)
