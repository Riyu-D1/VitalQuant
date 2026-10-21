#!/usr/bin/env python3
"""VitalQ v2 — net geometry + crosstalk extraction (read-only on the board).

Extracts per-net routed length per layer, same-layer parallel-run lengths
between aggressor (fast digital / LED driver / switcher) and victim
(electrode / ECG / EDA / BioZ / PPG-PD) nets, and adjacent-layer XY overlap.

Crosstalk model (documented, first-order):
  same-layer edge coupling between microstrip/stripline neighbours:
    Cg [pF/mm] = eps0*er*w/h          (to adjacent plane)
    Cm [pF/mm] = eps0*er*0.5*t/s      (edge fringe, s=edge spacing)
    Xtalk ~= 0.25 * Cm/(Cm+Cg) * min(1, Lpar/Lsat),  Lsat=10 mm
  adjacent-layer broadside overlap:
    C [pF] = eps0*er*w*Lovl/h_layers  -> reported as overlap mm, worst first
eps0=8.854e-3 pF/mm, er=4.2 (FR4), t=0.018 mm Cu (per stackup half-oz inner),
h=0.1 mm nominal adjacent dielectric (per 1.6 mm 8L stackup).
"""
import pcbnew, math, json
from collections import defaultdict

PCB='/Users/deepakdiwan/Downloads/VitalQ_Hand/fix_v22/_work/vitalq_v2_pre_move.kicad_pcb'
OUT='_work/reports/crosstalk_v22_pre'
mm=pcbnew.ToMM

import re
# aggressor = fast digital / LED driver / switcher nets (exact or prefix)
AGG_RE=re.compile(r'^(SPI_|MISO_|CS_|USB_|MIC_|TX[1-4]$|LED|IR_|NIR_|TX_SW$|SW_L|'
 r'TX_5V|I2C_|CP_|ESP_|STATUS_LED|AFE_CLK$|CHG_|EXP_INT|.*_ALERT$|.*_INT$|'
 r'TX5_EN|.*GATE|.*DRDY|.*RDY|FSR|VDD_CP|PD_GND|PD_A$)')
# victim = electrode / analog front-end / reference nets only
VIC_RE=re.compile(r'^(ECG|AFE_P|AFE_N|AFE_IN|PD_IN|PD2_|PD_K$|PD_A$|BIOZ|SWEAT|'
 r'J11_|J13_|MX_ECG|EDA|.*_PAD$|IN1[PN]|IN2[PN]|PGA|RLD|RESP|AIN|SE0|CE0|RE0|DE0|'
 r'VREF|VCAP|VZERO|VBIAS|LDR$|MX_VREF|.*_SURGE|.*_ISO|RCAL|FB_3V3|TS$)')
SKIP=('GND','+3','+1','VBAT','VBUS','TX_5V','VDD','IOVDD','DVDD','AVDD','unconnected','Net-','RTC_VBK','+5')

def cls(net,table):
    return bool(table.match(net)) if hasattr(table,'match') else any(net.startswith(p) or p in net for p in table)

def seg_dist(s1,e1,s2,e2):
    """min distance + approx parallel overlap fraction"""
    def dpt(p,a,b):
        ax,ay=a;bx,by=b
        dx,dy=bx-ax,by-ay
        L2=dx*dx+dy*dy
        if L2==0: return math.hypot(p[0]-ax,p[1]-ay),0
        t=max(0,min(1,((p[0]-ax)*dx+(p[1]-ay)*dy)/L2))
        return math.hypot(p[0]-ax-t*dx,p[1]-ay-t*dy),t
    d1,ta= dpt(s1,s2,e2); d2,tb= dpt(e1,s2,e2)
    d3,_ = dpt(s2,s1,e1); d4,_ = dpt(e2,s1,e1)
    d=min(d1,d2,d3,d4)
    # angle between segments
    a1=math.atan2(e1[1]-s1[1],e1[0]-s1[0]); a2=math.atan2(e2[1]-s2[1],e2[0]-s2[0])
    da=abs(math.sin(a1-a2))
    return d,da,(ta,tb)

def seglen(s,e): return math.hypot(e[0]-s[0],e[1]-s[1])

b=pcbnew.LoadBoard(PCB)
order=['F.Cu','In1.Cu','In2.Cu','In3.Cu','In4.Cu','In5.Cu','In6.Cu','B.Cu']
lname={i:pcbnew.LayerName(i) for i in range(pcbnew.PCB_LAYER_ID_COUNT)}
cuorder=[n for n in order if n in set(lname.values())]

segs=defaultdict(list)   # net -> list of (layer,sx,sy,ex,ey,w)
netlen=defaultdict(lambda:defaultdict(float))
for t in b.GetTracks():
    if t.Type()==pcbnew.PCB_VIA_T: continue
    net=t.GetNetname(); ln=lname[t.GetLayer()]
    s,e=t.GetStart(),t.GetEnd()
    seg=(ln,mm(s.x),mm(s.y),mm(e.x),mm(e.y),mm(t.GetWidth()))
    segs[net].append(seg)
    netlen[net][ln]+=seglen((seg[1],seg[2]),(seg[3],seg[4]))

# ---- net length report ----
with open(OUT+'_netlen.csv','w') as f:
    f.write('net,'+','.join(cuorder)+',total\n')
    for n,d in sorted(netlen.items(),key=lambda kv:-sum(kv[1].values())):
        f.write(n+','+','.join(f'{d.get(l,0):.2f}' for l in cuorder)+f',{sum(d.values()):.2f}\n')

agg_nets={n for n in segs if cls(n,AGG_RE) and not cls(n,VIC_RE) and not cls(n,SKIP)}
vic_nets={n for n in segs if cls(n,VIC_RE) and not cls(n,SKIP)}
# some nets are both (AFE_CLK is aggressor only; PD_A in both -> keep victim priority)
agg_nets-=vic_nets

rows=[]
for an in agg_nets:
    for vn in vic_nets:
        for s1 in segs[an]:
            for s2 in segs[vn]:
                if s1[0]!=s2[0]:
                    continue
                d,da,(ta,tb)=seg_dist((s1[1],s1[2]),(s1[3],s1[4]),(s2[1],s2[2]),(s2[3],s2[4]))
                if d>1.0 or da>0.5:   # >1mm spacing or non-parallel: skip
                    continue
                L=min(s1[5] and seglen((s1[1],s1[2]),(s1[3],s1[4])) or 0,
                          seglen((s2[1],s2[2]),(s2[3],s2[4])))*max(0,min(1,tb-ta))
                if L<0.2: continue
                rows.append((an,vn,s1[0],d,L,s1[5],s2[5]))

# adjacent-layer overlap
LIDX={n:i for i,n in enumerate(cuorder)}
ovl=[]
for an in agg_nets:
    for vn in vic_nets:
        for s1 in segs[an]:
            for s2 in segs[vn]:
                if abs(LIDX[s1[0]]-LIDX[s2[0]])!=1: continue
                # bbox overlap
                ax0,ax1=sorted((s1[1],s1[3]));ay0,ay1=sorted((s1[2],s1[4]))
                bx0,bx1=sorted((s2[1],s2[3]));by0,by1=sorted((s2[2],s2[4]))
                ox=min(ax1,bx1)-max(ax0,bx0);oy=min(ay1,by1)-max(ay0,by0)
                if ox>0.05 and oy>0.05:
                    ovl.append((an,vn,s1[0]+'/'+s2[0],min(ox,oy),ox*oy))

# crosstalk estimate
EPS0=8.854e-3; ER=4.2; TCU=0.018; H=0.1; LSAT=10.0
def xtalk(s,w,L):
    Cg=EPS0*ER*w/H
    Cm=EPS0*ER*0.5*TCU/max(s,0.02)
    return 0.25*Cm/(Cm+Cg)*min(1.0,L/LSAT)

with open(OUT+'_pairs.csv','w') as f:
    f.write('aggressor,victim,layer,min_spacing_mm,parallel_mm,w_a,w_v,xtalk_est_frac,dv3V3_mV\n')
    for r in sorted(rows,key=lambda r:r[3]):
        x=xtalk(r[3],min(r[5],r[6]),r[4])
        f.write(f'{r[0]},{r[1]},{r[2]},{r[3]:.3f},{r[4]:.2f},{r[5]:.3f},{r[6]:.3f},{x:.4f},{x*3300:.1f}\n')
with open(OUT+'_overlap.csv','w') as f:
    f.write('aggressor,victim,layers,min_dim_mm,area_mm2\n')
    for r in sorted(ovl,key=lambda r:-r[4]):
        f.write(f'{r[0]},{r[1]},{r[2]},{r[3]:.3f},{r[4]:.3f}\n')

print(f'agg_nets={len(agg_nets)} vic_nets={len(vic_nets)}')
print(f'parallel pairs={len(rows)} overlaps={len(ovl)}')
for r in sorted(rows,key=lambda r:r[3])[:15]:
    x=xtalk(r[3],min(r[5],r[6]),r[4])
    print(f'  {r[0]:16s}->{r[1]:16s} {r[2]:8s} s={r[3]:.3f}mm L={r[4]:.1f}mm x={x*100:.2f}% -> {x*3300:.0f}mV')
print('top overlaps:')
for r in sorted(ovl,key=lambda r:-r[4])[:12]:
    print(f'  {r[0]:16s}->{r[1]:16s} {r[2]:16s} {r[3]:.2f}mm {r[4]:.2f}mm2')
