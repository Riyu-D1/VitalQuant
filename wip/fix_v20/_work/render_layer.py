#!/usr/bin/env python3
"""Render one layer of a board region."""
import sys, math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MplPoly, Circle
from pcb import PCB

def render_layer(pcb,layer,x0,y0,x1,y1,out,figsize=(15,13),label_fp=True):
    fig,ax=plt.subplots(figsize=figsize)
    ax.set_aspect('equal'); ax.set_xlim(x0,x1); ax.set_ylim(y1,y0)
    ax.set_title(f"{layer}  {x0},{y0}-{x1},{y1}")
    ax.grid(True,alpha=0.3)
    # zone fills on this layer
    for z in pcb.zones:
        for lay,pts in z['fills']:
            if lay!=layer or not pts: continue
            ax.add_patch(MplPoly(pts,closed=True,fc='#ccc' if z['net']!='GND' else '#bbb',ec='none',alpha=0.25))
        for pts in z['polys']:
            if layer in z['layers'] or not z['layers']:
                col='#f0f' if z['net'] is None else '#0cc'
                ax.add_patch(MplPoly(pts,closed=True,fill=False,ec=col,lw=0.5,ls='--',alpha=0.5))
    for s in pcb.segs:
        if s['layer']!=layer: continue
        (xa,ya),(xb,yb)=s['start'],s['end']
        if max(xa,xb)<x0 or min(xa,xb)>x1 or max(ya,yb)<y0 or min(ya,yb)>y1: continue
        ax.plot([xa,xb],[ya,yb],color='#d00' if s['net']=='VBUS' else '#f80' if s['net']=='VBAT_SYS' else '#d80' if s['net']=='VBAT' else '#333',
                lw=max(s['width']*3,0.5),alpha=0.9,solid_capstyle='round')
    for v in pcb.vias:
        if layer not in v['layers']: continue
        x,y=v['at']
        if not(x0<=x<=x1 and y0<=y<=y1): continue
        ax.add_patch(Circle((x,y),v['size']/2,fc='none',ec='#00a',lw=0.8))
    for fp in pcb.fps:
        if fp.at is None: continue
        x,y=fp.at
        if not(x0-4<=x<=x1+4 and y0-4<=y<=y1+4): continue
        r=math.radians(fp.rot)
        fplayer = 'F.Cu' if fp.layer=='F.Cu' else 'B.Cu'
        for p in fp.pads:
            g=pcb.pad_global(fp,p)
            if g is None: continue
            gx,gy=g
            if not(x0<=gx<=x1 and y0<=gy<=y1): continue
            if fp.layer=='F.Cu' and 'F.Cu' in p.layers and layer!='F.Cu':
                pass  # F.Cu pad still occupies drill? smd no hole -> only F.Cu
            onlayer = (layer in p.layers)
            if fp.layer=='F.Cu' and layer.startswith('In'): onlayer=False
            if fp.layer=='B.Cu' and layer.startswith('In'): onlayer=False
            if layer in ('F.Cu','B.Cu') and fp.layer!=layer: onlayer=False
            if not onlayer: continue
            pr=math.radians(fp.rot+p.rot)
            w,h=p.size; dx,dy=w/2,h/2
            corners=[(-dx,-dy),(dx,-dy),(dx,dy),(-dx,dy)]
            gc=[(gx+cx*math.cos(pr)-cy*math.sin(pr),gy+cx*math.sin(pr)+cy*math.cos(pr)) for cx,cy in corners]
            ax.add_patch(MplPoly(gc,closed=True,fc='#07a' if p.net in ('VBUS','VBAT','VBAT_SYS') else '#555',ec='k',lw=0.3,alpha=0.8))
        for (ax0,ay0,ax1,ay1) in fp.crtyd:
            corners=[(ax0,ay0),(ax1,ay0),(ax1,ay1),(ax0,ay1)]
            gc=[(x+cx*math.cos(r)-cy*math.sin(r),y+cx*math.sin(r)+cy*math.cos(r)) for cx,cy in corners]
            ax.add_patch(MplPoly(gc,closed=True,fill=False,ec='#0aa',lw=0.4,alpha=0.4,ls=':'))
        if label_fp:
            ax.text(x,y,fp.ref,fontsize=4,ha='center',va='center',color='#555')
    fig.savefig(out,dpi=160,bbox_inches='tight')
    plt.close(fig)
    print("wrote",out)

if __name__=='__main__':
    pcb=PCB(sys.argv[1])
    layer=sys.argv[2]
    x0,y0,x1,y1=map(float,sys.argv[3:7])
    render_layer(pcb,layer,x0,y0,x1,y1,sys.argv[7])
