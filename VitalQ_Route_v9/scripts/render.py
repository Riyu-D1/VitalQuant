from datetime import datetime
import subprocess
import json
from session import ROOT, CLI, save_json

out = ROOT / 'renders'
out.mkdir(exist_ok=True)
board = str(ROOT / 'vitalq_v2.kicad_pcb')
jobs = [('final_top.png', ['pcb', 'render', '--side', 'top', '--width', '1400', '--height', '1800', '--quality', 'basic']), ('final_bottom.png', ['pcb', 'render', '--side', 'bottom', '--width', '1400', '--height', '1800', '--quality', 'basic']), ('final_3d.png', ['pcb', 'render', '--side', 'top', '--rotate', '35,0,25', '--width', '1600', '--height', '1200', '--quality', 'basic']), ('final_top_copper.svg', ['pcb', 'export', 'svg', '--layers', 'F.Cu,Edge.Cuts', '--mode-single', '--page-size-mode', '2', '--exclude-drawing-sheet']), ('final_bottom_copper.svg', ['pcb', 'export', 'svg', '--layers', 'B.Cu,Edge.Cuts', '--mirror', '--mode-single', '--page-size-mode', '2', '--exclude-drawing-sheet'])]
manifest = out / 'manifest.json'
results = json.loads(manifest.read_text()) if manifest.exists() else []
for name, args in jobs:
    if any(r['file'] == name and r['success'] for r in results):
        continue
    command = [CLI] + args + ['-o', str(out / name), board]
    run = subprocess.run(command, capture_output=True, text=True, timeout=240)
    suffix = '.retry1.log' if (out / (name + '.log')).exists() else '.log'
    (out / (name + suffix)).write_text(run.stdout + run.stderr)
    ok = run.returncode == 0 and (out / name).exists() and (out / name).stat().st_size > 0
    result = {'file': name, 'command': command, 'exit_code': run.returncode, 'success': ok, 'time': datetime.now().astimezone().isoformat()}
    results.append(result)
    print(name, 'OK' if ok else 'FAILED', flush=True)
    save_json(out / 'manifest.json', results)
assert all(any(r['file'] == name and r['success'] for r in results) for name, _ in jobs)
