"""Verify committed pipeline exports without Blender. No catalogue/coverage check yet (M8.2)."""

import hashlib
import json
from pathlib import Path
import sys

from game_data import facility_tiers

ART = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(art=ART):
    errors = []
    contract = json.loads((art / "contract.json").read_text())
    game_tiers = facility_tiers()
    for manifest_path in sorted(art.glob("**/*.json")):
        if manifest_path.parent == art:
            continue
        try:
            manifest = json.loads(manifest_path.read_text())
            identifier, kind = manifest["id"], manifest["kind"]
            if kind not in {"facility", "hull"}:
                raise ValueError("Station exports remain gated on M9 validation")
            folder = "facilities" if kind == "facility" else "hulls"
            if manifest_path != art / folder / identifier / f"{identifier}.json":
                raise ValueError("Identifier differs from file layout")
            if manifest["schema_version"] != 1:
                raise ValueError("Unsupported manifest version")
            source = manifest_path.with_suffix(".blend")
            glb, preview = source.with_suffix(".glb"), source.with_suffix(".png")
            if manifest["glb"] != glb.name or manifest["preview"] != preview.name:
                raise ValueError("Export filenames differ from identifier")
            if sha(glb) != manifest["sha256"] or sha(preview) != manifest["preview_sha256"]:
                raise ValueError("GLB or preview differs from checked export")
            for key, path in {
                "source": source, "contract": art / "contract.json",
                "materials": art / "library/materials.blend",
                "tool": art / "tools/frontier_art.py",
                "game_data_reader": art / "tools/game_data.py",
            }.items():
                if sha(path) != manifest["inputs"][key]:
                    raise ValueError(f"{key} changed; re-check and export the saved source")
            if kind == "facility":
                tiers = manifest["tiers"]
                if not isinstance(tiers, int) or tiers < 1:
                    raise ValueError("Invalid tier count")
                if identifier in game_tiers and tiers != game_tiers[identifier]:
                    raise ValueError("Tier count differs from gameplay rules")
                if identifier not in game_tiers and manifest["planned"] is not True:
                    raise ValueError("Unknown facility must remain planned")
                expected = {contract["roots"][k].format(id=identifier, tier=n): k
                            for n in range(1, tiers + 1) for k in ["facility", "tell"]}
            else:
                expected = {contract["roots"][kind].format(id=identifier): kind}
            if set(manifest["roots"]) != set(expected):
                raise ValueError("Missing or extra tier/tell roots")
            for name, root_kind in expected.items():
                record = manifest["roots"][name]
                if not 0 < record["triangles"] <= contract["triangle_budgets"][root_kind]:
                    raise ValueError(f"Triangle budget: {name}")
                if not record["materials"] or not set(record["materials"]) <= set(contract["materials"]):
                    raise ValueError(f"Unknown material: {name}")
                if name not in record["nodes"]:
                    raise ValueError(f"Missing root node: {name}")
        except (OSError, KeyError, ValueError, TypeError) as error:
            errors.append(f"{manifest_path.relative_to(art)}: {error}")
    # A .blend can be a work in progress; a GLB must always have its checked manifest.
    for glb in sorted(art.glob("**/*.glb")):
        if not glb.with_suffix(".json").exists():
            errors.append(f"{glb.relative_to(art)}: missing export manifest")
    return errors


if __name__ == "__main__":
    errors = verify()
    if errors:
        print("\n".join(errors), file=sys.stderr)
        sys.exit(1)
    print("Art export hashes, tiers, names and budgets verified")
