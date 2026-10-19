#!/usr/bin/env python3
"""Clearance checker for add-only routing on vitalq board.
Builds shapely obstacle sets per copper layer from segments, vias, pads,
zone fills and route keepouts. Queries:
  via_clear(x,y,r,net)  - can a via of radius r on net be placed at (x,y)?
  seg_clear(layer,x1,y1,x2,y2,w,net) - is a track segment legal?
"""
import sys, math
sys.path.insert(0,'_work')
from pcb import PCB, blocks
from shapely.geometry import Point, LineString, Polygon, box
from shapely.ops import unary_union
import re

CLR = 0.1016   # default netclass clearance

class Chk:
    def __init__(self, path):
        self.p = PCB(path)
        # per-layer geometry: list of (geom, net)
        self.layers = {}
        t = self.p.txt
        for s in self.p.segs:
            g = LineString([s['start'], s['end']]).buffer(s['width']/2, cap_style='flat')
            self.layers.setdefault(s['layer'], []).append((g, s['net']))
        ALLCU = ['F.Cu','In1.Cu','In2.Cu','In3.Cu','In4.Cu','In5.Cu','In6.Cu','B.Cu']
        for v in self.p.vias:
            x, y = v['at']
            g = Point(x, y).buffer(v['size']/2)
            lays = v['layers']
            # through-via serialized as F.Cu+B.Cu only => blocks every copper layer
            if set(lays) == {'F.Cu','B.Cu'}:
                lays = ALLCU
            for L in lays:
                self.layers.setdefault(L, []).append((g, v['net']))
        # board edges / internal slots: copper must keep 'edge clearance' (0.3mm)
        self.edges = []
        for nm, txt in self.p.gr:
            if 'Edge.Cuts' in txt and nm == 'gr_line':
                a = re.search(r'start ([-\d.e]+) ([-\d.e]+)', txt)
                b = re.search(r'end ([-\d.e]+) ([-\d.e]+)', txt)
                if a and b:
                    self.edges.append(LineString([(float(a.group(1)), float(a.group(2))),
                                                  (float(b.group(1)), float(b.group(2)))]))
        self.edges += []

        for fp in self.p.fps:
            if fp.at is None: continue
            for pad in fp.pads:
                gp = self.p.pad_global(fp, pad)
                if gp is None: continue
                hw, hh = pad.size[0]/2, pad.size[1]/2
                if pad.shape in ('thru_hole', 'np_thru_hole'):
                    layers = pad.layers if pad.layers else ['F.Cu','In1.Cu','In2.Cu','In3.Cu','In4.Cu','In5.Cu','In6.Cu','B.Cu']
                    r = max(hw, hh)
                    g = Point(*gp).buffer(r)
                    for L in layers:
                        self.layers.setdefault(L, []).append((g, pad.net))
                else:
                    # smd pad: rotated rect (KiCad CW convention)
                    r = math.radians(fp.rot + pad.rot)
                    cx, cy = gp
                    pts = []
                    for dx, dy in ((-hw,-hh),(hw,-hh),(hw,hh),(-hw,hh)):
                        pts.append((cx + dx*math.cos(r) + dy*math.sin(r),
                                    cy - dx*math.sin(r) + dy*math.cos(r)))
                    g = Polygon(pts)
                    L = 'F.Cu' if fp.layer == 'F.Cu' else 'B.Cu'
                    self.layers.setdefault(L, []).append((g, pad.net))
        # zone fills as copper
        for z in self.p.zones:
            for lay, pts in z['fills']:
                if len(pts) >= 3:
                    g = Polygon(pts)
                    self.layers.setdefault(lay, []).append((g, z['net']))
        # keepout zones (route-keepout): block listed layers
        self.keepouts = {}
        for z in self.p.zones:
            if 'keepout' in z['raw'] and ('tracks yes' in z['raw'] or 'vias yes' in z['raw']):
                for pts in z['polys']:
                    if len(pts) >= 3:
                        g = Polygon(pts)
                        for L in z['layers']:
                            self.keepouts.setdefault(L, []).append(g)

    def seg_clear(self, layer, x1, y1, x2, y2, w, net, verbose=False):
        """check a new track of width w on layer for net"""
        geom = LineString([(x1,y1),(x2,y2)]).buffer(w/2, cap_style='flat')
        need = CLR
        bad = []
        for g, n in self.layers.get(layer, []):
            if n == net: continue
            d = geom.distance(g)
            if d < need - 1e-9:
                bad.append((round(d,4), n, g))
        for ko in self.keepouts.get(layer, []):
            if geom.intersects(ko): bad.append(('keepout', None, ko))
        for e in self.edges:
            if geom.distance(e) < 0.3 - 1e-9:
                bad.append(('edge', round(geom.distance(e),4), e))
        if verbose:
            for d, n, g in bad: print('    seg hit', n, 'gap', d)
        return bad

    def via_clear(self, x, y, r, net, verbose=False):
        """through-via: check all copper layers + keepouts"""
        geom = Point(x, y).buffer(r)
        bad = []
        for L, items in self.layers.items():
            for g, n in items:
                if n == net: continue
                d = geom.distance(g)
                if d < CLR - 1e-9:
                    bad.append((L, round(d,4), n))
        for L, kos in self.keepouts.items():
            for ko in kos:
                if geom.intersects(ko): bad.append((L, 'keepout', None))
        for e in self.edges:
            if geom.distance(e) < 0.3 - 1e-9:
                bad.append(('edge', round(geom.distance(e),4), None))
        if verbose:
            for b in bad: print('    via hit', b)
        return bad

if __name__ == '__main__':
    c = Chk('vitalq_v2.kicad_pcb')
    # candidate vias
    tests = [
        ('VBAT', 45.7, 22.53),
        ('VBAT_SYS', 46.9, 25.5),
        ('VBUS_in5hop', 36.6, 16.8),
    ]
    for name, x, y in tests:
        net = name.split('_')[0]
        bad = c.via_clear(x, y, 0.2, net)
        print(f"via {name} ({x},{y}) net={net}: {'OK' if not bad else 'FAIL'}")
        if bad:
            seen = set()
            for b in bad:
                key = (b[0], b[2])
                if key not in seen:
                    seen.add(key); print('   ', b)
