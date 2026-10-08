import json,sys
sys.path.insert(0,'scripts')
import view
x0,y0,x1,y1=map(float,sys.argv[1:5])
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MplPoly, Circle
g=json.load(open('geom.json'))
fig,ax=plt.subplots(figsize=(12,10))
ax.set_xlim(x0,x1);ax.set_ylim(y0,y1);ax.set_aspect('equal');ax.invert_yaxis();ax.set_facecolor('#111')
LC={'Top Layer':'#ff4444','Bottom Layer':'#4488ff','Ground Layer 2':'#22aa66','Layer 3':'#ff9900','Power Layer 4':'#bb66ff','Ground Layer 5':'#007766'}
LID={0:'Top Layer',2:'Bottom Layer',4:'Ground Layer 2',6:'Layer 3',8:'Power Layer 4',10:'Ground Layer 5'}
for e in g['edge']:
    if min(e[0],e[2])>x1 or max(e[0],e[2])<x0 or min(e[1],e[3])>y1 or max(e[1],e[3])<y0: continue
    ax.plot([e[0],e[2]],[e[1],e[3]],color='#00ffff',lw=1.4,zorder=9)
for z in g['zones']:
    for poly in z['polys']:
        xs=[p[0] for p in poly];ys=[p[1] for p in poly]
        if max(xs)<x0 or min(xs)>x1 or max(ys)<y0 or min(ys)>y1:continue
        ax.add_patch(MplPoly(list(zip(xs,ys)),closed=True,facecolor=LC.get(z['layer'],'#888'),alpha=0.15,edgecolor=LC.get(z['layer'],'#888'),lw=0.5,ls='--'))
for k in g['keepouts']:
    xs=[p[0] for p in k['poly']];ys=[p[1] for p in k['poly']]
    if max(xs)<x0 or min(xs)>x1 or max(ys)<y0 or min(ys)>y1:continue
    ax.add_patch(MplPoly(list(zip(xs,ys)),closed=True,fill=False,edgecolor='#ffff00',lw=0.8,ls=':'))
for t in g['tracks']:
    mx,my=(t['x1']+t['x2'])/2,(t['y1']+t['y2'])/2
    if not(x0-1<=mx<=x1+1 and y0-1<=my<=y1+1):continue
    ax.plot([t['x1'],t['x2']],[t['y1'],t['y2']],color=LC.get(LID.get(t['l']),'#888'),lw=max(t['w']*5,0.5),alpha=0.8,solid_capstyle='round')
for v in g['vias']:
    if x0<=v['x']<=x1 and y0<=v['y']<=y1:
        ax.add_patch(Circle((v['x'],v['y']),v['d']/2,fc='#bbb',ec='#fff',lw=0.5));ax.add_patch(Circle((v['x'],v['y']),v['drill']/2,fc='#111'))
for p in g['pads']:
    if not(x0-0.3<=p['x']<=x1+0.3 and y0-0.3<=p['y']<=y1+0.3):continue
    ax.add_patch(Circle((p['x'],p['y']),max(p['w'],p['h'])/2,fc='#ff8888' if p['side']=='F' else '#88aaff',alpha=0.9))
    if p['net'] in ('+3V3','GND','TMP117_ALERT','I2C_SCL','I2C_SDA','unconnected-(J12-PadMP)','LED1_K','LED2_K','LED3_K','SWEAT_WE','MISO_FL','J11_RE','RLD_PAD','AFE_N_PAD','BIOZ_SP_PAD','EDA_RE_PAD','CS_MAX86178','MAX86178_INT','PD_A','PD_K'):
        ax.annotate(f"{p['ref']}.{p['no']}\n{p['net']}",(p['x'],p['y']),fontsize=4.5,color='#fff')
plt.savefig(sys.argv[5],dpi=130);print('wrote',sys.argv[5])
