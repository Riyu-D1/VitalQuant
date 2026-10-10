import argparse
from collections import Counter
import json
from pathlib import Path
from shapely.geometry import Point, LineString
from shapely.strtree import STRtree
from geometry import Geometry, LAYERS
from session import save_json, COSMETIC


def keepout_hits(g):
    result = []
    for layer in LAYERS:
        for area, shape in g.keepouts[layer]:
            if not area['tracks'] and not area['vias']:
                continue
            for index in g.trees[layer].query(shape, predicate='intersects'):
                item, copper = g.solids[layer][index]
                if (item['kind'] == 'tracks' and area['tracks']) or (item['kind'] == 'vias' and area['vias']):
                    result.append({'item': item['uuid'], 'net': item['net'], 'kind': item['kind'], 'area': area['uuid'], 'name': area['name'], 'layer': layer})
    return result


def via_checks(g):
    gaps, pairs, vip = [], [], []
    zone_trees = {l: STRtree([s for _, s in g.zones[l]]) for l in LAYERS}
    for via in g.raw['vias']:
        center = Point(via['xy'])
        radius = via['drill'] / 2
        bound = center.buffer(radius + .2)
        for layer in LAYERS:
            for idx in g.trees[layer].query(bound):
                other, shape = g.solids[layer][idx]
                gap = center.distance(shape) - radius
                if other['net'] != via['net'] and gap < .2 - .000002:
                    gaps.append({'via': via['uuid'], 'net': via['net'], 'xy': via['xy'], 'other': other['uuid'], 'layer': layer, 'gap': gap})
                if other['kind'] == 'pads' and layer in (0, 2) and shape.intersects(center.buffer(via['diameter'] / 2)):
                    vip.append({'via': via['uuid'], 'net': via['net'], 'xy': via['xy'], 'pad': other['ref'] + '.' + other['number'], 'filled': via['filled'], 'capped': via['capped'], 'layer': layer})
            for idx in zone_trees[layer].query(bound):
                other, shape = g.zones[layer][idx]
                gap = center.distance(shape) - radius
                if other['net'] != via['net'] and gap < .2 - .000002:
                    gaps.append({'via': via['uuid'], 'net': via['net'], 'xy': via['xy'], 'other': other['uuid'], 'layer': layer, 'gap': gap})
    tree = STRtree([shape for _, shape in g.drills])
    for i, (item, shape) in enumerate(g.drills):
        for j in tree.query(shape.buffer(.2)):
            if j <= i:
                continue
            other, foreign = g.drills[j]
            if shape.distance(foreign) < .2 - .000002:
                pairs.append({'a': item['uuid'], 'b': other['uuid'], 'gap': shape.distance(foreign)})
    return gaps, pairs, vip


def compare(before, after, allow_zone_changes=False, moved=(), allow_j8=False, stackup8=False):
    invariants = {}
    invariants['footprints_identical'] = sorted([f for f in before.raw['footprints'] if f['ref'] not in moved], key=lambda r: r['ref']) == sorted([f for f in after.raw['footprints'] if f['ref'] not in moved], key=lambda r: r['ref'])
    invariants['moved_footprints_same_side_same_lock'] = all(a['layer'] == b['layer'] and a['locked'] == b['locked'] for a in before.raw['footprints'] for b in after.raw['footprints'] if a['ref'] in moved and a['ref'] == b['ref'])
    pad_key = lambda p: {k: v for k, v in p.items() if k != 'connected' and (p['ref'] not in moved or k in ('uuid', 'ref', 'number', 'net', 'size', 'attribute', 'drill', 'layers'))}
    invariants['pad_geometry_and_nets_identical'] = [pad_key(p) for p in before.raw['pads']] == [pad_key(p) for p in after.raw['pads']]
    if stackup8:
        strip = lambda k: {a: b for a, b in k.items() if a != 'layers'}
        bk = [k for k in before.raw['keepouts'] if not (allow_j8 and k['parent'] == 'J8')]
        ak = [k for k in after.raw['keepouts'] if not (allow_j8 and k['parent'] == 'J8')]
        invariants['rule_areas_same_shape_layers_superset'] = len(bk) == len(ak) and all(strip(x) == strip(y) and set(x['layers']) <= set(y['layers']) for x, y in zip(bk, ak))
    elif allow_j8:
        invariants['rule_areas_identical_except_J8_notches'] = [k for k in before.raw['keepouts'] if k['parent'] != 'J8'] == [k for k in after.raw['keepouts'] if k['parent'] != 'J8']
    else:
        invariants['rule_areas_identical'] = before.raw['keepouts'] == after.raw['keepouts']
    invariants['outline_identical'] = before.raw['outline'] == after.raw['outline']
    if not allow_zone_changes:
        invariants['zone_definitions_identical'] = [{k: v for k, v in p.items() if k != 'fill'} for p in before.raw['zones']] == [{k: v for k, v in p.items() if k != 'fill'} for p in after.raw['zones']]
    invariants['copper_layer_count_ok'] = len(after.raw['layers']) == len(before.raw['layers']) or (stackup8 and len(after.raw['layers']) == 8)
    invariants['through_vias_only'] = all(len(v['layers']) == len(after.raw['layers']) for v in after.raw['vias'])
    regressions, groups = [], 0
    for net in sorted({p['net'] for p in before.raw['pads']}):
        comps, _ = before.components(net)
        _, new = after.components(net)
        for entries in comps.values():
            pads = [i['uuid'] for i, _ in entries if i['kind'] == 'pads']
            if len(pads) < 2:
                continue
            groups += 1
            if len({new.get(p, p) for p in pads}) > 1:
                regressions.append({'net': net, 'pads': pads})
    old_keep = keepout_hits(before)
    new_keep = keepout_hits(after)
    old_keys = {(r['item'], r['area'], r['layer']) for r in old_keep}
    new_hits = [r for r in new_keep if (r['item'], r['area'], r['layer']) not in old_keys]
    old_gaps, old_pairs, _ = via_checks(before)
    gaps, pairs, vip = via_checks(after)
    old_gap_keys = {(r['via'], r['other'], r['layer']) for r in old_gaps}
    old_pair_keys = {tuple(sorted((r['a'], r['b']))) for r in old_pairs}
    new_gaps = [r for r in gaps if (r['via'], r['other'], r['layer']) not in old_gap_keys]
    new_pairs = [r for r in pairs if tuple(sorted((r['a'], r['b']))) not in old_pair_keys]
    return {'invariants': invariants, 'preserved_pad_groups_checked': groups, 'pad_connectivity_regressions': regressions, 'keepout_layer_intersections': new_keep, 'new_keepout_hits': new_hits, 'via_hole_copper_gaps': gaps, 'new_via_hole_copper_gaps': new_gaps, 'hole_pairs': pairs, 'new_hole_pairs': new_pairs, 'via_in_pad': vip, 'pass': all(invariants.values()) and not any((regressions, new_hits, new_gaps, new_pairs))}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('before')
    parser.add_argument('after')
    parser.add_argument('output')
    parser.add_argument('--allow-zone-changes', action='store_true')
    parser.add_argument('--moved', default='')
    parser.add_argument('--allow-j8', action='store_true')
    parser.add_argument('--stackup8', action='store_true')
    args = parser.parse_args()
    before, after = Geometry(args.before), Geometry(args.after)
    result = compare(before, after, args.allow_zone_changes, tuple(r for r in args.moved.split(',') if r), args.allow_j8, args.stackup8)
    save_json(args.output, result)
    print(json.dumps({k: (len(v) if isinstance(v, list) else v) for k, v in result.items()}, indent=2), flush=True)
