import json
import math
from pathlib import Path
import pcbnew

ROOT=Path(__file__).resolve().parents[1]


def polygon(records):
    result=pcbnew.SHAPE_POLY_SET()
    for record in records:
        index=result.NewOutline()
        for x,y in record["outer"]:
            result.Append(round(x*1e6),round(y*1e6),index)
        for ring in record.get("holes",[]):
            hole=result.NewHole(index)
            for x,y in ring:
                result.Append(round(x*1e6),round(y*1e6),index,hole)
    return result


def apply_j8_policy(board,mark_existing=False,validate_only=False):
    policy=json.loads((ROOT/"j8_pofv_policy.json").read_text())
    original=polygon(policy["original_outline"])
    result=polygon(policy["original_outline"])
    footprints={f.GetReference():f for f in board.GetFootprints()}
    residuals=sorted([z for z in footprints["J8"].Zones() if z.GetZoneName().startswith("J8_POFV_remainder_")],key=lambda z:z.GetZoneName())
    zones=[z for z in footprints["J8"].Zones() if z.GetDoNotAllowVias() and not z.GetZoneName().startswith("J8_POFV_remainder_")]
    if len(zones)!=1:
        raise ValueError("J8 original no-via area identity is ambiguous")
    pads={p.GetNumber():p for p in footprints["U6"].Pads()}
    balls=[b for b in policy["balls"] if b["eligible"]]
    records=[]
    for via in board.GetTracks():
        if via.Type()!=pcbnew.PCB_VIA_T or not original.Collide(via.GetPosition(),via.GetWidth(pcbnew.F_Cu)//2):
            continue
        ball=next((b for b in balls if b["net"]==via.GetNetname() and pads[b["ball"]].GetEffectivePolygon(pcbnew.B_Cu).Contains(via.GetPosition())),None)
        if ball is None:
            raise ValueError("Unapproved via intersects J8 original keepout: "+via.m_Uuid.AsString())
        pad=pads[ball["ball"]]
        if pad.m_Uuid.AsString()!=ball["pad_uuid"] or math.dist([pad.GetPosition().x/1e6,pad.GetPosition().y/1e6],ball["xy"])>1e-6:
            raise ValueError("U6 ball moved or changed identity")
        if via.GetWidth(pcbnew.F_Cu)>300001 or via.GetDrillValue()>150001:
            raise ValueError("J8 exception exceeds reviewed 0.30/0.15mm via size")
        if mark_existing:
            via.SetFillingMode(pcbnew.FILLING_MODE_FILLED)
            via.SetCappingMode(pcbnew.CAPPING_MODE_CAPPED)
        if via.GetFillingMode()!=pcbnew.FILLING_MODE_FILLED or via.GetCappingMode()!=pcbnew.CAPPING_MODE_CAPPED:
            raise ValueError("J8 exception requires explicit filled and copper-capped POFV")
        x,y=via.GetPosition().x/1e6,via.GetPosition().y/1e6
        radius=via.GetWidth(pcbnew.F_Cu)/2e6+0.002
        ring=[[x+radius*math.cos(i*math.tau/128),y+radius*math.sin(i*math.tau/128)] for i in range(128)]
        result.BooleanSubtract(polygon([{"outer":ring,"holes":[]}]))
        records.append({"uuid":via.m_Uuid.AsString(),"ball":ball["ball"],"net":via.GetNetname(),"xy":[x,y],"diameter_mm":via.GetWidth(pcbnew.F_Cu)/1e6,"drill_mm":via.GetDrillValue()/1e6,"filled":True,"copper_capped":True,"process":"JLC POFV, planar filled/copper-capped via-in-pad", "no_other_escape_evidence":ball,"exception_radius_mm":radius})
    if validate_only:
        saved=pcbnew.SHAPE_POLY_SET()
        for zone in zones+residuals:
            if list(zone.GetLayerSet().Seq())!=list(zones[0].GetLayerSet().Seq()) or zone.GetDoNotAllowTracks()!=zones[0].GetDoNotAllowTracks() or not zone.GetDoNotAllowVias() or zone.GetDoNotAllowZoneFills()!=zones[0].GetDoNotAllowZoneFills() or zone.GetDoNotAllowPads()!=zones[0].GetDoNotAllowPads() or zone.GetDoNotAllowFootprints()!=zones[0].GetDoNotAllowFootprints():
                raise ValueError("J8 residual area restrictions changed")
            saved.Append(zone.Outline())
        extra=pcbnew.SHAPE_POLY_SET();extra.Append(saved);extra.BooleanSubtract(result)
        missing=pcbnew.SHAPE_POLY_SET();missing.Append(result);missing.BooleanSubtract(saved)
        if extra.OutlineCount() or missing.OutlineCount():
            raise ValueError("J8 saved geometry differs from original area minus only certified POFV annuli")
    else:
        from extract_exact import polygons
        pieces=polygons(result)
        for index,piece in enumerate(pieces):
            if index==0:
                zone=zones[0]
            elif index<=len(residuals):
                zone=residuals[index-1]
            else:
                zone=pcbnew.ZONE(footprints["J8"])
                zone.SetIsRuleArea(True);zone.SetLayerSet(zones[0].GetLayerSet())
                zone.SetDoNotAllowVias(True);zone.SetDoNotAllowTracks(zones[0].GetDoNotAllowTracks())
                zone.SetDoNotAllowZoneFills(zones[0].GetDoNotAllowZoneFills())
                zone.SetDoNotAllowPads(zones[0].GetDoNotAllowPads());zone.SetDoNotAllowFootprints(zones[0].GetDoNotAllowFootprints())
                zone.SetZoneName("J8_POFV_remainder_"+str(index))
                footprints["J8"].Add(zone)
            outline=polygon([piece]);outline.thisown=False;zone.SetOutline(outline)
        for obsolete in residuals[max(0,len(pieces)-1):]:
            footprints["J8"].Remove(obsolete)
    return records


def footprint_invariants(snapshot):
    data=json.loads(json.dumps(snapshot["footprints"]))
    data["J8"].pop("zones",None)
    return data
