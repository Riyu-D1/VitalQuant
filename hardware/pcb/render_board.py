#!/usr/bin/env python3
"""Render a .kicad_pcb to SVG (tracks/vias/pads/zones/edges)."""
import sys
sys.path.insert(0, '.')
import pcbnew

MM = 1e6
COL = {pcbnew.F_Cu: '#e33', pcbnew.B_Cu: '#36c',
       pcbnew.In1_Cu: '#f90', pcbnew.In2_Cu: '#6c3',
       pcbnew.In3_Cu: '#a5d', pcbnew.In4_Cu: '#3aa'}
LORD = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu,
        pcbnew.In3_Cu, pcbnew.In4_Cu, pcbnew.B_Cu]


def render(board_f, svg_f, opacity=0.55):
    b = pcbnew.LoadBoard(board_f)
    bb = b.GetBoardEdgesBoundingBox()
    x0, y0 = bb.GetLeft() / MM, bb.GetTop() / MM
    w, h = bb.GetWidth() / MM, bb.GetHeight() / MM
    out = []
    out.append(f'<svg xmlns="http://www.w3.org/2000/svg" '
               f'viewBox="{x0-1:.1f} {y0-1:.1f} {w+2:.1f} {h+2:.1f}" '
               f'style="background:#111">')
    # edges + slots
    for d in b.GetDrawings():
        if d.GetLayerName() != 'Edge.Cuts':
            continue
        if d.GetClass() == 'PCB_SHAPE' or hasattr(d, 'GetStart'):
            try:
                s, e = d.GetStart(), d.GetEnd()
                out.append(
                    f'<line x1="{s.x/MM:.2f}" y1="{s.y/MM:.2f}" '
                    f'x2="{e.x/MM:.2f}" y2="{e.y/MM:.2f}" '
                    f'stroke="#fff" stroke-width="0.15"/>')
            except Exception:
                pass
    # pads
    for fp in b.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            out.append(
                f'<rect x="{bb.GetLeft()/MM:.2f}" '
                f'y="{bb.GetTop()/MM:.2f}" '
                f'width="{bb.GetWidth()/MM:.2f}" '
                f'height="{bb.GetHeight()/MM:.2f}" '
                f'fill="#888" fill-opacity="0.35"/>')
    # tracks by layer
    for ly in LORD:
        col = COL[ly]
        for t in b.GetTracks():
            if t.Type() == pcbnew.PCB_VIA_T:
                continue
            if t.GetLayer() != ly:
                continue
            s, e = t.GetStart(), t.GetEnd()
            out.append(
                f'<line x1="{s.x/MM:.2f}" y1="{s.y/MM:.2f}" '
                f'x2="{e.x/MM:.2f}" y2="{e.y/MM:.2f}" '
                f'stroke="{col}" stroke-width="{t.GetWidth()/MM:.2f}" '
                f'stroke-linecap="round" stroke-opacity="{opacity}"/>')
    for t in b.GetTracks():
        if t.Type() != pcbnew.PCB_VIA_T:
            continue
        p = t.GetPosition()
        r = t.GetWidth(pcbnew.F_Cu) / 2 / MM
        out.append(f'<circle cx="{p.x/MM:.2f}" cy="{p.y/MM:.2f}" '
                   f'r="{r:.2f}" fill="#eee" fill-opacity="0.8"/>')
    out.append('</svg>')
    open(svg_f, 'w').write('\n'.join(out))
    print('wrote', svg_f)


if __name__ == '__main__':
    render(sys.argv[1], sys.argv[2])
