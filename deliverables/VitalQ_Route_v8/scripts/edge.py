import pcbnew, math
board=pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
NM=1e6
print("=== EDGE.CUTS shapes ===")
for dr in board.GetDrawings():
    if dr.GetLayerName()=='Edge.Cuts':
        st=dr.GetShapeStr() if hasattr(dr,'GetShapeStr') else '?'
        try:
            s,e=dr.GetStart(),dr.GetEnd()
            print(f"  {dr.GetClass()} shape={st} ({s.x/NM:.3f},{s.y/NM:.3f})->({e.x/NM:.3f},{e.y/NM:.3f})")
        except Exception as ex:
            try:
                c=dr.GetCenter(); r=dr.GetRadius()
                print(f"  {dr.GetClass()} shape={st} center=({c.x/NM:.3f},{c.y/NM:.3f}) r={r/NM:.3f}")
            except Exception as ex2:
                print('  ?',ex,ex2)
print("\n=== courtyard overlaps (F.CrtYd/B.CrtYd bbox intersection) ===")
fps=list(board.GetFootprints())
boxes=[]
for fp in fps:
    bb=None
    # get courtyard bbox from footprint drawings
    for gd in fp.GraphicalItems():
        ln=gd.GetLayerName()
        if ln in ('F.Courtyard','B.Courtyard','F.CrtYd','B.CrtYd'):
            b=gd.GetBoundingBox()
            bb=b if bb is None else bb.Merge(b)
    if bb: boxes.append((fp.GetReference(),bb.GetX()/NM,bb.GetY()/NM,bb.GetRight()/NM,bb.GetBottom()/NM))
n=0
for i in range(len(boxes)):
    for j in range(i+1,len(boxes)):
        r1=boxes[i]; r2=boxes[j]
        ox=min(r1[3],r2[3])-max(r1[1],r2[1]); oy=min(r1[4],r2[4])-max(r1[2],r2[2])
        if ox>0 and oy>0:
            n+=1; print(f"  {r1[0]} x {r2[0]} overlap {ox:.2f}x{oy:.2f}mm")
print('overlaps:',n)
print("\n=== footprint edge proximity ===")
# board edge segments
edges=[]
for dr in board.GetDrawings():
    if dr.GetLayerName()=='Edge.Cuts' and dr.GetClass()=='PCB_SHAPE':
        try:
            edges.append((dr.GetStart().x/NM,dr.GetStart().y/NM,dr.GetEnd().x/NM,dr.GetEnd().y/NM))
        except: pass
print('edge segs:',len(edges))
if edges:
    xs=[e[0] for e in edges]+[e[2] for e in edges]; ys=[e[1] for e in edges]+[e[3] for e in edges]
    print('outline bbox: x',min(xs),max(xs),' y',min(ys),max(ys))
