"""Copper finish for the placed VitalQ board: net classes, keep-outs, fan-out, pours, silk."""

from __future__ import annotations

import pcbnew

BOARD_W = 40.0
BOARD_H = 62.0

HV_NETS = (
    "ECG1_PAD", "ECG2_PAD", "RLD_PAD", "AFE_P_PAD", "AFE_N_PAD",
    "EDA_CE_PAD", "EDA_SE_PAD", "EDA_RE_PAD", "EDA_DE_PAD",
    "BIOZ_FP_PAD", "BIOZ_FN_PAD", "BIOZ_SP_PAD", "BIOZ_SN_PAD",
    # Connector-side tail nets upstream of the series-R footprints.
    # The device-side nets (SWEAT_*, MX_ECG_*) stay in the default
    # class on purpose: at U7/U22 they fan out of 0.4/0.35 mm BGA
    # fields where the HV class clearance and pad-shadow keepouts
    # cannot physically fit. The 0R cut-points (R118-R122) are the
    # HV boundary; protection clamps sit IC-side by design.
    "J11_WE", "J11_RE", "J11_CE", "J13_INP", "J13_INM",
)
LV_NETS = (
    "ECG_P", "ECG_N", "RLD_CLAMP", "AFE_P_AC", "AFE_N_AC",
    "CE_SURGE", "SE_SURGE", "RE_SURGE", "DE_SURGE",
    "FP_SURGE", "FN_SURGE", "SP_SURGE", "SN_SURGE",
)


def mm(v):
    return pcbnew.FromMM(v)


def _netclass(board, name, clearance, width, via_d=0.6, via_h=0.3):
    nc = pcbnew.NETCLASS(name)
    nc.SetClearance(mm(clearance))
    nc.SetTrackWidth(mm(width))
    nc.SetViaDiameter(mm(via_d))
    nc.SetViaDrill(mm(via_h))
    board.GetDesignSettings().m_NetSettings.SetNetclass(name, nc)


def _assign(board, names, netclass):
    settings = board.GetDesignSettings().m_NetSettings
    for name in names:
        if board.FindNet(name) is None:
            continue
        settings.SetNetclassPatternAssignment(name, netclass)


def _rect_zone(board, layers, pts, name, **flags):
    zone = pcbnew.ZONE(board)
    zone.SetIsRuleArea(True)
    zone.SetZoneName(name)
    zone.SetDoNotAllowTracks(flags.get("tracks", False))
    zone.SetDoNotAllowVias(flags.get("vias", False))
    zone.SetDoNotAllowZoneFills(flags.get("pours", False))
    zone.SetDoNotAllowPads(flags.get("pads", False))
    zone.SetDoNotAllowFootprints(False)
    lset = pcbnew.LSET()
    for layer in layers:
        lset.AddLayer(layer)
    zone.SetLayerSet(lset)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in pts:
        outline.Append(mm(x), mm(y))
    board.Add(zone)
    return zone


def _box(pad, margin):
    b = pad.GetBoundingBox()
    x0 = b.GetX() / 1e6 - margin
    y0 = b.GetY() / 1e6 - margin
    x1 = (b.GetX() + b.GetWidth()) / 1e6 + margin
    y1 = (b.GetY() + b.GetHeight()) / 1e6 + margin
    return x0, y0, x1, y1


def _hv_keepouts(board):
    """No inner copper and no vias under defibrillation copper. The pad's own layer stays."""
    rects = []
    for fp in board.GetFootprints():
        own = pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu
        blocked = [layer for layer in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu) if layer != own]
        for pad in fp.Pads():
            if pad.GetNetname() not in HV_NETS:
                continue
            x0, y0, x1, y1 = _box(pad, 1.0)
            rects.append((x0, y0, x1, y1))
            _rect_zone(
                board, blocked,
                ((x0, y0), (x1, y0), (x1, y1), (x0, y1)),
                "hv_inner", tracks=True, vias=True, pours=True,
            )
            # Own layer: bar pours only inside the pad shadow — the pad
            # still needs track access, but a same-layer pour must not
            # flush to within the zone-clearance of defib copper.
            _rect_zone(
                board, (own,),
                ((x0, y0), (x1, y0), (x1, y1), (x0, y1)),
                "hv_ownlayer", pours=True,
            )
    return rects


def _pour_keepouts(board):
    """No L3 power copper under the temperature sensors. L4 ground may still neck into the TMP117 island."""
    regions = (
        (15.9, 5.3, 21.3, 11.4),  # TMP117 island
        (7.3, 18.4, 12.8, 24.2),  # MLX90632
    )
    for x0, y0, x1, y1 in regions:
        _rect_zone(
            board, (pcbnew.In2_Cu,),
            ((x0, y0), (x1, y0), (x1, y1), (x0, y1)),
            "sensor_nopower", pours=True,
        )


def _hole_keepouts(board):
    for fp in board.GetFootprints():
        if not fp.GetReference().startswith("H"):
            continue
        pos = fp.GetPosition()
        cx, cy = pos.x / 1e6, pos.y / 1e6
        # 2.2 mm hole, plus 1.0 mm copper keep-out.
        r = 1.1 + 1.0
        _rect_zone(
            board,
            (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu),
            ((cx - r, cy - r), (cx + r, cy - r), (cx + r, cy + r), (cx - r, cy + r)),
            "hole_keepout", tracks=True, vias=True, pours=True,
        )


def _bga_areas(board):
    # U22 = MAX86178 WLP-49 at 0.35 mm pitch: perimeter balls escape direct,
    # the interior power column needs VIP+POFV per ROUTING_PLAN.md.
    for ref in ("U6", "U7", "U22"):
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            continue
        box = fp.GetBoundingBox(False, False)
        x0, y0 = box.GetX() / 1e6 - 0.4, box.GetY() / 1e6 - 0.4
        x1 = (box.GetX() + box.GetWidth()) / 1e6 + 0.4
        y1 = (box.GetY() + box.GetHeight()) / 1e6 + 0.4
        _rect_zone(
            board,
            (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu),
            ((x0, y0), (x1, y0), (x1, y1), (x0, y1)),
            "bga_fanout",
        )


def _is_inner(pad, pads):
    """True when the ball has a neighbour one pitch away on all four sides."""
    x, y = pad.GetPosition().x, pad.GetPosition().y
    tol = mm(0.12)
    near = mm(0.30)
    far = mm(0.55)

    def has(sx, sy):
        for other in pads:
            if other == pad:
                continue
            dx = other.GetPosition().x - x
            dy = other.GetPosition().y - y
            along = dx * sx + dy * sy
            cross = abs(dx * sy - dy * sx)
            if cross < tol and near < along < far:
                return True
        return False

    return has(1, 0) and has(-1, 0) and has(0, 1) and has(0, -1)


def _add_via(board, pos, net, diameter, drill, tent):
    via = pcbnew.PCB_VIA(board)
    via.SetPosition(pos)
    via.SetWidth(mm(diameter))
    via.SetDrill(mm(drill))
    via.SetNet(net)
    via.SetViaType(pcbnew.VIATYPE_THROUGH)
    mode = pcbnew.TENTING_MODE_TENTED if tent else pcbnew.TENTING_MODE_NOT_TENTED
    via.SetFrontTentingMode(mode)
    via.SetBackTentingMode(mode)
    board.Add(via)
    return via


def _vip(board):
    """Filled-and-capped via-in-pad on inner BGA balls. 0.20 / 0.30 mm, pad opened to 0.25 mm NSMD."""
    count = 0
    for ref in ("U6", "U7"):
        fp = board.FindFootprintByReference(ref)
        pads = list(fp.Pads())
        for pad in pads:
            net = pad.GetNet()
            name = pad.GetNetname()
            if name.startswith("unconnected") or net is None or net.GetNetname() == "":
                continue
            if not _is_inner(pad, pads):
                continue
            pad.SetSize(pcbnew.VECTOR2I(mm(0.25), mm(0.25)))
            pad.SetLocalSolderMaskMargin(mm(0.025))
            _add_via(board, pad.GetPosition(), net, 0.30, 0.20, tent=False)
            count += 1
    return count


def _blocked(x, y, rects, pad_boxes):
    if x < 7.0 or x > BOARD_W - 0.9 or y < 0.9 or y > BOARD_H - 0.9:
        return True
    for x0, y0, x1, y1 in rects:
        if x0 <= x <= x1 and y0 <= y <= y1:
            return True
    for x0, y0, x1, y1 in pad_boxes:
        if x0 - 0.45 <= x <= x1 + 0.45 and y0 - 0.45 <= y <= y1 + 0.45:
            return True
    return False


def _stitch(board, hv_rects):
    gnd = board.FindNet("GND")
    if gnd is None:
        return 0
    pad_boxes = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            pad_boxes.append(_box(pad, 0))
    # Also stay clear of the mounting-hole keep-out squares.
    for fp in board.GetFootprints():
        if fp.GetReference().startswith("H"):
            c = fp.GetPosition()
            cx, cy = c.x / 1e6, c.y / 1e6
            hv_rects.append((cx - 2.2, cy - 2.2, cx + 2.2, cy + 2.2))
    points = []
    step = 2.8
    y = 1.2
    while y < BOARD_H - 1.0:
        points.append((1.2, y))  # may be inside the antenna strip; _blocked rejects x < 7
        points.append((BOARD_W - 1.2, y))
        y += step
    x = 7.2
    while x < BOARD_W - 1.0:
        points.append((x, 1.2))
        points.append((x, BOARD_H - 1.2))
        x += step
    # Switcher neighbourhoods.
    for cx, cy in ((32.6, 9.8), (28.0, 3.0), (35.2, 8.5)):
        for dx in (-1.6, 0, 1.6):
            for dy in (-1.6, 0, 1.6):
                points.append((cx + dx, cy + dy))
    placed = []
    n = 0
    for x, y in points:
        if _blocked(x, y, hv_rects, pad_boxes):
            continue
        if any((x - px) ** 2 + (y - py) ** 2 < 1.3 ** 2 for px, py in placed):
            continue
        _add_via(board, pcbnew.VECTOR2I(mm(x), mm(y)), gnd, 0.6, 0.3, tent=True)
        placed.append((x, y))
        n += 1
    return n


def _zone(board, netname, layer, pts, priority):
    net = board.FindNet(netname)
    if net is None:
        return
    zone = pcbnew.ZONE(board)
    zone.SetLayer(layer)
    zone.SetNet(net)
    zone.SetAssignedPriority(priority)
    zone.SetLocalClearance(mm(0.127))
    zone.SetMinThickness(mm(0.2))
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    zone.SetThermalReliefGap(mm(0.2))
    zone.SetThermalReliefSpokeWidth(mm(0.3))
    zone.SetFillMode(pcbnew.ZONE_FILL_MODE_POLYGONS)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in pts:
        outline.Append(mm(x), mm(y))
    board.Add(zone)


def _pours(board):
    board_pts = ((0.3, 0.3), (BOARD_W - 0.3, 0.3), (BOARD_W - 0.3, BOARD_H - 0.3), (0.3, BOARD_H - 0.3))
    _zone(board, "GND", pcbnew.In1_Cu, board_pts, 0)
    _zone(board, "GND", pcbnew.B_Cu, board_pts, 0)
    _zone(board, "+3V3", pcbnew.In2_Cu, board_pts, 0)
    # Higher priority islands on L3, retargeted onto their real consumer
    # clusters on the 40 mm outline: VBAT under J2/U2, TX_5V under the
    # AFE/emitter row, +1V8 under U9/R66 with a south corridor to AS7341.
    _zone(board, "VBAT", pcbnew.In2_Cu, ((11.0, 9.5), (20.5, 9.5), (20.5, 17.5), (11.0, 17.5)), 2)
    _zone(board, "TX_5V", pcbnew.In2_Cu, ((12.5, 47.5), (27.5, 47.5), (27.5, 54.5), (12.5, 54.5)), 3)
    _zone(board, "+1V8", pcbnew.In2_Cu, ((17.0, 18.0), (26.5, 18.0), (26.5, 55.0), (23.3, 55.0), (23.3, 22.0), (17.0, 22.0)), 3)


def _silk_text(board, text, x, y, layer):
    item = pcbnew.PCB_TEXT(board)
    item.SetText(text)
    item.SetLayer(layer)
    item.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    item.SetTextSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
    item.SetTextThickness(mm(0.15))
    board.Add(item)


def _pad_boxes(board):
    boxes = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            boxes.append(_box(pad, 0.15))
    return boxes


def _hits(x0, y0, x1, y1, boxes):
    for a, b, c, d in boxes:
        if not (x1 <= a or x0 >= c or y1 <= b or y0 >= d):
            return True
    return False


def _pin1_dots(board, boxes):
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if not (ref.startswith("U") or ref.startswith("D") or ref.startswith("Q")):
            continue
        pad = None
        for candidate in fp.Pads():
            if candidate.GetNumber() in ("1", "A1"):
                pad = candidate
                break
        if pad is None:
            continue
        center = fp.GetPosition()
        pos = pad.GetPosition()
        dx, dy = pos.x - center.x, pos.y - center.y
        mag = (dx * dx + dy * dy) ** 0.5 or 1
        # Just outside the pad, on the side away from the body.
        reach = mm(0.55)
        cx = (pos.x + dx / mag * reach) / 1e6
        cy = (pos.y + dy / mag * reach) / 1e6
        if _hits(cx - 0.25, cy - 0.25, cx + 0.25, cy + 0.25, boxes):
            continue
        if min(cx, cy, BOARD_W - cx, BOARD_H - cy) < 0.3:
            continue
        # Skip footprints that already carry a pin-1 silk mark near pad 1.
        marked = False
        for item in fp.GraphicalItems():
            if not isinstance(item, pcbnew.PCB_SHAPE):
                continue
            if item.GetShape() != pcbnew.SHAPE_T_CIRCLE:
                continue
            if item.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                continue
            c = item.GetCenter()
            if ((c.x / 1e6 - cx) ** 2 + (c.y / 1e6 - cy) ** 2) ** 0.5 < 0.6:
                marked = True
                break
        if marked:
            continue
        shape = pcbnew.PCB_SHAPE(board)
        shape.SetShape(pcbnew.SHAPE_T_CIRCLE)
        layer = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
        shape.SetLayer(layer)
        shape.SetStart(pcbnew.VECTOR2I(mm(cx), mm(cy)))
        shape.SetEnd(pcbnew.VECTOR2I(mm(cx + 0.2), mm(cy)))
        shape.SetWidth(mm(0.15))
        shape.SetFilled(True)
        board.Add(shape)


def _silk(board):
    boxes = _pad_boxes(board)
    # Bottom debug band is free of bottom copper except two fiducials.
    candidates = (
        ("VitalQ hw_v1 rev A 2026-09", 8.5, 66.8, pcbnew.B_SilkS, 22.0, 1.2),
        ("JLCJLCJLCJLC", 22.0, 59.2, pcbnew.B_SilkS, 12.0, 1.2),
        ("J8 1 +3V3  2 GND  3 TX  4 RX  5 EN  6 IO0", 8.2, 53.6, pcbnew.F_SilkS, 26.0, 1.2),
    )
    for text, x, y, layer, width, height in candidates:
        if _hits(x, y - height, x + width, y, boxes):
            continue
        _silk_text(board, text, x, y, layer)
    labels = {
        "J3": "J3 FSR",
        "J5": "J5 ECG1 ECG2 RLD PPG+ PPG-",
        "J6": "J6 CE RE SE DE",
        "J7": "J7 F+ F- S+ S-",
        "J2": "J2 BAT+ BAT- NTC",
    }
    for ref, text in labels.items():
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            continue
        pos = fp.GetPosition()
        layer = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
        # Park the label above the connector, then fall back a step if it hits copper.
        for dy in (3.2, 4.4, -3.6):
            x, y = pos.x / 1e6 - 4.0, pos.y / 1e6 + dy
            if x < 6.8:
                x = 6.8
            if _hits(x, y - 1.1, x + min(len(text) * 0.7, 24), y, boxes):
                continue
            if y < 1.2 or y > BOARD_H - 0.4:
                continue
            _silk_text(board, text, x, y, layer)
            break
    _pin1_dots(board, boxes)


def finish_board(board):
    _netclass(board, "HV_ELECTRODE", 0.2, 0.25)
    _netclass(board, "ELECTRODE_LV", 0.2, 0.15)
    _netclass(board, "USB_90R", 0.15, 0.25)
    _assign(board, HV_NETS, "HV_ELECTRODE")
    _assign(board, LV_NETS, "ELECTRODE_LV")
    _assign(
        board,
        ("USB_DP", "USB_DM", "USB_DP_S3", "USB_DM_S3", "USB_DP_CP", "USB_DM_CP"),
        "USB_90R",
    )
    hv = _hv_keepouts(board)
    _pour_keepouts(board)
    _hole_keepouts(board)
    _bga_areas(board)
    vip = _vip(board)
    stitch = _stitch(board, hv)
    _pours(board)
    _silk(board)
    print(f"finish vip {vip}  stitch {stitch}  hv-keepouts {len(hv)}")
