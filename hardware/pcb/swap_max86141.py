#!/usr/bin/env python3
"""MAX86178 -> MAX86141 swap for the flat VitalQ schematic + Quilter PCB.

Usage: swap_max86141.py SCH_FILE PCB_FILE [--mod-out DIR]

Schematic frame note (schutil convention): kicad_sch lib_symbols are stored
Y-up while sheet elements are Y-down, so a lib pin at (x,y) connects at
instance (x, -y) relative sheet coords.
"""
import re
import sys
import uuid as _uuid


def U():
    return str(_uuid.uuid4())


def elems(txt):
    """Children of the root (depth-1 s-expr elements) -> [(text,start,end)]."""
    out = []
    depth = 0
    start = None
    instr = False
    esc = False
    for j, c in enumerate(txt):
        if instr:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                instr = False
            continue
        if c == '"':
            instr = True
        elif c == "(":
            if depth == 1:
                start = j
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 1 and start is not None:
                out.append((txt[start:j + 1], start, j + 1))
                start = None
    return out


def sub_blocks(seg, tok):
    out = []
    i = 0
    while True:
        j = seg.find(tok, i)
        if j < 0:
            break
        d = 0
        for k in range(j, len(seg)):
            if seg[k] == "(":
                d += 1
            elif seg[k] == ")":
                d -= 1
                if d == 0:
                    out.append(seg[j:k + 1])
                    i = k + 1
                    break
    return out


def at_of(el):
    m = re.search(r"\(at ([-\d.]+) ([-\d.]+)", el)
    return (float(m.group(1)), float(m.group(2))) if m else None


def pt(x, y):
    return (round(x, 2), round(y, 2))


# ---------------------------------------------------------------- schematic

# desired sheet-side pin layout for the new MAX86141 symbol.
# (num, name, etype, rel_x, rel_y, stub_dx, stub_dy, net | "NC")
U22_PINS = [
    # left column, stubs go -x
    ("A2", "SCLK", "input", -15.24, 7.62, -1, 0, "SPI_SCK"),
    ("A3", "SDO", "output", -15.24, 6.35, -1, 0, "MX_SDO_1V8"),
    ("A4", "SDI", "input", -15.24, 5.08, -1, 0, "SPI_MOSI"),
    ("A5", "CSB", "input", -15.24, 3.81, -1, 0, "CS_MAX86178"),
    ("B2", "INT", "open_collector", -15.24, 2.54, -1, 0, "MAX86178_INT"),
    ("B3", "GPIO1", "bidirectional", -15.24, 1.27, -1, 0, "NC"),
    ("B4", "GPIO2", "bidirectional", -15.24, 0.0, -1, 0, "NC"),
    # right column, stubs go +x
    ("D1", "LED1_DRV", "output", 15.24, 7.62, 1, 0, "LED1_K"),
    ("C1", "LED2_DRV", "output", 15.24, 6.35, 1, 0, "LED2_K"),
    ("B1", "LED3_DRV", "output", 15.24, 5.08, 1, 0, "LED3_K"),
    ("D5", "PD1_IN", "input", 15.24, 3.81, 1, 0, "PD_K"),
    ("C5", "PD_GND", "output", 15.24, 2.54, 1, 0, "PD_A"),
    ("D4", "PD2_IN", "input", 15.24, 1.27, 1, 0, "NC"),
    ("A1", "VLED", "power_in", 15.24, 0.0, 1, 0, "TX_5V"),
    # appears below body (sheet +12.7), stubs go +y
    ("C3", "GND_DIG", "power_in", -2.54, 12.7, 0, 1, "GND"),
    ("C4", "GND_ANA", "power_in", 0.0, 12.7, 0, 1, "GND"),
    ("D3", "PGND", "power_in", 2.54, 12.7, 0, 1, "GND"),
    # appears above body (sheet -12.7), stubs go -y
    ("D2", "VDD_ANA", "power_in", -3.81, -12.7, 0, -1, "+1V8"),
    ("C2", "VDD_DIG", "power_in", -1.27, -12.7, 0, -1, "+1V8"),
    ("B5", "VREF", "power_out", 1.27, -12.7, 0, -1, "MX_VREF"),
]

# U26 SN74AXC2T245: (num, name, etype, rel_x, rel_y, stubdx, stubdy, net|"NC")
U26_PINS = [
    ("8", "A1", "input", -8.89, -5.08, -1, 0, "MX_SDO_1V8"),
    ("9", "A2", "output", -8.89, -2.54, -1, 0, "NC"),
    ("10", "DIR1", "input", -8.89, 0.0, -1, 0, "+1V8"),
    ("2", "OE", "input", -8.89, 2.54, -1, 0, "CS_MAX86178"),
    ("5", "B1", "output", 8.89, -5.08, 1, 0, "MISO_MX"),
    ("4", "B2", "input", 8.89, -2.54, 1, 0, "GND"),
    ("1", "DIR2", "input", 8.89, 0.0, 1, 0, "GND"),
    ("7", "VCCA", "power_in", -2.54, 8.89, 0, 1, "+1V8"),
    ("3", "GND", "power_in", 0.0, 8.89, 0, 1, "GND"),
    ("6", "VCCB", "power_in", 2.54, 8.89, 0, 1, "+3V3"),
]

U26_AT = (1511.3, 720.09)  # 1190x567 grid cells, on 1.27mm grid

# (ref, value, footprint, rail net, lcsc)
NEW_CAPS = [
    ("C86", "100n", "Capacitor_SMD:C_0402_1005Metric", "+1V8", "C1525"),
    ("C87", "10u", "Capacitor_SMD:C_0402_1005Metric", "+1V8", "C15525"),
    ("C88", "100n", "Capacitor_SMD:C_0402_1005Metric", "+1V8", "C1525"),
    ("C89", "10u", "Capacitor_SMD:C_0603_1608Metric", "TX_5V", "C19702"),
    ("C90", "100n", "Capacitor_SMD:C_0402_1005Metric", "+1V8", "C1525"),
    ("C91", "100n", "Capacitor_SMD:C_0402_1005Metric", "+3V3", "C1525"),
]
CAP_Y = 781.05  # 615*1.27, on grid
CAP_XS = [1435.1 + 25.4 * k for k in range(6)]  # 25.4 = 20*1.27 pitch

STUB = 5.08


def lib_pin(num, name, etype, rx, ry, stubdx, stubdy):
    # desired sheet rel (rx,ry) -> lib coords (x, -y); rot to point away
    if stubdx == -1:
        rot, lx, ly = 0, rx, -ry
    elif stubdx == 1:
        rot, lx, ly = 180, rx, -ry
    elif stubdy == 1:
        rot, lx, ly = 90, rx, -ry
    else:
        rot, lx, ly = 270, rx, -ry
    return (
        f'\t\t\t\t(pin {etype} line\n'
        f'\t\t\t\t\t(at {lx:g} {ly:g} {rot})\n'
        f'\t\t\t\t\t(length 2.54)\n'
        f'\t\t\t\t\t(name "{name}"\n'
        f'\t\t\t\t\t\t(effects\n\t\t\t\t\t\t\t(font\n\t\t\t\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t\t\t\t)\n\t\t\t\t\t\t)\n'
        f'\t\t\t\t\t)\n'
        f'\t\t\t\t\t(number "{num}"\n'
        f'\t\t\t\t\t\t(effects\n\t\t\t\t\t\t\t(font\n\t\t\t\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t\t\t\t)\n\t\t\t\t\t\t)\n'
        f'\t\t\t\t\t)\n'
        f'\t\t\t\t)'
    )


def lib_prop(name, value, y=0.0, hide=True):
    h = "\n\t\t\t\t(hide yes)" if hide else ""
    return (
        f'\t\t\t(property "{name}" "{value}"\n'
        f"\t\t\t\t(at 0 {y:g} 0)\n"
        f"\t\t\t\t(show_name no)\n"
        f"\t\t\t\t(do_not_autoplace no){h}\n"
        f"\t\t\t\t(effects\n\t\t\t\t\t(font\n\t\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t\t)\n\t\t\t\t)\n"
        f"\t\t\t)\n"
    )


def lib_symbol(libname, ref, descr, keywords, fpfilter, rect, pins):
    x1, y1, x2, y2 = rect
    body = f'(symbol "{libname}"\n'
    body += "\t\t\t(pin_numbers\n\t\t\t\t(hide yes)\n\t\t\t)\n"
    body += "\t\t\t(pin_names\n\t\t\t\t(offset 0.254)\n\t\t\t)\n"
    body += "\t\t\t(exclude_from_sim no)\n\t\t\t(in_bom yes)\n\t\t\t(on_board yes)\n\t\t\t(in_pos_files yes)\n"
    body += "\t\t\t(duplicate_pin_numbers_are_jumpers no)\n"
    body += lib_prop("Reference", ref, 0.0, hide=False)
    body += lib_prop("Value", libname.split(":")[-1], 0.0, hide=False)
    body += lib_prop("Footprint", "")
    body += lib_prop("Datasheet", "")
    body += lib_prop("Description", descr)
    body += lib_prop("ki_keywords", keywords)
    body += lib_prop("ki_fp_filters", fpfilter)
    base = libname.split(":")[-1]
    body += f'\t\t\t(symbol "{base}_0_1"\n'
    body += (
        "\t\t\t\t(rectangle\n"
        f"\t\t\t\t\t(start {x1:g} {y1:g})\n\t\t\t\t\t(end {x2:g} {y2:g})\n"
        "\t\t\t\t\t(stroke\n\t\t\t\t\t\t(width 0.254)\n\t\t\t\t\t\t(type default)\n\t\t\t\t\t)\n"
        "\t\t\t\t\t(fill\n\t\t\t\t\t\t(type background)\n\t\t\t\t\t)\n"
        "\t\t\t\t)\n"
    )
    body += "\t\t\t)\n"
    body += f'\t\t\t(symbol "{base}_1_1"\n'
    body += "\n".join(lib_pin(*p[:7]) for p in pins)
    body += "\n\t\t\t)\n"
    body += "\t\t\t(embedded_fonts no)\n\t\t"
    body += ")"
    return body


def inst_prop(name, value, x, y, hide=False):
    h = "\n\t\t\t(hide yes)" if hide else ""
    return (
        f'\t\t(property "{name}" "{value}"\n'
        f"\t\t\t(at {x:g} {y:g} 0){h}\n"
        f"\t\t\t(show_name no)\n"
        f"\t\t\t(do_not_autoplace no)\n"
        f"\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n"
        f"\t\t)\n"
    )


def instance_el(lib_id, ref, value, fp, descr, x, y, pin_nums, proj, rootpath, lcsc=None, sym_uuid=None):
    s = f'(symbol\n\t\t(lib_id "{lib_id}")\n'
    s += f"\t\t(at {x:g} {y:g} 0)\n"
    s += "\t\t(unit 1)\n\t\t(body_style 1)\n"
    s += "\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(in_pos_files yes)\n\t\t(dnp no)\n"
    s += f'\t\t(uuid "{sym_uuid or U()}")\n'
    s += inst_prop("Reference", ref, x, y - 11.43)
    s += inst_prop("Value", value, x, y + 11.43)
    s += inst_prop("Footprint", fp, x, y, hide=True)
    s += inst_prop("Datasheet", "", x, y, hide=True)
    s += inst_prop("Description", descr, x, y, hide=True)
    if lcsc:
        s += inst_prop("LCSC", lcsc, x, y, hide=True)
    for n in pin_nums:
        s += f'\t\t(pin "{n}"\n\t\t\t(uuid "{U()}")\n\t\t)\n'
    s += (
        "\t\t(instances\n"
        f'\t\t\t(project "{proj}"\n'
        f'\t\t\t\t(path "/{rootpath}"\n'
        f'\t\t\t\t\t(reference "{ref}")\n'
        "\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)"
    )
    return s


def wire_el(x1, y1, x2, y2):
    return (
        "\t(wire\n\t\t(pts\n"
        f"\t\t\t(xy {x1:g} {y1:g}) (xy {x2:g} {y2:g})\n"
        "\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n"
        f'\t\t(uuid "{U()}")\n\t)'
    )


def label_el(net, x, y, dx, dy):
    if dx < 0:
        rot, just = 180, "right"
    elif dx > 0:
        rot, just = 0, "left"
    elif dy < 0:
        rot, just = 270, "right"
    else:
        rot, just = 90, "left"
    return (
        f'(global_label "{net}"\n\t\t(shape passive)\n'
        f"\t\t(at {x:g} {y:g} {rot})\n"
        "\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n"
        f"\t\t\t(justify {just})\n\t\t)\n"
        f'\t\t(uuid "{U()}")\n'
        '\t\t(property "Intersheetrefs" "${INTERSHEET_REFS}"\n'
        f"\t\t\t(at {x:g} {y:g} 0)\n\t\t\t(hide yes)\n\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n"
        "\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)"
    )


def noconn_el(x, y):
    return f'(no_connect\n\t\t(at {x:g} {y:g})\n\t\t(uuid "{U()}")\n\t)'


def sch_transform(path):
    txt = open(path).read()
    els = elems(txt)

    # locate lib_symbols + old MAX86178 lib symbol pins
    lib_i = next(k for k, (e, _, _) in enumerate(els) if e.startswith("(lib_symbols"))
    lib_el = els[lib_i][0]
    m87_start = lib_el.index('(symbol "vitalq:MAX86178"')
    depth = 0
    for j in range(m87_start, len(lib_el)):
        if lib_el[j] == "(":
            depth += 1
        elif lib_el[j] == ")":
            depth -= 1
            if depth == 0:
                m87 = lib_el[m87_start:j + 1]
                break

    old_pins = {}  # num -> (x, y)
    for pb in sub_blocks(m87, "\n\t\t\t\t(pin "):
        at = re.search(r"\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)", pb)
        num = re.search(r'\(number "([^"]+)"', pb)
        old_pins[num.group(1)] = (float(at.group(1)), float(at.group(2)))

    # U22 instance
    U22_UUID = "29d3af96-bc5d-4a32-8c0b-3b3f396edf9a"
    u22_i = next(k for k, (e, _, _) in enumerate(els) if f'(uuid "{U22_UUID}")' in e and e.lstrip().startswith("(symbol"))
    u22_el = els[u22_i][0]
    ix, iy = at_of(u22_el)
    print(f"U22 instance at ({ix}, {iy}); old lib pins: {len(old_pins)}")

    # sheet-side connect points of every old pin: (x, -y)
    tips = set()
    for px, py in old_pins.values():
        tips.add(pt(ix + px, iy - py))

    # element endpoints for matching
    def endpoints(el):
        return [pt(float(a), float(b)) for a, b in re.findall(r"\(xy ([-\d.]+) ([-\d.]+)\)", el)]

    # iterative sweep: wires touching pin tips die; their far ends kill
    # labels / no_connects / junctions / power symbols.
    kill = set()
    frontier = set(tips)
    changed = True
    while changed:
        changed = False
        for k, (e, s, t) in enumerate(els):
            if k in kill or k == u22_i or k == lib_i:
                continue
            head = e.lstrip()[:40]
            if head.startswith("(wire") or head.startswith("(bus"):
                ends = endpoints(e)
                if any(p in frontier for p in ends):
                    kill.add(k)
                    changed = True
                    for p in ends:
                        if p not in frontier:
                            frontier.add(p)
            elif head.startswith("(global_label") or head.startswith("(label") or head.startswith("(hierarchical_label"):
                if at_of(e) and pt(*at_of(e)) in frontier:
                    kill.add(k)
                    changed = True
            elif head.startswith("(no_connect") or head.startswith("(junction"):
                a = at_of(e)
                if a and pt(*a) in frontier:
                    kill.add(k)
                    changed = True
            elif head.startswith("(symbol"):
                lid = re.search(r'\(lib_id "([^"]+)"', e)
                is_pwr = lid and (lid.group(1).startswith("power:") or '"#PWR' in e)
                if is_pwr:
                    a = at_of(e)
                    if a and pt(*a) in frontier:
                        kill.add(k)
                        changed = True

    for k in sorted(kill):
        e = els[k][0].lstrip()
        head = e.split("\n")[0]
        lab = re.search(r'"([^"]+)"', e)
        print("  kill:", head[:60], "at", at_of(e))

    n_wire = sum(1 for k in kill if els[k][0].lstrip().startswith("(wire"))
    n_lab = sum(1 for k in kill if els[k][0].lstrip().startswith("(global_label"))
    n_sym = sum(1 for k in kill if els[k][0].lstrip().startswith("(symbol"))
    n_jn = sum(1 for k in kill if els[k][0].lstrip().startswith("(junction"))
    n_nc = sum(1 for k in kill if els[k][0].lstrip().startswith("(no_connect"))
    print(f"sweep kills: wires={n_wire} labels={n_lab} powersyms={n_sym} junctions={n_jn} noconns={n_nc}")

    # rebuild U22 instance element (same uuid/at, new lib, 20 pin uuids)
    proj = re.search(r'\(project "([^"]+)"', u22_el).group(1)
    rootpath = re.search(r'\(path "/([0-9a-f-]+)"', u22_el).group(1)
    new_u22 = instance_el(
        "vitalq:MAX86141", "U22", "MAX86141ENP+",
        "vitalq:MAX86141_WLP20",
        "Optical PPG AFE, 3-ch LED driver, 20-WLP",
        ix, iy, [p[0] for p in U22_PINS], proj, rootpath, lcsc="C5328762",
        sym_uuid=U22_UUID)

    sym_uuids = {r: U() for r in ["U26", "C86", "C87", "C88", "C89", "C90", "C91"]}

    edits = []
    dels = {(els[k][1], els[k][2]) for k in kill}
    dels.add((els[u22_i][1], els[u22_i][2]))

    mx_lib = lib_symbol(
        "vitalq:MAX86141", "U", "Optical PPG AFE, MAX86141, 20-WLP",
        "MAX86141 PPG AFE optical", "MAX86141*",
        (-12.7, -12.7, 12.7, 12.7), U22_PINS)
    sn_lib = lib_symbol(
        "vitalq:SN74AXC2T245", "U", "Dual-bit dual-supply bus transceiver, RSW-10",
        "SN74AXC2T245 level shifter", "*RSW0010A*",
        (-10.16, -10.16, 10.16, 10.16), U26_PINS)

    # new wiring elements appended at end of file
    tail = []
    for num, name, etype, rx, ry, sdx, sdy, net in U22_PINS:
        x, y = ix + rx, iy + ry
        if net == "NC":
            tail.append(noconn_el(x, y))
        else:
            ex, ey = x + sdx * STUB, y + sdy * STUB
            tail.append(wire_el(x, y, ex, ey))
            tail.append(label_el(net, ex, ey, sdx, sdy))

    # U26 instance + wiring
    tail.append(instance_el(
        "vitalq:SN74AXC2T245", "U26", "SN74AXC2T245RSWR",
        "Package_DFN_QFN:Texas_RSW0010A_UQFN-10_1.4x1.8mm_P0.4mm",
        "2-bit dual-supply bus transceiver",
        U26_AT[0], U26_AT[1], [p[0] for p in U26_PINS], proj, rootpath,
        lcsc="C1882550", sym_uuid=sym_uuids["U26"]))
    for num, name, etype, rx, ry, sdx, sdy, net in U26_PINS:
        x, y = U26_AT[0] + rx, U26_AT[1] + ry
        if net == "NC":
            tail.append(noconn_el(x, y))
        else:
            ex, ey = x + sdx * STUB, y + sdy * STUB
            tail.append(wire_el(x, y, ex, ey))
            tail.append(label_el(net, ex, ey, sdx, sdy))

    # C86-C91 instances + wiring
    for (ref, val, fp, rail, lcsc), cx in zip(NEW_CAPS, CAP_XS):
        tail.append(instance_el(
            "vitalq:C", ref, val, fp, "Unpolarized capacitor",
            cx, CAP_Y, ["1", "2"], proj, rootpath, lcsc=lcsc,
            sym_uuid=sym_uuids[ref]))
        # pin1 appears at sheet (cx, CAP_Y-3.81): stub up to rail label
        tail.append(wire_el(cx, CAP_Y - 3.81, cx, CAP_Y - 3.81 - STUB))
        tail.append(label_el(rail, cx, CAP_Y - 3.81 - STUB, 0, -1))
        # pin2 at (cx, CAP_Y+3.81): stub down to GND label
        tail.append(wire_el(cx, CAP_Y + 3.81, cx, CAP_Y + 3.81 + STUB))
        tail.append(label_el("GND", cx, CAP_Y + 3.81 + STUB, 0, 1))

    out = []
    prev = 0
    spans = sorted(dels)
    for s, t in spans:
        out.append(txt[prev:s])
        prev = t
    out.append(txt[prev:])
    new_txt = "".join(out)

    # insert new lib symbols before lib_symbols element's closing paren
    start = new_txt.index("(lib_symbols")
    depth = 0
    lib_close = None
    for j in range(start, len(new_txt)):
        if new_txt[j] == "(":
            depth += 1
        elif new_txt[j] == ")":
            depth -= 1
            if depth == 0:
                lib_close = j
                break
    ins = "\n\t\t" + mx_lib + "\n\t\t" + sn_lib + "\n"
    new_txt = new_txt[:lib_close] + ins + "\t" + new_txt[lib_close:]

    # append new elements before final ')'
    new_txt = new_txt.rstrip()
    assert new_txt.endswith(")")
    new_txt = new_txt[:-1] + "\n" + "\n".join(tail) + "\n)\n"

    # write new U22 where old one was: easiest—append it with the tail too
    # (delete old + append new anywhere in file; order irrelevant to KiCad)
    new_txt = new_txt[:-2] + "\n" + new_u22 + "\n)\n"

    open(path, "w").write(new_txt)
    print(f"schematic rewritten: {path}")
    return sym_uuids


# ---------------------------------------------------------------- pcb

WLP20_PAD_NETS = {
    "A1": "TX_5V", "A2": "SPI_SCK", "A3": "MX_SDO_1V8", "A4": "SPI_MOSI",
    "A5": "CS_MAX86178",
    "B1": "LED3_K", "B2": "MAX86178_INT", "B3": "NC", "B4": "NC",
    "B5": "MX_VREF",
    "C1": "LED2_K", "C2": "+1V8", "C3": "GND", "C4": "GND", "C5": "PD_A",
    "D1": "LED1_K", "D2": "+1V8", "D3": "GND", "D4": "NC", "D5": "PD_K",
}
WLP20_PAD_NAMES = {
    "A1": "VLED", "A2": "SCLK", "A3": "SDO", "A4": "SDI", "A5": "CSB",
    "B1": "LED3_DRV", "B2": "INT", "B3": "GPIO1", "B4": "GPIO2",
    "B5": "VREF",
    "C1": "LED2_DRV", "C2": "VDD_DIG", "C3": "GND_DIG", "C4": "GND_ANA",
    "C5": "PD_GND",
    "D1": "LED1_DRV", "D2": "VDD_ANA", "D3": "PGND", "D4": "PD2_IN",
    "D5": "PD1_IN",
}
ROWS = "ABCD"
COL_X = {1: -0.8, 2: -0.4, 3: 0.0, 4: 0.4, 5: 0.8}
ROW_Y_LIB = {"A": -0.6, "B": -0.2, "C": 0.2, "D": 0.6}


def wlp20_mod():
    """Library footprint (F.Cu frame)."""
    s = '(footprint "vitalq:MAX86141_WLP20"\n'
    s += '\t(layer "F.Cu")\n\t(uuid "%s")\n' % U()
    s += '\t(at 0 0 0)\n\t(descr "MAX86141 PPG AFE, 20-bump WLP 2.048x1.848mm, 0.4mm pitch (21-100134)")\n'
    s += '\t(tags "BGA WLP MAX86141")\n'
    s += '\t(attr smd exclude_from_pos_files no)\n'
    s += '\t(fp_line (start -1.024 -0.924) (end 1.024 -0.924) (stroke (width 0.05) (type solid)) (layer "F.Fab") (uuid "%s"))\n' % U()
    s += '\t(fp_line (start -1.024 0.924) (end 1.024 0.924) (stroke (width 0.05) (type solid)) (layer "F.Fab") (uuid "%s"))\n' % U()
    s += '\t(fp_line (start -1.024 -0.924) (end -1.024 0.924) (stroke (width 0.05) (type solid)) (layer "F.Fab") (uuid "%s"))\n' % U()
    s += '\t(fp_line (start 1.024 -0.924) (end 1.024 0.924) (stroke (width 0.05) (type solid)) (layer "F.Fab") (uuid "%s"))\n' % U()
    s += '\t(fp_circle (center -1.15 -0.95) (end -1.05 -0.95) (stroke (width 0.1) (type solid)) (fill no) (layer "F.SilkS") (uuid "%s"))\n' % U()
    s += '\t(fp_rect (start -1.15 -1.05) (end 1.15 1.05) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd") (uuid "%s"))\n' % U()
    s += '\t(fp_text reference "REF**" (at 0 -1.4 0) (layer "F.SilkS") (uuid "%s") (effects (font (size 0.7 0.7) (thickness 0.12))))\n' % U()
    s += '\t(fp_text value "MAX86141_WLP20" (at 0 1.4 0) (layer "F.Fab") (uuid "%s") (effects (font (size 0.7 0.7) (thickness 0.12))))\n' % U()
    s += '\t(fp_text user "${REFERENCE}" (at 0 0 0) (layer "F.Fab") (uuid "%s") (effects (font (size 0.3 0.3) (thickness 0.05))))\n' % U()
    for r in ROWS:
        for c in range(1, 6):
            n = f"{r}{c}"
            s += ('\t(pad "%s" smd circle (at %g %g) (size 0.24 0.24)'
                  ' (layers "F.Cu" "F.Mask" "F.Paste") (uuid "%s"))\n'
                  % (n, COL_X[c], ROW_Y_LIB[r], U()))
    s += ")"
    return s


def wlp20_pads_bcu():
    """Pads for the B.Cu-embedded U22: lib (x,y) -> board-local (x,-y)."""
    s = ""
    for r in ROWS:
        for c in range(1, 6):
            n = f"{r}{c}"
            net = WLP20_PAD_NETS[n]
            if net == "NC":
                net = f"unconnected-(U22-{WLP20_PAD_NAMES[n]}-Pad{n})"
            s += ('\t\t(pad "%s" smd circle\n'
                  "\t\t\t(at %g %g 180)\n"
                  "\t\t\t(size 0.24 0.24)\n"
                  '\t\t\t(layers "B.Cu" "B.Mask" "B.Paste")\n'
                  '\t\t\t(net "%s")\n'
                  '\t\t\t(pinfunction "%s_%s")\n'
                  '\t\t\t(pintype "passive")\n'
                  '\t\t\t(uuid "%s")\n\t\t) \n'
                  % (n, COL_X[c], -ROW_Y_LIB[r], net, WLP20_PAD_NAMES[n], n, U()))
    return s


def fp_prop(name, value, y, layer="F.Fab", hide=True):
    h = "\n\t\t\t(hide yes)" if hide else ""
    return (
        f'\t\t(property "{name}" "{value}"\n'
        f"\t\t\t(at 0 {y:g} 0)\n"
        f'\t\t\t(layer "{layer}"){h}\n'
        f'\t\t\t(uuid "{U()}")\n'
        "\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n"
    )


OFFICIAL_UQFN = ("/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/"
                 "Package_DFN_QFN.pretty/Texas_RSW0010A_UQFN-10_1.4x1.8mm_P0.4mm.kicad_mod")


def lcsc_prop(lcsc):
    return fp_prop("LCSC", lcsc, 0)


def fp_by_ref(txt, ref):
    """Extract the embedded footprint element holding Reference==ref."""
    i = txt.find(f'(property "Reference" "{ref}"')
    st = txt.rfind('\n\t(footprint "', 0, i)
    d = 0
    for j in range(st + 1, len(txt)):
        if txt[j] == "(":
            d += 1
        elif txt[j] == ")":
            d -= 1
            if d == 0:
                return txt[st:j + 1]


def set_pad_net(block, net):
    if '(net "' in block:
        return re.sub(r'\(net "[^"]*"\)', f'(net "{net}")', block, count=1)
    return block[:-1].rstrip() + f'\n\t\t\t(net "{net}")\n\t\t)'


def clone_fp(seg, ref, value, path_uuid, x, y, nets, lcsc):
    """Clone an embedded footprint element: fresh uuids, new ref/value/pos/
    path/LCSC, pad nets by pad number."""
    seg = re.sub(r'\(uuid "[0-9a-f-]+"\)', lambda m: f'(uuid "{U()}")', seg)
    seg = re.sub(r'\n\t\t\(at [-\d.]+ [-\d.]+( [-\d.]+)?\)', f'\n\t\t(at {x:g} {y:g})', seg, count=1)
    seg = re.sub(r'\(property "Reference" "[^"]+"', f'(property "Reference" "{ref}"', seg, count=1)
    seg = re.sub(r'\(property "Value" "[^"]+"', f'(property "Value" "{value}"', seg, count=1)
    if '(path "' in seg:
        seg = re.sub(r'\(path "/[^"]+"\)', f'(path "/{path_uuid}")', seg, count=1)
    else:
        seg = seg.replace("\t\t(attr ", f'\t\t(path "/{path_uuid}")\n\t\t(attr ', 1)
    seg = re.sub(r'(\(property "Description" ")[^"]*(")',
                 r'\g<1>Unpolarized capacitor\g<2>', seg, count=1)
    seg = seg.replace("\t\t(path ", lcsc_prop(lcsc) + "\t\t(path ", 1)
    for num, net in nets.items():
        for pb in sub_blocks(seg, f'(pad "{num}"'):
            seg = seg.replace(pb, set_pad_net(pb, net), 1)
    return seg


def uqfn10_footprint(x, y, ref, value, path_uuid, lcsc, sheetfile="vitalq_v2.kicad_sch"):
    """Embed the official RSW0010A geometry verbatim (+props/path/nets)."""
    off = open(OFFICIAL_UQFN).read()
    body = []
    for tok in ("(fp_line", "(fp_rect", "(fp_circle", "(fp_arc", "(fp_text", "(pad "):
        body += sub_blocks(off, tok)
    nets = {"1": "GND", "2": "CS_MAX86178", "3": "GND", "4": "GND",
            "5": "MISO_MX", "6": "+3V3", "7": "+1V8", "8": "MX_SDO_1V8",
            "9": "unconnected-(U26-A2-Pad9)", "10": "+1V8"}
    s = '\t(footprint "Package_DFN_QFN:Texas_RSW0010A_UQFN-10_1.4x1.8mm_P0.4mm"\n'
    s += '\t\t(layer "F.Cu")\n'
    s += f'\t\t(uuid "{U()}")\n'
    s += f"\t\t(at {x:g} {y:g})\n"
    s += '\t\t(descr "Texas RSW0010A UQFN, 10 Pin (https://www.ti.com/lit/ds/symlink/ts3a5223.pdf#page=19)")\n'
    s += '\t\t(tags "Texas UQFN NoLead")\n'
    s += fp_prop("Reference", ref, -2.2, "F.SilkS")
    s += fp_prop("Value", value, 2.2)
    s += fp_prop("Datasheet", "", 0)
    s += fp_prop("Description", "2-bit dual-supply bus transceiver", 0)
    s += lcsc_prop(lcsc)
    s += f'\t\t(path "/{path_uuid}")\n\t\t(sheetname "/")\n\t\t(sheetfile "{sheetfile}")\n'
    s += "\t\t(attr smd)\n\t\t(duplicate_pad_numbers_are_jumpers no)\n"
    for b in body:
        if b.startswith("(pad "):
            num = re.search(r'\(pad "([^"]+)"', b).group(1)
            b = b[:-1].rstrip() + (f'\n\t\t\t(net "{nets[num]}")\n'
                                   f'\t\t\t(pintype "passive")\n'
                                   f'\t\t\t(uuid "{U()}")\n\t\t)')
        # add uuids to drawing elements that lack them
        if "(uuid" not in b and b.startswith("(fp_"):
            b = b[:-1].rstrip() + f'\n\t\t\t(uuid "{U()}")\n\t\t)'
        s += "\t\t" + b.replace("\n\t", "\n\t\t").rstrip() + "\n"
    s += "\t)"
    return s


def pcb_transform(path, sym_uuids, sch_path_hint=None):
    import os
    txt = open(path).read()

    # --- U22 footprint swap
    i = txt.index('(footprint "vitalq:MAX86178_WLP49"')
    depth = 0
    for j in range(i, len(txt)):
        if txt[j] == "(":
            depth += 1
        elif txt[j] == ")":
            depth -= 1
            if depth == 0:
                fp_old = txt[i:j + 1]
                fp_span = (i, j + 1)
                break
    fp_uuid = re.search(r'\n\t\t\(uuid "([0-9a-f-]+)"', fp_old).group(1)
    m_path = re.search(r'\(path "([^"]+)"\)', fp_old)
    fp_path = m_path.group(1) if m_path else "/29d3af96-bc5d-4a32-8c0b-3b3f396edf9a"
    sheetfile = os.path.basename(sch_path_hint) if sch_path_hint else "vitalq_v2.kicad_sch"
    ref_prop = re.search(r'(\(property "Reference" "U22".*?\n\t\t\))', fp_old, re.S).group(1)

    s = '\t(footprint "vitalq:MAX86141_WLP20"\n'
    s += '\t\t(layer "B.Cu")\n'
    s += '\t\t(locked yes)\n'
    s += f'\t\t(uuid "{fp_uuid}")\n'
    s += "\t\t(at 26.1 45.05 180)\n"
    s += '\t\t(descr "MAX86141 PPG AFE, 20-bump WLP 2.048x1.848mm, 0.4mm pitch (21-100134)")\n'
    s += '\t\t(tags "BGA WLP MAX86141")\n'
    s += ref_prop
    s += fp_prop("Value", "MAX86141ENP+", 1.4, "B.Fab")
    s += fp_prop("Datasheet", "", 0, "B.Fab")
    s += fp_prop("Description", "Optical PPG AFE, 3-ch LED driver, 20-WLP", 0, "B.Fab")
    s += fp_prop("LCSC", "C5328762", 0, "B.Fab")
    s += f'\t\t(path "{fp_path}")\n\t\t(sheetname "/")\n\t\t(sheetfile "{sheetfile}")\n'
    s += "\t\t(attr smd)\n"
    s += "\t\t(duplicate_pad_numbers_are_jumpers no)\n"
    # mirrored drawings (lib x,-y -> local x,-y); footprint rot 180 renders them
    s += '\t\t(fp_line (start -1.024 0.924) (end 1.024 0.924) (stroke (width 0.05) (type solid)) (layer "B.Fab") (uuid "%s"))\n' % U()
    s += '\t\t(fp_line (start -1.024 -0.924) (end 1.024 -0.924) (stroke (width 0.05) (type solid)) (layer "B.Fab") (uuid "%s"))\n' % U()
    s += '\t\t(fp_line (start -1.024 0.924) (end -1.024 -0.924) (stroke (width 0.05) (type solid)) (layer "B.Fab") (uuid "%s"))\n' % U()
    s += '\t\t(fp_line (start 1.024 0.924) (end 1.024 -0.924) (stroke (width 0.05) (type solid)) (layer "B.Fab") (uuid "%s"))\n' % U()
    s += '\t\t(fp_circle (center -1.15 0.95) (end -1.05 0.95) (stroke (width 0.1) (type solid)) (fill no) (layer "B.SilkS") (uuid "%s"))\n' % U()
    s += '\t\t(fp_rect (start -1.15 -1.05) (end 1.15 1.05) (stroke (width 0.05) (type solid)) (fill no) (layer "B.CrtYd") (uuid "%s"))\n' % U()
    s += '\t\t(fp_text user "${REFERENCE}" (at 0 0 180) (layer "B.Fab") (uuid "%s") (effects (font (size 0.3 0.3) (thickness 0.05)) (justify mirror)))\n' % U()
    s += wlp20_pads_bcu()
    s += "\t)"
    txt = txt[:fp_span[0]] + s + txt[fp_span[1]:]

    # --- add U26 + C86-C91 footprints, parked off-board
    tail = []
    tail.append(uqfn10_footprint(57.0, 21.0, "U26", "SN74AXC2T245RSWR",
                                 sym_uuids["U26"], "C1882550", sheetfile))
    # clone embedded cap footprints so lib_footprint_mismatch stays clean
    tmpl_0402 = fp_by_ref(txt, "C76")   # C_0402_1005Metric
    tmpl_0603 = fp_by_ref(txt, "C1")    # C_0603_1608Metric
    for k, (ref, val, fp, rail, lcsc) in enumerate(NEW_CAPS):
        x = 65.0 + 8.0 * k
        tmpl = tmpl_0603 if ref == "C89" else tmpl_0402
        tail.append(clone_fp(tmpl, ref, val, sym_uuids[ref], x, 21.0,
                             {"1": rail, "2": "GND"}, lcsc))
    txt = txt.rstrip()
    assert txt.endswith(")")
    txt = txt[:-1] + "\n" + "\n".join(tail) + "\n)\n"
    open(path, "w").write(txt)
    print(f"pcb rewritten: {path}")


def main():
    sch, pcb = sys.argv[1], sys.argv[2]
    mod_out = sys.argv[4] if len(sys.argv) > 4 and sys.argv[3] == "--mod-out" else None

    sym_uuids = sch_transform(sch)
    print("new symbol uuids:", sym_uuids)

    if mod_out:
        import os
        p = os.path.join(mod_out, "MAX86141_WLP20.kicad_mod")
        open(p, "w").write('(kicad_mod (version 20241209) (generator "swap_max86141")\n' + wlp20_mod() + "\n)\n")
        print("wrote", p)

    pcb_transform(pcb, sym_uuids, sch_path_hint=sch)


if __name__ == "__main__":
    main()
