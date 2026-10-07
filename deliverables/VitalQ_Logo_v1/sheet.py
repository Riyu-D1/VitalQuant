"""comparison_sheet.png: all concepts x variants on one canvas."""

from make_logos import (HERE, CONCEPTS, cols, text_paths, BLUE, TEAL, INK,
                        WHITE, svg_doc)
import subprocess

W, H = 1560, 1180
ROW_H = 200
LABEL_X = 30


def mark_group(cid_fn, variant, x, y, size=64):
    c1, c2, bg, ink = cols(variant)
    inner = cid_fn(c1, c2, ink)
    g = f'<g transform="translate({x} {y}) scale({size/64})">{inner}</g>'
    if bg:
        g = (f'<rect x="{x}" y="{y}" width="{size}" height="{size}" rx="12" '
             f'fill="{bg}"/>' + g)
    return g


def lockup_group(cid_fn, variant, x, y, h=64):
    c1, c2, bg, ink = cols(variant)
    inner = cid_fn(c1, c2, ink)
    mark = f'<g transform="scale(0.6875)">{inner}</g>'
    wm, wm_w = text_paths("VitalQ", 30, 52, 44, c1)
    rect = (f'<rect x="0" y="0" width="{58 + wm_w + 4:.0f}" height="64" '
            f'rx="12" fill="{bg}"/>' if bg else "")
    return f'<g transform="translate({x} {y})">{rect}{mark}{wm}</g>'


def label(text, x, y, size=15, color=INK):
    p, _ = text_paths(text, size, x, y, color)
    return p


def main():
    parts = []
    # title
    parts.append(label("VitalQ - logo concepts v1", LABEL_X, 60, 26, INK))
    parts.append(label("Space Grotesk 620 - VQ-BLUE #0B4EA2 / VQ-TEAL #0E8F8F "
                       "- WCAG: blue ~8:1, teal ~3.9:1 on white",
                       LABEL_X, 92, 14, "#4A5A6A"))

    heads = ["mark  colour", "mark  mono", "mark  reversed",
             "lockup  colour", "lockup  mono", "lockup  reversed",
             "16 px", "32 px"]
    col_x = [230, 400, 570, 740, 1020, 1300, 1482, 1518]
    for h, x in zip(heads, col_x):
        parts.append(label(h, x, 130, 12, "#4A5A6A"))

    y = 150
    for cid, title, fn in CONCEPTS:
        parts.append(label(title, LABEL_X, y + 40, 15, INK))
        parts.append(label(cid, LABEL_X, y + 62, 11, "#7A8A9A"))
        parts.append(mark_group(fn, "color", col_x[0], y))
        parts.append(mark_group(fn, "mono", col_x[1], y))
        parts.append(mark_group(fn, "reversed", col_x[2], y))
        parts.append(lockup_group(fn, "color", col_x[3], y))
        parts.append(lockup_group(fn, "mono", col_x[4], y))
        parts.append(lockup_group(fn, "reversed", col_x[5], y - 10, 84))
        parts.append(mark_group(fn, "color", col_x[6], y + 24, 16))
        parts.append(mark_group(fn, "color", col_x[7], y + 16, 32))
        y += ROW_H

    svg = HERE / "comparison_sheet.svg"
    svg.write_text(svg_doc(W, H, "".join(parts), bg=None), encoding="utf-8")
    subprocess.run(["qlmanage", "-t", "-s", "1560", "-o", str(HERE),
                    str(svg)], capture_output=True)
    produced = svg.with_suffix(".svg.png")
    if produced.exists():
        produced.rename(HERE / "comparison_sheet.png")
    print("sheet done")


if __name__ == "__main__":
    main()
