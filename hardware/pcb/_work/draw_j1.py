import json,matplotlib,sys; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle
d=json.load(open('_work/j1area.json')); dx=0
NC=(20.655+dx,-11.975,31.345+dx,-2.505)
print('parts intersecting NEW J1 courtyard',NC)
for r,v in d['F'].items():
    if r!='J1' and v[2]>NC[0] and v[0]<NC[2] and v[3]>NC[1] and v[1]<NC[3]: print(' ',r,[round(x,3) for x in v[:4]],v[4],[(p[0],p[3]) for p in v[5]])
vs=[v for v in d['V'] if NC[0]<v[0]<NC[2] and NC[1]<v[1]<NC[3]]
print('vias inside new J1 courtyard',len(vs),sorted(set(v[2] for v in vs)))
NM={'Top Layer':'F.Cu','Ground Layer 2':'In1.Cu','Layer 3':'In2.Cu','Signal Layer 4':'In3.Cu','Power Layer 5':'In4.Cu','Signal Layer 6':'In5.Cu','Ground Layer 7':'In6.Cu','Bottom Layer':'B.Cu'}
J1N={'USB_CC1','USB_CC2','USB_DM','USB_DP','VBUS'}
for lay,fn in NM.items():
    f,ax=plt.subplots(figsize=(13,11),dpi=110); ax.set_facecolor('black')
    for e in d['E']: ax.plot([e[0],e[2]],[e[1],e[3]],color='gold',lw=2)
    for t in d['L'].get(lay,[]):
        c='cyan' if t[5] in J1N else ('#607060' if t[5]=='GND' else '#c06030')
        ax.plot([t[0],t[2]],[t[1],t[3]],color=c,lw=max(1,t[4]*25),solid_capstyle='round')
        if t[5] in J1N: ax.text((t[0]+t[2])/2,(t[1]+t[3])/2,t[5],color='cyan',fontsize=6)
    for v in d['V']: ax.add_patch(plt.Circle((v[0],v[1]),0.225,color='cyan' if v[2] in J1N else '#888'))
    for r,v in d['F'].items():
        x0,y0,x1,y1=v[:4]; c='red' if r=='J1' else ('orange' if r in('R2','TP11','TP20') else '#3070c0')
        ax.add_patch(Rectangle((x0,y0),x1-x0,y1-y0,fill=False,ec=c,lw=1.5 if c!='#3070c0' else 0.5))
        ax.text(x0,y0,r,color=c,fontsize=7 if c!='#3070c0' else 5)
    ax.add_patch(Rectangle((NC[0],NC[1]),NC[2]-NC[0],NC[3]-NC[1],fill=False,ec='lime',lw=2,ls='--'))
    ax.text(NC[0],NC[3]+0.6,'NEW J1 courtyard (x 29.5)',color='lime',fontsize=8)
    ax.add_patch(Rectangle((29.5-6.2,-26.5),12.4,26.5-11.5,fill=False,ec='magenta',lw=1.5,ls=':'))
    ax.text(23.4,-26.8,'plug grip corridor 12.4 mm wide (x 23.3-35.7), keep clear',color='magenta',fontsize=8)
    ax.axvline(22.5,color='red',lw=0.6,ls='-.'); ax.text(22.6,-19,'tab edge x22.5',color='red',fontsize=7)
    ax.set_xlim(15,42); ax.set_ylim(4,-28); ax.set_aspect('equal'); ax.grid(True,color='#333',lw=0.4)
    ax.set_title(f'{lay} ({fn}) J1 area, fix_v22 FINAL. Cyan = J1 nets; red = J1 (x29.5); lime = old courtyard (x26); orange = R2/TP11/TP20',fontsize=9)
    plt.savefig(f'_prompt_pics/final/J1area_{fn}.png',bbox_inches='tight',facecolor='black'); plt.close()
