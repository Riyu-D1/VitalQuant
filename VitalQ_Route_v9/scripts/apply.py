import json
from pathlib import Path
import shutil
import sys
import wx
APP = wx.App(False)
import pcbnew
from session import ROOT, drc, save_json


def vec(xy):
    return pcbnew.VECTOR2I(round(xy[0] * 1e6), round(xy[1] * 1e6))


def main():
    state_path = ROOT / 'state.json'
    if state_path.exists():
        assert json.loads(state_path.read_text())['streak'] < 2, 'Mandatory routing stop: no further candidates permitted'
    plan = json.loads(Path(sys.argv[1]).read_text())
    candidate = ROOT / 'candidates' / plan['batch']
    candidate.mkdir(parents=True, exist_ok=False)
    for ext in ('kicad_pro', 'kicad_dru', 'kicad_sch'):
        shutil.copy2(ROOT / ('vitalq_v2.' + ext), candidate / ('vitalq_v2.' + ext))
    board = pcbnew.LoadBoard(str(ROOT / 'vitalq_v2.kicad_pcb'))
    items = {t.m_Uuid.AsString(): t for t in board.GetTracks()}
    nets = {n.GetNetname(): n.GetNetCode() for n in board.GetNetsByNetcode().values()}
    for record in plan.get('remove', []):
        board.Remove(items[record['uuid']])
    for record in plan.get('modify_vias', []):
        via = items[record['uuid']]
        via.SetPosition(vec(record['xy']))
        via.SetWidth(round(record['diameter'] * 1e6))
        via.SetDrill(round(record['drill'] * 1e6))
    for record in plan.get('tracks', []):
        track = pcbnew.PCB_TRACK(board)
        track.SetStart(vec(record['a']))
        track.SetEnd(vec(record['b']))
        track.SetWidth(round(record['width'] * 1e6))
        track.SetLayer(record['layer'])
        track.SetNetCode(nets[record['net']])
        board.Add(track)
        record['uuid'] = track.m_Uuid.AsString()
    for record in plan.get('vias', []):
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(vec(record['xy']))
        via.SetWidth(round(record['diameter'] * 1e6))
        via.SetDrill(round(record['drill'] * 1e6))
        via.SetViaType(pcbnew.VIATYPE_THROUGH)
        via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        via.SetNetCode(nets[record['net']])
        if record.get('pofv'):
            via.SetFillingMode(pcbnew.FILLING_MODE_FILLED)
            via.SetCappingMode(pcbnew.CAPPING_MODE_CAPPED)
        board.Add(via)
        record['uuid'] = via.m_Uuid.AsString()
    pcbnew.SaveBoard(str(candidate / 'vitalq_v2.kicad_pcb'), board)
    save_json(candidate / 'applied.json', plan)
    drc(candidate / 'vitalq_v2.kicad_pcb', candidate / 'drc.json')
    print('Candidate retained for audit:', candidate, flush=True)


if __name__ == '__main__':
    main()
