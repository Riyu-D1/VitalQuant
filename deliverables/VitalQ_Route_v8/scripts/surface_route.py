import heapq
import math
import numpy as np
import shapely
from shapely.geometry import Point, LineString, box
from scipy.ndimage import distance_transform_edt, label


def route_surface(allowed, starts, goal, bounds, resolution=0.05, max_expansions=500000):
    x0, y0, x1, y1 = bounds
    x0, y0 = math.floor(x0 / resolution) * resolution, math.floor(y0 / resolution) * resolution
    xs = np.arange(x0, x1 + resolution / 2, resolution)
    ys = np.arange(y0, y1 + resolution / 2, resolution)
    xx, yy = np.meshgrid(xs, ys)
    allowed = allowed.intersection(box(x0, y0, x1, y1))
    shapely.prepare(allowed)
    free = shapely.contains_xy(allowed, xx, yy)
    shapely.prepare(goal)
    target = shapely.contains_xy(goal, xx, yy) & free
    if not target.any():
        return None
    height, width = free.shape
    seeds = {}
    for start in starts:
        cx, cy = int(round((start[0] - x0) / resolution)), int(round((start[1] - y0) / resolution))
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                x, y = cx + dx, cy + dy
                if 0 <= x < width and 0 <= y < height and free[y, x] and allowed.covers(LineString([start, (xs[x], ys[y])])):
                    cost = math.dist(start, (xs[x], ys[y]))
                    if (x, y) not in seeds or cost < seeds[x, y][0]:
                        seeds[x, y] = (cost, start)
    if not seeds:
        return None
    labels, _ = label(free, structure=np.ones((3,3)))
    reachable = set(labels[y, x] for x, y in seeds)
    if not (reachable & set(labels[target])):
        return None
    heuristic = distance_transform_edt(~target) * resolution
    costs = np.full(free.shape, np.inf)
    previous = {}
    queue = []
    for (x, y), (cost, _) in seeds.items():
        costs[y, x] = cost
        heapq.heappush(queue, (cost + heuristic[y, x], cost, x, y))
    neighbors = ((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1))
    found = None
    expanded = 0
    while queue and expanded < max_expansions:
        _, cost, x, y = heapq.heappop(queue)
        if cost > costs[y, x] + 1e-9:
            continue
        expanded += 1
        if target[y, x]:
            found = (x, y)
            break
        for dx, dy in neighbors:
            nx, ny = x + dx, y + dy
            if not (0 <= nx < width and 0 <= ny < height and free[ny, nx]):
                continue
            if dx and dy and not (free[y, nx] and free[ny, x]) and not allowed.covers(LineString([(xs[x],ys[y]),(xs[nx],ys[ny])])):
                continue
            distance = resolution * (math.sqrt(2) if dx and dy else 1)
            nc = cost + distance
            if nc < costs[ny, nx] - 1e-9:
                costs[ny, nx] = nc
                previous[nx, ny] = (x, y)
                heapq.heappush(queue, (nc + heuristic[ny, nx], nc, nx, ny))
    if found is None:
        return None
    cells = [found]
    while cells[-1] in previous:
        cells.append(previous[cells[-1]])
    cells.reverse()
    points = [tuple(seeds[cells[0]][1])] + [(float(xs[x]), float(ys[y])) for x, y in cells]
    compact = [points[0]]
    for point in points[1:]:
        if math.dist(compact[-1], point) > 1e-8:
            compact.append(point)
    if len(compact) == 1:
        return compact
    simplified = [compact[0]]
    for i in range(1, len(compact)-1):
        ax, ay = np.subtract(compact[i], simplified[-1])
        bx, by = np.subtract(compact[i+1], compact[i])
        if abs(ax * by - ay * bx) > 1e-9 or ax * bx + ay * by < 0:
            simplified.append(compact[i])
    simplified.append(compact[-1])
    if not allowed.covers(LineString(simplified)):
        return None
    if not goal.covers(Point(simplified[-1])):
        return None
    return simplified
