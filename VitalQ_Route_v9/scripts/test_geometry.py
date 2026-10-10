import unittest
from shapely.geometry import Point
from geometry import Geometry, LAYERS


def fixture():
    return {'outline': [{'outer': [[0, 0], [10, 0], [10, 10], [0, 10]], 'holes': []}], 'pads': [], 'tracks': [], 'vias': [], 'keepouts': [], 'zones': []}


def via(net, xy, uid='via'):
    return {'uuid': uid, 'net': net, 'xy': xy, 'diameter': .4, 'drill': .2, 'layers': list(LAYERS)}


class GeometryTests(unittest.TestCase):
    def test_foreign_track_hole_clearance_all_layers(self):
        for layer in LAYERS:
            data = fixture()
            data['tracks'].append({'uuid': 't', 'net': 'OTHER', 'layer': layer, 'a': [5.32, 4], 'b': [5.32, 6], 'width': .1016})
            self.assertFalse(Geometry(data).via_allowed('N', .25, .15).covers(Point(5, 5)))

    def test_bga_pitch_legal_without_weakening_hole_rule(self):
        data = fixture()
        ring = list(Point(5.4, 5).buffer(.12, quad_segs=64).exterior.coords)
        data['pads'].append({'uuid': 'p', 'net': 'OTHER', 'xy': [5.4, 5], 'drill': [0, 0], 'rotation': 0, 'polys': {'0': [{'outer': ring, 'holes': []}]}})
        self.assertTrue(Geometry(data).via_allowed('N', .25, .15).covers(Point(5, 5)))
        self.assertFalse(Geometry(data).via_allowed('N', .25, .15).covers(Point(5.01, 5)))

    def test_same_net_drills_still_require_spacing(self):
        data = fixture()
        data['vias'].append(via('N', [5.399, 5]))
        self.assertFalse(Geometry(data).via_allowed('N').covers(Point(5, 5)))
        self.assertTrue(Geometry(data).via_allowed('N').covers(Point(4.99, 5)))

    def test_keepout_applies_to_entire_via(self):
        data = fixture()
        data['keepouts'].append({'uuid': 'ko', 'layers': [6], 'outline': [{'outer': [[5, 4], [6, 4], [6, 6], [5, 6]], 'holes': []}], 'vias': True, 'tracks': True})
        self.assertFalse(Geometry(data).via_allowed('N').covers(Point(4.85, 5)))
        self.assertTrue(Geometry(data).via_allowed('N').covers(Point(4.79, 5)))

    def test_exact_route_endpoints(self):
        g = Geometry(fixture())
        self.assertEqual(g.path('N', 0, [1.01, 1.02], [8.03, 8.04], [0, 0, 10, 10]), [[1.01, 1.02], [8.03, 8.04]])


if __name__ == '__main__':
    unittest.main()
