import sys, json, time
sys.path.insert(0,'scripts')
from router import Router, LNAME
r=Router('geom.json')
REV={0:'F',1:'B',2:'G2',3:'L3',4:'P4',5:'G5'}
t0=time.time()
res=r.route('CP_RX',(12.487,14.190,0),layers=[0,1,2,3,4,5],twidth=0.1016)
print('route took',time.time()-t0)
if res:
    path,viaok=res
    segs,vias=r.simplify(path)
    print('segs:',[(REV[s[0]],round(s[1],2),round(s[2],2),round(s[3],2),round(s[4],2)) for s in segs])
    print('vias:',[(round(v[0],2),round(v[1],2)) for v in vias])
else:
    print('NO PATH')
