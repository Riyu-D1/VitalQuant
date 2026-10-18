#!/usr/bin/env python3
"""Flatten the hierarchical VitalQ schematic into one .kicad_sch.

All sheets already use global_label only (no hierarchical/local labels), so
merging sheet contents into one file preserves connectivity exactly. Each
subsheet's content is shifted into its own labelled block cell; sheet objects
and sheet_instances are dropped; symbol instance paths are rewritten to the
root path; lib_symbols are merged by name.

Output: vitalq_flat.kicad_sch (sources untouched).
"""
import re
import uuid as _uuid
from pathlib import Path

D = Path(__file__).resolve().parent
ROOT_SCH = D / "vitalq_hw_v1.kicad_sch"
OUT = D / "vitalq_flat.kicad_sch"

HEADER = {"version", "generator", "generator_version", "uuid", "paper",
          "title_block", "lib_symbols", "sheet_instances", "embedded_fonts"}
DROP = {"sheet", "sheet_instances"}
POS = re.compile(
    r"\((at|xy|start|end|mid|corner)\s+(-?[\d.eE+]+)\s+(-?[\d.eE+]+)")


def split_top(text):
    """Top-level sexprs inside the root (kicad_sch ...) element."""
    items, depth, start, instr, esc = [], 0, None, False, False
    for i, ch in enumerate(text):
        if instr:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                instr = False
            continue
        if ch == '"':
            instr = True
        elif ch == "(":
            if depth == 1:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 1 and start is not None:
                items.append(text[start:i + 1])
                start = None
    return items


def head(el):
    return re.match(r"\(\s*([^\s()\"]+)", el).group(1)


def prop(el, name):
    m = re.search(rf'\(property "{name}" "([^"]*)"', el)
    return m.group(1) if m else ""


def bbox(els):
    xs, ys = [], []
    for el in els:
        for m in POS.finditer(el):
            xs.append(float(m.group(2)))
            ys.append(float(m.group(3)))
    return (min(xs), min(ys), max(xs), max(ys)) if xs else (0, 0, 0, 0)


def shift(el, dx, dy):
    return POS.sub(lambda m: f"({m.group(1)} {float(m.group(2)) + dx:.6g} "
                            f"{float(m.group(3)) + dy:.6g}", el)


def uid():
    return str(_uuid.uuid4())


def label_text(s, x, y):
    return ('\t(text "%s"\n\t\t(exclude_from_sim no)\n\t\t(at %s %s 0)\n'
            '\t\t(effects (font (size 5 5) bold) (justify left bottom))\n'
            '\t\t(uuid "%s")\n\t)' % (s, round(x, 4), round(y, 4), uid()))


def block_rect(x1, y1, x2, y2):
    return ('\t(rectangle\n\t\t(start %s %s)\n\t\t(end %s %s)\n'
            '\t\t(stroke (width 0.4) (type dash))\n\t\t(fill (type none))\n'
            '\t\t(uuid "%s")\n\t)' % (round(x1, 4), round(y1, 4),
                                      round(x2, 4), round(y2, 4), uid()))


def main():
    root_txt = ROOT_SCH.read_text()
    root_els = split_top(root_txt)
    root_uuid = re.search(r'\(uuid "([^"]+)"', root_txt).group(1)

    # sheet blocks on root -> ordered subsheet list
    sheets = []
    for el in root_els:
        if head(el) == "sheet":
            m = re.search(r"\(at ([\d.]+) ([\d.]+)\)", el)
            sheets.append({"file": prop(el, "Sheetfile"),
                           "name": prop(el, "Sheetname"),
                           "x": float(m.group(1)), "y": float(m.group(2))})
    sheets.sort(key=lambda s: (s["y"], s["x"]))
    print("sheets:", [(s["name"], s["file"]) for s in sheets])

    libsyms = {}          # symbol name -> element text
    flat_content = []     # shifted content elements, in order

    # --- root content stays at origin, labelled ------------------------
    root_content = [el for el in root_els if head(el) not in HEADER | DROP]
    rx0, ry0, rx1, ry1 = bbox(root_content)
    PAD = 12.0
    flat_content.append(label_text("POWER & CHARGING (root sheet)",
                                   rx0 - PAD, ry0 - 18))
    flat_content.append(block_rect(rx0 - PAD, ry0 - PAD,
                                   rx1 + PAD, ry1 + PAD))
    for el in root_content:
        flat_content.append(el)

    # --- lib_symbols: collect from every sheet --------------------------
    all_files = [ROOT_SCH] + [D / s["file"] for s in sheets]
    sheet_els = {}
    for f in all_files:
        els = split_top(f.read_text())
        sheet_els[f.name] = els
        for el in els:
            if head(el) == "lib_symbols":
                for sym in _inner_els(el):
                    nm = re.match(r'\(\s*symbol\s+"([^"]+)"', sym)
                    if nm and nm.group(1) not in libsyms:
                        libsyms[nm.group(1)] = sym

    # --- subsheets into grid cells right of the root block --------------
    CELL_W, CELL_H, COLS = 560.0, 430.0, 3
    gx0 = rx1 + 80.0
    gy0 = 0.0
    for i, s in enumerate(sheets):
        els = sheet_els[s["file"]]
        content = [el for el in els if head(el) not in HEADER | DROP]
        bad = [head(el) for el in content if head(el) in ("sheet",)]
        assert not bad, f"nested sheet in {s['file']}"
        bx0, by0, bx1, by1 = bbox(content)
        col, row = i % COLS, i // COLS
        cx, cy = gx0 + col * CELL_W, gy0 + row * CELL_H
        # snap the translation to the 1.27 mm grid so pins stay on-grid
        dx = round((cx - bx0) / 1.27) * 1.27
        dy = round((cy - by0) / 1.27) * 1.27
        w, h = bx1 - bx0, by1 - by0
        nx0, ny0 = bx0 + dx, by0 + dy   # block extents after shift
        flat_content.append(label_text(s["name"], nx0, ny0 - 14))
        flat_content.append(block_rect(nx0 - PAD, ny0 - PAD - 10,
                                       nx0 + w + PAD, ny0 + h + PAD))
        pat = re.compile(r'\(path\s+"/%s/[^"]+"' % root_uuid)
        for el in content:
            el2 = shift(el, dx, dy)
            el2 = pat.sub(f'(path "/{root_uuid}"', el2)
            flat_content.append(el2)

    # --- assemble --------------------------------------------------------
    out = ['(kicad_sch\n',
           '\t(version 20250114)\n',
           '\t(generator "eeschema")\n',
           '\t(generator_version "9.0")\n',
           f'\t(uuid "{root_uuid}")\n',
           '\t(paper "A0")\n',
           '\t(title_block\n'
           '\t\t(title "VitalQ hw_v1 — flat")\n'
           '\t\t(date "2026-10-07")\n'
           '\t\t(rev "v1")\n'
           '\t\t(company "VitalQ")\n'
           '\t\t(comment 1 "Research prototype. Not a medical device.")\n'
           '\t\t(comment 2 "Flattened from the hierarchical sheet set; '
           'connectivity identical.")\n\t)\n',
           '\t(lib_symbols\n' + "\n".join(libsyms.values()) + "\n\t)\n"]
    out += flat_content
    out.append('\n\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n'
               '\t\t)\n\t)\n\t(embedded_fonts no)\n)')
    OUT.write_text("".join(out))
    print("wrote", OUT, f"({len(flat_content)} content elements, "
          f"{len(libsyms)} lib symbols)")


def _inner_els(lib_symbols_el):
    """Split children of a (lib_symbols ...) element."""
    inner = lib_symbols_el.strip()
    assert inner.startswith("(lib_symbols")
    inner = inner[len("(lib_symbols"):-1]
    items, depth, start, instr, esc = [], 0, None, False, False
    for i, ch in enumerate(inner):
        if instr:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                instr = False
            continue
        if ch == '"':
            instr = True
        elif ch == "(":
            if depth == 0:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0 and start is not None:
                items.append(inner[start:i + 1])
                start = None
    return items


if __name__ == "__main__":
    main()
