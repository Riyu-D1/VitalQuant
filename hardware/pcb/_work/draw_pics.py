import json,re,sys,matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle
g=json.load(open(sys.argv[1])); out=sys.argv[2]
xs=[e[1] for e in g['edge']]+[e[3] for e in g['edge']]; ys=[e[2] for e in g['edge']]+[e[4] for e in g['edge']]
print('EDGE bbox',min(xs),max(xs),min(ys),max(ys))
rats=[]
for l in open('_work/gb/unconnected41.txt'):
    m=re.findall(r'@\(([-\d.]+), ([-\d.]+)\): (.*?)(?=\s+<->|$)',l.strip())
    if len(m)==2: rats.append(((float(m[0][0]),float(m[0][1])),(float(m[1][0]),float(m[1][1])),re.search(r'\[([^\]]+)\]|^(\S+)',m[0][2]).group(0).strip('[]').split()[0]))
LAY=['F.Cu','In1.Cu','In2.Cu','In3.Cu','In4.Cu','In5.Cu','In6.Cu','B.Cu']
NAME={'F.Cu':'Top','In1.Cu':'L2 GND','In2.Cu':'L3','In3.Cu':'L4','In4.Cu':'L5 Power','In5.Cu':'L6','In6.Cu':'L7 GND','B.Cu':'Bottom'}
# planned lanes per layer: list of polylines (label, pts)
P={'In5.Cu':[('p4 LSM6_INT1',[(4.25,-10.95),(3.5,-10.95),(3.5,3.5)]),('p12 I2C_SDA',[(4.25,-0.79),(3.8,-0.79),(3.8,3.9),(4.3,4.4)]),
 ('p5 TMP117',[(4.25,-9.68),(6.75,-9.68),(6.75,3.3),(7.4,3.3)]),('p8 CS_AD5940',[(4.25,-5.87),(6.4,-5.87),(6.4,3.9),(9.3,3.9)]),
 ('p10 STATUS_LED',[(4.25,-3.33),(6.05,-3.33),(6.05,6.2),(10.2,6.2)]),('p11 CS_AFE',[(4.25,-2.06),(5.7,-2.06),(5.7,6.9),(9.7,6.9)]),
 ('p36 ESP_RX',[(18.75,-9.68),(16.6,-9.68),(16.6,4.2)]),('p34 MIC_DAT',[(18.75,-7.14),(16.95,-7.14),(16.95,4.8),(19.5,4.8)]),
 ('p33 MIC_CLK',[(18.75,-5.87),(17.3,-5.87),(17.3,4.2),(20.0,4.2)]),('p32 MAX_INT',[(18.75,-4.6),(17.65,-4.6),(17.65,3.6),(21.5,3.6)])],
 'In3.Cu':[('p9 CS_FLASH',[(4.25,-4.6),(9.5,-4.6),(9.5,7.8)]),('p6 GAUGE',[(4.25,-8.41),(16.6,-8.41),(16.6,3.8)]),('p7 CHG_PG',[(4.25,-7.14),(16.95,-7.14),(16.95,3.2)]),
 ('p31 CS_MAX',[(18.75,-3.33),(17.6,-3.33),(17.6,5.5),(17.1,5.5)])],
 'In2.Cu':[('p39 FSR',[(18.75,-13.49),(18.75,-13.2),(3.75,-13.2),(3.75,4.3)]),('p27 IO0',[(18.75,1.75),(18.75,1.0),(3.4,1.0),(3.4,4.2)]),('p18a CS_ADS',[(8.325,1.55),(8.325,1.3),(3.7,1.3),(3.7,4.3)])],
 'B.Cu':[('p22',[(13.405,1.55),(13.405,0.95),(21.8,0.95)]),('p21',[(12.135,1.55),(12.135,0.55),(22.25,0.55),(22.25,1.2)]),('p20',[(10.865,1.55),(10.865,0.15),(22.7,0.15),(22.7,1.754),(21.3,1.754)]),
 ('p19',[(9.595,1.55),(9.595,-0.25),(23.15,-0.25),(23.15,3.652),(21.31,3.652)]),('p18b',[(8.325,1.55),(8.325,-0.65),(23.6,-0.65),(23.6,2.5)]),
 ('p38 DRDY',[(18.75,-12.22),(23.45,-12.22),(23.45,-2.376),(23.0,-2.376)]),('p35 SPARE',[(18.75,-8.41),(22.65,-8.41),(22.65,-0.793),(23.3,-0.793)]),('DRDY link',[(3.8,-1.05),(21.3,-1.05)])]}
def base(ax,layer,crop):
    for e in g['edge']: ax.plot([e[1],e[3]],[e[2],e[4]],'y-',lw=1.5)
    for t in g['tracks']:
        if t[0]==layer: ax.plot([t[1],t[3]],[t[2],t[4]],'-',color='#c06030' if t[6] not in('GND',) else '#607060',lw=max(t[5]*72/ (crop and 0.035 or 0.08)/10,0.4),solid_capstyle='round')
    for v in g['vias']: ax.add_patch(plt.Circle((v[0],v[1]),0.225,color='#888',zorder=3))
    for p in g['pads']:
        if layer in p[7] or (layer=='F.Cu' and 'F.Cu' in p[7]):
            ax.add_patch(Rectangle((p[2],p[3]),p[4],p[5],color='#d4b000' if p[0]=='U1' else '#3070c0',alpha=0.8,zorder=2))
    ax.add_patch(Rectangle((2.0,-16.5),19,19.95,fill=False,ec='cyan',ls='--',lw=1))
def rat(ax):
    for a,b,n in rats:
        ax.plot([a[0],b[0]],[a[1],b[1]],'-',color='lime',lw=0.6,zorder=5)
        ax.text((a[0]+b[0])/2,(a[1]+b[1])/2,n,fontsize=5,color='lime',zorder=6)
def plan(ax,layer):
    for lab,pts in P.get(layer,[]):
        ax.plot([p[0] for p in pts],[p[1] for p in pts],'-',color='magenta',lw=1.2,zorder=7)
        ax.plot(pts[0][0],pts[0][1],'o',color='magenta',ms=5,zorder=8)
        ax.text(pts[-1][0],pts[-1][1],lab,fontsize=6,color='white',zorder=9,bbox=dict(fc='purple',alpha=0.6,pad=0.5))
def fig(layer,crop,fn,showrat,showplan):
    f,ax=plt.subplots(figsize=(14,14) if crop else (12,16),dpi=110); ax.set_facecolor('black')
    base(ax,layer,crop)
    if showrat: rat(ax)
    if showplan: plan(ax,layer)
    if crop: ax.set_xlim(-4,27); ax.set_ylim(15,-19)
    else: ax.set_xlim(min(xs)-1,max(xs)+1); ax.set_ylim(max(ys)+1,min(ys)-1)
    ax.set_aspect('equal'); ax.grid(True,color='#333',lw=0.4); ax.set_xticks(range(-4,28,1) if crop else ax.get_xticks()); 
    ax.tick_params(labelsize=6); ax.set_title(f'{NAME[layer]} ({layer}) - start board base_fixed4. Lime=41 ratsnest, magenta=suggested lane, dot=escape via, cyan=U1 body',fontsize=9)
    plt.savefig(out+'/'+fn,bbox_inches='tight',facecolor='black'); plt.close()
for L in LAY: fig(L,True,f'U1zoom_{L}.png',L=='F.Cu',True)
fig('F.Cu',True,'U1zoom_ALL_ratsnest_top.png',True,False)
# overview all layers planned
f,ax=plt.subplots(figsize=(14,14),dpi=110); ax.set_facecolor('black')
for e in g['edge']: ax.plot([e[1],e[3]],[e[2],e[4]],'y-',lw=1.5)
cols={'In5.Cu':'magenta','In3.Cu':'orange','In2.Cu':'cyan','B.Cu':'deepskyblue'}
for p in g['pads']:
    if p[0]=='U1': ax.add_patch(Rectangle((p[2],p[3]),p[4],p[5],color='#d4b000'))
rat(ax)
for L,c in cols.items():
    for lab,pts in P[L]:
        ax.plot([p[0] for p in pts],[p[1] for p in pts],'-',color=c,lw=1.3); ax.text(pts[-1][0],pts[-1][1],lab,fontsize=6,color=c)
ax.set_xlim(-4,27); ax.set_ylim(15,-19); ax.set_aspect('equal'); ax.grid(True,color='#333',lw=0.4)
ax.set_title('PLAN overview: magenta L6(In5), orange L4(In3), cyan L3(In2), blue Bottom; lime = ratsnest',fontsize=9)
plt.savefig(out+'/PLAN_overview.png',bbox_inches='tight',facecolor='black')
