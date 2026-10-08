import pcbnew, sys
f=sys.argv[1]
board=pcbnew.LoadBoard(f)
filler=pcbnew.ZONE_FILLER(board)
filler.Fill(board.Zones())
board.Save(f)
print('refilled',f)
