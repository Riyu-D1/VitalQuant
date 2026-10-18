#!/usr/bin/env python3
"""Import a Freerouting .ses session back into the board, fill zones, save.

Usage: pcbnew-python route_import.py <board.kicad_pcb> <session.ses> [out.kicad_pcb]
Saves in place when no output path is given.
"""
import sys

import pcbnew

board_path = sys.argv[1]
ses_path = sys.argv[2]
out_path = sys.argv[3] if len(sys.argv) > 3 else board_path

board = pcbnew.LoadBoard(board_path)
if not pcbnew.ImportSpecctraSES(board, ses_path):
    print("SES import FAILED")
    sys.exit(1)

# Fill all copper zones (pours are deliberately unfilled pre-route).
filler = pcbnew.ZONE_FILLER(board)
filler.Fill(board.Zones(), False)

pcbnew.SaveBoard(out_path, board)

tracks = len(board.GetTracks())
vias = sum(1 for t in board.GetTracks() if t.Type() == pcbnew.PCB_VIA_T)
print(f"imported {tracks} track items ({vias} vias), zones filled -> {out_path}")
