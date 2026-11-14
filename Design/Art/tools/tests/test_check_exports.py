"""Ordinary Python tests for the CI verifier; no bpy or Blender required."""

import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_exports


class ExportChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.art = Path(self.temp.name) / "Art"
        shutil.copytree(check_exports.ART, self.art, ignore=shutil.ignore_patterns("__pycache__", "*.blend1", "*.blend2"))
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

    def test_stale_contract(self):
        with (self.art / "contract.json").open("a") as output:
            output.write("\n")
        self.assertIn("contract changed", " ".join(check_exports.verify(self.art)))


if __name__ == "__main__":
    unittest.main()
