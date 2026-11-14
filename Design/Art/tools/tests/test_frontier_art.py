"""Run inside Blender; test geometry is disposable and never added to game assets."""

import contextlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import frontier_art as art
from game_data import facility_tiers


def cube(root, name, size, position=(0, 0, 0), material=None):
    bpy.ops.mesh.primitive_cube_add(size=1)
    obj = bpy.context.object
    obj.name = name
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    bpy.data.collections["Export"].objects.link(obj)
    obj.parent = root
    obj.scale = size
    obj.location = position
    obj.data.materials.append(material or bpy.data.materials["Armor"])
    return obj


class PipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="frontier-art-tests-")
        cls.art = Path(cls.temp.name)
        (cls.art / "library").mkdir()
        shutil.copyfile(art.ART / "library/materials.blend", cls.art / "library/materials.blend")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def fixture(self, kind="facility"):
        folder = self.art / ("facilities/refinery" if kind == "facility" else "hulls/frigate")
        shutil.rmtree(folder, ignore_errors=True)
        path = art.new_asset(kind, "refinery" if kind == "facility" else "frigate", art=self.art)
        if kind == "facility":
            for tier in [1, 2]:
                root = bpy.data.objects[f"Facility_refinery_T{tier}"]
                height = 1 + (tier - 1) * .6
                cube(root, f"body{tier}", (1, 1, height), (0, 0, height / 2))
                for n in range(1, tier + 1):
                    cube(root, art.operating_name(n, root), (.05, .05, .05), (0, 0, .1))
                tell = bpy.data.objects[f"Tell_refinery_T{tier}"]
                size = tier * .3
                cube(tell, f"tell{tier}", (size, size, size), (0, 0, size / 2))
        else:
            cube(bpy.data.objects["Hull_frigate"], "hull", (1, 2, 1))
        bpy.context.view_layer.update()
        return path

    def assertFailure(self, code):
        bpy.context.view_layer.update()
        errors, _ = art.check_asset(self.art)
        self.assertTrue(any(e.startswith(code + ":") for e in errors), errors)

    def test_game_tiers(self):
        self.assertEqual(facility_tiers(), {"sensor_array": 2, "refinery": 2, "fabrication_bay": 2})

    def test_good_facility_and_hull(self):
        for kind in ["facility", "hull"]:
            with self.subTest(kind=kind):
                self.fixture(kind)
                self.assertEqual(art.check_asset(self.art)[0], [])

    def test_invalid_rules(self):
        cases = {
            "units": lambda: setattr(bpy.context.scene.unit_settings, "scale_length", .01),
            "contract": lambda: bpy.context.scene.__setitem__("frontier_contract", -1),
            "tiers": lambda: bpy.context.scene.__setitem__("frontier_tiers", 3),
            "roots": lambda: setattr(bpy.data.objects["Tell_refinery_T2"], "name", "wrong"),
            "origin": lambda: setattr(bpy.data.objects["Facility_refinery_T1"].location, "x", 1),
            "metadata": lambda: bpy.data.objects["Facility_refinery_T1"].__setitem__("frontier_id", "wrong"),
            "tier": lambda: bpy.data.objects["Facility_refinery_T1"].__setitem__("tier", 2),
            "envelope": lambda: setattr(bpy.data.objects["body1"].scale, "x", 3),
            "base": lambda: setattr(bpy.data.objects["body1"].location, "z", .3),
            "tier_step": lambda: setattr(bpy.data.objects["body2"].scale, "z", 1.2),
            "tell": lambda: setattr(bpy.data.objects["tell1"].scale, "x", 2),
            "tell_growth": lambda: setattr(bpy.data.objects["tell2"], "scale", (.1, .1, .1)),
            "runtime": lambda: bpy.data.objects.remove(bpy.data.objects["Operating_Tier_1__Facility_refinery_T2"], do_unlink=True),
            "materials": lambda: bpy.data.objects["body1"].data.materials.clear(),
            "visibility": lambda: setattr(bpy.data.objects["body1"], "hide_render", True),
            "animation": lambda: bpy.data.objects["body1"].constraints.new("COPY_LOCATION"),
            "budget": lambda: setattr(bpy.data.objects["body1"].modifiers.new("Too dense", "SUBSURF"), "levels", 6),
            "geometry": lambda: bpy.data.objects.remove(bpy.data.objects["tell1"], do_unlink=True),
        }
        for code, mutate in cases.items():
            with self.subTest(rule=code):
                self.fixture()
                mutate()
                self.assertFailure(code)

    def test_local_material_rejected(self):
        self.fixture()
        bpy.data.objects["body1"].data.materials[0] = bpy.data.materials.new("Impostor")
        self.assertFailure("materials")

    def test_non_mesh_export_rejected(self):
        self.fixture()
        camera = bpy.data.objects.new("BadCamera", bpy.data.cameras.new("BadCamera"))
        bpy.data.collections["Export"].objects.link(camera)
        camera.parent = bpy.data.objects["Facility_refinery_T1"]
        self.assertFailure("type")

    def test_hidden_export_collection_rejected(self):
        self.fixture()
        bpy.data.collections["Export"].hide_render = True
        self.assertFailure("visibility")

    def test_template_retains_library_on_reopen(self):
        path = art.asset_path("hull", "template_test", self.art)
        shutil.rmtree(path.parent, ignore_errors=True)
        art.new_asset("hull", "template_test", art=self.art)
        bpy.ops.wm.open_mainfile(filepath=str(path))
        self.assertTrue(all(role in bpy.data.materials for role in art.CONTRACT["materials"]))
        for role in art.CONTRACT["materials"]:
            self.assertIsNotNone(bpy.data.materials[role].library)

    def test_modifier_visibility_mismatch_rejected(self):
        self.fixture()
        modifier = bpy.data.objects["body1"].modifiers.new("Mismatch", "BEVEL")
        modifier.show_render = False
        self.assertFailure("geometry")

    def test_evaluated_modifier_envelope(self):
        self.fixture()
        array = bpy.data.objects["body1"].modifiers.new("Expand", "ARRAY")
        array.count = 3
        self.assertFailure("envelope")

    def test_check_writes_nothing_and_new_refuses_overwrite(self):
        source = self.fixture()
        before = {p: (p.stat().st_mtime_ns, art.sha(p)) for p in self.art.rglob("*") if p.is_file()}
        art.check_asset(self.art)
        after = {p: (p.stat().st_mtime_ns, art.sha(p)) for p in self.art.rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        with self.assertRaisesRegex(ValueError, "overwrite"):
            art.new_asset("facility", "refinery", art=self.art)
        self.assertTrue(source.exists())

    def test_planned_requires_explicit_tiers(self):
        with self.assertRaisesRegex(ValueError, "--tiers"):
            art.new_asset("facility", "not_yet_in_game", art=self.art)
        with self.assertRaisesRegex(ValueError, "differs"):
            art.new_asset("facility", "sensor_array", tiers=3, art=self.art)

    def test_stations_are_explicitly_gated(self):
        for kind, identifier in [("station_exterior", "exterior"), ("station_interior", "interior")]:
            path = art.asset_path(kind, identifier, self.art)
            shutil.rmtree(path.parent, ignore_errors=True)
            art.new_asset(kind, identifier, art=self.art)
            self.assertFailure("station_pending")

    def test_export_and_idempotence(self):
        source = self.fixture()
        bpy.ops.wm.save_as_mainfile(filepath=str(source))
        bpy.ops.wm.open_mainfile(filepath=str(source))
        manifest_path = art.export_asset(self.art)
        manifest = json.loads(manifest_path.read_text())
        self.assertEqual(manifest["sha256"], art.sha(source.with_suffix(".glb")))
        self.assertEqual(len(manifest["roots"]), 4)
        # Inspect real exported nodes, not only the Python manifest.
        import struct
        blob = source.with_suffix(".glb").read_bytes()
        length = struct.unpack_from("<I", blob, 12)[0]
        gltf = json.loads(blob[20:20 + length])
        names = {node.get("name") for node in gltf["nodes"]}
        self.assertTrue(set(manifest["roots"]) <= names)
        self.assertNotIn("TacticalCamera", names)
        self.assertTrue(all("camera" not in node for node in gltf["nodes"]))
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.gltf(filepath=str(source.with_suffix(".glb")))
        bpy.context.view_layer.update()
        for name, record in manifest["roots"].items():
            imported = art.geometry(bpy.data.objects[name])
            self.assertEqual(imported["triangles"], record["triangles"])
            for side in ["min", "max"]:
                for actual, expected in zip(imported["bounds"][side], record["bounds"][side]):
                    self.assertAlmostEqual(actual, expected, places=4)
        # Reopen to discard temporary preview layout and restore saved source state.
        bpy.ops.wm.open_mainfile(filepath=str(source))
        before = {p: p.stat().st_mtime_ns for p in source.parent.iterdir() if p.suffix in {".glb", ".json", ".png"}}
        with contextlib.redirect_stdout(io.StringIO()) as output:
            art.export_asset(self.art)
        self.assertIn("UNCHANGED", output.getvalue())
        self.assertEqual(before, {p: p.stat().st_mtime_ns for p in before})
        bpy.data.objects["body1"].scale.x = 9
        bpy.ops.wm.save_as_mainfile(filepath=str(source))
        with self.assertRaises(ValueError):
            art.export_asset(self.art)
        self.assertEqual(before, {p: p.stat().st_mtime_ns for p in before})


if __name__ == "__main__":
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PipelineTests))
    sys.exit(0 if result.wasSuccessful() else 1)
