#!/usr/bin/env python3
"""Explicit KiCad 9 schematic writer.

Symbols come from hardware/pcb/lib/vitalq.kicad_sym. Placement is in
millimetres, schematic Y down. Symbol libraries are Y up; rotation 0 flips Y.
"""

from __future__ import annotations

import re
import uuid
from pathlib import Path

LIB_PATH = Path(__file__).resolve().parent / "lib" / "vitalq.kicad_sym"
PROJECT = "vitalq_hw_v1"


def uid() -> str:
    return str(uuid.uuid4())


def r2(v: float) -> float:
    return round(float(v) + 0.0, 2)


def snap(v: float) -> float:
    """KiCad's 50 mil connection grid. Off-grid endpoints are ERC warnings."""
    return r2(round(float(v) / 1.27) * 1.27)


class Pin:
    def __init__(self, num, name, etype, x, y, rot, length):
        self.num = num
        self.name = name
        self.etype = etype
        self.x = r2(x)
        self.y = r2(y)
        self.rot = int(rot)
        self.length = r2(length)


class SymbolDef:
    def __init__(self, name, block, pins):
        self.name = name
        self.block = block
        self.pins = pins  # number -> Pin


def _top_symbols(text: str) -> list[str]:
    i = text.find("(kicad_symbol_lib")
    depth = 0
    child = None
    out = []
    j = i
    while j < len(text):
        c = text[j]
        if c == "(":
            if depth == 1:
                child = j
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 1 and child is not None:
                out.append(text[child : j + 1])
                child = None
            if depth == 0:
                break
        j += 1
    return out


def load_lib(path: Path = LIB_PATH) -> dict[str, SymbolDef]:
    text = path.read_text(encoding="utf-8")
    lib = {}
    for block in _top_symbols(text):
        m = re.match(r'\(symbol "([^"]+)"', block.lstrip())
        if not m:
            continue
        name = m.group(1)
        pins = {}
        for pm in re.finditer(
            r"\(pin (\w+) line\s+\(at ([-\d.]+) ([-\d.]+) (\d+)\)\s+\(length ([-\d.]+)\)(.*?)\(number \"([^\"]+)\"",
            block,
            re.S,
        ):
            etype, x, y, rot, length, mid, num = pm.groups()
            nm = re.search(r'\(name "([^"]*)"', mid)
            pins[num] = Pin(num, nm.group(1) if nm else "~", etype, x, y, rot, length)
        lib[name] = SymbolDef(name, block, pins)
    return lib


def xform(px: float, py: float, rot: int) -> tuple[float, float]:
    """Symbol Y-up offset -> schematic Y-down offset. rot is CCW degrees."""
    if rot == 0:
        return r2(px), r2(-py)
    if rot == 90:
        return r2(py), r2(px)
    if rot == 180:
        return r2(-px), r2(py)
    if rot == 270:
        return r2(-py), r2(-px)
    raise ValueError(rot)


def outward(pin: Pin, rot: int) -> tuple[float, float]:
    """Unit vector in schematic space pointing away from the symbol body.

    A KiCad pin is drawn FROM its connection point along `pin.rot`
    (0=+X, 90=+Y in symbol space) back into the body.
    """
    into = {0: (1.0, 0.0), 90: (0.0, 1.0), 180: (-1.0, 0.0), 270: (0.0, -1.0)}[pin.rot]
    ox, oy = -into[0], -into[1]
    dx, dy = xform(ox, oy, rot)
    # xform of a unit axis stays unit for 90-degree rotations
    return dx, dy


class Inst:
    def __init__(self, sym: SymbolDef, ref, value, footprint, x, y, rot=0, bom=True, board=True):
        self.sym = sym
        self.ref = ref
        self.value = value
        self.footprint = footprint
        self.x = snap(x)
        self.y = snap(y)
        self.rot = rot
        self.bom = bom
        self.board = board
        self.uuid = uid()
        self.pin_uuids = {n: uid() for n in sym.pins}
        self.ref_at = None
        self.val_at = None

    def pin_xy(self, num: str) -> tuple[float, float]:
        p = self.sym.pins[str(num)]
        dx, dy = xform(p.x, p.y, self.rot)
        return r2(self.x + dx), r2(self.y + dy)

    def pin_out(self, num: str) -> tuple[float, float, float, float]:
        p = self.sym.pins[str(num)]
        x, y = self.pin_xy(num)
        vx, vy = outward(p, self.rot)
        return x, y, vx, vy

    def bbox(self) -> tuple[float, float, float, float]:
        xs, ys = [], []
        for n in self.sym.pins:
            x, y = self.pin_xy(n)
            xs.append(x)
            ys.append(y)
        if not xs:
            return self.x, self.y, self.x, self.y
        return min(xs), min(ys), max(xs), max(ys)


def _effects(size=1.27, bold=False, hide=False, justify=None):
    font = f"(size {size} {size})"
    if bold:
        font += " (bold yes)"
    just = f" (justify {justify})" if justify else ""
    hide_s = " (hide yes)" if hide else ""
    return f"(effects (font {font}){just}{hide_s})"


class Sheet:
    def __init__(self, title, filename, page):
        self.title = title
        self.filename = filename
        self.page = page
        self.uuid = uid()
        self.insts: list[Inst] = []
        self.draw: list[str] = []
        self.used: set[str] = set()

    def add(self, lib, name, ref, value, footprint, x, y, rot=0, bom=True, board=True) -> Inst:
        sym = lib[name]
        inst = Inst(sym, ref, value, footprint, x, y, rot, bom, board)
        self.insts.append(inst)
        self.used.add(name)
        return inst

    def wire(self, pts):
        clean = []
        for x, y in pts:
            p = (snap(x), snap(y))
            if not clean or clean[-1] != p:
                clean.append(p)
        if len(clean) < 2:
            return
        # Split into segments so each wire is a single straight run.
        for a, b in zip(clean, clean[1:]):
            if a == b:
                continue
            self.draw.append(
                "\t(wire\n"
                "\t\t(pts\n"
                f"\t\t\t(xy {a[0]} {a[1]})\n"
                f"\t\t\t(xy {b[0]} {b[1]})\n"
                "\t\t)\n"
                "\t\t(stroke (width 0) (type default))\n"
                f"\t\t(uuid \"{uid()}\")\n"
                "\t)"
            )

    def junction(self, x, y):
        x, y = snap(x), snap(y)
        self.draw.append(
            f'\t(junction (at {x} {y}) (diameter 0) (color 0 0 0 0) (uuid "{uid()}"))'
        )

    def noconn(self, x, y):
        x, y = snap(x), snap(y)
        self.draw.append(f'\t(no_connect (at {x} {y}) (uuid "{uid()}"))')

    def label(self, name, x, y, angle, local=False, shape="bidirectional"):
        x, y, angle = snap(x), snap(y), int(angle)
        just = {0: "left", 180: "right", 90: "right", 270: "left"}.get(angle, "left")
        fx = _effects(1.27, justify=just)
        if local:
            self.draw.append(
                f'\t(label "{name}"\n'
                f"\t\t(at {x} {y} {angle})\n"
                f"\t\t{fx}\n"
                f'\t\t(uuid "{uid()}")\n'
                "\t)"
            )
        else:
            self.draw.append(
                f'\t(global_label "{name}"\n'
                f"\t\t(shape {shape})\n"
                f"\t\t(at {x} {y} {angle})\n"
                f"\t\t{fx}\n"
                f'\t\t(uuid "{uid()}")\n'
                '\t\t(property "Intersheetrefs" "${INTERSHEET_REFS}"\n'
                f"\t\t\t(at {x} {y} 0)\n"
                f"\t\t\t{_effects(1.27, hide=True)}\n"
                "\t\t)\n"
                "\t)"
            )

    def text(self, s, x, y, size=1.8, bold=False):
        # Escape quotes
        s = s.replace("\\", "\\\\").replace('"', '\\"')
        self.draw.append(
            "\t(text \"%s\"\n"
            "\t\t(exclude_from_sim no)\n"
            "\t\t(at %s %s 0)\n"
            "\t\t%s\n"
            "\t\t(uuid \"%s\")\n"
            "\t)" % (s, r2(x), r2(y), _effects(size, bold=bold, justify="left bottom"), uid())
        )

    def rect(self, x1, y1, x2, y2):
        self.draw.append(
            "\t(rectangle\n"
            "\t\t(start %s %s)\n"
            "\t\t(end %s %s)\n"
            "\t\t(stroke (width 0.4) (type dash))\n"
            "\t\t(fill (type none))\n"
            "\t\t(uuid \"%s\")\n"
            "\t)" % (r2(x1), r2(y1), r2(x2), r2(y2), uid())
        )

    def stub(self, inst: Inst, num, dist=5.08) -> tuple[float, float]:
        x, y, vx, vy = inst.pin_out(num)
        x2, y2 = r2(x + vx * dist), r2(y + vy * dist)
        self.wire([(x, y), (x2, y2)])
        return x2, y2, vx, vy

    def flag_nc(self, inst: Inst, num):
        x, y = inst.pin_xy(num)
        self.noconn(x, y)

    def emit_symbol(self, inst: Inst, path: str) -> str:
        x0, y0, x1, y1 = inst.bbox()
        # Reference above the symbol (smaller schematic Y), value below.
        ref_x = r2((x0 + x1) / 2)
        ref_y = r2(y0 - 1.8)
        val_y = r2(y1 + 1.8)
        if inst.ref_at:
            ref_x, ref_y = r2(inst.ref_at[0]), r2(inst.ref_at[1])
        if inst.val_at:
            val_x_override, val_y = inst.val_at
            # val_x is assigned below for non-power parts; stash the override.
            inst._val_x_override = r2(val_x_override)
            val_y = r2(val_y)
        power = inst.sym.name in {"GND", "+3V3", "+1V8", "VBUS", "PWR_FLAG"}
        ref_fx = _effects(1.27, hide=power)
        if inst.sym.name == "GND":
            val_x, val_y = inst.x, r2(inst.y + 4.5)
        elif inst.sym.name in {"+3V3", "+1V8", "VBUS"}:
            val_x, val_y = inst.x, r2(inst.y - 4.5)
        elif inst.sym.name == "PWR_FLAG":
            val_x, val_y = r2(inst.x + 3.5), inst.y
        else:
            val_x = getattr(inst, "_val_x_override", ref_x)
        bom = "yes" if inst.bom else "no"
        board = "yes" if inst.board else "no"
        pins = "\n".join(
            f'\t\t(pin "{n}" (uuid "{inst.pin_uuids[n]}"))' for n in inst.sym.pins
        )
        fp = inst.footprint or ""
        return f"""\t(symbol
\t\t(lib_id "vitalq:{inst.sym.name}")
\t\t(at {inst.x} {inst.y} {inst.rot})
\t\t(unit 1)
\t\t(exclude_from_sim no)
\t\t(in_bom {bom})
\t\t(on_board {board})
\t\t(dnp no)
\t\t(uuid "{inst.uuid}")
\t\t(property "Reference" "{inst.ref}"
\t\t\t(at {ref_x} {ref_y} 0)
\t\t\t{ref_fx}
\t\t)
\t\t(property "Value" "{inst.value}"
\t\t\t(at {val_x} {val_y} 0)
\t\t\t{_effects(1.27, hide=(inst.sym.name == "PWR_FLAG"))}
\t\t)
\t\t(property "Footprint" "{fp}"
\t\t\t(at {inst.x} {inst.y} 0)
\t\t\t{_effects(1.27, hide=True)}
\t\t)
\t\t(property "Datasheet" ""
\t\t\t(at {inst.x} {inst.y} 0)
\t\t\t{_effects(1.27, hide=True)}
\t\t)
{pins}
\t\t(instances
\t\t\t(project "{PROJECT}"
\t\t\t\t(path "{path}"
\t\t\t\t\t(reference "{inst.ref}")
\t\t\t\t\t(unit 1)
\t\t\t\t)
\t\t\t)
\t\t)
\t)"""


def embed_lib(lib: dict[str, SymbolDef], names: set[str]) -> str:
    chunks = []
    for name in sorted(names):
        block = lib[name].block.lstrip()
        # Only the outer symbol name takes the library prefix.
        block = re.sub(r'^\(symbol "[^"]+"', f'(symbol "vitalq:{name}"', block, count=1)
        # Indent one tab so it sits inside lib_symbols.
        indented = "\n".join("\t" + line if line else line for line in block.splitlines())
        chunks.append(indented)
    return "\t(lib_symbols\n" + "\n".join(chunks) + "\n\t)"


def emit_sheet_file(sheet: Sheet, lib, root_uuid: str, path: str) -> str:
    body_syms = "\n".join(sheet.emit_symbol(i, path) for i in sheet.insts)
    draw = "\n".join(sheet.draw)
    return f"""(kicad_sch
\t(version 20250114)
\t(generator "eeschema")
\t(generator_version "9.0")
\t(uuid "{sheet.uuid}")
\t(paper "A3")
\t(title_block
\t\t(title "{sheet.title}")
\t\t(date "2026-09-26")
\t\t(rev "v1")
\t\t(company "VitalQ")
\t\t(comment 1 "Research prototype. Not a medical device.")
\t\t(comment 2 "Unrouted starting point. Confirm datasheet assumptions in README.")
\t)
{embed_lib(lib, sheet.used)}
{body_syms}
{draw}
\t(sheet_instances
\t\t(path "/"
\t\t\t(page "1")
\t\t)
\t)
\t(embedded_fonts no)
)
"""


def emit_root(root: Sheet, lib, children: list[Sheet], root_uuid: str) -> str:
    path = f"/{root_uuid}"
    body_syms = "\n".join(root.emit_symbol(i, path) for i in root.insts)
    sheets = []
    for ch in children:
        # Small navigation box. Parts live on the child sheet, not here.
        # Position is stored on the child as nav_at / nav_size by the caller.
        x, y = ch.nav_at
        w, h = ch.nav_size
        sheets.append(
            f"""\t(sheet
\t\t(at {r2(x)} {r2(y)})
\t\t(size {r2(w)} {r2(h)})
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(dnp no)
\t\t(stroke (width 0.254) (type solid))
\t\t(fill (color 0 0 0 0.0000))
\t\t(uuid "{ch.uuid}")
\t\t(property "Sheetname" "{ch.title}"
\t\t\t(at {r2(x)} {r2(y - 0.8)} 0)
\t\t\t{_effects(1.27, justify="left bottom")}
\t\t)
\t\t(property "Sheetfile" "{ch.filename}"
\t\t\t(at {r2(x)} {r2(y + h + 0.6)} 0)
\t\t\t{_effects(1.0, justify="left top")}
\t\t)
\t\t(instances
\t\t\t(project "{PROJECT}"
\t\t\t\t(path "/{root_uuid}"
\t\t\t\t\t(page "{ch.page}")
\t\t\t\t)
\t\t\t)
\t\t)
\t)"""
        )
    draw = "\n".join(root.draw)
    return f"""(kicad_sch
\t(version 20250114)
\t(generator "eeschema")
\t(generator_version "9.0")
\t(uuid "{root_uuid}")
\t(paper "A3")
\t(title_block
\t\t(title "{root.title}")
\t\t(date "2026-09-26")
\t\t(rev "v1")
\t\t(company "VitalQ")
\t\t(comment 1 "Research prototype. Not a medical device.")
\t\t(comment 2 "Power is drawn on this sheet. Other sheets contain their parts.")
\t)
{embed_lib(lib, root.used)}
{body_syms}
{draw}
{chr(10).join(sheets)}
\t(sheet_instances
\t\t(path "/"
\t\t\t(page "1")
\t\t)
\t)
\t(embedded_fonts no)
)
"""


# Label angle: text continues in the outward direction so it does not cover the symbol.
def label_angle(vx: float, vy: float) -> int:
    if vx > 0.5:
        return 0
    if vx < -0.5:
        return 180
    if vy > 0.5:
        return 90
    return 270
