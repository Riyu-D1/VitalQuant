#!/usr/bin/env python3
# move_j1_txt.py — text-level edit of vitalq_v2.kicad_pcb
# Usage: move_j1_txt.py in.kicad_pcb out.kicad_pcb log.json
import sys, json, uuid, re

INP, OUTP, LOGP = sys.argv[1], sys.argv[2], sys.argv[3]

MOVES = {
    'J1':   (29.5,  -7.8),
    'R2':   (27.3,  -1.4),
    'TP11': (36.3,  -8.474),
    'TP20': (34.3,  -0.4),
    'R26':  (32.0,  -1.9),
}

# ---------- segments to rip: (net, x1,y1, x2,y2, layer, tol)
RIP = []
def R(net,x1,y1,x2,y2,l): RIP.append((net,x1,y1,x2,y2,l))
# --- GND old J1 pad/tab chains
R('GND',29.250,-3.755,29.400,-3.936,'F.Cu')
R('GND',29.400,-3.936,29.551,-4.118,'F.Cu')
R('GND',29.551,-4.118,29.817,-3.968,'F.Cu')
R('GND',29.817,-3.968,30.068,-4.319,'F.Cu')
R('GND',30.068,-4.319,30.320,-4.670,'F.Cu')
R('GND',22.750,-3.392,22.750,-3.755,'F.Cu')
R('GND',22.750,-3.029,22.750,-3.392,'F.Cu')
R('GND',22.677,-2.629,22.750,-3.029,'F.Cu')
# --- R2 old stubs (GND north run to via 32.475)
R('GND',32.324,-4.703,32.324,-4.432,'F.Cu')
R('GND',32.324,-4.432,32.324,-4.140,'F.Cu')
R('GND',32.324,-4.140,32.324,-2.796,'F.Cu')
R('GND',32.324,-2.796,32.475,-2.645,'F.Cu')
# --- CC1 old pad stub
R('USB_CC1',27.250,-3.755,27.250,-4.480,'F.Cu')
R('USB_CC1',27.250,-4.480,27.250,-4.773,'F.Cu')
R('USB_CC1',27.250,-4.773,27.074,-4.949,'F.Cu')
R('USB_CC1',27.074,-4.949,27.074,-6.047,'F.Cu')
R('USB_CC1',27.074,-6.047,27.453,-6.426,'F.Cu')
# --- CC2 old pad stub + east chain to R2
R('USB_CC2',24.250,-3.755,24.250,-3.030,'F.Cu')
R('USB_CC2',24.250,-3.030,24.250,-2.877,'F.Cu')
R('USB_CC2',24.250,-2.877,24.426,-2.701,'F.Cu')
R('USB_CC2',24.426,-2.701,24.426,-2.690,'F.Cu')
R('USB_CC2',24.426,-2.690,30.714,-2.690,'In2.Cu')
R('USB_CC2',30.714,-2.690,31.663,-3.639,'In2.Cu')
R('USB_CC2',31.663,-3.639,31.663,-5.035,'F.Cu')
R('USB_CC2',31.663,-5.035,31.909,-5.281,'F.Cu')
R('USB_CC2',31.909,-5.281,32.267,-5.281,'F.Cu')
R('USB_CC2',32.267,-5.281,32.324,-5.338,'F.Cu')
R('USB_CC2',32.324,-5.338,32.324,-5.452,'F.Cu')
R('USB_CC2',32.324,-5.452,32.324,-5.723,'F.Cu')
# --- VBUS old stubs
R('VBUS',28.450,-3.755,28.450,-3.392,'F.Cu')
R('VBUS',28.450,-3.392,28.450,-3.029,'F.Cu')
R('VBUS',28.450,-3.029,28.536,-2.810,'F.Cu')
R('VBUS',28.536,-2.810,26.847,-1.121,'F.Cu')
R('VBUS',28.450,-4.118,28.450,-3.755,'F.Cu')
R('VBUS',28.450,-4.481,28.450,-4.118,'F.Cu')
R('VBUS',28.312,-4.710,28.450,-4.481,'F.Cu')
R('VBUS',27.500,-5.700,28.300,-4.800,'F.Cu')
R('VBUS',23.550,-3.755,23.550,-4.118,'F.Cu')
R('VBUS',23.550,-4.118,23.550,-4.481,'F.Cu')
R('VBUS',23.550,-4.481,23.688,-4.710,'F.Cu')
R('VBUS',23.688,-4.710,23.881,-4.902,'F.Cu')
R('VBUS',23.881,-4.902,24.359,-4.902,'F.Cu')
R('VBUS',24.359,-4.902,25.353,-5.896,'B.Cu')
R('VBUS',25.353,-5.896,27.501,-5.896,'B.Cu')
R('VBUS',27.930,-2.230,29.740,-1.000,'In3.Cu')
# --- USB_DP old stubs
R('USB_DP',25.250,-3.755,25.367,-4.248,'F.Cu')
R('USB_DP',25.367,-4.248,25.367,-4.480,'F.Cu')
R('USB_DP',25.367,-4.480,25.367,-4.512,'F.Cu')
R('USB_DP',25.367,-4.512,23.793,-6.086,'F.Cu')
R('USB_DP',25.250,-3.755,25.382,-3.261,'F.Cu')
R('USB_DP',25.382,-3.261,25.382,-3.030,'F.Cu')
R('USB_DP',25.382,-3.030,25.382,-1.749,'F.Cu')
R('USB_DP',25.382,-1.749,23.957,-0.324,'F.Cu')
R('USB_DP',23.957,-0.324,23.932,-0.286,'F.Cu')
R('USB_DP',23.932,-0.286,23.923,-0.241,'F.Cu')
R('USB_DP',23.923,-0.241,23.923,1.062,'F.Cu')
R('USB_DP',26.250,-3.755,26.250,-3.030,'F.Cu')
R('USB_DP',26.250,-3.030,26.250,-2.684,'F.Cu')
# --- USB_DM old stubs
R('USB_DM',25.750,-3.755,25.602,-4.250,'F.Cu')
R('USB_DM',25.602,-4.250,25.602,-4.480,'F.Cu')
R('USB_DM',25.602,-4.480,25.602,-4.561,'F.Cu')
R('USB_DM',25.602,-4.561,25.593,-4.606,'F.Cu')
R('USB_DM',25.593,-4.606,25.568,-4.644,'F.Cu')
R('USB_DM',25.568,-4.644,23.925,-6.287,'F.Cu')
R('USB_DM',26.750,-3.755,26.750,-4.480,'F.Cu')
R('USB_DM',26.750,-4.480,26.750,-4.711,'F.Cu')
R('USB_DM',26.750,-4.711,26.574,-4.887,'F.Cu')
R('USB_DM',26.574,-4.887,25.969,-4.887,'F.Cu')
R('USB_DM',25.969,-4.887,25.750,-4.667,'F.Cu')
R('USB_DM',25.750,-4.667,25.750,-4.480,'F.Cu')
R('USB_DM',25.750,-4.480,25.750,-3.755,'F.Cu')
R('USB_DM',25.750,-3.755,25.617,-3.261,'F.Cu')
R('USB_DM',25.617,-3.261,25.617,-3.030,'F.Cu')
R('USB_DM',25.617,-3.030,25.617,-1.700,'F.Cu')
R('USB_DM',25.617,-1.700,25.608,-1.655,'F.Cu')
R('USB_DM',25.608,-1.655,25.583,-1.617,'F.Cu')
R('USB_DM',25.583,-1.617,24.158,-0.192,'F.Cu')
R('USB_DM',24.158,-0.192,24.158,1.111,'F.Cu')
R('USB_DM',25.870,-2.564,25.750,-2.684,'F.Cu')
R('USB_DM',25.750,-2.684,25.750,-3.030,'F.Cu')
R('USB_DM',25.750,-3.030,25.750,-3.755,'F.Cu')
# --- ESP_TX old east stubs
R('ESP_TX',31.516,-6.780,33.210,-8.474,'F.Cu')
R('ESP_TX',33.210,-8.474,33.503,-8.474,'F.Cu')
R('ESP_TX',33.503,-8.474,34.006,-8.474,'F.Cu')
# --- IN1P old via chain
R('IN1P',34.348,-5.766,33.736,-5.154,'F.Cu')
R('IN1P',33.736,-5.154,33.736,-4.705,'F.Cu')
R('IN1P',33.736,-4.705,33.736,-4.488,'F.Cu')
R('IN1P',33.736,-4.488,33.736,-4.167,'F.Cu')
R('IN1P',29.921,-1.339,34.348,-5.766,'In2.Cu')
R('IN1P',34.348,-5.766,43.266,-5.766,'In2.Cu')
# --- +3V3_ANA R26 stub
R('+3V3_ANA',35.027,-4.167,34.756,-4.167,'F.Cu')
R('+3V3_ANA',35.251,-4.167,35.027,-4.167,'F.Cu')
R('+3V3_ANA',35.807,-3.612,35.251,-4.167,'F.Cu')
# --- CS TP20 stub
R('CS_ADS1292',33.714,-1.736,33.892,-1.558,'F.Cu')
R('CS_ADS1292',33.892,-1.558,34.277,-1.173,'F.Cu')

# ---------- vias to rip
RIP_VIA = [
    (24.359,-4.902),   # VBUS — near west tab
    (24.426,-2.690),   # CC2 west feed (orphaned)
    (31.663,-3.639),   # CC2 east (under pad row)
    (34.348,-5.766),   # IN1P (within 0.3mm of east tab pad)
    (27.501,-5.896),   # VBUS feed stub (replaced)
]

# ---------- new copper ----------
W   = 0.1017   # standard signal
WU  = 0.129    # USB pair (matches existing)
WV  = 0.5      # VBUS outer layer (>=0.4)
WVI = 0.6      # VBUS inner layer (>=0.6)
WG  = 0.3      # GND stubs
SEGS=[]; VIAS=[]
def A(net,pts,layer,w):
    for i in range(len(pts)-1):
        SEGS.append((net,pts[i][0],pts[i][1],pts[i+1][0],pts[i+1][1],layer,w))
def V(x,y,net,size=0.4004,drill=0.2002): VIAS.append((x,y,net,size,drill))
T,L2,L3,B='F.Cu','In2.Cu','In3.Cu','B.Cu'

# === J1 nets ===
# GND signal-row pads -> vias
A('GND',[(26.25,-3.755),(26.0,-4.0)],T,WG); V(26.0,-4.0,'GND')
A('GND',[(32.75,-3.755),(32.75,-4.3)],T,WG); V(32.75,-4.3,'GND')
# 4 shield stitch vias (outside pads/tabs, inside board)
V(24.35,-5.5,'GND'); V(24.4,-8.9,'GND'); V(34.75,-4.5,'GND'); V(34.75,-8.6,'GND')
# VBUS pad 27.05 -> north trunk (26.847,-1.121)
A('VBUS',[(27.05,-3.755),(27.05,-4.3),(26.4,-4.05),(26.0,-3.5),(25.6,-2.5),(25.6,-1.7),(26.4,-1.2),(26.847,-1.121)],T,WV)
# VBUS pad 31.95 -> 2 vias -> In3 feed to (29.74,-1.0) run
A('VBUS',[(31.95,-3.755),(31.95,-4.5),(32.9,-4.9),(33.2,-5.9),(33.2,-6.1)],T,WV)
V(33.2,-6.1,'VBUS',0.45,0.2)
A('VBUS',[(33.2,-6.1),(33.4,-6.5)],T,WV); V(33.4,-6.5,'VBUS',0.45,0.2)
A('VBUS',[(33.2,-6.1),(31.5,-6.6)],L3,WVI)
A('VBUS',[(33.4,-6.5),(31.5,-6.6)],L3,WVI)
A('VBUS',[(31.5,-6.6),(29.5,-4.2),(29.74,-1.0)],L3,WVI)
# USB_CC2 pad 27.75 -> R2 pad1 (27.3,-0.89)
A('USB_CC2',[(27.75,-3.755),(27.75,-2.8),(27.9,-1.9),(27.9,-0.9),(27.57,-0.89)],T,W)
# R2 pad2 GND (27.3,-1.91) -> via
A('GND',[(27.3,-1.91),(27.0,-2.3),(26.9,-2.4)],T,W); V(26.9,-2.4,'GND')
# USB_CC1 pad 30.75 -> kept run at (30.0,-6.426)
A('USB_CC1',[(30.75,-3.755),(30.75,-4.5),(30.3,-5.0),(30.0,-5.5),(30.0,-6.426)],T,W)
# USB_DP: A6/B6 join + north trunk join + west via escape
A('USB_DP',[(28.75,-3.755),(28.75,-4.05),(29.75,-4.05),(29.75,-3.755)],T,WU)
A('USB_DP',[(29.75,-3.755),(29.75,-2.95),(28.0,-2.45),(26.4,-2.2),(26.25,-2.2)],T,WU)
A('USB_DP',[(28.75,-3.755),(28.5,-4.2),(26.5,-6.0),(24.3,-6.0),(23.793,-6.086)],T,WU)
# USB_DM: A7/B7 join + north trunk join + west via escape
A('USB_DM',[(29.25,-3.755),(29.25,-4.35),(30.25,-4.35),(30.25,-3.755)],T,WU)
A('USB_DM',[(30.25,-3.755),(30.25,-2.95),(28.3,-2.5),(26.0,-1.9),(25.87,-1.9)],T,WU)
A('USB_DM',[(29.25,-4.35),(28.6,-4.7),(28.0,-5.7),(24.5,-6.3),(23.925,-6.287)],T,WU)

# === displaced neighbours ===
# ESP_TX: keep (21.31,-6.78)-(31.516,-6.78); reroute to TP11 (36.3,-8.474)
A('ESP_TX',[(31.516,-6.78),(33.4,-7.3),(34.9,-7.5),(35.9,-8.0),(36.3,-8.474)],T,W)
# CS_ADS1292: TP20 pad (34.3,-0.4) -> rejoin (34.277,-1.173)
A('CS_ADS1292',[(34.3,-0.4),(34.45,-0.85),(34.277,-1.173)],T,W)
# IN1P: R26 pad2 (31.49,-1.9) -> via -> In2 join; new In2 east leg dodging east tab
A('IN1P',[(31.49,-1.9),(30.9,-1.6),(30.9,-1.45)],T,W); V(30.9,-1.45,'IN1P')
A('IN1P',[(30.9,-1.45),(29.921,-1.339)],L2,W)
A('IN1P',[(29.921,-1.339),(32.3,-1.8),(34.0,-3.2),(34.7,-3.5),(35.2,-4.4),(35.566,-5.766),(43.266,-5.766)],L2,W)
# +3V3_ANA: R26 pad1 (32.51,-1.9) -> trunk (35.807,-3.612)
A('+3V3_ANA',[(32.51,-1.9),(33.6,-1.6),(35.0,-1.5),(35.7,-1.7),(36.0,-2.4),(36.0,-3.2),(35.807,-3.612)],T,W)

# ================= engine =================
def parse_kv(line,tag):
    m=re.search(r'\(%s\s+([^)]*)\)'%tag,line)
    return m.group(1).split() if m else None
def close(a,b,tol=0.03): return abs(a-b)<=tol
def seg_hit(block,x1,y1,x2,y2,net,lay):
    sx=sy=ex=ey=None;nt=None;ly=None
    for ln in block:
        v=parse_kv(ln,'start')
        if v: sx,sy=float(v[0]),float(v[1])
        v=parse_kv(ln,'end')
        if v: ex,ey=float(v[0]),float(v[1])
        v=parse_kv(ln,'net')
        if v: nt=v[0].strip('"')
        v=parse_kv(ln,'layer')
        if v: ly=v[0].strip('"')
    if sx is None or ex is None: return False
    a=close(sx,x1) and close(sy,y1) and close(ex,x2) and close(ey,y2)
    b=close(sx,x2) and close(sy,y2) and close(ex,x1) and close(ey,y1)
    return (a or b) and nt==net and ly==lay
def via_hit(block,x,y):
    for ln in block:
        v=parse_kv(ln,'at')
        if v and close(float(v[0]),x) and close(float(v[1]),y): return True
    return False
def fp_ref(block):
    for ln in block:
        m=re.search(r'\(property "Reference" "([^"]+)"',ln)
        if m: return m.group(1)

src=open(INP).read().splitlines(keepends=True)
out=[];i=0;n=len(src)
log={'moved':[],'ripped_seg':[],'miss_seg':[],'ripped_via':[],'miss_via':[]}
rip_left=set(range(len(RIP))); rvia_left=set(range(len(RIP_VIA)))
while i<n:
    ln=src[i]
    m=re.match(r'^\t\((segment|via|footprint)\b',ln)
    if not m: out.append(ln);i+=1;continue
    kind=m.group(1);block=[ln];d=ln.count('(')-ln.count(')');i+=1
    while i<n and d>0:
        l2=src[i];block.append(l2);i+=1;d+=l2.count('(')-l2.count(')')
    if kind=='footprint':
        ref=fp_ref(block)
        if ref in MOVES:
            nx,ny=MOVES[ref]
            for k,l2 in enumerate(block):
                if re.match(r'^\t\t\(at ',l2):
                    rot=re.search(r'\(at\s+\S+\s+\S+\s+(\S+)\)',l2)
                    r=(' '+rot.group(1)) if rot else ''
                    block[k]='\t\t(at %s %s%s)\n'%(nx,ny,r)
                    log['moved'].append([ref,nx,ny]);break
        out.extend(block);continue
    if kind=='segment':
        found=None
        for ri in rip_left:
            net,x1,y1,x2,y2,lay=RIP[ri]
            if seg_hit(block,x1,y1,x2,y2,net,lay): found=ri;break
        if found is not None:
            log['ripped_seg'].append(RIP[found]);rip_left.discard(found);continue
        out.extend(block);continue
    if kind=='via':
        found=None
        for vi in rvia_left:
            x,y=RIP_VIA[vi]
            if via_hit(block,x,y): found=vi;break
        if found is not None:
            log['ripped_via'].append(RIP_VIA[found]);rvia_left.discard(found);continue
        out.extend(block);continue

log['miss_seg']=[RIP[r] for r in sorted(rip_left)]
log['miss_via']=[RIP_VIA[v] for v in sorted(rvia_left)]

newblk=[]
for (net,x1,y1,x2,y2,lay,w) in SEGS:
    newblk.append('\t(segment\n\t\t(start %s %s)\n\t\t(end %s %s)\n\t\t(width %s)\n\t\t(layer "%s")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n'%(x1,y1,x2,y2,w,lay,net,uuid.uuid4()))
for (x,y,net,sz,dr) in VIAS:
    newblk.append('\t(via\n\t\t(at %s %s)\n\t\t(size %s)\n\t\t(drill %s)\n\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "%s")\n\t\t(uuid "%s")\n\t)\n'%(x,y,sz,dr,net,uuid.uuid4()))
assert out[-1].rstrip()==')',out[-1]
out=out[:-1]+newblk+[out[-1]]
open(OUTP,'w').writelines(out)
log['added_seg']=len(SEGS);log['added_via']=len(VIAS)
json.dump(log,open(LOGP,'w'),indent=1)
print('moved:',log['moved'])
print('ripped_seg',len(log['ripped_seg']),'miss',len(log['miss_seg']))
print('ripped_via',len(log['ripped_via']),'miss',len(log['miss_via']))
print('added',log['added_seg'],'segs',log['added_via'],'vias')
for m in log['miss_seg']: print('  MISS_SEG',m)
for m in log['miss_via']: print('  MISS_VIA',m)
