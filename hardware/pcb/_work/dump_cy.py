import pcbnew,json,sys
b=pcbnew.LoadBoard(sys.argv[1]); mm=lambda v:v/1e6
out={}
for fp in b.GetFootprints():
    r=fp.GetReference()
    seg=[]
    for g in fp.GraphicalItems():
        if g.GetLayerName() in ('F.CrtYd','B.CrtYd'):
            try:
                bb=g.GetBoundingBox()
                seg.append([mm(bb.GetX()),mm(bb.GetY()),mm(bb.GetRight()),mm(bb.GetBottom())])
            except Exception: pass
    pos=fp.GetPosition(); bb=fp.GetBoundingBox(False)
    out[r]={'pos':[round(mm(pos.x),3),round(mm(pos.y),3)],'rot':fp.GetOrientationDegrees(),
            'cy':[[round(x,3) for x in s] for s in seg],
            'bbox':[round(mm(bb.GetX()),3),round(mm(bb.GetY()),3),round(mm(bb.GetRight()),3),round(mm(bb.GetBottom()),3)]}
json.dump(out,open(sys.argv[2],'w'),indent=0)
