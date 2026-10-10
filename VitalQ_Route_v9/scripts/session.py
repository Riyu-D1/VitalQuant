import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CLI = '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'
PYPCB = '/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3'
COSMETIC = {'lib_footprint_issues', 'lib_footprint_mismatch', 'text_height', 'text_thickness', 'silk_edge_clearance', 'silk_over_copper', 'silk_overlap'}


def save_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def backup(tag):
    directory = ROOT / 'backups' / (datetime.now().strftime('%Y%m%d_%H%M%S_') + tag)
    directory.mkdir(parents=True, exist_ok=False)
    hashes = {}
    for extension in ('kicad_pcb', 'kicad_pro', 'kicad_sch', 'kicad_prl', 'kicad_dru'):
        source = ROOT / ('vitalq_v2.' + extension)
        if source.exists():
            shutil.copy2(source, directory / source.name)
            hashes[source.name] = hashlib.sha256(source.read_bytes()).hexdigest()
    save_json(directory / 'manifest.json', {'tag': tag, 'time': datetime.now().astimezone().isoformat(), 'hashes': hashes})
    print(directory, flush=True)
    return directory


def summary(path):
    data = json.loads(Path(path).read_text())
    kinds = Counter(item['type'] for item in data['violations'])
    return {'unconnected': len(data['unconnected_items']), 'real': sum(n for k, n in kinds.items() if k not in COSMETIC), 'total': len(data['violations']), 'parity': len(data.get('schematic_parity', [])), 'types': dict(kinds)}


def drc(board, output, save=True):
    command = [CLI, 'pcb', 'drc', '--refill-zones', '--all-track-errors', '--schematic-parity', '--format', 'json', '-o', str(output)]
    if save:
        command.append('--save-board')
    subprocess.run(command + [str(board)], check=True)
    result = summary(output)
    print(json.dumps(result), flush=True)
    return result


def log(batch, nets, result, disposition):
    path = ROOT / 'PROGRESS.md'
    if not path.exists():
        path.write_text('# VitalQ v9 progress\n\nBatch | nets done / disposition | unconnected | real violations (reported, may be capped) | time\n---|---|---:|---:|---\n')
    with path.open('a') as stream:
        stream.write(f"{batch} | {nets}; {disposition} | {result['unconnected']} | {result['real']} | {datetime.now().astimezone().isoformat()}\n")


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['backup', 'summary', 'baseline'])
    parser.add_argument('argument')
    args = parser.parse_args()
    if args.action == 'backup':
        backup(args.argument)
    elif args.action == 'summary':
        print(json.dumps(summary(args.argument), indent=2))
    else:
        result = drc(ROOT / 'vitalq_v2.kicad_pcb', ROOT / ('drc_' + args.argument + '.json'))
        log(args.argument, 'none (rules baseline)', result, 'refilled saved board; no routing attempts')
