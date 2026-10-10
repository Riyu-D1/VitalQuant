import gzip
import hashlib
import json
from pathlib import Path
import sys
import wx
APP = wx.App(False)
import pcbnew

ALL_LAYERS = [pcbnew.F_Cu, pcbnew.B_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu, pcbnew.In4_Cu, pcbnew.In5_Cu, pcbnew.In6_Cu]
LAYERS = ALL_LAYERS


def xy(p):
    return [p.x / 1e6, p.y / 1e6]


def polygons(poly):
    def ring(chain):
        return [xy(chain.CPoint(i)) for i in range(chain.PointCount())]
    return [{'outer': ring(poly.Outline(i)), 'holes': [ring(poly.Hole(i, j)) for j in range(poly.HoleCount(i))]} for i in range(poly.OutlineCount())]


def uid(item):
    return item.m_Uuid.AsString()


def extract(source, output):
    board = pcbnew.LoadBoard(str(source))
    board.BuildConnectivity()
    conn = board.GetConnectivity()
    data = {'source': str(source), 'sha256': hashlib.sha256(Path(source).read_bytes()).hexdigest(), 'layers': {l: board.GetLayerName(l) for l in LAYERS if board.IsLayerEnabled(l)}, 'pads': [], 'tracks': [], 'vias': [], 'zones': [], 'keepouts': [], 'footprints': []}
    for fp in board.GetFootprints():
        data['footprints'].append({'ref': fp.GetReference(), 'position': xy(fp.GetPosition()), 'rotation': fp.GetOrientationDegrees(), 'layer': fp.GetLayer(), 'locked': fp.IsLocked()})
        for p in fp.Pads():
            layers = [l for l in LAYERS if p.IsOnLayer(l)]
            data['pads'].append({'uuid': uid(p), 'ref': fp.GetReference(), 'number': p.GetNumber(), 'net': p.GetNetname(), 'xy': xy(p.GetPosition()), 'size': xy(p.GetSize()), 'rotation': p.GetOrientationDegrees(), 'attribute': p.GetAttribute(), 'drill': xy(p.GetDrillSize()), 'layers': layers, 'polys': {l: polygons(p.GetEffectivePolygon(l)) for l in layers}, 'connected': [uid(t) for t in conn.GetConnectedItems(p)]})
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            data['vias'].append({'uuid': uid(t), 'net': t.GetNetname(), 'xy': xy(t.GetPosition()), 'diameter': t.GetWidth(pcbnew.F_Cu) / 1e6, 'drill': t.GetDrillValue() / 1e6, 'layers': [l for l in LAYERS if t.IsOnLayer(l)], 'via_type': t.GetViaType(), 'filled': t.GetFillingMode() == pcbnew.FILLING_MODE_FILLED, 'capped': t.GetCappingMode() == pcbnew.CAPPING_MODE_CAPPED})
        else:
            assert t.Type() == pcbnew.PCB_TRACE_T
            data['tracks'].append({'uuid': uid(t), 'net': t.GetNetname(), 'layer': t.GetLayer(), 'a': xy(t.GetStart()), 'b': xy(t.GetEnd()), 'width': t.GetWidth() / 1e6})
    for z, parent in [(z, None) for z in board.Zones()] + [(z, fp.GetReference()) for fp in board.GetFootprints() for z in fp.Zones()]:
        record = {'uuid': uid(z), 'name': z.GetZoneName(), 'parent': parent, 'net': z.GetNetname(), 'layers': [l for l in LAYERS if z.IsOnLayer(l)], 'outline': polygons(z.Outline())}
        if z.GetIsRuleArea():
            record.update(tracks=z.GetDoNotAllowTracks(), vias=z.GetDoNotAllowVias(), fills=z.GetDoNotAllowZoneFills())
            data['keepouts'].append(record)
        else:
            record.update(fill={l: polygons(z.GetFilledPolysList(l)) for l in record['layers']}, thermal_gap=z.GetThermalReliefGap() / 1e6, thermal_spoke=z.GetThermalReliefSpokeWidth() / 1e6, connection=z.GetPadConnection(), min_island_area=z.GetMinIslandArea() / 1e12, island_removal=z.GetIslandRemovalMode())
            data['zones'].append(record)
    outline = pcbnew.SHAPE_POLY_SET()
    assert board.GetBoardPolygonOutlines(outline, False)
    data['outline'] = polygons(outline)
    with gzip.open(output, 'wt', compresslevel=1) as stream:
        json.dump(data, stream, separators=(',', ':'))
    print({k: len(data[k]) for k in ('pads', 'tracks', 'vias', 'zones', 'keepouts', 'outline')}, flush=True)


if __name__ == '__main__':
    extract(sys.argv[1], sys.argv[2])
