import json
import wx
APP = wx.App(False)
import pcbnew
from r2 import ROOT, begin, candidate_dir, verify, finish, save_json
from session import drc

BATCH = 'R2-1'
checkpoint, _ = begin(BATCH, 'restack_SGSSPS')
d = candidate_dir(BATCH)
board = pcbnew.LoadBoard(str(ROOT / 'vitalq_v2.kicad_pcb'))


def zone_record(z):
    poly = z.Outline()
    return {'uuid': z.m_Uuid.AsString(), 'name': z.GetZoneName(), 'net': z.GetNetname(), 'layers': [int(l) for l in z.GetLayerSet().Seq()], 'priority': z.GetAssignedPriority(), 'clearance_nm': z.GetLocalClearance(), 'min_thickness_nm': z.GetMinThickness(), 'thermal_gap_nm': z.GetThermalReliefGap(), 'thermal_spoke_nm': z.GetThermalReliefSpokeWidth(), 'pad_connection': z.GetPadConnection(), 'island_removal': z.GetIslandRemovalMode(), 'min_island_area': z.GetMinIslandArea(), 'fill_mode': z.GetFillMode(), 'outline': [[[poly.Outline(i).CPoint(j).x, poly.Outline(i).CPoint(j).y] for j in range(poly.Outline(i).PointCount())] for i in range(poly.OutlineCount())]}


zones = {z.GetZoneName(): z for z in board.Zones() if not z.GetIsRuleArea()}
before = {n: zone_record(z) for n, z in zones.items()}
(ROOT / 'archive').mkdir(exist_ok=True)
save_json(ROOT / 'archive' / 'R2-1_removed_zone_GND.LAYER_4.json', {'reason': 'R2-1 restack S/G/S/S/P/S: In4 GND pour removed, In4 becomes power; GND remains on In1 GND.LAYER_1. The full original zone also remains in backup ' + str(checkpoint), 'zone': before['GND.LAYER_4']})
for name in ('+3V3.LAYER_3', 'VBAT_SYS.LAYER_3'):
    assert list(zones[name].GetLayerSet().Seq()) == [pcbnew.In3_Cu]
    zones[name].SetLayer(pcbnew.In4_Cu)
assert list(zones['GND.LAYER_4'].GetLayerSet().Seq()) == [pcbnew.In4_Cu]
board.Remove(zones['GND.LAYER_4'])
board.SetLayerName(pcbnew.In3_Cu, 'Signal Layer 4')
board.SetLayerName(pcbnew.In4_Cu, 'Power Layer 5')
pcbnew.SaveBoard(str(d / 'vitalq_v2.kicad_pcb'), board)

check = pcbnew.LoadBoard(str(d / 'vitalq_v2.kicad_pcb'))
after = {z.GetZoneName(): zone_record(z) for z in check.Zones() if not z.GetIsRuleArea()}
expected = {}
ok = 'GND.LAYER_4' not in after and set(after) == set(before) - {'GND.LAYER_4'}
for name, rec in before.items():
    if name == 'GND.LAYER_4':
        continue
    want = dict(rec)
    if name in ('+3V3.LAYER_3', 'VBAT_SYS.LAYER_3'):
        want['layers'] = [int(pcbnew.In4_Cu)]
    got = after[name]
    ok &= got == want
    expected[name] = {'before_layers': rec['layers'], 'after_layers': got['layers']}
names = {l: check.GetLayerName(l) for l in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu, pcbnew.In4_Cu, pcbnew.B_Cu)}
ok &= names[pcbnew.In3_Cu] == 'Signal Layer 4' and names[pcbnew.In4_Cu] == 'Power Layer 5' and names[pcbnew.In1_Cu] == 'Ground Layer 2'
ok &= sum(1 for t in check.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetLayer() in (pcbnew.In3_Cu, pcbnew.In4_Cu)) == 0
save_json(d / 'restack_check.json', {'zones': expected, 'layer_names': {str(k): v for k, v in names.items()}, 'pass': ok})
print('restack structural check', ok, json.dumps(expected), flush=True)
drc(d / 'vitalq_v2.kicad_pcb', d / 'drc.json')
verify(d, allow_zone_changes=True)
save_json(d / 'applied.json', {'batch': BATCH, 'backup': str(checkpoint), 'zones_moved_In3_to_In4': ['+3V3.LAYER_3', 'VBAT_SYS.LAYER_3'], 'zone_removed': 'GND.LAYER_4 (archived)', 'renamed': {'In3.Cu': 'Signal Layer 4', 'In4.Cu': 'Power Layer 5'}, 'rule_areas': 'untouched'})
finish(BATCH, d, 'stackup S/G/S/S/P/S (zones moved, In4 GND pour archived, layers renamed)', 'R2-1 restack', 'R2-2 U18 nudge, C59/U18 +3V3 stitches, U19 outer-layer escapes', extra_ok=ok)
