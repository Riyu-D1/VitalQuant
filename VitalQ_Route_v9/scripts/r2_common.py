import json
import math
import shutil
import subprocess
from pathlib import Path
from shapely.geometry import LineString, box
from r2 import ROOT, KICAD_PY, state, record_attempt, save_json
from r2route import Board, route, window, SIGNAL_LAYERS, BGA

SCRIPTS = ROOT / 'scripts'


def kicad_job(job, path):
    src = Path(job['input']).parent
    for ext in ('kicad_pro', 'kicad_dru'):
        assert (src / ('vitalq_v2.' + ext)).read_bytes() == (ROOT / ('vitalq_v2.' + ext)).read_bytes(), f'input dir {src} lacks the project rules ({ext}); refusing to run with default rules'
    save_json(path, job)
    subprocess.run([KICAD_PY, '-B', str(SCRIPTS / 'kicad_ops.py'), str(path)], check=True)
    out = Path(job['output']).parent
    for ext in ('kicad_pro', 'kicad_dru'):
        assert (out / ('vitalq_v2.' + ext)).read_bytes() == (ROOT / ('vitalq_v2.' + ext)).read_bytes(), f'output dir {out} project rules differ from ROOT ({ext})'


def stage_dir(d, name):
    s = d / name
    s.mkdir()
    for ext in ('kicad_pro', 'kicad_dru', 'kicad_sch'):
        shutil.copy2(ROOT / ('vitalq_v2.' + ext), s / ('vitalq_v2.' + ext))
    return s


def extract(board_path, out):
    subprocess.run([KICAD_PY, '-B', str(SCRIPTS / 'extract.py'), str(board_path), str(out)], check=True)


def opens(drc_path):
    data = json.loads(Path(drc_path).read_text())
    return data['unconnected_items']


def open_comps(b, item):
    net = None
    uids = [e['uuid'] for e in item['items']]
    for u in uids:
        if u in b.items:
            net = b.items[u]['net']
            break
    if net is None:
        z = next((z for z in b.raw['zones'] if z['uuid'] in uids), None)
        net = z['net'] if z else None
    if net is None:
        return None
    comps, ids = b.components(net)
    keys = []
    for u in uids:
        k = ids.get(u)
        if k is None:
            k = next((key for key, entries in comps.items() if any(e.get('uuid') == u for e, _ in entries)), None)
        keys.append(k)
    return net, comps, keys


def route_open(b, item, label, approaches, ledger, small_vias=True, layers=SIGNAL_LAYERS):
    """approaches: list of (description, margin_mm, layers). Each is a recorded attempt."""
    info = open_comps(b, item)
    if info is None or None in info[2]:
        ledger.append({'connection': label, 'status': 'skipped', 'reason': 'endpoint not resolvable'})
        return None
    net, comps, (ka, kb) = info
    if ka == kb:
        ledger.append({'connection': label, 'net': net, 'status': 'already connected in working geometry'})
        return 'done'
    a, c = comps[ka], comps[kb]
    src, dst = (a, c) if len(a) <= len(c) else (c, a)
    return route_entries(b, net, src, dst, label, approaches, ledger, small_vias)


def route_entries(b, net, src, dst, label, approaches, ledger, small_vias=True, align=None):
    pads = []
    if small_vias:
        for e, _ in src + dst:
            if e['kind'] == 'pads' and e.get('ref') in BGA:
                pads.append(e)
    if align is None:
        align = (0.0, 0.0)
        for e, _ in src + dst:
            if e['kind'] == 'pads' and e.get('ref') == 'U7':
                align = (e['xy'][0] % 0.05, e['xy'][1] % 0.05)
    cap = box(*unary_bounds(src)).buffer(45)
    for desc, margin, lay in approaches:
        key = net + ':' + label
        n = record_attempt(key, desc)
        if not n:
            ledger.append({'connection': label, 'net': net, 'status': 'failed', 'reason': 'BLOCKED: 3 attempts used or approach repeated'})
            print('BLOCKED', label, flush=True)
            return None
        bounds, gap = window(src, dst, margin, b.outline.bounds, cap)
        res, err = route(b, net, src, dst, bounds, layers=lay, align=align, small_via_pads=pads)
        entry = {'connection': label, 'net': net, 'attempt': n, 'approach': desc, 'bounds': bounds, 'gap_mm': gap}
        if res:
            b.add_items(res['tracks'], res['vias'])
            entry.update(status='routed', tracks=len(res['tracks']), vias=res['vias'], expanded=res['expanded'])
            ledger.append(entry)
            print('ROUTED', label, len(res['tracks']), 'tracks', len(res['vias']), 'vias', flush=True)
            return res
        entry.update(status='failed', reason=err)
        ledger.append(entry)
        print('FAILED', label, desc, err, flush=True)
    return None


def unary_bounds(entries):
    xs0, ys0, xs1, ys1 = [], [], [], []
    for _, d in entries:
        for s in d.values():
            x0, y0, x1, y1 = s.bounds
            xs0.append(x0); ys0.append(y0); xs1.append(x1); ys1.append(y1)
    return min(xs0), min(ys0), max(xs1), max(ys1)
