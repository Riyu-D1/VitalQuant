import pcbnew
board=pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
NM=1e6
# edge cutout rectangles (interior slots)
slots=[(6.825,24.8,7.825,29.2),(6.825,40.5,7.825,44.9),(7.3,51.55,8.3,52.6),
(9.3,47.6,11.5,48.6),(11.9,49.5,12.9,52.3),(13.275,24.8,14.275,29.2),
(13.275,40.5,14.275,44.9),(19.725,24.8,20.725,29.2),(20.1,40.5,21.1,44.9),
(26.175,24.8,27.175,29.2),(17.2,46.19,23.1,52.7)]
def pts_dist(px,py,s):
    x0,y0,x1,y1=s
    cx=min(max(px,x0),x1); cy=min(max(py,y0),y1)
    return ((px-cx)**2+(py-cy)**2)**.5
print("=== pads inside slot rectangles ===")
for fp in board.GetFootprints():
    for p in fp.Pads():
        pp=p.GetPosition(); x,y=pp.x/NM,pp.y/NM
        for s in slots:
            if s[0]-0.05<=x<=s[2]+0.05 and s[1]-0.05<=y<=s[3]+0.05:
                print(f"  {fp.GetReference()}.{p.GetNumber()} ({x:.2f},{y:.2f}) inside slot {s}")
print("=== footprints crossing outer edge (pad outside bbox -2..48,-10.5..64.5) ===")
for fp in board.GetFootprints():
    for p in fp.Pads():
        pp=p.GetPosition(); x,y=pp.x/NM,pp.y/NM
        if x<-2 or x>48 or y<-10.5 or y>64.5:
            print(f"  {fp.GetReference()}.{p.GetNumber()} ({x:.2f},{y:.2f}) outside board")
            break
print("=== pads within 0.4mm of outer edge ===")
for fp in board.GetFootprints():
    for p in fp.Pads():
        pp=p.GetPosition(); sz=p.GetSize(); x,y=pp.x/NM,pp.y/NM
        r=max(sz.x,sz.y)/2e6
        d=min(x-(-2),48-x,y-(-10.5),64.5-y)-r
        if d<0.4:
            print(f"  {fp.GetReference()}.{p.GetNumber()} ({x:.2f},{y:.2f}) edge-dist {d:.2f}")
