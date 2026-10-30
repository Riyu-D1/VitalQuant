#!/usr/bin/env python3
"""Parse fix_v24 kicad_pcb: board outline, antenna keepout, U1 geometry,
all copper inside keepout region, footprints inside keepout, nearest-copper distance.
Outputs JSON to sim_v24/data/rf_analysis.json"""
import sys, json, math, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sexpr import parse, find_all, child, children, num

PCB = '/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v24/vitalq_v2.kicad_pcb'
OUT = '/Users/deepakdiwan/Downloads/VitalQ_Hand/sim_v24/data/rf_analysis.json'

tree = parse(open(PCB).read())

def get_at(node):
    at = child(node, 'at')
    if at:
        x = num(at[1]); y = num(at[2]); r = num(at[3], 0) if len(at) > 3 else 0
        return x, y, r
    return None, None, 0

# ---------- board outline ----------
edge_items = []
for tag in ('gr_line', 'gr_arc', 'gr_poly', 'gr_rect', 'gr_curve', 'gr_circle'):
    for n in find_all(tree, tag):
        lay = child(n, 'layer')
        if lay and 'Edge.Cuts' in lay[1:]:
            edge_items.append(n)

def node_pts(n):
    # polygon pts blocks
    for pblock in children(n, 'pts'):
        pts = []
        for xy in children(pblock, 'xy'):
            pts.append((num(xy[1]), num(xy[2])))
        return pts
    # preserve document order of start/mid/end/xy children
    pts = []
    for c in n:
        if isinstance(c, list) and c and c[0] in ('start', 'end', 'mid', 'xy') and len(c) >= 3:
            pts.append((num(c[1]), num(c[2])))
    return pts

print(f"edge items: {len(edge_items)}")
minx, miny, maxx, maxy = 1e9, 1e9, -1e9, -1e9
for n in edge_items:
    for p in node_pts(n):
        minx = min(minx, p[0]); maxx = max(maxx, p[0])
        miny = min(miny, p[1]); maxy = max(maxy, p[1])
print(f"board bbox: x {minx}..{maxx}, y {miny}..{maxy}")

# collect outline segments as (p1,p2) for distance calcs
def segs_of(n):
    tag = n[0]
    pts = node_pts(n)
    if tag == 'gr_line':
        return [(pts[0], pts[1])]
    if tag == 'gr_arc':
        # approximate arc with segments
        st, mid, en = pts[0], pts[1], pts[2]
        cx = (st[0]+en[0])/2  # fallback
        # KiCad arc: start, mid, end. Compute circle through 3 points.
        (x1,y1),(x2,y2),(x3,y3) = st, mid, en
        d = 2*(x1*(y2-y3)+x2*(y3-y1)+x3*(y1-y2))
        if abs(d) < 1e-12:
            return [(st,en)]
        ux = ((x1*x1+y1*y1)*(y2-y3)+(x2*x2+y2*y2)*(y3-y1)+(x3*x3+y3*y3)*(y1-y2))/d
        uy = ((x1*x1+y1*y1)*(x3-x2)+(x2*x2+y2*y2)*(x1-x3)+(x3*x3+y3*y3)*(x2-x1))/d
        import math as m
        r = m.hypot(x1-ux,y1-uy)
        a1 = m.atan2(y1-uy,x1-ux); a3 = m.atan2(y3-uy,x3-ux)
        # direction: go from a1 to a3 passing through a2
        a2 = m.atan2(y2-uy,x2-ux)
        def ang_norm(a): return a % (2*m.pi)
        a1n,a2n,a3n = ang_norm(a1),ang_norm(a2),ang_norm(a3)
        # determine if a2 is between a1->a3 ccw
        if a3n < a1n: a3n += 2*m.pi
        if a1n < a2n < a3n:
            ccw=True
        else:
            ccw=False; a3n = ang_norm(a3)
            if a1n < a3n: a1n -= 2*m.pi
        out=[]
        N=16
        for i in range(N):
            t0=i/N; t1=(i+1)/N
            aa0 = a1n+(a3n-a1n)*t0 if ccw else a1n-(a1n-a3n)*t0
            aa1 = a1n+(a3n-a1n)*t1 if ccw else a1n-(a1n-a3n)*t1
            out.append(((ux+r*m.cos(aa0),uy+r*m.sin(aa0)),(ux+r*m.cos(aa1),uy+r*m.sin(aa1))))
        return out
    if tag == 'gr_rect':
        (x1,y1),(x2,y2) = pts[0], pts[1]
        c = [(x1,y1),(x2,y1),(x2,y2),(x1,y2)]
        return [(c[i],c[(i+1)%4]) for i in range(4)]
    if tag in ('gr_poly','gr_curve','gr_circle'):
        ss=[]
        for i in range(len(pts)-1):
            ss.append((pts[i],pts[i+1]))
        if tag == 'gr_poly' and len(pts)>2:
            ss.append((pts[-1],pts[0]))
        return ss
    return []

edge_segs = []
for n in edge_items:
    edge_segs += segs_of(n)
print(f"edge segments: {len(edge_segs)}")

# ---------- zones (keepouts and copper pours) ----------
zones = []
for z in find_all(tree, 'zone'):
    name_n = child(z, 'name')
    name = name_n[1] if name_n else ''
    layers_n = child(z, 'layers')
    layers = layers_n[1:] if layers_n else []
    ko = child(z, 'keepout')
    net = child(z, 'net')
    netname = child(z, 'net_name')
    poly = child(z, 'polygon')
    pts = node_pts(poly) if poly else []
    zones.append(dict(name=name, layers=layers,
                      keepout={k[0]:k[1] for k in ko[1:]} if ko else None,
                      net=netname[1] if netname else (net[1] if net else None),
                      pts=pts))
keepouts = [z for z in zones if z['keepout']]
pours = [z for z in zones if not z['keepout']]
print(f"zones: {len(zones)}, keepouts: {len(keepouts)}, pours: {len(pours)}")
for k in keepouts:
    xs=[p[0] for p in k['pts']]; ys=[p[1] for p in k['pts']]
    print(f"  keepout '{k['name']}' layers={len(k['layers'])} bbox x {min(xs):.2f}..{max(xs):.2f} y {min(ys):.2f}..{max(ys):.2f} rules={k['keepout']}")

# ---------- antenna keepout zone = the one named 'antenna keepout' (zone) ----------
ant = None
for k in keepouts:
    if 'antenna' in k['name'].lower() and len(k['pts']) >= 4 and len(k['layers'])>4:
        # largest antenna-named keepout
        xs=[p[0] for p in k['pts']]; ys=[p[1] for p in k['pts']]
        area = (max(xs)-min(xs))*(max(ys)-min(ys))
        if ant is None or area > ant['area']:
            ant = dict(name=k['name'], pts=k['pts'], layers=k['layers'], rules=k['keepout'],
                       xmin=min(xs),xmax=max(xs),ymin=min(ys),ymax=max(ys),area=area)
print("antenna keepout:", ant)

# ---------- U1 footprint geometry ----------
u1 = None
for fp in find_all(tree, 'footprint'):
    ref = None
    for p in children(fp, 'property'):
        if p[1] == 'Reference':
            ref = p[2]
    if ref == 'U1':
        u1 = fp
        break
x0,y0,rot = get_at(u1)
print(f"U1 at ({x0},{y0}) rot {rot}")
# U1 pads + fp geometry extents (in board coords)
def rot_pt(x,y,deg):
    a = math.radians(deg)
    return (x*math.cos(a)-y*math.sin(a), x*math.sin(a)+y*math.cos(a))
u1_pts=[]
for g in list(find_all(u1,'fp_line'))+list(find_all(u1,'fp_poly'))+list(find_all(u1,'fp_rect')):
    for p in node_pts(g):
        rx,ry = rot_pt(p[0],p[1],rot)
        u1_pts.append((x0+rx,y0+ry))
u1x=[p[0] for p in u1_pts]; u1y=[p[1] for p in u1_pts]
print(f"U1 fp bbox x {min(u1x):.2f}..{max(u1x):.2f} y {min(u1y):.2f}..{max(u1y):.2f}")

# U1 pads
u1_pads=[]
for pad in find_all(u1,'pad'):
    pname = pad[1]
    at = child(pad,'at')
    px,py,pr = num(at[1]),num(at[2]),(num(at[3],0) if len(at)>3 else 0)
    rx,ry = rot_pt(px,py,rot)
    size = child(pad,'size')
    sx,sy = num(size[1]),num(size[2])
    layers = child(pad,'layers')
    u1_pads.append(dict(name=pname,x=x0+rx,y=y0+ry,sx=sx,sy=sy,layers=layers[1:] if layers else []))
print(f"U1 pads: {len(u1_pads)}")

# ---------- point-in-rect + distance helpers ----------
def in_rect(p,r):
    return r['xmin']<=p[0]<=r['xmax'] and r['ymin']<=p[1]<=r['ymax']
def seg_dist(p,a,b):
    ax,ay=a; bx,by=b; px,py=p
    dx,dy=bx-ax,by-ay
    if dx==0 and dy==0: return math.hypot(px-ax,py-ay)
    t=max(0,min(1,((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy)))
    return math.hypot(px-(ax+t*dx),py-(ay+t*dy))
def pt_seg_min_dist(a1,a2,b1,b2):
    # min distance between segments a1a2 and b1b2
    def seg_seg_dist(p1,p2,p3,p4):
        # if segments intersect -> 0
        def ccw(A,B,C): return (C[1]-A[1])*(B[0]-A[0])-(B[1]-A[1])*(C[0]-A[0])
        d1=ccw(p3,p4,p1); d2=ccw(p3,p4,p2); d3=ccw(p1,p2,p3); d4=ccw(p1,p2,p4)
        if ((d1>0)!=(d2>0)) and ((d3>0)!=(d4>0)): return 0.0
        return min(seg_dist(p1,p3,p4),seg_dist(p2,p3,p4),seg_dist(p3,p1,p2),seg_dist(p4,p1,p2))
    return seg_seg_dist(a1,a2,b1,b2)

ant_rect = ant
# pad centers / items inside keepout
violations=[]
# 1) tracks (segments) inside keepout
n_seg=0
for seg in find_all(tree,'segment'):
    pts=node_pts(seg)
    if len(pts)<2: continue
    net=child(seg,'net'); lay=child(seg,'layer')
    netid=net[1] if net else '?'
    n_seg+=1
    # check midpoint + endpoints in rect
    a,b=pts[0],pts[1]
    mid=((a[0]+b[0])/2,(a[1]+b[1])/2)
    if in_rect(a,ant_rect) or in_rect(b,ant_rect) or in_rect(mid,ant_rect):
        violations.append(dict(type='track',net=netid,layer=lay[1] if lay else '?',a=a,b=b))
print(f"segments: {n_seg}, track violations: {sum(1 for v in violations if v['type']=='track')}")

# 2) vias inside keepout
for via in find_all(tree,'via'):
    at=child(via,'at'); p=(num(at[1]),num(at[2]))
    net=child(via,'net')
    if in_rect(p,ant_rect):
        violations.append(dict(type='via',net=net[1] if net else '?',at=p))

# 3) pads of all footprints inside keepout (excluding U1 pads that are the antenna side? count all)
pad_viol=0
for fp in find_all(tree,'footprint'):
    ref=None
    for p in children(fp,'property'):
        if p[1]=='Reference': ref=p[2]
    fx,fy,frot=get_at(fp)
    for pad in find_all(fp,'pad'):
        at=child(pad,'at'); px,py=num(at[1]),num(at[2])
        rx,ry=rot_pt(px,py,frot or 0)
        P=(fx+rx,fy+ry)
        net=child(pad,'net')
        if in_rect(P,ant_rect):
            violations.append(dict(type='pad',ref=ref,pad=pad[1],net=net[1] if net else '?',at=P))
            pad_viol+=1
print(f"pad violations: {pad_viol}")

# 4) footprints whose body (bbox of fp graphics + pads) intersects keepout
fp_viol=0
for fp in find_all(tree,'footprint'):
    ref=None
    for p in children(fp,'property'):
        if p[1]=='Reference': ref=p[2]
    fx,fy,frot=get_at(fp)
    pts=[]
    for g in list(find_all(fp,'fp_line'))+list(find_all(fp,'fp_poly'))+list(find_all(fp,'fp_rect'))+list(find_all(fp,'fp_circle')):
        for pp in node_pts(g):
            rx,ry=rot_pt(pp[0],pp[1],frot or 0); pts.append((fx+rx,fy+ry))
    for pad in find_all(fp,'pad'):
        at=child(pad,'at'); px,py=num(at[1]),num(at[2])
        rx,ry=rot_pt(px,py,frot or 0); pts.append((fx+rx,fy+ry))
    if not pts: continue
    xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
    # bbox intersect
    if min(xs)<=ant_rect['xmax'] and max(xs)>=ant_rect['xmin'] and min(ys)<=ant_rect['ymax'] and max(ys)>=ant_rect['ymin']:
        fp_viol+=1
        violations.append(dict(type='footprint',ref=ref,bbox=[min(xs),min(ys),max(xs),max(ys)]))
print(f"footprint-in-keepout: {fp_viol}")

# 5) copper pours intersecting keepout (zones with fill) - check fill polygons too
pour_viol=0
for z in pours:
    for pp in z['pts']:
        if in_rect(pp,ant_rect):
            pour_viol+=1
            violations.append(dict(type='pour_vertex',net=z['net'],at=pp))
            break
    else:
        # edge crossing?
        zp=z['pts']
        if len(zp)>=2:
            for i in range(len(zp)):
                a=zp[i]; b=zp[(i+1)%len(zp)]
                mid=((a[0]+b[0])/2,(a[1]+b[1])/2)
                if in_rect(mid,ant_rect):
                    pour_viol+=1
                    violations.append(dict(type='pour_edge',net=z['net'],a=a,b=b))
                    break
print(f"pour violations: {pour_viol}")

# ---------- antenna pad/end position & nearest copper ----------
# module body edge (silk) is at y_rel -15.75 -> abs -22.25; the fp bbox -22.55
# includes courtyard margin. Use the silk body edge for the antenna end.
BODY_Y_REL = -15.75
ant_y = y0 + BODY_Y_REL          # module body north edge (antenna end)
board_north = miny
overhang = ant_y < board_north
dist_ant_to_edge = board_north - ant_y
print(f"U1 antenna end (body edge) y={ant_y:.2f}, board north y={board_north:.2f}, overhang={overhang}, overhang_mm={dist_ant_to_edge:.2f} mm")

# nearest copper to antenna end: scan all segments/vias/pads/pour polygons
# antenna end segment: U1 north edge from x min..max at y=ant_y
ant_edge = ((x0-9.0,ant_y),(x0+9.0,ant_y))   # module body 18 mm wide
cand=[]
for seg in find_all(tree,'segment'):
    pts=node_pts(seg)
    if len(pts)>=2:
        cand.append(('track',pts[0],pts[1]))
for via in find_all(tree,'via'):
    at=child(via,'at'); p=(num(at[1]),num(at[2]))
    cand.append(('via',p,p))
for fp in find_all(tree,'footprint'):
    ref=None
    for p in children(fp,'property'):
        if p[1]=='Reference': ref=p[2]
    if ref=='U1': continue
    fx,fy,frot=get_at(fp)
    for pad in find_all(fp,'pad'):
        at=child(pad,'at'); px,py=num(at[1]),num(at[2])
        rx,ry=rot_pt(px,py,frot or 0); P=(fx+rx,fy+ry)
        cand.append((f'pad:{ref}.{pad[1]}',P,P))
for z in pours:
    zp=z['pts']
    for i in range(len(zp)):
        cand.append((f'pour:{z["net"]}',zp[i],zp[(i+1)%len(zp)]))

best=(1e9,None)
# sample antenna edge every 0.5 mm
ax0,ax1=x0-9.0,x0+9.0
import numpy as np
for xx in np.arange(ax0,ax1+0.01,0.5):
    P=(xx,ant_y)
    for name,c1,c2 in cand:
        d=seg_dist(P,c1,c2)
        if d<best[0]: best=(d,(name,P,c1,c2))
print(f"nearest copper to antenna end: {best[0]:.2f} mm -> {best[1][0]} at {best[1][3]}")

result=dict(
    board_bbox=[minx,miny,maxx,maxy],
    edge_seg_count=len(edge_segs),
    u1=dict(at=[x0,y0],rot=rot,bbox=[min(u1x),min(u1y),max(u1x),max(u1y)],pads=len(u1_pads)),
    antenna_keepout=ant,
    u1_antenna_end_y=ant_y,board_north_y=board_north,overhang=overhang,antenna_inside_mm=dist_ant_to_edge,
    nearest_copper_mm=best[0],nearest_copper_what=str(best[1]),
    violations=violations,
    keepouts=[dict(name=k['name'],bbox=[min(p[0] for p in k['pts']),min(p[1] for p in k['pts']),max(p[0] for p in k['pts']),max(p[1] for p in k['pts'])],rules=k['keepout']) for k in keepouts],
)
os.makedirs(os.path.dirname(OUT),exist_ok=True)
json.dump(result,open(OUT,'w'),indent=1)
print(f"\nwrote {OUT}; total violations={len(violations)}")
