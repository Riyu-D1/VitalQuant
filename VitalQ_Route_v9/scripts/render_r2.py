"""renders/R2/: one SVG per copper layer + top/bottom/3D PNG of the saved working board."""
import subprocess
from session import ROOT, CLI
out = ROOT / 'renders' / 'R2'
out.mkdir(parents=True, exist_ok=True)
board = str(ROOT / 'vitalq_v2.kicad_pcb')
for i, l in enumerate(['F.Cu', 'In1.Cu', 'In2.Cu', 'In3.Cu', 'In4.Cu', 'In5.Cu', 'In6.Cu', 'B.Cu']):
    r = subprocess.run([CLI, 'pcb', 'export', 'svg', '--layers', l + ',Edge.Cuts', '--mode-single', '--page-size-mode', '2', '--exclude-drawing-sheet', '-o', str(out / f'{i}_{l.replace(".", "_")}.svg'), board], capture_output=True, text=True)
    print(l, r.returncode)
for name, args in [('top.png', ['--side', 'top']), ('bottom.png', ['--side', 'bottom']), ('iso.png', ['--side', 'top', '--rotate', '35,0,25'])]:
    r = subprocess.run([CLI, 'pcb', 'render', *args, '--width', '1400', '--height', '1800', '--quality', 'basic', '-o', str(out / name), board], capture_output=True, text=True)
    print(name, r.returncode)
