"""Verify committed pipeline exports without Blender. No catalogue/coverage check yet (M8.2)."""

import hashlib
import json
from pathlib import Path
import sys

from game_data import facility_definitions, npc_radii, station_geometry

ART = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def mouth_errors(bounds, art, contract):
    """As frontier_art's hull_fit: a flown hull passes the exterior's mouth flying straight in."""
    exterior = art / "station/exterior/exterior.json"
    if not exterior.exists():
        return []
    record = json.loads(exterior.read_text())["roots"][contract["roots"]["station_exterior"]]
    mouth = record["anchors"][contract["runtime_nodes"]["mouth"]]["properties"]
    clearance = 2 * contract["clearance"]
    if (bounds["max"][0] - bounds["min"][0] + clearance > mouth["opening_width"]
            or bounds["max"][2] - bounds["min"][2] + clearance > mouth["opening_height"]):
        return ["Flown hull cannot pass the station's mouth with the clearance"]
    return []


def station_mismatches(record, contract):
    """As frontier_art.station_rule_errors, from the manifest and today's rules."""
    rules = station_geometry()
    epsilon = contract["epsilon"] * 100
    mouth = record["anchors"].get(contract["runtime_nodes"]["mouth"], {})
    position = [mouth.get("matrix", [[0] * 4] * 3)[i][3] for i in range(3)] if mouth else None
    found = []
    if position is None or any(abs(a - b) > epsilon for a, b in zip(position, [0, rules["mouth_distance"], 0])):
        found.append("Exterior mouth differs from the rules' station")
    properties = mouth.get("properties", {})
    if any(abs(properties.get(key, 0) - rules[rule]) > epsilon
           for key, rule in [("opening_width", "mouth_width"), ("opening_height", "mouth_height")]):
        found.append("Exterior mouth opening differs from the rules' station")
    if not record.get("footprint_radius", float("inf")) <= rules["footprint_radius"] + epsilon:
        found.append("Exterior reaches beyond the rules' station footprint")
    return found


def verify(art=ART):
    errors = []
    contract = json.loads((art / "contract.json").read_text())
    definitions = facility_definitions()
    game_tiers = {name: data["tiers"] for name, data in definitions.items()}
    radii = npc_radii()
    seen = set()
    for manifest_path in sorted(art.glob("**/*.json")):
        if manifest_path.parent == art:
            continue
        try:
            manifest = json.loads(manifest_path.read_text())
            identifier, kind = manifest["id"], manifest["kind"]
            seen.add((kind, identifier))
            if kind not in {"facility", "hull", "npc", "station_exterior", "station_interior"}:
                raise ValueError("Unknown asset kind")
            folder = {"facility": "facilities", "hull": "hulls", "npc": "npcs"}.get(kind, "station")
            if manifest_path != art / folder / identifier / f"{identifier}.json":
                raise ValueError("Identifier differs from file layout")
            if kind == "hull" and identifier not in contract["hulls"]["flown"] + contract["hulls"]["display"]:
                raise ValueError("Unknown hull identifier in the contract")
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
                if identifier not in game_tiers:
                    raise ValueError("Unknown facility identifier in gameplay data")
                expected = {contract["roots"][k].format(id=identifier, tier=n): k
                            for n in range(1, tiers + 1) for k in ["facility", "tell"]}
            else:
                expected = {contract["roots"][kind].format(id=identifier): kind}
                if kind == "hull":
                    if identifier in contract["hulls"]["flown"]:
                        bounds = manifest["roots"][contract["roots"]["hull"].format(id=identifier)]["bounds"]
                        for error in mouth_errors(bounds, art, contract):
                            raise ValueError(error)
                if kind == "npc":
                    if identifier not in radii:
                        raise ValueError("Unknown NPC identifier in gameplay data")
                    record = manifest["roots"].get(contract["roots"]["npc"].format(id=identifier), {})
                    # Rechecked against today's profile: a smaller collision radius needs a new model.
                    if not record.get("footprint_radius", float("inf")) <= radii[identifier] + contract["epsilon"]:
                        raise ValueError("Footprint exceeds the NPC's collision radius")
                if kind == "station_exterior":
                    errors_here = station_mismatches(manifest["roots"][contract["roots"][kind]], contract)
                    if errors_here:
                        raise ValueError("; ".join(errors_here))
                if kind.startswith("station_"):
                    if identifier != kind.removeprefix("station_"):
                        raise ValueError("Invalid station identifier")
                    if sha(art / "tools/station_checks.py") != manifest["inputs"]["station_checks"]:
                        raise ValueError("station checks changed; re-export")
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
    for identifier, definition in definitions.items():
        if definition["art"] != "pending" and ("facility", identifier) not in seen:
            errors.append(f"facility {identifier}: missing ready facility asset")
    return errors


if __name__ == "__main__":
    errors = verify()
    if errors:
        print("\n".join(errors), file=sys.stderr)
        sys.exit(1)
    print("Art export hashes, tiers, names and budgets verified")
