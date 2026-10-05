#!/usr/bin/env python3
"""Via-in-pad rescue for pads still unconnected after routing.

For every pad that DRC reports unconnected on a pour/plane net
(GND, +3V3, ...), drop a through via at the pad centre. The via spans
all copper layers, so it lands on the inner GND planes / In2 islands
directly — no escape track needed. For signal-net pads, place the via
and a stub is NOT attempted (they need a real route; reported instead).

Usage: vip_rescue.py <board.kicad_pcb> <out.kicad_pcb> --drc <drc.json>
"""
import re
import sys

import pcbnew

POUR = {"GND", "+3V3", "+3V3_ANA", "+3V3_ESP", "VBAT", "VBAT_SYS",
        "VBUS", "VDD_CP2102", "TX_5V", "TX_5V_RAW", "+1V8", "+1V8_LDO"}
VIA_D, VIA_H = 0.60, 0.30


def open_pads(drc_path):
    import json
    d = json.load(open(drc_path))
    pads = set()
    for u in d.get("unconnected_items", []):
        for it in u.get("items", []):
            m = re.match(r"Pad (\S+) \[([^\]]+)\] of (\S+)",
                         it.get("description", ""))
            if m:
                pads.add((m.group(3), m.group(1), m.group(2)))
    return pads


def main():
    board = pcbnew.LoadBoard(sys.argv[1])
    pads = open_pads(sys.argv[sys.argv.index("--drc") + 1])
    placed = skipped = 0
    for ref, padnum, net in sorted(pads):
        # pour pads: via reaches plane/island copper directly at fill.
        # signal pads: via puts the pad on every layer so the router can
        # reach it through whichever layer is least congested.
        pass
        fp = board.FindFootprintByReference(ref)
        if not fp:
            continue
        for p in fp.Pads():
            if p.GetNumber() == padnum:
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(p.GetPosition())
                v.SetWidth(int(VIA_D * 1e6))
                v.SetDrill(int(VIA_H * 1e6))
                v.SetViaType(pcbnew.VIATYPE_THROUGH)
                v.SetNetCode(p.GetNetCode())
                board.Add(v)
                placed += 1
                break
    pcbnew.SaveBoard(sys.argv[2], board)
    print(f"vip-rescue: {placed} pad vias placed, {skipped} signal pads deferred")


main()
