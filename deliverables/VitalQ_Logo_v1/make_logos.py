"""
VitalQ logo generator - hand-authored geometric SVGs, self-contained.

Run:  ~/VitalQuant-place/case/.venv/bin/python make_logos.py

Emits per concept: mark + lockup, each in color / mono / reversed,
plus 16/32 px favicon PNGs and a comparison sheet.
Text is converted to glyph outlines (Space Grotesk, OFL) via fontTools -
the SVGs are fully self-contained vector files.
"""

import base64
import subprocess
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.varLib.instancer import instantiateVariableFont

HERE = Path(__file__).resolve().parent

# ----------------------------- palette ------------------------------------
BLUE = "#0B4EA2"   # primary, ~8:1 on white
TEAL = "#0E8F8F"   # accent (optical/PPG channel)
INK = "#0E1B2C"
WHITE = "#FFFFFF"

VARIANTS = ("color", "mono", "reversed")

# ------------------------- text -> outlines -------------------------------
_font = TTFont(HERE / "assets" / "SpaceGrotesk-var.ttf")
instantiateVariableFont(_font, {"wght": 620}, inplace=True)
_gs = _font.getGlyphSet()
_cmap = _font.getBestCmap()
_UPM = _font["head"].unitsPerEm
_TRACK = 28  # letter tracking, font units


def text_paths(text, size, x, y, fill):
    """Glyph outlines for `text` at `size` px, baseline y. Returns svg str."""
    scale = size / _UPM
    out = []
    pen_x = x
    for ch in text:
        gname = _cmap.get(ord(ch))
        if gname is None:
            pen_x += size * 0.32
            continue
        pen = SVGPathPen(_gs)
        _gs[gname].draw(pen)
        d = pen.getCommands()
        if d:
            out.append(
                f'<path transform="translate({pen_x:.3f} {y:.3f}) '
                f'scale({scale:.5f} {-scale:.5f})" fill="{fill}" d="{d}"/>')
        pen_x += (_gs[gname].width + _TRACK) * scale
    return "".join(out), pen_x - x  # width incl tracking


def text_width(text, size):
    return sum((_gs[_cmap[ord(ch)]].width + _TRACK) for ch in text
               if ord(ch) in _cmap) * size / _UPM


# ----------------------------- helpers -------------------------------------
def svg_doc(w, h, body, bg=None):
    r = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}">'
    if bg:
        r += f'<rect width="{w}" height="{h}" rx="{min(w,h)*0.18:.1f}" fill="{bg}"/>'
    return r + body + "</svg>"


def save(name, w, h, body, bg=None):
    p = HERE / f"{name}.svg"
    p.write_text(svg_doc(w, h, body, bg), encoding="utf-8")
    return p


def cols(variant):
    """(primary, accent, background, inner-glyph colour) per variant"""
    if variant == "color":
        return BLUE, TEAL, None, INK
    if variant == "mono":
        return INK, INK, None, WHITE
    return WHITE, WHITE, INK, INK  # reversed


# ============================ concept marks ================================
# All marks draw inside a 64x64 viewBox.

def m_qrst(c1, c2, _ink):
    """Q monogram; tail is an accurate PQRST complex exiting the bowl."""
    s = []
    s.append(f'<circle cx="30" cy="28" r="21" fill="none" stroke="{c1}" '
             f'stroke-width="9"/>')
    # PQRST tail: small P bump, Q dip, R spike, S dip, rounded T - a real
    # complex, not a zig-zag. Drawn along +x then rotated 40 deg.
    tail = ('<path d="M39 45 l3.2 0 q1.6 -2.4 3.2 0 l2.2 0 '      # P
            'l1.0 2.0 l1.3 -2.8 l1.6 -9.4 l1.7 10.6 l1.2 2.6 '   # QRS
            'l2.6 0 q2.2 -3.2 4.4 0 l3.0 0" '                    # ST + T hump
            f'fill="none" stroke="{c2}" stroke-width="4.4" '
            'stroke-linecap="round" stroke-linejoin="round" '
            'transform="rotate(38 39 45)"/>')
    s.append(tail)
    return "".join(s)


def m_einthoven(c1, c2, _ink):
    """Einthoven triangle: 3 electrode nodes, pulse node at the centre."""
    import math
    cx, cy, R = 32, 32, 23
    # inverted triangle: RA(-150deg), LA(-30deg), LL(90deg)
    pts = [(cx + R * math.cos(math.radians(a)),
            cy + R * math.sin(math.radians(a)))
           for a in (-150, -30, 90)]
    (ra, la, ll) = pts
    s = [f'<path d="M{ra[0]:.1f} {ra[1]:.1f} L{la[0]:.1f} {la[1]:.1f} '
         f'L{ll[0]:.1f} {ll[1]:.1f} Z" fill="none" stroke="{c1}" '
         'stroke-width="3.4" stroke-linejoin="round" opacity="0.55"/>']
    for p in (ra, la, ll):
        s.append(f'<circle cx="{p[0]:.1f}" cy="{p[1]:.1f}" r="7.2" '
                 f'fill="{c1}"/>')
    # centre node = heart of the triangle: filled dot + tiny R spike
    s.append(f'<circle cx="32" cy="32" r="9" fill="{c2}"/>')
    s.append(f'<path d="M26 33.5 l2.6 0 l1.6 -5.4 l2.0 7.2 l1.4 -2.6 l2.4 0" '
             f'fill="none" stroke="{_ink}" stroke-width="2.6" '
             'stroke-linecap="round" stroke-linejoin="round"/>')
    return "".join(s)


def m_ohm(c1, c2, _ink):
    """Omega whose arch is one PPG pulse: anacrotic rise, dicrotic notch."""
    # single-stroke ohm: foot -> rise -> peak -> fall w/ notch -> foot
    d = ("M9 54 L19 54 "
         "C21 40 25 18 32 16 "            # anacrotic rise to systolic peak
         "C40 18 44 34 46.5 42 "          # catacrotic fall
         "c0.8 -3.6 3.9 -3.6 4.9 -0.4 "   # dicrotic notch bump
         "C51.8 46 52 50 52 54 L56 54")
    s = [f'<path d="{d}" fill="none" stroke="{c1}" stroke-width="8" '
         'stroke-linecap="round" stroke-linejoin="round"/>']
    # accent tick marks on the baseline feet = measurement flavour
    s.append(f'<path d="M14 58.5 v0" stroke="{c2}" stroke-width="0"/>'
             f'<circle cx="11.5" cy="57.5" r="2.1" fill="{c2}"/>'
             f'<circle cx="53.5" cy="57.5" r="2.1" fill="{c2}"/>')
    return "".join(s)


def m_patch(c1, c2, _ink):
    """Patch silhouette + concentric measurement rings + sensor dot."""
    s = [f'<rect x="9" y="9" width="46" height="46" rx="12" fill="none" '
         f'stroke="{c1}" stroke-width="7"/>']
    for r in (10.5, 17.5):
        s.append(f'<circle cx="32" cy="32" r="{r}" fill="none" '
                 f'stroke="{c2}" stroke-width="2.6" stroke-dasharray="5 4" '
                 'stroke-linecap="round" opacity="0.85"/>')
    s.append(f'<circle cx="32" cy="32" r="4.6" fill="{c2}"/>')
    return "".join(s)


def m_bars(c1, c2, _ink):
    """Quantized complex: five bars whose heights ARE the PQRST amplitudes -
    quantified waveform. R bar takes the accent colour."""
    # (x, height>0 up / <0 down from baseline, accent?)
    bars = [(14.5, 7, 0), (24.5, -5, 0), (34.5, 24, 1), (44.5, -6, 0),
            (54.5, 11, 0)]
    y0 = 36  # baseline
    s = [f'<path d="M10 {y0} h48" stroke="{c1}" stroke-width="2.6" '
         'stroke-linecap="round" opacity="0.55"/>']
    for x, h, acc in bars:
        c = c2 if acc else c1
        y_top = y0 - h if h > 0 else y0
        hh = abs(h)
        s.append(f'<rect x="{x - 3.1}" y="{y_top}" width="6.2" '
                 f'height="{hh}" rx="2.4" fill="{c}"/>')
    return "".join(s)


CONCEPTS = [
    ("c1_qrst",      "QRST monogram",  m_qrst),
    ("c2_einthoven", "Einthoven triad", m_einthoven),
    ("c3_ohm",       "Ohm wave",       m_ohm),
    ("c4_patch",     "Patch ring",     m_patch),
    ("c5_qbars",     "Quantized PQRST", m_bars),
]

WORDMARK = "VitalQ"
WM_SIZE = 30
WM_FONT_SIZE_PX = 30


def build():
    built = []
    for cid, _title, fn in CONCEPTS:
        for v in VARIANTS:
            c1, c2, bg, ink = cols(v)
            mark_body = fn(c1, c2, ink)
            built.append(save(f"{cid}_mark_{v}", 64, 64, mark_body, bg))
            # lockup: mark on the left + wordmark (glyph outlines) right
            wm_paths, wm_w = text_paths(WORDMARK, WM_FONT_SIZE_PX,
                                        58, 44, c1)
            body = (f'<g transform="translate(6 10) scale(0.6875)">'
                    f'{mark_body}</g>' + wm_paths)
            built.append(save(f"{cid}_lockup_{v}", 58 + wm_w + 4, 64,
                              body, bg))
    return built


# ----------------------------- rendering -----------------------------------
def png(svg_path, size=None, out=None):
    out = out or svg_path.with_suffix(".png")
    args = ["qlmanage", "-t", "-o", str(HERE)]
    args += ["-s", str(size)] if size else ["-s", "1024"]
    args.append(str(svg_path))
    r = subprocess.run(args, capture_output=True)
    produced = svg_path.with_suffix(svg_path.suffix + ".png")
    if produced.exists():
        produced.rename(out)
    return out.exists()


def main():
    built = build()
    print(f"{len(built)} svg files")
    fav16 = fav32 = []
    for p in built:
        png(p)                      # full-size preview
    for cid, _t, _f in CONCEPTS:
        for v in ("color", "mono", "reversed"):
            m = HERE / f"{cid}_mark_{v}.svg"
            png(m, 16, HERE / f"{cid}_mark_{v}_16.png")
            png(m, 32, HERE / f"{cid}_mark_{v}_32.png")
    print("renders done")


if __name__ == "__main__":
    main()
