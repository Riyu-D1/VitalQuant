#!/usr/bin/env python3
"""Render a board region with all copper, pads, zones, courtyards."""
import sys, math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MplPoly, Circle, Rectangle
from pcb import PCB, blocks
import re

LCOL={'F.Cu':'#e33','In1.Cu':'#e90','In2.Cu':'#3a3','In3.Cu':'#39c',
      'In4.Cu':'#c3c','In5.Cu':'#96c','In6.Cu':'#666','B.Cu':'#33f'}
NETCOL={'VBAT':'#d00','VBAT_SYS':'#f80','VBUS':'#909','GND':'#888','+3V3':'#0a0'}

def render(pcb,x0,y0,x1,y1,out,labels=True,figsize=(16,12)):
    fig,ax=plt.subplots(figsize=figsize)
    ax.set_aspect('equal'); ax.set_xlim(x0,x1); ax.set_ylim(y1,y0)
    # zone fills (very light)
    for z in pcb.zones:
        for lay,pts in z['fills']:
            if not pts: continue
            if not all(x0-5<=p[0]<=x1+5 and y0-5<=p[1]<=y1+5 for p in pts):
                pass
            ax.add_patch(MplPoly(pts,closed=True,fc=LCOL.get(lay,'#ccc'),ec='none',alpha=0.08))
        for pts in z['polys']:
            col='#f0f' if z['net'] is None else NETCOL.get(z['net'],'#0cc')
            ax.add_patch(MplPoly(pts,closed=True,fill=False,ec=col,lw=0.6,ls='--',alpha=0.7))
    # segments
    for s in pcb.segs:
        (xa,ya),(xb,yb)=s['start'],s['end']
        if max(xa,xb)<x0 or min(xa,xb)>x1 or max(ya,yb)<y0 or min(ya,yb)>y1: continue
        col=NETCOL.get(s['net'],LCOL.get(s['layer'],'#333'))
        ax.plot([xa,xb],[ya,yb],color=col,lw=max(s['width']*3,0.4),alpha=0.85,solid_capstyle='round')
    # vias
    for v in pcb.vias:
        x,y=v['at']
        if not(x0<=x<=x1 and y0<=y<=y1): continue
        col=NETCOL.get(v['net'],'#333')
        ax.add_patch(Circle((x,y),v['size']/2,fc='none',ec=col,lw=0.8))
        ax.add_patch(Circle((x,y),v['drill']/2,fc=col,ec='none',alpha=0.5))
    # footprints
    for fp in pcb.fps:
        if fp.at is None: continue
        x,y=fp.at
        if not(x0-4<=x<=x1+4 and y0-4<=y<=y1+4): continue
        r=math.radians(fp.rot)
        for p in fp.pads:
            g=pcb.pad_global(fp,p)
            if g is None: continue
            gx,gy=g
            if not(x0<=gx<=x1 and y0<=gy<=y1): continue
            pr=math.radians(fp.rot+p.rot)
            w,h=p.size
            col=NETCOL.get(p.net,'#000')
            dx,dy=w/2,h/2
            corners=[(-dx,-dy),(dx,-dy),(dx,dy),(-dx,dy)]
            gc=[(gx+cx*math.cos(pr)-cy*math.sin(pr),gy+cx*math.sin(pr)+cy*math.cos(pr)) for cx,cy in corners]
            ax.add_patch(MplPoly(gc,closed=True,fc=col,ec='k',lw=0.3,alpha=0.75))
        for (ax0,ay0,ax1,ay1) in fp.crtyd:
            corners=[(ax0,ay0),(ax1,ay0),(ax1,ay1),(ax0,ay1)]
            gc=[(x+cx*math.cos(r)-cy*math.sin(r),y+cx*math.sin(r)+cy*math.cos(r)) for cx,cy in corners]
            ax.add_patch(MplPoly(gc,closed=True,fill=False,ec='#0aa',lw=0.5,alpha=0.6))
        if labels:
            ax.text(x,y,fp.ref,fontsize=5,ha='center',va='center',color='#000',
                    bbox=dict(fc='white',ec='none',alpha=0.5,pad=0.1))
    # net label legend
    ax.set_title(f"{x0},{y0} - {x1},{y1}")
    fig.savefig(out,dpi=150,bbox_inches='tight')
    print("wrote",out)

if __name__=='__main__':
    pcb=PCB(sys.argv[1])
    x0,y0,x1,y1=map(float,sys.argv[2:6])
    render(pcb,x0,y0,x1,y1,sys.argv[6])
