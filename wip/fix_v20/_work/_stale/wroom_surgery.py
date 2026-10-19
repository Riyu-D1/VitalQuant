#!/usr/bin/env python3
# WROOM fix_v20: U1 swap surgery — stage 1
# - rip U1-net copper inside RZ except kept components (touch boundary or non-U1 pads)
# - clip boundary-crossing segments to RZ edge
# - nudge colliding passives inside RZ
# - replace U1 footprint, assign pad nets
# Run: KIPYTHON wroom_surgery.py
import pcbnew, math, sys

PCB = '/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/vitalq_v2.kicad_pcb'
RZ = (1.2, -10.55, 24.2, 12.2)      # x0,y0,x1,y1 rework zone
NEWX, NEWY = 12.75, -0.5            # U1 new anchor (+0.35 east clears FID4 body overlap)
MM = 1000000

def inside(x, y, m=0.0):
    return RZ[0]-m <= x <= RZ[2]+m and RZ[1]-m <= y <= RZ[3]+m

def clip_to_rz(x, y, px, py):
    """clip segment (x,y)->(px,py) so that (x,y) side inside; return new inside-boundary point"""
    # only called for crossers; find t where segment crosses RZ edge nearest the inside end
    best = None
    for t in [i/200.0 for i in range(201)]:
        xx, yy = x + (px-x)*t, y + (py-y)*t
        if inside(xx, yy, 0.001):
            best = (xx, yy)
        else:
            break
    return best

board = pcbnew.LoadBoard(PCB)

u1 = board.FindFootprintByReference('U1')
assert u1, "U1 not found"
print("U1:", u1.GetFPID().GetLibItemName(), u1.GetPosition().x/MM, u1.GetPosition().y/MM)

# ---- 1. U1 pad nets
u1nets = set()
for pad in u1.Pads():
    n = pad.GetNetname()
    if n: u1nets.add(n)
print("U1 nets:", sorted(u1nets))

# ---- 2. non-U1 pads inside RZ (per net) -> keep-terminals
nonu1_pads_in_rz = []   # (netname, x, y)
moved = {'C7':(2.2,0.72),'C8':(2.3,2.29),'C50':(15.0,10.7),'C64':(23.4,-3.0),
         'R71':(12.0,10.5),'R73':(16.5,10.7),'R67':(23.14,-0.07)}
for fp in board.GetFootprints():
    ref = fp.GetReference()
    if ref in moved:
        nx, ny = moved[ref]
        print("moving", ref, fp.GetPosition().x/MM, fp.GetPosition().y/MM, "->", nx, ny)
        fp.SetPosition(pcbnew.VECTOR2I_MM(nx, ny))
    for pad in fp.Pads():
        p = pad.GetPosition()
        if inside(p.x/MM, p.y/MM) and ref != 'U1':
            n = pad.GetNetname()
            if n: nonu1_pads_in_rz.append((n, p.x/MM, p.y/MM, ref))
print("non-U1 pads in RZ:", len(nonu1_pads_in_rz))

# ---- 3. copper graph on U1 nets inside RZ
segs = []   # (track, x1,y1,x2,y2,layer,net)
vias = []
for t in board.GetTracks():
    net = t.GetNetname()
    if net not in u1nets: continue
    if t.GetClassName() if hasattr(t,'GetClassName') else '' : pass
    if isinstance(t, pcbnew.PCB_VIA):
        p = t.GetPosition()
        vias.append((t, p.x/MM, p.y/MM, net))
    else:
        s, e = t.GetStart(), t.GetEnd()
        segs.append((t, s.x/MM, s.y/MM, e.x/MM, e.y/MM, t.GetLayer(), net))

def ptkey(x,y): return (round(x*50), round(y*50))   # 20um buckets

# build graph: nodes = ptkeys; pads connect to overlapping segment endpoints
from collections import defaultdict
adj = defaultdict(set)
seg_nodes = {}   # id -> (p1key,p2key)
obj_inside = {}

for idx,(t,x1,y1,x2,y2,layer,net) in enumerate(segs):
    i1, i2 = inside(x1,y1), inside(x2,y2)
    obj_inside[('s',idx)] = (i1,i2)
    k1,k2 = ptkey(x1,y1),ptkey(x2,y2)
    seg_nodes[('s',idx)] = (net,(k1,x1,y1),(k2,x2,y2))
for idx,(t,x,y,net) in enumerate(vias):
    obj_inside[('v',idx)] = (inside(x,y),)
    k = ptkey(x,y)
    seg_nodes[('v',idx)] = (net,(k,x,y),)

# connectivity among objects sharing endpoint keys or crossing
node_objs = defaultdict(list)
for oid,(net,*pts) in seg_nodes.items():
    for k,_,_ in pts:
        node_objs[(net,k)].append(oid)
# union-find over objects of same net sharing a node
parent = {}
def find(a):
    while parent.get(a,a)!=a: a=parent.get(a,a)
    return a
def union(a,b):
    ra,rb=find(a),find(b)
    if ra!=rb: parent[ra]=rb
for key,lst in node_objs.items():
    for o in lst[1:]: union(lst[0],o)

# a component is "keep" if any obj crosses boundary (an end outside) or touches non-U1 pad
keep_roots = set()
for oid,(net,*pts) in seg_nodes.items():
    ins = obj_inside[oid]
    if oid[0]=='s':
        i1,i2 = ins
        if i1 != i2:  # crosses boundary
            keep_roots.add(find(oid))
    else:
        pass  # vias alone don't cross
# non-U1 pads touching a node -> keep that component
pad_nodes = set()
for n,x,y,ref in nonu1_pads_in_rz:
    pad_nodes.add((n,ptkey(x,y)))
for (n,k) in pad_nodes:
    for oid in node_objs.get((n,k),[]):
        keep_roots.add(find(oid))

deleted = 0; clipped = 0; kept=0
for oid,(net,*pts) in seg_nodes.items():
    ins = obj_inside[oid]
    idx = oid[1]
    if oid[0]=='s':
        t,x1,y1,x2,y2,layer,net2 = segs[idx]
        i1,i2 = ins
        if not i1 and not i2:
            continue                    # fully outside: untouched
        if find(oid) not in keep_roots:
            board.Remove(t); deleted+=1; continue
        if i1 != i2:                    # crosser: clip inside end to boundary
            if i1:
                np_ = clip_to_rz(x1,y1,x2,y2)
                t.SetStart(pcbnew.VECTOR2I_MM(*np_))
            else:
                np_ = clip_to_rz(x2,y2,x1,y1)
                t.SetEnd(pcbnew.VECTOR2I_MM(*np_))
            clipped+=1; kept+=1
        else:
            kept+=1                     # fully inside but part of kept component
    else:
        t,x,y,net2 = vias[idx]
        if not ins[0]: continue
        if find(oid) not in keep_roots:
            board.Remove(t); deleted+=1
        else: kept+=1
print(f"copper: kept {kept}, clipped {clipped}, deleted {deleted}")

# ---- 4. replace U1
board.Remove(u1)
fp = pcbnew.FootprintLoad('/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v20/vitalq.pretty', 'ESP32_S3_WROOM_1')
assert fp, "footprint load failed"
fp.SetPosition(pcbnew.VECTOR2I_MM(NEWX, NEWY))
fp.SetReference('U1')
fp.SetValue('ESP32-S3-WROOM-1-N16R8')
fp.SetLocked(True)
board.Add(fp)

PADNET = {'1':'GND','2':'+3V3_ESP','3':'ESP_EN','4':'LSM6_INT1','5':'TMP117_ALERT',
'6':'GAUGE_ALRT','7':'CHG_PG','8':'CS_AD5940','9':'CS_FLASH','10':'STATUS_LED',
'11':'CS_AFE4900','12':'I2C_SDA','13':'USB_DM_S3','14':'USB_DP_S3','15':'EXP_INT',
'16':None,'17':'I2C_SCL','18':'CS_ADS1292','19':'SPI_MOSI','20':'SPI_SCK','21':'SPI_MISO',
'22':'AD5940_GPIO0','23':'AFE4900_ADC_RDY','24':None,'25':None,'26':None,'27':'ESP_IO0',
'28':None,'29':None,'30':None,'31':'CS_MAX86178','32':'MAX86178_INT','33':'MIC_CLK',
'34':'MIC_DAT','35':'SPARE_IN','36':'ESP_RX','37':'ESP_TX','38':'ADS1292_DRDY',
'39':'FSR_ADC','40':'GND','41':'GND'}
netmap = {}
pads_by_num = {}
for pad in fp.Pads():
    pads_by_num.setdefault(pad.GetNumber(), []).append(pad)
for pnum, netname in PADNET.items():
    net = None
    if netname:
        net = netmap.get(netname)
        if net is None:
            net = board.FindNet(netname)
            assert net, netname
            netmap[netname] = net
    for pad in pads_by_num.get(pnum, []):
        pad.SetNet(net)   # None -> unconnected

board.Save(PCB+'.stg1')
print("stage1 saved ->", PCB+'.stg1')
