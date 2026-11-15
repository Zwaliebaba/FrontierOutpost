"""Ordinary Python tests for the CI verifier; no bpy or Blender required."""

import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_exports


class ExportChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.art = Path(self.temp.name) / "Art"
        shutil.copytree(check_exports.ART, self.art, ignore=shutil.ignore_patterns("__pycache__", "*.blend1", "*.blend2"))
        # The frozen station's berths sit beside the art, as in the repository.
        (self.art.parent / "FrontierOutpost").mkdir()
        shutil.copyfile(check_exports.ART.parent / "FrontierOutpost/berths.json",
                        self.art.parent / "FrontierOutpost/berths.json")
        self.manifest = self.art / "facilities/crew_quarters/crew_quarters.json"

    def test_good_committed_exports(self):
        self.assertEqual(check_exports.verify(self.art), [])

    def test_tampered_glb(self):
        with self.manifest.with_suffix(".glb").open("ab") as output:
            output.write(b"tampered")
        self.assertIn("differs", " ".join(check_exports.verify(self.art)))

    def test_missing_tier(self):
        manifest = json.loads(self.manifest.read_text())
        del manifest["roots"]["Tell_crew_quarters_T2"]
        self.manifest.write_text(json.dumps(manifest))
        self.assertIn("Missing or extra", " ".join(check_exports.verify(self.art)))

    def test_missing_source(self):
        self.manifest.with_suffix(".blend").unlink()
        self.assertTrue(check_exports.verify(self.art))

    def test_unchecked_glb(self):
        self.manifest.unlink()
        self.assertIn("missing export manifest", " ".join(check_exports.verify(self.art)))

    def test_a_ready_facility_cannot_be_omitted(self):
        shutil.rmtree(self.manifest.parent)
        self.assertIn("missing ready facility asset", " ".join(check_exports.verify(self.art)))

    def test_pending_facilities_can_merge_before_their_art(self):
        shutil.rmtree(self.manifest.parent)
        definitions = check_exports.facility_definitions()
        definitions["crew_quarters"]["art"] = "pending"
        with patch("check_exports.facility_definitions", return_value=definitions):
            self.assertEqual(check_exports.verify(self.art), [])

    def test_stale_contract(self):
        with (self.art / "contract.json").open("a") as output:
            output.write("\n")
        self.assertIn("contract changed", " ".join(check_exports.verify(self.art)))

    def npc_manifest(self):
        manifest = json.loads((self.art / "npcs/raider/raider.json").read_text())
        return manifest, manifest["roots"]["Npc_raider"]

    def test_npc_footprint_must_fit_its_collision_radius(self):
        manifest, record = self.npc_manifest()
        record["footprint_radius"] = 1.31
        (self.art / "npcs/raider/raider.json").write_text(json.dumps(manifest))
        self.assertIn("collision radius", " ".join(check_exports.verify(self.art)))

    def test_npc_footprint_rechecked_when_the_profile_shrinks(self):
        with patch("check_exports.npc_radii", return_value={"raider": 1.0}):
            self.assertIn("collision radius", " ".join(check_exports.verify(self.art)))

    def test_npc_identifier_must_be_a_ship_kind(self):
        with patch("check_exports.npc_radii", return_value={"drone": 0.9}):
            self.assertIn("Unknown NPC", " ".join(check_exports.verify(self.art)))

    def hull_manifest(self, identifier):
        path = self.art / f"hulls/{identifier}/{identifier}.json"
        return path, json.loads(path.read_text())

    def test_flown_hull_rechecked_against_the_berths(self):
        path, manifest = self.hull_manifest("frigate")
        # 3.65 m of beam on a 3.6 m berth.
        manifest["roots"]["Hull_frigate"]["bounds"]["max"][0] = 3.0
        path.write_text(json.dumps(manifest))
        self.assertIn("fit a berth", " ".join(check_exports.verify(self.art)))

    def test_display_hull_is_not_held_to_a_berth(self):
        # The cruiser is about four times the berth's length, and shown, never landed.
        self.assertEqual(check_exports.verify(self.art), [])
        _, manifest = self.hull_manifest("cruiser")
        bounds = manifest["roots"]["Hull_cruiser"]["bounds"]
        self.assertGreater(bounds["max"][1] - bounds["min"][1], 3.6)

    def test_hull_must_be_listed_in_the_contract(self):
        contract = json.loads((self.art / "contract.json").read_text())
        contract["hulls"]["display"] = []
        (self.art / "contract.json").write_text(json.dumps(contract))
        self.assertIn("Unknown hull", " ".join(check_exports.verify(self.art)))


if __name__ == "__main__":
    unittest.main()
