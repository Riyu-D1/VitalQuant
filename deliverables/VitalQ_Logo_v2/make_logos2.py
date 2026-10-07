"""
VitalQ logo v2 - hidden-meaning concepts. Self-contained geometric SVGs.

Run:  ~/VitalQuant-place/case/.venv/bin/python make_logos2.py

Per concept: mark + lockups (VitalQuant AND VitalQ), each in
color/mono/reversed; favicon PNGs at 16/32/64; hero PNG at ~600px.
Text -> glyph outlines (Space Grotesk 620, OFL).
"""

import subprocess
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.varLib.instancer import instantiateVariableFont

HERE = Path(__file__).resolve().parent

BLUE = "#0B4EA2"
TEAL = "#0E8F8F"
INK = "#0E1B2C"
WHITE = "#FFFFFF"

VARIANTS = ("color", "mono", "reversed")
WORDMARKS = ("VitalQuant", "VitalQ")
WM_SIZE = 30

_font = TTFont(HERE / "assets" / "SpaceGrotesk-var.ttf")
instantiateVariableFont(_font, {"wght": 620}, inplace=True)
_gs = _font.getGlyphSet()
_cmap = _font.getBestCmap()
_UPM = _font["head"].unitsPerEm
_TRACK = 28


def glyph_ds(text):
    """[(char, d-string, advance-fontunits)]"""
    out = []
    for ch in text:
        gname = _cmap.get(ord(ch))
        if gname is None:
            out.append((ch, "", _UPM * 0.32))
            continue
        pen = SVGPathPen(_gs)
        _gs[gname].draw(pen)
        out.append((ch, pen.getCommands(), _gs[gname].width + _TRACK))
    return out


def text_paths(text, size, x, y, fill, tittle_fill=None):
    """Glyph outlines at baseline y. tittle_fill: if set, the i-dot uses it."""
    scale = size / _UPM
    out = []
    pen_x = x
    for ch, d, adv in glyph_ds(text):
        if d:
            if tittle_fill and ch == "i":
                subs = [s for s in d.split("M") if s.strip()]
                if len(subs) == 2:  # stem + dot
                    for i, sub in enumerate(subs):
                        f = tittle_fill if i == 1 else fill
                        out.append(
                            f'<path transform="translate({pen_x:.3f} {y:.3f})'
                            f' scale({scale:.5f} {-scale:.5f})" fill="{f}" '
                            f'd="M{sub}"/>')
                else:
                    out.append(
                        f'<path transform="translate({pen_x:.3f} {y:.3f})'
                        f' scale({scale:.5f} {-scale:.5f})" fill="{fill}"'
                        f' d="{d}"/>')
            else:
                out.append(
                    f'<path transform="translate({pen_x:.3f} {y:.3f}) '
                    f'scale({scale:.5f} {-scale:.5f})" fill="{fill}" d="{d}"/>')
        pen_x += adv * scale
    return "".join(out), pen_x - x


def svg_doc(w, h, body, bg=None):
    r = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} {h:.0f}">'
    if bg:
        r += f'<rect width="{w:.0f}" height="{h:.0f}" fill="{bg}"/>'
    return r + body + "</svg>"


def save(name, w, h, body, bg=None):
    p = HERE / f"{name}.svg"
    p.write_text(svg_doc(w, h, body, bg), encoding="utf-8")
    return p


def cols(variant):
    """(primary, accent, background, inner-glyph colour)"""
    if variant == "color":
        return BLUE, TEAL, None, INK
    if variant == "mono":
        return INK, INK, None, WHITE
    return WHITE, WHITE, INK, INK  # reversed


# ======================= concept marks (64x64) =============================

def m_rwave_v(c1, c2, _ink):
    """V whose right stroke overshoots into a QRS spike: the name's first
    letter IS a heartbeat."""
    return (f'<path d="M13 13 L27.5 51" stroke="{c1}" stroke-width="8.5" '
            'stroke-linecap="round" fill="none"/>'
            f'<path d="M27.5 51 L33 9 L37.5 23 L51 14" stroke="{c2}" '
            'stroke-width="8.5" stroke-linecap="round" '
            'stroke-linejoin="round" fill="none"/>')


def m_qcounter(c1, c2, _ink):
    """Ring Q; the tail is the SAME trace that spikes inside the counter -
    a heartbeat hiding in the hole."""
    return (f'<circle cx="30" cy="30" r="20" fill="none" stroke="{c1}" '
            'stroke-width="8.5"/>'
            f'<path d="M22 32 h3.4 l1.6 -8.5 l2 10 l1.3 -2.3 h3.4 '
            'L48 54" '
            f'fill="none" stroke="{c2}" stroke-width="4.4" '
            'stroke-linecap="round" stroke-linejoin="round"/>')


def m_qdots(c1, c2, _ink):
    """PQRST traced by discrete sample dots - 'life, measured'."""
    # trace (faint) + sample dots on the vertices
    trace = ("M7 42 h5 q1.6 -2.6 3.2 0 h3.4 "
             "l1.2 2.2 l1.4 -3 l1.8 -19 l1.9 21 l1.3 2.2 h3.6 "
             "q2.4 -3.8 4.8 0 h4.6 q1.6 -2.4 3.2 0 h5")
    dots = [(8, 42, 2.4, c1), (13.5, 40, 2.8, c1),
            (21, 43.6, 2.6, c1), (24, 32, 2.6, c1),
            (25.5, 22.5, 4.0, c2),        # R peak - accent
            (27.5, 44.5, 2.6, c1), (33, 42, 2.4, c1),
            (38, 38.8, 3.2, c1),          # T peak
            (44, 42, 2.4, c1), (56, 42, 2.4, c1)]
    s = [f'<path d="{trace}" fill="none" stroke="{c1}" stroke-width="2.2" '
         'stroke-linecap="round" stroke-linejoin="round" opacity="0.4"/>']
    for x, y, r, c in dots:
        s.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{c}"/>')
    return "".join(s)


def m_breath_beat(c1, c2, _ink):
    """One stroke: slow respiration wave whose crest IS the QRS spike -
    two vitals in a single line."""
    return (f'<path d="M6 44 C12 44 14 30 21 24 L25 12 L29 30 '
            'C33 40 40 47 47 45 C53 43 56 38 58 36" '
            f'fill="none" stroke="{c1}" stroke-width="5.4" '
            'stroke-linecap="round" stroke-linejoin="round"/>'
            f'<circle cx="25" cy="12" r="3.2" fill="{c2}"/>')


def m_vcg(c1, c2, _ink):
    """Vectorcardiogram QRS loop forming a Q - the heart's spatial vector."""
    # teardrop QRS loop: cusp bottom-left (the E-point), bulging top-right;
    # tail exits the cusp -> Q letterform
    return (f'<path d="M24 53 C20 46 19 30 25 19 C31 9 44 9 51 19 '
            'C57 29 54 44 44 50 C37 55 28 57 24 53 Z" '
            f'fill="none" stroke="{c1}" stroke-width="6.2" '
            'stroke-linecap="round" stroke-linejoin="round"/>'
            f'<path d="M28 50 L49 58" stroke="{c2}" stroke-width="5" '
            'stroke-linecap="round"/>')


def m_body_hub(c1, c2, _ink):
    """A chest trace that ends radiating wireless arcs - from skin to
    clinician."""
    arc = ""
    import math
    hx, hy = 43, 40  # hub centre: where the trace ends
    for r in (8, 13.5, 19):
        x0 = hx + r * math.cos(math.radians(-52))
        y0 = hy + r * math.sin(math.radians(-52))
        x1 = hx + r * math.cos(math.radians(52))
        y1 = hy + r * math.sin(math.radians(52))
        arc += (f'<path d="M{x0:.1f} {y0:.1f} A{r} {r} 0 0 0 '
                f'{x1:.1f} {y1:.1f}" fill="none" stroke="{c2}" '
                'stroke-width="3.6" stroke-linecap="round"/>')
    # trace flows right into the hub dot - one continuous line of custody
    return (f'<path d="M6 40 h5 l1.6 -2.4 l1.4 0 l1.2 2 l1.4 -3.4 '
            'l1.6 -15 l1.8 17 l1.2 2 L39 40" '
            f'fill="none" stroke="{c1}" stroke-width="4.6" '
            'stroke-linecap="round" stroke-linejoin="round"/>'
            f'<circle cx="{hx}" cy="{hy}" r="3.4" fill="{c2}"/>' + arc)


def m_tittle(c1, c2, _ink):
    """Torso arc + the device dot at the sternum; in the lockup that dot IS
    the tittle of the 'i'."""
    return (f'<path d="M14 50 C14 28 21 15 32 15 C43 15 50 28 50 50" '
            f'fill="none" stroke="{c1}" stroke-width="6.5" '
            'stroke-linecap="round"/>'
            f'<path d="M20 44 q-1.5 -7 1 -12" stroke="{c1}" '
            'stroke-width="2.8" fill="none" stroke-linecap="round" '
            'opacity="0.5"/>'
            f'<path d="M44 44 q1.5 -7 -1 -12" stroke="{c1}" '
            'stroke-width="2.8" fill="none" stroke-linecap="round" '
            'opacity="0.5"/>'
            f'<circle cx="32" cy="33" r="5" fill="{c2}"/>')


CONCEPTS = [
    ("c1_rwave_v",    "R-wave V",        m_rwave_v),
    ("c2_qcounter",   "Hidden-pulse Q",  m_qcounter),
    ("c3_qdots",      "Quantised beat",  m_qdots),
    ("c4_breath",     "Breath + beat",   m_breath_beat),
    ("c5_vcg",        "VCG loop Q",      m_vcg),
    ("c6_hub",        "Body to hub",     m_body_hub),
    ("c7_tittle",     "Chest tittle",    m_tittle),
]

# mark scale/pos inside lockups
MARK_SC = 0.6875


def build():
    built = []
    for cid, _title, fn in CONCEPTS:
        tittle_concept = (cid == "c7_tittle")
        for v in VARIANTS:
            c1, c2, bg, ink = cols(v)
            mark_body = fn(c1, c2, ink)
            built.append(save(f"{cid}_mark_{v}", 64, 64, mark_body, bg))
            for wm in WORDMARKS:
                tf = c2 if tittle_concept and v != "reversed" else (
                    INK if tittle_concept and v == "reversed" else None)
                wm_paths, wm_w = text_paths(
                    wm, WM_SIZE, 58, 44, c1, tittle_fill=tf)
                body = (f'<g transform="translate(6 10) scale({MARK_SC})">'
                        f'{mark_body}</g>' + wm_paths)
                wname = "vitalquant" if wm == "VitalQuant" else "vitalq"
                built.append(save(f"{cid}_lockup_{wname}_{v}",
                                  58 + wm_w + 4, 64, body, bg))
    return built


def png(svg_path, size=1024, out=None):
    out = out or svg_path.with_suffix(".png")
    subprocess.run(["qlmanage", "-t", "-s", str(size), "-o", str(HERE),
                    str(svg_path)], capture_output=True)
    produced = svg_path.with_suffix(svg_path.suffix + ".png")
    if produced.exists():
        produced.rename(out)
    return out.exists()


def main():
    built = build()
    print(f"{len(built)} svg files")
    for p in built:
        png(p)
    for cid, _t, _f in CONCEPTS:
        for v in VARIANTS:
            m = HERE / f"{cid}_mark_{v}.svg"
            for s in (16, 32, 64):
                png(m, s, HERE / f"{cid}_mark_{v}_{s}.png")
        png(HERE / f"{cid}_mark_color.svg", 600,
            HERE / f"{cid}_mark_hero.png")
    print("renders done")


if __name__ == "__main__":
    main()
