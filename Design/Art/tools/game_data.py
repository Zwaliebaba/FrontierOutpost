"""Read the current literal facility definitions without maintaining a second tier list.

This deliberately accepts only the existing STAGE1 literal layout. A future data-driven
rules format should replace this bridge; an unrecognised layout fails rather than guesses.
"""

import re
from pathlib import Path


def facility_definitions(repo=None):
    repo = Path(repo) if repo else Path(__file__).resolve().parents[3]
    source = (repo / "crates/sim/src/outpost.rs").read_text(encoding="utf-8")
    source = re.sub(r"//[^\n]*", "", source)
    source = source.split("pub const STAGE1: OutpostRules = OutpostRules {", 1)[1]
    source = source.split("facilities: &[", 1)[1].split("adjacency:", 1)[0]
    result = {}
    for block in source.split("FacilityRule {")[1:]:
        identifier = re.search(r'\bid:\s*"([a-z][a-z0-9_]*)"', block)
        tiers = len(re.findall(r"\bTierRule\s*\{", block))
        if not identifier or not tiers or identifier[1] in result:
            raise ValueError("Unsupported STAGE1 facility data; update the art data reader")
        state = re.search(r"\bart:\s*Art::(Ready|Pending)", block)
        if not state:
            raise ValueError("Facility must declare art: Art::Ready or Art::Pending")
        result[identifier[1]] = {"tiers": tiers, "art": state[1].lower()}
    if not result:
        raise ValueError("No facilities found in STAGE1")
    return result


def facility_tiers(repo=None):
    return {name: definition["tiers"] for name, definition in facility_definitions(repo).items()}


def npc_radii(repo=None):
    """Collision radius by NPC identifier, from frontier_sim's literal profiles.

    An identifier is the lowercase `ShipKind` (`raider`), as the wire's `EntityKind` spells it.
    The pilot's profile is not an NPC. Like the facility reader, it fails rather than guesses.
    """
    repo = Path(repo) if repo else Path(__file__).resolve().parents[3]
    source = (repo / "crates/sim/src/profile.rs").read_text(encoding="utf-8")
    source = re.sub(r"//[^\n]*", "", source)
    constants = {"crate::collision::SHIP_RADIUS": None}
    radius = re.search(r"pub const SHIP_RADIUS: f32 = ([0-9.]+);",
                       (repo / "crates/sim/src/collision.rs").read_text(encoding="utf-8"))
    if radius:
        constants["crate::collision::SHIP_RADIUS"] = float(radius[1])
    result = {}
    for name, body in re.findall(r"\bconst ([A-Z]+): Profile = Profile \{(.*?)\n\};", source, re.S):
        value = re.search(r"\bradius:\s*([0-9.]+|crate::collision::SHIP_RADIUS)\s*,", body)
        if not value:
            raise ValueError(f"Unsupported profile {name}; update the art data reader")
        number = constants.get(value[1]) if value[1] in constants else float(value[1])
        if number is None or number <= 0:
            raise ValueError(f"Unsupported radius in profile {name}")
        if name != "PILOT":
            result[name.lower()] = number
    if "raider" not in result:
        raise ValueError("No NPC profiles found in frontier_sim")
    return result


STATION_CONSTANTS = ("MOUTH_DISTANCE", "MOUTH_WIDTH", "MOUTH_HEIGHT", "LANE_LENGTH", "FOOTPRINT_RADIUS")


def station_geometry(repo=None):
    """The station's gameplay geometry, from frontier_sim's literal constants (station.rs).

    The rules own the mouth, the lane and the footprint; the exterior model is checked against
    them, never the other way round. Fails rather than guesses if a constant is missing.
    """
    repo = Path(repo) if repo else Path(__file__).resolve().parents[3]
    source = (repo / "crates/sim/src/station.rs").read_text(encoding="utf-8")
    result = {}
    for name in STATION_CONSTANTS:
        match = re.search(rf"\bpub const {name}: f32 = ([0-9.]+);", source)
        if not match:
            raise ValueError(f"No {name} in frontier_sim's station.rs; update the art data reader")
        result[name.lower()] = float(match[1])
    return result
