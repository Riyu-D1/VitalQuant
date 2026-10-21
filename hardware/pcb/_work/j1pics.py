import pcbnew,json,sys
b=pcbnew.LoadBoard(sys.argv[1]); mm=lambda v:v/1e6
X0,X1,Y0,Y1=15,42,-20,4
L={}
for t in b.GetTracks():
    if t.GetClass()=='PCB_TRACK':
        s,e=t.GetStart(),t.GetEnd()
        if X0<mm(s.x)<X1 and Y0<mm(s.y)<Y1 or X0<mm(e.x)<X1 and Y0<mm(e.y)<Y1:
            L.setdefault(b.GetLayerName(t.GetLayer()),[]).append([mm(s.x),mm(s.y),mm(e.x),mm(e.y),mm(t.GetWidth()),t.GetNetname()])
V=[[mm(v.GetPosition().x),mm(v.GetPosition().y),v.GetNetname()] for v in b.GetTracks() if v.GetClass()=='PCB_VIA' and X0<mm(v.GetPosition().x)<X1 and Y0<mm(v.GetPosition().y)<Y1]
F={}
for fp in b.GetFootprints():
    bb=fp.GetBoundingBox(False)
    if mm(bb.GetRight())>X0 and mm(bb.GetX())<X1 and mm(bb.GetBottom())>Y0 and mm(bb.GetY())<Y1:
        F[fp.GetReference()]=[mm(bb.GetX()),mm(bb.GetY()),mm(bb.GetRight()),mm(bb.GetBottom()),'B' if fp.IsFlipped() else 'F',[[p.GetNumber(),mm(p.GetPosition().x),mm(p.GetPosition().y),p.GetNetname()] for p in fp.Pads()]]
E=[[mm(d.GetStart().x),mm(d.GetStart().y),mm(d.GetEnd().x),mm(d.GetEnd().y)] for d in b.GetDrawings() if d.GetLayer()==pcbnew.Edge_Cuts]
json.dump({'L':L,'V':V,'F':F,'E':E},open(sys.argv[2],'w'))
