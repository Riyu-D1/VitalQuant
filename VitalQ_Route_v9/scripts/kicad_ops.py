"""pcbnew operations driven by JSON (run with KiCad's python)."""
import json
import sys
import wx
APP = wx.App(False)
import pcbnew


def vec(xy):
    return pcbnew.VECTOR2I(round(xy[0] * 1e6), round(xy[1] * 1e6))


def xy(p):
    return [p.x / 1e6, p.y / 1e6]


PENDING = {}
TRACKS = []
NEW = []


def all_tracks(board):
    if not TRACKS:
        TRACKS.extend(board.GetTracks())
    return TRACKS


def move_footprints(board, moves, log):
    for m in moves:
        fp = board.FindFootprintByReference(m['ref'])
        was_locked = fp.IsLocked()
        assert not was_locked or m.get('allow_locked'), m['ref'] + ' is locked'
        old = xy(fp.GetPosition())
        assert all(abs(a - b) < 1e-5 for a, b in zip(old, m['from'])), (m, old)
        if 'rotate_to' in m:
            assert m.get('detach'), 'rotation requires detach mode'
            doomed = []
            for t in all_tracks(board):
                if t.m_Uuid.AsString() in PENDING:
                    continue
                for p in fp.Pads():
                    if p.GetNetname() != t.GetNetname():
                        continue
                    pts = [t.GetPosition()] if t.Type() == pcbnew.PCB_VIA_T else [t.GetStart(), t.GetEnd()]
                    if any(p.HitTest(q, 10000) and (t.Type() == pcbnew.PCB_VIA_T or p.IsOnLayer(t.GetLayer())) for q in pts):
                        doomed.append(t)
                        break
            rot0 = fp.GetOrientationDegrees()
            fp.SetLocked(False)
            fp.SetPosition(vec(m['to']))
            fp.SetOrientationDegrees(m['rotate_to'])
            fp.SetLocked(was_locked)
            detached = []
            for t in doomed:
                rec = {'uuid': t.m_Uuid.AsString(), 'net': t.GetNetname(), 'kind': 'via' if t.Type() == pcbnew.PCB_VIA_T else 'track'}
                if rec['kind'] == 'via':
                    rec.update(xy=xy(t.GetPosition()), diameter=t.GetWidth(pcbnew.F_Cu) / 1e6, drill=t.GetDrillValue() / 1e6)
                else:
                    rec.update(layer=t.GetLayer(), a=xy(t.GetStart()), b=xy(t.GetEnd()), width=t.GetWidth() / 1e6)
                detached.append(rec)
                PENDING[t.m_Uuid.AsString()] = t
            log.append({'ref': m['ref'], 'from': old, 'to': xy(fp.GetPosition()), 'rotation': rot0, 'rotation_to': fp.GetOrientationDegrees(), 'locked': was_locked, 'reason': m['reason'], 'detached': detached})
            continue
        dx = round((m['to'][0] - old[0]) * 1e6)
        dy = round((m['to'][1] - old[1]) * 1e6)
        rot = fp.GetOrientationDegrees()
        fp.SetPosition(vec(m['to']))
        assert fp.GetOrientationDegrees() == rot

        class Old:
            """Old pad shape test: point p was in old pad <=> p+delta is in the moved pad."""
            def __init__(self, pad, layer):
                self.pad, self.layer = pad, layer

            def Collide(self, pt, tol):
                return self.pad.HitTest(pcbnew.VECTOR2I(pt.x + dx, pt.y + dy), tol) and self.pad.IsOnLayer(self.layer)
        polys = {}
        for p in fp.Pads():
            for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
                if p.IsOnLayer(layer):
                    polys.setdefault(layer, []).append((p.GetNetname(), Old(p, layer)))
        shifted = []
        detached = []
        if m.get('detach'):
            doomed = []
            for t in all_tracks(board):
                if t.m_Uuid.AsString() in PENDING:
                    continue
                if t.Type() == pcbnew.PCB_VIA_T:
                    for layer in polys:
                        if any(net == t.GetNetname() and s.Collide(t.GetPosition(), 10000) for net, s in polys[layer]):
                            doomed.append(t)
                            break
                    continue
                if t.GetLayer() not in polys:
                    continue
                if any(net == t.GetNetname() and (s.Collide(t.GetStart(), 10000) or s.Collide(t.GetEnd(), 10000)) for net, s in polys[t.GetLayer()]):
                    doomed.append(t)
            for t in doomed:
                rec = {'uuid': t.m_Uuid.AsString(), 'net': t.GetNetname(), 'kind': 'via' if t.Type() == pcbnew.PCB_VIA_T else 'track'}
                if rec['kind'] == 'via':
                    rec.update(xy=xy(t.GetPosition()), diameter=t.GetWidth(pcbnew.F_Cu) / 1e6, drill=t.GetDrillValue() / 1e6)
                else:
                    rec.update(layer=t.GetLayer(), a=xy(t.GetStart()), b=xy(t.GetEnd()), width=t.GetWidth() / 1e6)
                detached.append(rec)
                PENDING[t.m_Uuid.AsString()] = t
            log.append({'ref': m['ref'], 'from': old, 'to': xy(fp.GetPosition()), 'rotation': rot, 'reason': m['reason'], 'detached': detached})
            continue
        for t in all_tracks(board):
            if t.Type() == pcbnew.PCB_VIA_T:
                pt = t.GetPosition()
                if any(net == t.GetNetname() and s.Collide(pt, 10000) for layer in polys for net, s in polys[layer]):
                    t.SetPosition(pcbnew.VECTOR2I(pt.x + dx, pt.y + dy))
                    shifted.append({'uuid': t.m_Uuid.AsString(), 'net': t.GetNetname(), 'kind': 'via', 'xy': xy(t.GetPosition())})
                continue
            if t.GetLayer() not in polys:
                continue
            changed = False
            for getter, setter in ((t.GetStart, t.SetStart), (t.GetEnd, t.SetEnd)):
                pt = getter()
                for net, s in polys[t.GetLayer()]:
                    if net == t.GetNetname() and s.Collide(pt, 10000):
                        setter(pcbnew.VECTOR2I(pt.x + dx, pt.y + dy))
                        changed = True
                        break
            if changed:
                shifted.append({'uuid': t.m_Uuid.AsString(), 'net': t.GetNetname(), 'layer': t.GetLayer(), 'a': xy(t.GetStart()), 'b': xy(t.GetEnd())})
        log.append({'ref': m['ref'], 'from': old, 'to': xy(fp.GetPosition()), 'rotation': rot, 'reason': m['reason'], 'shifted_track_endpoints': shifted})


def apply_plan(board, plan, log):
    items = {t.m_Uuid.AsString(): t for t in all_tracks(board)}
    nets = {n.GetNetname(): n.GetNetCode() for n in board.GetNetsByNetcode().values()}
    for rec in plan.get('remove', []):
        PENDING[rec['uuid']] = items[rec['uuid']]
    for rec in plan.get('modify_vias', []):
        v = items[rec['uuid']]
        v.SetPosition(vec(rec['xy']))
        v.SetWidth(round(rec['diameter'] * 1e6))
        v.SetDrill(round(rec['drill'] * 1e6))
        v.SetFillingMode(pcbnew.FILLING_MODE_FILLED)
        v.SetCappingMode(pcbnew.CAPPING_MODE_CAPPED)
    for rec in plan.get('modify_tracks', []):
        t = items[rec['uuid']]
        t.SetStart(vec(rec['a']))
        t.SetEnd(vec(rec['b']))
    for rec in plan.get('tracks', []):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(vec(rec['a']))
        t.SetEnd(vec(rec['b']))
        t.SetWidth(round(rec['width'] * 1e6))
        t.SetLayer(rec['layer'])
        t.SetNetCode(nets[rec['net']])
        board.Add(t)
        NEW.append(t)
        rec['board_uuid'] = t.m_Uuid.AsString()
    for rec in plan.get('vias', []):
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(vec(rec['xy']))
        v.SetViaType(pcbnew.VIATYPE_THROUGH)
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetWidth(round(rec['diameter'] * 1e6))
        v.SetDrill(round(rec['drill'] * 1e6))
        v.SetNetCode(nets[rec['net']])
        if rec.get('pofv'):
            v.SetFillingMode(pcbnew.FILLING_MODE_FILLED)
            v.SetCappingMode(pcbnew.CAPPING_MODE_CAPPED)
        board.Add(v)
        NEW.append(v)
        rec['board_uuid'] = v.m_Uuid.AsString()
    log.append({'removed': len(plan.get('remove', [])), 'modified': len(plan.get('modify_tracks', [])), 'tracks_added': len(plan.get('tracks', [])), 'vias_added': len(plan.get('vias', []))})


def stackup8(board):
    """SUPERVISOR (8-layer order): F sig / In1 GND / In2 sig / In3 sig / In4 power / In5 sig / In6 GND / B sig."""
    board.SetCopperLayerCount(8)
    ds = board.GetDesignSettings()
    try:
        ds.GetStackupDescriptor().BuildDefaultStackupList(ds, 8)
        stack = 'rebuilt default 8-layer stackup'
    except Exception as e:
        stack = 'stackup descriptor not rebuilt: ' + str(e)
    board.SetLayerName(pcbnew.In5_Cu, 'Signal Layer 6')
    board.SetLayerName(pcbnew.In6_Cu, 'Ground Layer 7')
    inner4 = [pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu, pcbnew.In4_Cu]
    extended = []
    gnd1 = None
    for z in board.Zones():
        if z.GetIsRuleArea():
            ls = z.GetLayerSet()
            if all(ls.Contains(l) for l in inner4):
                ls.AddLayer(pcbnew.In5_Cu)
                ls.AddLayer(pcbnew.In6_Cu)
                z.SetLayerSet(ls)
                extended.append(z.GetZoneName())
        elif z.GetZoneName() == 'GND.LAYER_1':
            gnd1 = z
    g6 = pcbnew.ZONE(gnd1)
    g6.SetLayer(pcbnew.In6_Cu)
    g6.SetZoneName('GND.LAYER_6')
    board.Add(g6)
    return {'copper_layers': board.GetCopperLayerCount(), 'stackup': stack, 'rule_areas_extended_to_In5_In6': len(extended), 'gnd_plane_In6': 'GND.LAYER_6 (copy of GND.LAYER_1 outline/settings)'}


def rewrite_stackup8(path):
    """Board-setup stackup for 8 copper layers (~1.6 mm; JLC assigns its standard 8-layer build at order time)."""
    text = open(path).read()
    a = text.index('\t\t\t(layer "F.Cu"')
    bpos = text.index('\t\t\t(layer "B.Mask"')
    t = '\t\t\t'
    def cu(name, th):
        return f'{t}(layer "{name}"\n{t}\t(type "copper")\n{t}\t(thickness {th})\n{t})\n'
    def di(n, kind, th, er):
        return f'{t}(layer "dielectric {n}"\n{t}\t(type "{kind}")\n{t}\t(thickness {th})\n{t}\t(material "FR4")\n{t}\t(epsilon_r {er})\n{t}\t(loss_tangent 0.02)\n{t})\n'
    seq = [cu('F.Cu', 0.035), di(1, 'prepreg', 0.0994, 4.1), cu('In1.Cu', 0.0152), di(2, 'core', 0.3, 4.6), cu('In2.Cu', 0.0152),
           di(3, 'prepreg', 0.1088, 4.1), cu('In3.Cu', 0.0152), di(4, 'core', 0.3, 4.6), cu('In4.Cu', 0.0152), di(5, 'prepreg', 0.1088, 4.1),
           cu('In5.Cu', 0.0152), di(6, 'core', 0.3, 4.6), cu('In6.Cu', 0.0152), di(7, 'prepreg', 0.0994, 4.1), cu('B.Cu', 0.035)]
    open(path, 'w').write(text[:a] + ''.join(seq) + text[bpos:])


def _poly(records):
    out = pcbnew.SHAPE_POLY_SET()
    for rec in records:
        i = out.NewOutline()
        for x, y in rec['outer']:
            out.Append(round(x * 1e6), round(y * 1e6), i)
        for ring in rec.get('holes', []):
            h = out.NewHole(i)
            for x, y in ring:
                out.Append(round(x * 1e6), round(y * 1e6), i, h)
    return out


def j8_notch(board, cfg):
    """SUPERVISOR_R2_05: J8 no-via area = original rectangle minus (annulus + 0.002) of each certified U6 ball POFV."""
    import math
    original = _poly(cfg['original_outline'])
    result = _poly(cfg['original_outline'])
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    j8 = fps['J8']
    zones = [z for z in j8.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowVias()]
    proto = max(zones, key=lambda z: z.Outline().Area())
    u6 = [p for p in fps['U6'].Pads()]
    records = []
    for v in all_tracks(board) + NEW:
        if v.m_Uuid.AsString() in PENDING or v.Type() != pcbnew.PCB_VIA_T:
            continue
        if not original.Collide(v.GetPosition(), v.GetWidth(pcbnew.F_Cu) // 2):
            continue
        ball = next((p for p in u6 if p.GetPosition() == v.GetPosition() and p.GetNetname() == v.GetNetname()), None)
        assert ball is not None, 'non-ball-centre via in J8 area: ' + v.m_Uuid.AsString()
        assert v.GetWidth(pcbnew.F_Cu) <= 250001 and v.GetDrillValue() <= 150001, 'J8 exception must be 0.25/0.15'
        assert v.GetFillingMode() == pcbnew.FILLING_MODE_FILLED and v.GetCappingMode() == pcbnew.CAPPING_MODE_CAPPED, 'J8 exception must be POFV'
        x, y = v.GetPosition().x / 1e6, v.GetPosition().y / 1e6
        r = v.GetWidth(pcbnew.F_Cu) / 2e6 + 0.002
        ring = [[x + r * math.cos(i * math.tau / 128), y + r * math.sin(i * math.tau / 128)] for i in range(128)]
        result.BooleanSubtract(_poly([{'outer': ring, 'holes': []}]))
        records.append({'uuid': v.m_Uuid.AsString(), 'ball': 'U6.' + ball.GetNumber(), 'net': v.GetNetname(), 'xy': [x, y], 'diameter_mm': v.GetWidth(pcbnew.F_Cu) / 1e6, 'drill_mm': v.GetDrillValue() / 1e6, 'notch_radius_mm': r, 'process': 'POFV mandatory (planar, epoxy-filled + copper-capped) under Tag-Connect J8 landing'})
    pieces = []
    for i in range(result.OutlineCount()):
        one = pcbnew.SHAPE_POLY_SET()
        one.AddOutline(result.Outline(i))
        for j in range(result.HoleCount(i)):
            one.AddHole(result.Hole(i, j))
        pieces.append(one)
    for k, piece in enumerate(pieces):
        if k < len(zones):
            z = zones[k]
        else:
            z = pcbnew.ZONE(j8)
            z.SetIsRuleArea(True)
            z.SetLayerSet(proto.GetLayerSet())
            z.SetDoNotAllowVias(True)
            z.SetDoNotAllowTracks(proto.GetDoNotAllowTracks())
            z.SetDoNotAllowZoneFills(proto.GetDoNotAllowZoneFills())
            z.SetDoNotAllowPads(proto.GetDoNotAllowPads())
            z.SetDoNotAllowFootprints(proto.GetDoNotAllowFootprints())
            j8.Add(z)
        z.SetZoneName('J8_POFV_R2_' + str(k) if k else proto.GetZoneName())
        piece.thisown = False
        z.SetOutline(piece)
    for z in zones[len(pieces):]:
        j8.Remove(z)
    return {'pieces': len(pieces), 'vias': records}


if __name__ == '__main__':
    job = json.loads(open(sys.argv[1]).read())
    board = pcbnew.LoadBoard(job['input'])
    log = []
    if job.get('stackup8') and board.GetCopperLayerCount() == 6:
        log.append({'stackup8': stackup8(board)})
    if job.get('moves'):
        move_footprints(board, job['moves'], log)
    if job.get('plan'):
        plan = json.loads(open(job['plan']).read())
        apply_plan(board, plan, log)
        open(job['plan'], 'w').write(json.dumps(plan, indent=1))
    if job.get('j8_notch'):
        log.append({'j8_notch': j8_notch(board, job['j8_notch'])})
    for t in PENDING.values():
        board.Remove(t)
    if job.get('refill'):
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(job['output'], board)
    if board.GetCopperLayerCount() == 8:
        rewrite_stackup8(job['output'])
    open(job['log'], 'w').write(json.dumps(log, indent=1))
    print('kicad_ops done', job['output'], flush=True)
