"""Assemble the runtime catalogue from verified manifests, without Blender."""

import argparse
import json
from pathlib import Path
import sys

from check_exports import ART, sha, verify
from game_data import facility_definitions


def assemble(art=ART):
    errors = verify(art)
    if errors:
        raise ValueError("\n".join(errors))
    definitions = facility_definitions()
    assets = {}
    for path in sorted(art.glob("**/*.json")):
        if path.parent == art:
            continue
        manifest = json.loads(path.read_text())
        kind, identifier = manifest["kind"], manifest["id"]
        assets[f"{kind}:{identifier}"] = {
            "kind": kind, "id": identifier,
            "glb": path.with_suffix(".glb").relative_to(art).as_posix(),
            "sha256": manifest["sha256"],
            "pending": kind == "facility" and definitions[identifier]["art"] == "pending",
            "roots": manifest["roots"],
        }
    # Frozen assets also pass through the catalogue until their planned migration.
    frozen = art.parent / "FrontierOutpost"
    legacy = {}
    for identifier, root in [("station", "Station_ROOT"), ("frigate", "Frigate_ROOT"),
                             ("asteroids", None), ("cruiser", "Cruiser_ROOT")]:
        path = frozen / f"{identifier}.glb"
        legacy[identifier] = {"glb": "../FrontierOutpost/" + path.name,
                              "sha256": sha(path), "root": root}
    return {"schema_version": 1, "assets": assets, "legacy": legacy}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if the committed catalogue is stale")
    args = parser.parse_args()
    value = json.dumps(assemble(), indent=2, sort_keys=True) + "\n"
    path = ART / "catalogue.json"
    if args.check:
        if not path.exists() or path.read_text() != value:
            raise ValueError("catalogue.json is stale; run python3 Design/Art/tools/catalogue.py")
    else:
        path.write_text(value, encoding="utf-8")
    print("Art catalogue verified" if args.check else "Art catalogue assembled")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
