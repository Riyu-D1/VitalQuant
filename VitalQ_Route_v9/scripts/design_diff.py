import hashlib
import json
from pathlib import Path
import sys
import wx
APP = wx.App(False)
import pcbnew
from session import ROOT, save_json


def xy(p):
    return [p.x, p.y]


def uid(p):
    return p.m_Uuid.AsString()


def polys(s):
    def ring(c):
        return [xy(c.CPoint(i)) for i in range(c.PointCount())]
    return [{'outer': ring(s.Outline(i)), 'holes': [ring(s.Hole(i, j)) for j in range(s.HoleCount(i))]} for i in range(s.OutlineCount())]


def snapshot(path):
    b = pcbnew.LoadBoard(str(path))
    result = {'copper_layers': b.GetCopperLayerCount(), 'footprints': {}, 'pads': {}, 'tracks': {}, 'vias': {}, 'rule_areas': {}}
    for f in b.GetFootprints():
        result['footprints'][uid(f)] = {'ref': f.GetReference(), 'xy_nm': xy(f.GetPosition()), 'rotation': f.GetOrientationDegrees(), 'layer': f.GetLayer(), 'locked': f.IsLocked(), 'value': f.GetValue()}
        for p in f.Pads():
            result['pads'][uid(p)] = {'ref': f.GetReference(), 'number': p.GetNumber(), 'net': p.GetNetname(), 'xy_nm': xy(p.GetPosition()), 'size_nm': xy(p.GetSize()), 'rotation': p.GetOrientationDegrees(), 'layers': list(p.GetLayerSet().Seq()), 'shape': p.GetShape(), 'drill_nm': xy(p.GetDrillSize()), 'attribute': p.GetAttribute()}
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            result['vias'][uid(t)] = {'net': t.GetNetname(), 'xy_nm': xy(t.GetPosition()), 'diameter_nm': t.GetWidth(pcbnew.F_Cu), 'drill_nm': t.GetDrillValue(), 'type': t.GetViaType(), 'filled': t.GetFillingMode(), 'capped': t.GetCappingMode(), 'layers': list(t.GetLayerSet().Seq())}
        else:
            result['tracks'][uid(t)] = {'net': t.GetNetname(), 'a_nm': xy(t.GetStart()), 'b_nm': xy(t.GetEnd()), 'layer': t.GetLayer(), 'width_nm': t.GetWidth(), 'type': t.Type()}
    for z in list(b.Zones()) + [z for f in b.GetFootprints() for z in f.Zones()]:
        if z.GetIsRuleArea():
            result['rule_areas'][uid(z)] = {'name': z.GetZoneName(), 'layers': list(z.GetLayerSet().Seq()), 'polygons_nm': polys(z.Outline()), 'tracks': z.GetDoNotAllowTracks(), 'vias': z.GetDoNotAllowVias(), 'fills': z.GetDoNotAllowZoneFills(), 'pads': z.GetDoNotAllowPads(), 'footprints': z.GetDoNotAllowFootprints()}
    return result


original = ROOT / 'backups' / '20261009_114802_B0_original'
before = snapshot(original / 'vitalq_v2.kicad_pcb')
after = snapshot(ROOT / 'vitalq_v2.kicad_pcb')
diff = {k: {'added': sorted(set(after[k]) - set(before[k])), 'removed': sorted(set(before[k]) - set(after[k])), 'changed': [{'uuid': u, 'before': before[k][u], 'after': after[k][u]} for u in sorted(set(before[k]) & set(after[k])) if before[k][u] != after[k][u]]} for k in ('footprints', 'pads', 'tracks', 'vias', 'rule_areas')}
checks = {'six_layers': before['copper_layers'] == after['copper_layers'] == 6, 'schematic_byte_identical': (original / 'vitalq_v2.kicad_sch').read_bytes() == (ROOT / 'vitalq_v2.kicad_sch').read_bytes(), 'no_parts_pad_nets_tracks_or_rule_areas_changed': not any(diff[k][mode] for k in ('footprints', 'pads', 'tracks', 'rule_areas') for mode in ('added', 'removed', 'changed')), 'only_five_vias_resized': len(diff['vias']['changed']) == 5 and not diff['vias']['added'] and not diff['vias']['removed']}
for change in diff['vias']['changed']:
    a, b = change['before'], change['after']
    checks['only_five_vias_resized'] &= a['diameter_nm'] == 600000 and a['drill_nm'] == 300000 and b['diameter_nm'] == 400000 and b['drill_nm'] == 200000 and {k: v for k, v in a.items() if k not in ('diameter_nm', 'drill_nm')} == {k: v for k, v in b.items() if k not in ('diameter_nm', 'drill_nm')}
result = {'checks': checks, 'diff': diff, 'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in ROOT.glob('vitalq_v2.*') if p.suffix in ('.kicad_pcb', '.kicad_pro', '.kicad_sch', '.kicad_dru')}, 'pass': all(checks.values())}
save_json(ROOT / 'design_diff_final.json', result)
print(json.dumps(checks, indent=2), flush=True)
assert result['pass']
