import pcbnew, json, sys
b=pcbnew.LoadBoard(sys.argv[1]); mm=lambda v: v/1e6
out={'tracks':[], 'vias':[], 'pads':[], 'edge':[]}
for t in b.GetTracks():
    if t.GetClass()=='PCB_VIA':
        out['vias'].append([mm(t.GetPosition().x),mm(t.GetPosition().y),mm(t.GetWidth(pcbnew.F_Cu)) if hasattr(t,'GetWidth') else 0.45,t.GetNetname()])
    elif t.GetClass()=='PCB_TRACK':
        out['tracks'].append([b.GetLayerName(t.GetLayer()),mm(t.GetStart().x),mm(t.GetStart().y),mm(t.GetEnd().x),mm(t.GetEnd().y),mm(t.GetWidth()),t.GetNetname()])
for fp in b.GetFootprints():
    for p in fp.Pads():
        bb=p.GetBoundingBox()
        out['pads'].append([fp.GetReference(),p.GetNumber(),mm(bb.GetX()),mm(bb.GetY()),mm(bb.GetWidth()),mm(bb.GetHeight()),p.GetNetname(),[b.GetLayerName(l) for l in p.GetLayerSet().CuStack()]])
for d in b.GetDrawings():
    if d.GetLayer()==pcbnew.Edge_Cuts:
        out['edge'].append([d.GetClass(),mm(d.GetStart().x),mm(d.GetStart().y),mm(d.GetEnd().x),mm(d.GetEnd().y)])
json.dump(out,open(sys.argv[2],'w'))
print(len(out['tracks']),len(out['vias']),len(out['pads']),len(out['edge']))
