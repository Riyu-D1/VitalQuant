"""comparison_sheet.png for v2: 7 hidden-meaning concepts."""

from make_logos2 import (HERE, CONCEPTS, cols, text_paths, INK, svg_doc)
import subprocess

W, H = 1500, 1400
ROW_H = 168
LABEL_X = 24


def mark_group(fn, variant, x, y, size=64):
    c1, c2, bg, ink = cols(variant)
    inner = fn(c1, c2, ink)
    g = f'<g transform="translate({x} {y}) scale({size/64})">{inner}</g>'
    if bg:
        g = (f'<rect x="{x}" y="{y}" width="{size}" height="{size}" rx="12" '
             f'fill="{bg}"/>' + g)
    return g


def lockup_group(fn, variant, wordmark, x, y, tittle=False):
    c1, c2, bg, ink = cols(variant)
    inner = fn(c1, c2, ink)
    mark = f'<g transform="scale(0.6875)">{inner}</g>'
    tf = c2 if tittle else None
    wm, wm_w = text_paths(wordmark, 30, 52, 44, c1, tittle_fill=tf)
    rect = (f'<rect x="0" y="0" width="{58 + wm_w + 4:.0f}" height="64" '
            f'rx="10" fill="{bg}"/>' if bg else "")
    return f'<g transform="translate({x} {y})">{rect}{mark}{wm}</g>'


def label(text, x, y, size=15, color=INK):
    p, _ = text_paths(text, size, x, y, color)
    return p


def main():
    parts = [label("VitalQ / VitalQuant - hidden-meaning concepts v2",
                   LABEL_X, 56, 25, INK),
             label("Space Grotesk 620 - VQ-BLUE #0B4EA2 / VQ-TEAL #0E8F8F "
                   "- every mark holds a double reading (see RECOMMENDATION.md)",
                   LABEL_X, 88, 13, "#4A5A6A")]

    heads = ["mark  colour", "mark  mono", "mark  reversed",
             "VitalQ lockup", "VitalQuant lockup", "16px", "32px", "64px"]
    col_x = [200, 330, 460, 600, 860, 1260, 1310, 1375]
    for h, x in zip(heads, col_x):
        parts.append(label(h, x, 128, 11, "#4A5A6A"))

    y = 150
    for cid, title, fn in CONCEPTS:
        parts.append(label(title, LABEL_X, y + 36, 14, INK))
        parts.append(label(cid, LABEL_X, y + 56, 10, "#7A8A9A"))
        parts.append(mark_group(fn, "color", col_x[0], y))
        parts.append(mark_group(fn, "mono", col_x[1], y))
        parts.append(mark_group(fn, "reversed", col_x[2], y))
        parts.append(lockup_group(fn, "color", "VitalQ", col_x[3], y,
                                  cid == "c7_tittle"))
        parts.append(lockup_group(fn, "color", "VitalQuant", col_x[4], y,
                                  cid == "c7_tittle"))
        for i, s in enumerate((16, 32, 64)):
            parts.append(mark_group(fn, "color", col_x[5 + i],
                                    y + 48 - s // 2, s))
        y += ROW_H

    svg = HERE / "comparison_sheet.svg"
    svg.write_text(svg_doc(W, H, "".join(parts)), encoding="utf-8")
    subprocess.run(["qlmanage", "-t", "-s", "1500", "-o", str(HERE),
                    str(svg)], capture_output=True)
    produced = svg.with_suffix(".svg.png")
    if produced.exists():
        produced.rename(HERE / "comparison_sheet.png")
    print("sheet done")


if __name__ == "__main__":
    main()
