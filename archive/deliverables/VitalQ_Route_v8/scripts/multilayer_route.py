import heapq
import math
import numpy as np
import shapely
from scipy.ndimage import distance_transform_edt
from shapely.geometry import Point, LineString, GeometryCollection
from shapely.ops import unary_union, nearest_points
from route_geometry import WIDTH, disk


def component_shapes(entries, layers):
    return {l:unary_union([shapes[l] for _,shapes in entries if l in shapes]) for l in layers}


def pad_via(g,net,entries,allowed):
    if any(obj["kind"]=="vias" for obj,_ in entries):
        return None
    pads=[(obj,shapes) for obj,shapes in entries if obj["kind"]=="pads"]
    if len(pads)!=1:
        return None
    obj,shapes=pads[0]
    surface=unary_union(list(shapes.values()))
    possible=allowed.intersection(surface).buffer(-0.002)
    if possible.is_empty:
        return None
    p=nearest_points(Point(obj["xy"]),possible)[1]
    return list(p.coords[0])


def route_components(g,net,source,destination,layers=(0,2,6),res=0.05,margin=5,max_expansions=700000):
    src=component_shapes(source,layers)
    dst=component_shapes(destination,layers)
    source_all=unary_union(list(src.values()))
    dest_all=unary_union(list(dst.values()))
    p,q=nearest_points(source_all,dest_all)
    minx,miny,maxx,maxy=source_all.bounds
    minx,miny=min(minx,q.x)-margin,min(miny,q.y)-margin
    maxx,maxy=max(maxx,q.x)+margin,max(maxy,q.y)+margin
    bx0,by0,bx1,by1=g.outline.bounds
    minx,miny=max(minx,bx0),max(miny,by0)
    maxx,maxy=min(maxx,bx1),min(maxy,by1)
    minx,miny=math.floor(minx/res)*res,math.floor(miny/res)*res
    xs=np.arange(minx,maxx+res/2,res);ys=np.arange(miny,maxy+res/2,res)
    xx,yy=np.meshgrid(xs,ys)
    h,w=xx.shape
    allowed=[g.obstacles(net,l) for l in layers]
    free=np.array([shapely.contains_xy(a,xx,yy) for a in allowed])
    via_allowed=g.via_allowed(net)
    via_mask=shapely.contains_xy(via_allowed,xx,yy)
    source_via=pad_via(g,net,source,via_allowed)
    dest_via=pad_via(g,net,destination,via_allowed)
    original_start=np.array([shapely.contains_xy(src[l],xx,yy) for l in layers]) & free
    original_goal=np.array([shapely.contains_xy(dst[l],xx,yy) for l in layers]) & free
    starts=original_start.copy();goals=original_goal.copy()
    if source_via:
        ring=disk(source_via,0.15)
        starts|=shapely.contains_xy(ring,xx,yy)[None,:,:]&free
    if dest_via:
        ring=disk(dest_via,0.15)
        goals|=shapely.contains_xy(ring,xx,yy)[None,:,:]&free
    if not starts.any() or not goals.any():
        return None,"No legal source/goal cells, including exact via-in-pad options"
    heuristic=distance_transform_edt(~goals.any(axis=0))*res
    costs=np.full(free.shape,np.inf)
    parent={}
    queue=[]
    for li,y,x in zip(*np.nonzero(starts)):
        cost=0.0 if original_start[li,y,x] else 3.0
        costs[li,y,x]=cost
        heapq.heappush(queue,(cost+heuristic[y,x],cost,int(li),int(y),int(x)))
    existing={}
    for via in g.raw["vias"]:
        if via["net"]!=net:
            continue
        x=int(round((via["xy"][0]-minx)/res));y=int(round((via["xy"][1]-miny)/res))
        if 0<=x<w and 0<=y<h:
            existing[x,y]=via
    neighbors=((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1))
    expanded=0;found=None
    while queue and expanded<max_expansions:
        _,cost,li,y,x=heapq.heappop(queue)
        if cost>costs[li,y,x]+1e-9:
            continue
        expanded+=1
        if goals[li,y,x]:
            found=(li,y,x);break
        for dx,dy in neighbors:
            nx,ny=x+dx,y+dy
            if not(0<=nx<w and 0<=ny<h and free[li,ny,nx]):
                continue
            if dx and dy and not(free[li,y,nx] and free[li,ny,x]) and not allowed[li].covers(LineString([(xs[x],ys[y]),(xs[nx],ys[ny])])):
                continue
            nc=cost+res*(math.sqrt(2) if dx and dy else 1)
            if nc<costs[li,ny,nx]-1e-9:
                costs[li,ny,nx]=nc;parent[li,ny,nx]=(li,y,x)
                heapq.heappush(queue,(nc+heuristic[ny,nx],nc,li,ny,nx))
        if via_mask[y,x] or (x,y) in existing:
            for nli in range(len(layers)):
                if nli==li or not free[nli,y,x]:
                    continue
                nc=cost+(0.02 if (x,y) in existing else 3)
                if nc<costs[nli,y,x]-1e-9:
                    costs[nli,y,x]=nc;parent[nli,y,x]=(li,y,x)
                    heapq.heappush(queue,(nc+heuristic[y,x],nc,nli,y,x))
    if found is None:
        return None,f"No F/B/L3 path within {margin}mm endpoint corridor; expanded {expanded} cells"
    cells=[found]
    while cells[-1] in parent:
        cells.append(parent[cells[-1]])
    cells.reverse()
    vias=[]
    if not original_start[cells[0]]:
        vias.append(source_via)
    if not original_goal[cells[-1]]:
        vias.append(dest_via)
    runs=[];current=[];last_layer=None
    for li,y,x in cells:
        point=(float(xs[x]),float(ys[y]))
        layer=layers[li]
        if layer!=last_layer:
            if current:
                runs.append((last_layer,current))
                if (x,y) not in existing:
                    vias.append(list(point))
            current=[point];last_layer=layer
        else:
            current.append(point)
    if current:
        runs.append((last_layer,current))
    tracks=[]
    for layer,points in runs:
        if len(points)<2:
            continue
        simple=[points[0]]
        for i in range(1,len(points)-1):
            ax,ay=np.subtract(points[i],simple[-1]);bx,by=np.subtract(points[i+1],points[i])
            if abs(ax*by-ay*bx)>1e-9 or ax*bx+ay*by<0:
                simple.append(points[i])
        simple.append(points[-1])
        if not g.obstacles(net,layer).covers(LineString(simple)):
            return None,"Exact continuous segment validation rejected a grid path"
        tracks.extend({"net":net,"layer":layer,"a":a,"b":b,"width":WIDTH} for a,b in zip(simple,simple[1:]))
    unique=[]
    for via in vias:
        if via is None or not via_allowed.covers(Point(via)):
            return None,"Exact via validation failed"
        if any(math.dist(via,other)<1e-5 for other in unique):
            continue
        if any(math.dist(via,other)<0.35-1e-6 for other in unique):
            return None,"Planned via pair violates JLC hole-edge spacing"
        unique.append(via)
    return {"net":net,"tracks":tracks,"vias":[{"net":net,"xy":v,"diameter":0.3,"drill":0.15} for v in unique],"expanded":expanded},None
