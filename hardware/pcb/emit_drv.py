#!/usr/bin/env python3
"""Driver for emit_one.py — processes the open-HV queue one process
per net. Exit 3 = cager ripped -> retry net immediately, queue the
sibling. Ping-pong bounded by ripped_once.

usage: emit_drv.py <board> <pro> <NET,NET,...>
"""
import sys, subprocess, os

IN, PRO = sys.argv[1], sys.argv[2]
PY = '/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3'
HERE = os.path.dirname(os.path.abspath(__file__))
RIPQ = IN.replace('.kicad_pcb', '.ripq')
open(RIPQ, 'w').close()

queue = list(sys.argv[3].split(','))
ripped_once = set()
done = []
final_open = []
guard = 0
while queue and guard < 60:
    net = queue.pop(0)
    guard += 1
    rc = subprocess.call(
        [PY, '-u', os.path.join(HERE, 'emit_one.py'),
         IN, PRO, net, RIPQ],
        cwd=HERE)
    if rc == 0:
        done.append(net)
        print(f'== {net} CONNECTED ({len(done)} total)', flush=True)
    elif rc == 3:
        queue.insert(0, net)          # retry uncaged net first
        sib = open(RIPQ).read().strip().split('\n')[-1]
        if sib and sib not in ripped_once:
            ripped_once.add(sib)
            if sib not in queue and sib not in done:
                queue.append(sib)
        # safety: if same net caged twice by already-ripped sibs
        if queue.count(net) > 3:
            queue = [q for q in queue if q != net]
            final_open.append(net)
    else:
        final_open.append(net)
        print(f'== {net} OPEN', flush=True)

print(f'== DRIVER DONE. connected={done} open={final_open} '
      f'still-queued={queue}', flush=True)
