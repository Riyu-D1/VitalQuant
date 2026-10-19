import json, sys, math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Rectangle, Polygon as MplPoly
import matplotlib.patches as mpatches

g = json.load(open('geom.json'))
LC = {'Top Layer':'#ff4444','Bottom Layer':'#4488ff','Ground Layer 2':'#22aa66','Layer 3':'#ff9900','Power Layer 4':'#bb66ff','Ground Layer 5':'#007766'}
SHORT = {'Top Layer':'F','Bottom Layer':'B','Ground Layer 2':'G2','Layer 3':'L3','Power Layer 4':'P4','Ground Layer 5':'G5'}
LID = {0:'Top Layer',2:'Bottom Layer',4:'Ground Layer 2',6:'Layer 3',8:'Power Layer 4',10:'Ground Layer 5'}

def region(x0,y0,x1,y1,fname,layers=None,nets_hl=None,title=''):
    fig,ax=plt.subplots(figsize=(14,14*(y1-y0)/(x1-x0)))
    ax.set_xlim(x0,x1); ax.set_ylim(y0,y1); ax.set_aspect('equal')
    ax.invert_yaxis()
    ax.set_facecolor('#111')
    # zones filled polys
    for z in g['zones']:
        for poly in z['polys']:
            xs=[p[0] for p in poly]; ys=[p[1] for p in poly]
            if max(xs)<x0 or min(xs)>x1 or max(ys)<y0 or min(ys)>y1: continue
            col=LC.get(z['layer'],'#888')
            ax.add_patch(MplPoly(list(zip(xs,ys)),closed=True,facecolor=col,alpha=0.10,edgecolor=col,linewidth=0.4,linestyle='--'))
    # keepouts
    for k in g['keepouts']:
        xs=[p[0] for p in k['poly']]; ys=[p[1] for p in k['poly']]
        if max(xs)<x0 or min(xs)>x1 or max(ys)<y0 or min(ys)>y1: continue
        style = {'hv_inner':('#ffff00',0.6),'hv_ownlayer':('#aaaa00',0.4),'antenna_keepout':('#ff00ff',0.7),'hole_keepout':('#ff00ff',0.7),'bga_fanout':('#00ffff',0.3)}.get(k['name'],('#fff',0.4))
        ax.add_patch(MplPoly(list(zip(xs,ys)),closed=True,fill=False,edgecolor=style[0],linewidth=style[1],linestyle=':'))
    # tracks
    for t in g['tracks']:
        ln=LID.get(t['l'],'?')
        if layers and ln not in layers: continue
        mx,my=(t['x1']+t['x2'])/2,(t['y1']+t['y2'])/2
        if not (x0-1<=mx<=x1+1 and y0-1<=my<=y1+1): continue
        hl = nets_hl and t['net'] in nets_hl
        col = '#ffffff' if hl else LC.get(ln,'#888')
        ax.plot([t['x1'],t['x2']],[t['y1'],t['y2']],color=col,lw=max(t['w']*8,1.4) if hl else max(t['w']*4,0.5),alpha=1.0 if hl else 0.75,solid_capstyle='round',zorder=5 if hl else 3)
    # vias
    for v in g['vias']:
        if not (x0<=v['x']<=x1 and y0<=v['y']<=y1): continue
        hl = nets_hl and v['net'] in nets_hl
        ax.add_patch(Circle((v['x'],v['y']),v['d']/2,facecolor='#ddd' if hl else '#666',edgecolor='#fff' if hl else '#999',lw=0.8,zorder=6))
        ax.add_patch(Circle((v['x'],v['y']),v['drill']/2,facecolor='#111',zorder=7))
    # pads
    for p in g['pads']:
        if not (x0-0.3<=p['x']<=x1+0.3 and y0-0.3<=p['y']<=y1+0.3): continue
        hl = nets_hl and p['net'] in nets_hl
        col='#00ff00' if hl else ('#ff8888' if p['side']=='F' else '#88aaff')
        ax.add_patch(Circle((p['x'],p['y']),max(p['w'],p['h'])/2,facecolor=col,edgecolor='#fff' if hl else 'none',lw=0.7,alpha=0.9,zorder=8))
    ax.set_title(title or f'({x0},{y0})-({x1},{y1})',color='w',fontsize=9)
    ax.tick_params(colors='#888',labelsize=7)
    plt.tight_layout(); plt.savefig(fname,dpi=110); plt.close()
    print('wrote',fname)

if __name__=='__main__':
    import sys
    a=sys.argv[1:]
    x0,y0,x1,y1=map(float,a[:4]); fname=a[4]
    nets=set(a[5].split(',')) if len(a)>6 else None
    region(x0,y0,x1,y1,fname,nets_hl=nets)
