import pcbnew, json
board = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
NM=1e6
out={'tracks':[],'vias':[],'pads':[],'zones':[],'keepouts':[],'edge':[],'fps':[]}
for t in board.GetTracks():
    if t.Type()==pcbnew.PCB_VIA_T:
        p=t.GetPosition()
        out['vias'].append({'net':t.GetNetname(),'x':p.x/NM,'y':p.y/NM,'d':t.GetWidth(pcbnew.F_Cu)/NM,'drill':t.GetDrillValue()/NM})
    else:
        s,e=t.GetStart(),t.GetEnd()
        out['tracks'].append({'net':t.GetNetname(),'l':t.GetLayer(),'x1':s.x/NM,'y1':s.y/NM,'x2':e.x/NM,'y2':e.y/NM,'w':t.GetWidth()/NM})
for fp in board.GetFootprints():
    ref=fp.GetReference(); side='B' if fp.GetSide() else 'F'
    pos=fp.GetPosition()
    out['fps'].append({'ref':ref,'x':pos.x/NM,'y':pos.y/NM,'side':side})
    rot=fp.GetOrientationDegrees() if hasattr(fp,'GetOrientationDegrees') else 0
    for p in fp.Pads():
        pp=p.GetPosition(); sz=p.GetSize(); off=p.GetOffset()
        prot=p.GetOrientationDegrees() if hasattr(p,'GetOrientationDegrees') else rot
        layers=[board.GetLayerName(l) for l in p.GetLayerSet().Seq() if pcbnew.IsCopperLayer(l)]
        out['pads'].append({'ref':ref,'no':p.GetNumber(),'net':p.GetNetname(),'x':pp.x/NM,'y':pp.y/NM,'w':sz.x/NM,'h':sz.y/NM,'layers':layers,'side':side,'shape':p.GetShape(),'rot':prot})
for z in board.Zones():
    layers=[board.GetLayerName(l) for l in z.GetLayerSet().Seq()]
    if z.GetIsRuleArea():
        # outline polygon
        poly=[]
        outl=z.Outline()
        for ci in range(outl.OutlineCount()):
            ol=outl.Outline(ci)
            for vi in range(ol.PointCount()):
                pt=ol.GetPoint(vi); poly.append([pt.x/NM,pt.y/NM])
        out['keepouts'].append({'name':z.GetZoneName(),'layers':layers,'tr':z.GetDoNotAllowTracks(),'via':z.GetDoNotAllowVias(),'fill':z.GetDoNotAllowZoneFills(),'poly':poly})
    else:
        # filled polys per layer
        for l in z.GetLayerSet().Seq():
            fps=z.GetFilledPolysList(l)
            polys=[]
            if fps:
                for ci in range(fps.OutlineCount()):
                    ol=fps.Outline(ci)
                    pts=[]
                    for vi in range(ol.PointCount()):
                        pt=ol.GetPoint(vi); pts.append([pt.x/NM,pt.y/NM])
                    polys.append(pts)
            outl=z.Outline()
            opts=[]
            for ci in range(outl.OutlineCount()):
                ol=outl.Outline(ci)
                pts=[[ol.GetPoint(vi).x/NM,ol.GetPoint(vi).y/NM] for vi in range(ol.PointCount())]
                opts.append(pts)
            out['zones'].append({'name':z.GetZoneName(),'net':z.GetNetname(),'layer':board.GetLayerName(l),'polys':polys,'outline':opts})
for dr in board.GetDrawings():
    if dr.GetLayerName()=='Edge.Cuts':
        try:
            s,e=dr.GetStart(),dr.GetEnd()
            out['edge'].append([s.x/NM,s.y/NM,e.x/NM,e.y/NM])
        except: pass
json.dump(out, open('geom.json','w'))
print('tracks',len(out['tracks']),'vias',len(out['vias']),'pads',len(out['pads']),'zones',len(out['zones']),'keepouts',len(out['keepouts']),'edge',len(out['edge']))
