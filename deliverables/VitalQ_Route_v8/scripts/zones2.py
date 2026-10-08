import pcbnew
board = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
print("=== COPPER ZONES (non-rule-area) ===")
for i, z in enumerate(board.Zones()):
    if z.GetIsRuleArea():
        continue
    name = z.GetZoneName()
    net = z.GetNetname()
    layers = [board.GetLayerName(l) for l in z.GetLayerSet().Seq()]
    bbox = z.GetBoundingBox()
    filled = z.IsFilled()

    print(f"[{i}] '{name}' net={net} filled={filled} pri={z.GetAssignedPriority()} L={layers}")
    print(f"     bbox=({bbox.GetX()/1e6:.2f},{bbox.GetY()/1e6:.2f})-({bbox.GetRight()/1e6:.2f},{bbox.GetBottom()/1e6:.2f})")
    # per-layer fill
    for l in z.GetLayerSet().Seq():
        try:
            has = z.HasFilledPolysForLayer(l)
            polys = z.GetFilledPolysList(l)
            n = polys.OutlineCount() if polys else 0
            print(f"       {board.GetLayerName(l)}: filled_polys={has} outlines={n}")
        except Exception as e:
            print(f"       {board.GetLayerName(l)}: err {e}")
