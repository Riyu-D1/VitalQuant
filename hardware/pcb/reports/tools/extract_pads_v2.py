#!/usr/bin/env python3
"""Extract every pad from vitalq_v2.kicad_pcb -> pinmap CSV.
v2 fix: this board stores pad nets as (net "NAME") -- name-only, no numeric id --
and has no top-level (net id "name") table. v1 read net[2] and emitted ''.
Pure s-expression parse (no pcbnew). One CSV row per PAD."""
import re, sys, csv, math

PATH = sys.argv[1] if len(sys.argv) > 1 else "hardware/pcb/vitalq_v2.kicad_pcb"
OUT  = sys.argv[2] if len(sys.argv) > 2 else "hardware/pcb/reports/06_tool_output/01_netlist_pinmap_fixed.csv"

src = open(PATH, encoding="utf-8").read()

TOK = re.compile(r'\s*([()]|"(?:[^"\\]|\\.)*"|[^\s()]+)')
toks = TOK.findall(src)
pos = 0
def parse():
    global pos
    out = []
    while pos < len(toks):
        t = toks[pos]; pos += 1
        if t == '(':
            out.append(parse())
        elif t == ')':
            return out
        else:
            out.append(t)
    return out

def tstr(s):
    return s[1:-1] if s.startswith('"') else s

def tree():
    global pos
    root = []
    while pos < len(toks):
        t = toks[pos]
        if t == '(':
            pos += 1
            root.append(parse())
        else:
            pos += 1
            root.append(t)
    return root

doc = tree()[0]
assert doc[0] == 'kicad_pcb', doc[0]

def child(node, name):
    for ch in node[1:]:
        if isinstance(ch, list) and ch and ch[0] == name:
            return ch
    return None

rows = []
fp_total = 0
noname = 0
for ch in doc[1:]:
    if not (isinstance(ch, list) and ch[0] == 'footprint'):
        continue
    fp_total += 1
    lib  = tstr(ch[1]) if len(ch) > 1 else ''
    at   = child(ch, 'at') or []
    fx, fy = float(at[1]), float(at[2])
    fr   = float(at[3]) if len(at) > 3 else 0.0
    cr, sr = math.cos(math.radians(fr)), math.sin(math.radians(fr))
    side = 'F'
    lyr  = child(ch, 'layer')
    if lyr and 'B.Cu' in lyr[1]:
        side = 'B'
    ref = '?'
    for p in ch[1:]:
        if isinstance(p, list) and p[0] == 'property' and tstr(p[1]) == 'Reference':
            ref = tstr(p[2])
    for p in ch[1:]:
        if not (isinstance(p, list) and p[0] == 'pad'):
            continue
        num  = tstr(p[1])
        ptype = p[2] if len(p) > 2 else ''
        prole = p[3] if len(p) > 3 else ''
        pat  = child(p, 'at') or []
        px, py = (float(pat[1]), float(pat[2])) if len(pat) >= 3 else (0.0, 0.0)
        net  = child(p, 'net')
        # fix_v19 board: (net "NAME") single-arg form
        if net and len(net) >= 2:
            netn = tstr(net[-1])
        else:
            netn = ''
            noname += 1
        wx = fx + px * cr - py * sr
        wy = fy + px * sr + py * cr
        rows.append([ref, lib, num, f"{ptype}/{prole}", netn,
                     f"{wx:.3f}", f"{wy:.3f}", side])

with open(OUT, 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['ref','footprint','pad_number','pad_type','net','x_mm','y_mm','side'])
    w.writerows(rows)

mech = sum(1 for r in rows if 'np_thru_hole' in r[3])
print(f"footprints={fp_total} pads={len(rows)} np_th={mech} no_net={noname} -> {OUT}")
