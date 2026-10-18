import sys
sys.path.insert(0,'scripts')
from router import Router, LNAME
import numpy as np, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
r=Router('geom.json')
blocked,goal,viaok=r.masks_for('CP_RX')
x0,y0,x1,y1=9,6,25,16
c0=r.cell(x0,y0); c1=r.cell(x1,y1)
fig,axs=plt.subplots(1,2,figsize=(18,7))
axs[0].imshow(blocked[0][c0[1]:c1[1],c0[0]:c1[0]],extent=[x0,x1,y1,y0],cmap='RdGy_r')
axs[0].set_title('blocked F')
axs[1].imshow(goal[0][c0[1]:c1[1],c0[0]:c1[0]],extent=[x0,x1,y1,y0],cmap='RdGy')
axs[1].set_title('goal F')
sx,sy=r.cell(12.487,14.190)
axs[0].plot([12.487],[14.190],'g*',ms=15)
axs[0].plot([23.21],[8.72],'b*',ms=15)
plt.savefig('renders/dbg.png',dpi=110)
print('done', r.cell(12.487,14.190))
print('blocked at start?',blocked[0][sy,sx])
ys,xs=np.nonzero(goal[0])
print('goal cells F:',len(xs))
