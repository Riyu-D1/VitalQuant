#!/usr/bin/env python3
"""Drive the board to routed: loop opens_nets -> liberate until stable.

Each pass: regenerate the open-net list from the current board, run
liberate.py over it (microvia escapes + patch-rips + heals), repeat while
the open count is shrinking. Bounded passes. Then zone-fill + verify and
print the final OPEN-NETS line.

usage: converge.py <board.kicad_pcb> <workdir> [--passes N] (default 8)
"""
import os
import subprocess
import sys
import time

PCB_DIR = os.path.dirname(os.path.abspath(__file__))
PY = '/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3'
BOARD, WDIR = sys.argv[1], sys.argv[2]
MAXP = 8
for i, a in enumerate(sys.argv):
    if a == '--passes':
        MAXP = int(sys.argv[i + 1])

def run(script, *args, log):
    p = subprocess.run(
        [PY, '-u', os.path.join(PCB_DIR, script), *args],
        cwd=WDIR, stdout=log, stderr=subprocess.STDOUT)
    return p.returncode

def opens(board):
    lst = os.path.join(WDIR, 'opens_cur.txt')
    with open(lst, 'w') as f:
        subprocess.run([PY, '-u', os.path.join(PCB_DIR, 'opens_nets.py'),
                        board], cwd=WDIR, stdout=f,
                       stderr=subprocess.DEVNULL)
    return [l.strip() for l in open(lst) if l.strip()]

prev = None
for ps in range(MAXP):
    cur = opens(BOARD)
    print(f'== pass {ps}: {len(cur)} open nets ==', flush=True)
    if not cur:
        break
    if prev is not None and len(cur) >= prev:
        print('open count not shrinking; stop after this pass', flush=True)
        last = True
    else:
        last = False
    prev = len(cur)
    with open(os.path.join(WDIR, 'lib_pass.log'), 'a') as lg:
        lg.write(f'\n===== converge pass {ps} ({len(cur)} nets) =====\n')
        lg.flush()
        run('liberate.py', BOARD, 'opens_cur.txt', '--max-rips', '8', log=lg)
    if last:
        break

# finish: fill zones, final verify
log = open(os.path.join(WDIR, 'converge_final.log'), 'w')
subprocess.run([PY, '-u', '-c', f'''
import pcbnew
b = pcbnew.LoadBoard("{BOARD}")
pcbnew.ZONE_FILLER(b).Fill(b.Zones(), False)
pcbnew.SaveBoard("{BOARD}", b)
print("zones filled:", len(b.Zones()))
'''], cwd=WDIR, stdout=log, stderr=subprocess.STDOUT)
subprocess.run([PY, '-u', os.path.join(PCB_DIR, 'verify_connectivity.py'),
                BOARD], cwd=WDIR, stdout=log, stderr=subprocess.STDOUT)
print('CONVERGE-DONE', flush=True)
