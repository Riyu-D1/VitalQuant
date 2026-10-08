import pcbnew, json, re, math
from collections import defaultdict

board = pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
NM = 1e6

def mm(v): return v/NM

with open('drc_baseline_quilter_1_1.json') as f:
    drc = json.load(f)

# Collect all pads and tracks per net for proximity queries
pads_by_net = defaultdict(list)   # net -> [(ref,padno,x,y,side,layers,obj)]
for fp in board.GetFootprints():
    ref = fp.GetReference()
    side = fp.GetSide()  # 0 front 1 back
    for p in fp.Pads():
        pos = p.GetPosition()
        pads_by_net[p.GetNetname()].append((ref, p.GetNumber(), mm(pos.x), mm(pos.y), 'B' if side else 'F', p))

tracks_by_net = defaultdict(list)
for t in board.GetTracks():
    net = t.GetNetname()
    if t.Type() == pcbnew.PCB_VIA_T:
        pos = t.GetPosition()
        tracks_by_net[net].append(('VIA', mm(pos.x), mm(pos.y), t))
    else:
        s, e = t.GetStart(), t.GetEnd()
        tracks_by_net[net].append(('SEG', mm(s.x), mm(s.y), mm(e.x), mm(e.y), board.GetLayerName(t.GetLayer()), t))

def dist(a,b): return math.hypot(a[0]-b[0], a[1]-b[1])

# group unconnected by net
net_eps = defaultdict(list)
for u in drc['unconnected_items']:
    m = re.search(r'\[([^\]]+)\]', u['items'][0]['description'])
    net = m.group(1) if m else '?'
    pair = [(it['description'], (it['pos']['x'], it['pos']['y'])) for it in u['items']]
    net_eps[net].append(pair)

def nearest(net, pt, n=3):
    cands = []
    for tp in tracks_by_net.get(net, []):
        if tp[0]=='VIA': cands.append((dist(pt, tp[1:3]), f"VIA@({tp[1]:.2f},{tp[2]:.2f})"))
        else:
            mid = ((tp[1]+tp[3])/2, (tp[2]+tp[4])/2)
            cands.append((dist(pt, mid), f"SEG {tp[5]} ({tp[1]:.2f},{tp[2]:.2f})->({tp[3]:.2f},{tp[4]:.2f})"))
    for pd in pads_by_net.get(net, []):
        cands.append((dist(pt, pd[2:4]), f"PAD {pd[0]}.{pd[1]}@({pd[2]:.2f},{pd[3]:.2f}){pd[4]}"))
    cands.sort()
    return cands[:n]

for net in sorted(net_eps):
    print(f"\n##### {net}")
    allpts = []
    for pair in net_eps[net]:
        for desc, pt in pair:
            allpts.append((desc, pt))
    uniq = []
    seen = set()
    for desc, pt in allpts:
        k = desc
        if k in seen: continue
        seen.add(k); uniq.append((desc, pt))
    for desc, pt in uniq:
        print(f"  EP ({pt[0]:7.3f},{pt[1]:7.3f}) {desc}")
        for dd, lab in nearest(net, pt, 2):
            print(f"      near: {dd:6.3f} {lab}")
