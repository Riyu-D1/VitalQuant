import pcbnew, math
board=pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
NM=1e6
print("=== EDGE.CUTS ===")
edges=[]
for dr in board.GetDrawings():
    if dr.GetLayerName()!='Edge.Cuts': continue
    t=dr.GetShape()  # SHAPE_T
    tn=str(t)
    if t==pcbnew.SHAPE_T_SEGMENT or 'SEGMENT' in tn:
        s,e=dr.GetStart(),dr.GetEnd()
        edges.append((s.x/NM,s.y/NM,e.x/NM,e.y/NM))
        print(f"  seg ({s.x/NM:.3f},{s.y/NM:.3f})->({e.x/NM:.3f},{e.y/NM:.3f})")
    elif t==pcbnew.SHAPE_T_ARC or 'ARC' in tn:
        c=dr.GetCenter(); s=dr.GetStart(); e=dr.GetEnd()
        print(f"  arc c=({c.x/NM:.3f},{c.y/NM:.3f}) s=({s.x/NM:.3f},{s.y/NM:.3f}) e=({e.x/NM:.3f},{e.y/NM:.3f})")
    elif t==pcbnew.SHAPE_T_CIRCLE or 'CIRCLE' in tn:
        c=dr.GetCenter(); r=dr.GetRadius()
        print(f"  circle c=({c.x/NM:.3f},{c.y/NM:.3f}) r={r/NM:.3f}")
    elif t==pcbnew.SHAPE_T_RECT or 'RECT' in tn:
        s,e=dr.GetStart(),dr.GetEnd()
        print(f"  rect ({s.x/NM:.3f},{s.y/NM:.3f})->({e.x/NM:.3f},{e.y/NM:.3f})")
    elif t==pcbnew.SHAPE_T_POLY or 'POLY' in tn:
        ps=dr.GetPolyShape()
        out=ps.Outline(0)
        pts=[(out.GetPoint(i).x/NM,out.GetPoint(i).y/NM) for i in range(out.PointCount())]
        print('  poly',pts)
    else:
        print('  other',tn)
xs=[e[0] for e in edges]+[e[2] for e in edges]; ys=[e[1] for e in edges]+[e[3] for e in edges]
if xs: print('outline bbox: x',round(min(xs),2),'..',round(max(xs),2),' y',round(min(ys),2),'..',round(max(ys),2))
