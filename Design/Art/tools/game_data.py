"""Read the current literal facility definitions without maintaining a second tier list.

This deliberately accepts only the existing STAGE1 literal layout. A future data-driven
rules format should replace this bridge; an unrecognised layout fails rather than guesses.
"""

import re
from pathlib import Path


def facility_tiers(repo=None):
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
        result[identifier[1]] = tiers
    if not result:
        raise ValueError("No facilities found in STAGE1")
    return result
