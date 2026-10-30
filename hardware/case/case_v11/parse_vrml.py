#!/usr/bin/env python3
"""Parse KiCad VRML export -> per-top-level-subtree world bounding boxes.
Each direct child of the root transform is one placed 3D model (or board body).
Output: vrml_boxes.json  [{pts_min, pts_max, npts}] in mm
"""
import re, json, sys
import numpy as np

WRL = sys.argv[1] if len(sys.argv) > 1 else 'board_vrml.wrl'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'vrml_boxes.json'

TOK = re.compile(r'\[|\]|\{|\}|[^\s\[\]\{\}]+')

class Node:
    __slots__ = ('kind','t','r','s','kids','pts','name')
    def __init__(self, kind):
        self.kind = kind
        self.t = [0.,0.,0.]; self.r = [0.,0.,1.,0.]; self.s = [1.,1.,1.]
        self.kids = []; self.pts = None; self.name = None

DEFS = {}

def load_tokens(path):
    toks = []
    with open(path) as f:
        for line in f:
            toks += TOK.findall(line)
    return toks

def parse(toks):
    pos = 0
    n = len(toks)
    def block():
        nonlocal pos
        # skip 'DEF' name
        dname = None
        if toks[pos] == 'DEF':
            dname = toks[pos+1]
            pos += 2
        if toks[pos] == 'USE':
            node = Node('USE'); node.name = toks[pos+1]
            pos += 2
            return node
        kind = toks[pos]; pos += 1
        assert toks[pos] == '{', (toks[pos], pos)
        pos += 1
        node = Node(kind)
        if dname:
            node.name = dname
            DEFS[dname] = node
        while toks[pos] != '}':
            t = toks[pos]; pos += 1
            if t == 'children':
                assert toks[pos] == '['; pos += 1
                while toks[pos] != ']':
                    node.kids.append(block())
                pos += 1
            elif t == 'point' or t == 'vector':
                assert toks[pos] == '['; pos += 1
                start = pos
                while toks[pos] != ']': pos += 1
                if t == 'point' and node.kind == 'Coordinate':
                    vals = []
                    for x in toks[start:pos]:
                        for v in x.split(','):
                            if v:
                                vals.append(float(v))
                    node.pts = np.array(vals, dtype=np.float64).reshape(-1,3)
                pos += 1
            elif t in ('translation','center','scale','rotation','scaleOrientation'):
                vals = []
                while toks[pos] not in ('}','[',']','{') and toks[pos] not in (
                    'children','translation','center','scale','rotation','scaleOrientation',
                    'appearance','geometry','coord','coordIndex','normal','normalPerVertex',
                    'material','point','vector','color','solid','ccw','convex','creaseAngle',
                    'DEF','USE','Transform','Shape','Group','Inline','Anchor','IndexedFaceSet',
                    'Appearance','Material','Coordinate','Normal','Color','TextureCoordinate',
                    'ambientIntensity','diffuseColor','emissiveColor','shininess','specularColor',
                    'transparency','bboxCenter','bboxSize','texCoord','colorPerVertex','url',
                    'description','info','title','collision','proxy','children','metadata',
                    'whichchoice','level','range','rootUrl','parameter','bboxSize','size',
                    'crossSection','spine','orientation','beginCap','endCap','key','keyValue'):
                    vals.append(toks[pos]); pos += 1
                if t == 'translation' and node.kind=='Transform': node.t = [float(v) for v in vals[:3]]
                if t == 'rotation' and node.kind=='Transform': node.r = [float(v) for v in vals[:4]]
                if t == 'scale' and node.kind=='Transform': node.s = [float(v) for v in vals[:3]]
            else:
                # nested block (appearance, geometry, coord, material, ...)
                if toks[pos] == '{':
                    pos -= 1
                    node.kids.append(block())
                else:
                    # bare field value(s) we don't care about: consume numbers/strings until next keyword-ish
                    while pos < n and toks[pos] not in ('}','[',']','{') and not toks[pos][0].isalpha():
                        pos += 1
                    # if next is alphabetic keyword, loop handles it
        pos += 1  # consume '}'
        return node
    roots = []
    while pos < n:
        t = toks[pos]
        if t.startswith('#'):
            pos += 1; continue
        if t in ('DEF','Transform','Shape','Group','Inline','Anchor'):
            roots.append(block())
        else:
            pos += 1
    return roots

def rot_matrix(r):
    x,y,z,w = r
    n = (x*x+y*y+z*z)**0.5
    if n < 1e-12 or abs(w) < 1e-12:
        return np.eye(3)
    x,y,z = x/n, y/n, z/n
    c, s = np.cos(w), np.sin(w)
    C = 1-c
    return np.array([
        [x*x*C+c,   x*y*C-z*s, x*z*C+y*s],
        [y*x*C+z*s, y*y*C+c,   y*z*C-x*s],
        [z*x*C-y*s, z*y*C+x*s, z*z*C+c]])

def trs(node):
    M = np.eye(4)
    M[:3,:3] = rot_matrix(node.r) @ np.diag(node.s)
    M[:3,3] = node.t
    return M

def collect(node, M, out):
    if node.kind == 'Transform' or node.kind in ('Group','Anchor'):
        M2 = M @ trs(node)
        for k in node.kids:
            collect(k, M2, out)
    elif node.kind == 'Shape':
        for k in node.kids:
            collect(k, M, out)
    elif node.kind == 'IndexedFaceSet':
        for k in node.kids:
            collect(k, M, out)
    elif node.kind == 'Coordinate' and node.pts is not None:
        P = node.pts
        out.append((M[:3,:3] @ P.T).T + M[:3,3])
    elif node.kind == 'USE':
        tgt = DEFS.get(node.name)
        if tgt is not None:
            collect(tgt, M, out)
    else:
        for k in node.kids:
            collect(k, M, out)

toks = load_tokens(WRL)
print("tokens", len(toks))
roots = parse(toks)
print("roots", len(roots))
res = []
for r in roots:
    if abs(r.s[0]-2.54) > 0.01:
        r.s = [2.54, 2.54, 2.54]   # KiCad 10 writes 0.1 on the root; child
                                   # coords are still 0.1-inch units -> x2.54 mm
    Mr = trs(r)  # root scale -> mm
    for i, kid in enumerate(r.kids):
        out = []
        collect(kid, Mr, out)
        if not out: continue
        P = np.vstack(out)
        res.append({"i": i, "npts": int(P.shape[0]),
                    "min": np.round(P.min(0),4).tolist(),
                    "max": np.round(P.max(0),4).tolist()})
json.dump(res, open(OUT,'w'), indent=1)
print(len(res), "subtrees")
for e in res[:6]: print(e)
