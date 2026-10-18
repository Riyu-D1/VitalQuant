import unittest
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from shapely.geometry import Point
from route_geometry import Geometry

ROOT = Path(__file__).resolve().parents[1]


def polygon(x0, y0, x1, y1, holes=None):
    return {"outer": [[x0,y0],[x1,y0],[x1,y1],[x0,y1]], "holes": holes or []}


def track(uid, net, layer, a, b):
    return {"uuid": uid, "net": net, "layer": layer, "a": a, "b": b, "width": 0.1}


class GeometryTests(unittest.TestCase):
    def geometry(self, **extra):
        base = {"outline": [polygon(0,0,10,10)], "pads": [], "vias": [], "tracks": [], "zones": [], "keepouts": []}
        base.update(extra)
        temp = TemporaryDirectory(dir=ROOT / "backups")
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "geometry.json"
        path.write_text(json.dumps(base))
        return Geometry(path)

    def test_compressed_delivery_geometry_is_readable(self):
        import gzip
        g = self.geometry(tracks=[track("own", "A", 0, [1,5], [9,5])])
        temp = TemporaryDirectory(dir=ROOT / "backups")
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "geometry.json"
        with gzip.open(str(path)+".gz", "wt") as stream:
            json.dump(g.raw, stream)
        self.assertEqual(Geometry(path).raw, g.raw)

    def test_foreign_overlap_not_removed_by_own_copper(self):
        g = self.geometry(tracks=[track("own", "A", 0, [1,5], [9,5]), track("foreign", "B", 0, [5,1], [5,9])])
        self.assertFalse(g.obstacles("A",0).covers(Point(5,5)))
        self.assertTrue(g.obstacles("A",0).covers(Point(2,5)))

    def test_crossing_layers_do_not_connect_without_via(self):
        g = self.geometry(tracks=[track("front", "A", 0, [1,5], [9,5]), track("back", "A", 2, [5,1], [5,9])])
        self.assertEqual(len(g.components("A")[0]), 2)

    def test_existing_via_connects_layers(self):
        g = self.geometry(tracks=[track("front", "A", 0, [1,5], [9,5]), track("back", "A", 2, [5,1], [5,9])],
                          vias=[{"uuid":"v", "net":"A", "xy":[5,5], "diameter":0.3, "drill":0.15, "layers":[0,2,4,6,8,10]}])
        self.assertEqual(len(g.components("A")[0]), 1)
        self.assertFalse(g.via_allowed("A").covers(Point(5,5)))

    def test_through_via_checks_foreign_inner_copper(self):
        g = self.geometry(tracks=[track("inner", "B", 8, [1,5], [9,5])])
        self.assertFalse(g.via_allowed("A").covers(Point(5,5)))
        self.assertTrue(g.via_allowed("A").covers(Point(5,6)))

    def test_small_via_enforces_hole_to_track_clearance(self):
        g = self.geometry(tracks=[track("foreign", "B", 6, [1,5], [9,5])])
        self.assertFalse(g.via_allowed("A").covers(Point(5,5.31)))
        self.assertTrue(g.via_allowed("A").covers(Point(5,5.34)))

    def test_track_enforces_clearance_to_small_via_hole(self):
        g = self.geometry(vias=[{"uuid":"v", "net":"B", "xy":[5,5], "diameter":0.3, "drill":0.15, "layers":[0,2,4,6,8,10]}])
        self.assertFalse(g.obstacles("A",0).covers(Point(5,5.31)))
        self.assertTrue(g.obstacles("A",0).covers(Point(5,5.35)))

    def test_keepout_checks_via_body_not_only_center(self):
        keepout = {"uuid":"ko", "layers":[0], "tracks":False, "vias":True, "outline":[polygon(4,4,6,6)]}
        g = self.geometry(keepouts=[keepout])
        self.assertFalse(g.via_allowed("A").covers(Point(3.9,5)))
        self.assertTrue(g.via_allowed("A").covers(Point(3.8,5)))

    def test_slot_is_not_routable(self):
        slot = polygon(4,4,6,6)["outer"]
        g = self.geometry(outline=[polygon(0,0,10,10,[slot])])
        self.assertFalse(g.obstacles("A",0).covers(Point(5,5)))
        self.assertFalse(g.via_allowed("A").covers(Point(3.7,5)))

    def test_j8_exception_is_ball_and_net_specific(self):
        area = polygon(4,4,6,6)
        policy = {"original_outline":[area], "balls":[{"net":"A", "eligible":True, "pad_polygons":[polygon(4.9,4.9,5.1,5.1)]}]}
        g = self.geometry(keepouts=[{"uuid":"j8", "parent":"J8", "layers":[0], "vias":True, "tracks":False, "outline":[area]}], j8_pofv_policy=policy)
        self.assertTrue(g.via_allowed("A").covers(Point(5,5)))
        self.assertFalse(g.via_allowed("B").covers(Point(5,5)))
        self.assertFalse(g.via_allowed("A").covers(Point(5.3,5)))
        self.assertFalse(g.via_allowed("A",diameter=0.4,drill=0.2).covers(Point(5,5)))

    def test_multilayer_route_crosses_wall_with_two_legal_vias(self):
        from multilayer_route import route_components
        g = self.geometry(tracks=[track("start", "A", 0, [1,1], [1.5,1]), track("goal", "A", 0, [8.5,1], [9,1]), track("wall", "B", 0, [5,0], [5,10])])
        comps, ids = g.components("A")
        route, error = route_components(g, "A", comps[ids["start"]], comps[ids["goal"]], res=0.1)
        self.assertIsNone(error)
        self.assertEqual(len(route["vias"]), 2)
        for via in route["vias"]:
            self.assertTrue(g.via_allowed("A").covers(Point(via["xy"])))

    def test_zone_hole_does_not_connect_a_pad_or_track(self):
        zone = {"uuid":"zone", "net":"A", "fill":{"0":[polygon(1,1,9,9,[polygon(4,4,6,6)["outer"]])]}}
        g = self.geometry(tracks=[track("island", "A", 0, [4.5,5], [5.5,5])], zones=[zone])
        self.assertEqual(len(g.components("A")[0]), 2)


class SurfaceRouteTests(unittest.TestCase):
    def test_path_reaches_exact_goal_geometry(self):
        from shapely.geometry import box, LineString
        from surface_route import route_surface
        allowed = box(0,0,5,5).difference(box(2,0,3,4))
        goal = Point(4,1).buffer(0.03)
        path = route_surface(allowed, [(1.013,1.017)], goal, (0,0,5,5))
        self.assertIsNotNone(path)
        self.assertEqual(path[0], (1.013,1.017))
        self.assertTrue(goal.covers(Point(path[-1])))
        self.assertTrue(allowed.covers(LineString(path)))

    def test_narrow_diagonal_uses_exact_segment_clearance(self):
        from shapely.geometry import LineString
        from surface_route import route_surface
        allowed = LineString([(0,0),(1,1)]).buffer(0.02)
        goal = Point(0.9,0.9).buffer(0.02)
        path = route_surface(allowed, [(0.1,0.1)], goal, (0,0,1,1))
        self.assertIsNotNone(path)
        self.assertTrue(allowed.covers(LineString(path)))

    def test_blocked_start_is_not_forced_free(self):
        from shapely.geometry import box
        from surface_route import route_surface
        allowed = box(0,0,5,5).difference(box(1,1,2,2))
        self.assertIsNone(route_surface(allowed, [(1.98,1.5)], Point(4,4).buffer(0.1), (0,0,5,5)))


if __name__ == "__main__":
    unittest.main()
