import math
import unittest
from shapely.geometry import Point, LineString
from r2route import Board, route, LAYERS


def ring(xy, r=.15):
    return list(Point(xy).buffer(r, quad_segs=16).exterior.coords)


def base():
    return {'outline': [{'outer': [[0, 0], [12, 0], [12, 8], [0, 8]], 'holes': []}], 'pads': [], 'tracks': [], 'vias': [], 'keepouts': [], 'zones': []}


def pad(uid, net, xy, layer=0, r=.15, ref='R1', num='1'):
    return {'uuid': uid, 'net': net, 'xy': xy, 'drill': [0, 0], 'rotation': 0, 'ref': ref, 'number': num, 'polys': {str(layer): [{'outer': ring(xy, r), 'holes': []}]}}


class RouterTests(unittest.TestCase):
    def check(self, b, net, res):
        for t in res['tracks']:
            self.assertTrue(b.allowed_local(net, t['layer'], [0, 0, 12, 8]).covers(LineString([t['a'], t['b']])))
        for v in res['vias']:
            self.assertEqual(b.via_issues(net, v['xy'], v['diameter'], v['drill']), [])

    def test_wall_forces_inner_layer_and_vias(self):
        d = base()
        d['pads'] += [pad('a', 'N', [1, 4]), pad('b', 'N', [11, 4])]
        d['tracks'].append({'uuid': 'wall', 'net': 'X', 'layer': 0, 'a': [6, 0.2], 'b': [6, 7.8], 'width': 1.0})
        d['tracks'].append({'uuid': 'wallB', 'net': 'X', 'layer': 2, 'a': [6, 0.2], 'b': [6, 7.8], 'width': 1.0})
        b = Board(d)
        comps, ids = b.components('N')
        res, err = route(b, 'N', comps[ids['a']], comps[ids['b']], [0, 0, 12, 8])
        self.assertIsNone(err)
        self.assertTrue(any(t['layer'] in (6, 8) for t in res['tracks']))
        self.assertGreaterEqual(len(res['vias']), 2)
        self.check(b, 'N', res)

    def test_keepout_blocks_inner_and_vias(self):
        d = base()
        d['pads'] += [pad('a', 'N', [1, 4]), pad('b', 'N', [11, 4])]
        for l in (0, 2):
            d['tracks'].append({'uuid': 'w' + str(l), 'net': 'X', 'layer': l, 'a': [6, 0.2], 'b': [6, 7.8], 'width': 1.0})
        d['keepouts'].append({'uuid': 'k', 'name': 'hv_inner', 'layers': [4, 6, 8, 10], 'outline': [{'outer': [[4, -1], [8, -1], [8, 9], [4, 9]], 'holes': []}], 'vias': True, 'tracks': True})
        b = Board(d)
        comps, ids = b.components('N')
        res, err = route(b, 'N', comps[ids['a']], comps[ids['b']], [0, 0, 12, 8])
        self.assertIsNone(res)

    def test_incremental_add_blocks_next_route(self):
        d = base()
        d['pads'] += [pad('a', 'N', [1, 4]), pad('b', 'N', [11, 4]), pad('c', 'M', [6, 1]), pad('e', 'M', [6, 7])]
        b = Board(d)
        comps, ids = b.components('N')
        res, err = route(b, 'N', comps[ids['a']], comps[ids['b']], [0, 0, 12, 8], layers=(0,))
        self.assertIsNone(err)
        b.add_items(res['tracks'], res['vias'])
        comps, ids = b.components('M')
        res2, err2 = route(b, 'M', comps[ids['c']], comps[ids['e']], [0, 0, 12, 8], layers=(0,))
        self.assertIsNone(err2)
        self.check(b, 'M', res2)
        self.assertGreater(min(math.dist(t['a'], [1, 4]) for t in res2['tracks']), 0.25)
        res3, err3 = route(b, 'M', comps[ids['c']], comps[ids['e']], [0, 0, 12, 8])
        self.assertIsNone(err3)
        self.check(b, 'M', res3)


if __name__ == '__main__':
    unittest.main()
