import pcbnew
board = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
print("=== ZONES ===")
for i, z in enumerate(board.Zones()):
    name = z.GetZoneName()
    net = z.GetNetname()
    ko = z.GetIsRuleArea()
    keepout = ""
    if ko:
        keepout = f" KO(tr={z.GetDoNotAllowTracks()} via={z.GetDoNotAllowVias()} pad={z.GetDoNotAllowPads()} fill={z.GetDoNotAllowZoneFills()} fp={z.GetDoNotAllowFootprints()})"
    layers = [board.GetLayerName(l) for l in z.GetLayerSet().Seq()]
    bbox = z.GetBoundingBox()
    print(f"[{i}] '{name}' net={net} rule={ko}{keepout} pri={z.GetAssignedPriority()} L={layers}")
    print(f"     bbox=({bbox.GetX()/1e6:.2f},{bbox.GetY()/1e6:.2f})-({bbox.GetRight()/1e6:.2f},{bbox.GetBottom()/1e6:.2f})")
