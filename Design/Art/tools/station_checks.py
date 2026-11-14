"""Conservative station clearance and tactical-view checks, using evaluated bounds.

Boxes may reject tightly interleaved detail; they never waive a clearance because an
unmodelled hull or module happens not to be installed in the review scene.
"""

import itertools
import json

import bpy
from mathutils import Vector

import frontier_art as art


def hulls(art_root=art.ART):
    result = {}
    frozen = json.loads((art.ART.parent / "FrontierOutpost/scene_manifest.json").read_text())
    bounds = frozen["docking_validation"]["ship_bounds_station_space"]
    result["frigate"] = [bounds["max"][i] - bounds["min"][i] for i in range(3)]
    for path in (art_root / "hulls").glob("*/*.json"):
        manifest = json.loads(path.read_text())
        bounds = next(iter(manifest["roots"].values()))["bounds"]
        result[manifest["id"]] = [bounds["max"][i] - bounds["min"][i] for i in range(3)]
    return result


def corners(lo, hi):
    return [Vector(p) for p in itertools.product(*zip(lo, hi))]


def overlaps(a, b, axes=range(3)):
    epsilon = art.CONTRACT["epsilon"]
    return all(a[0][i] < b[1][i] - epsilon and b[0][i] < a[1][i] - epsilon for i in axes)


def projected(box):
    view = Vector(art.CONTRACT["camera_direction"]).normalized()
    right = Vector((0, 0, 1)).cross(view).normalized()
    up = view.cross(right).normalized()
    points = [Vector((p.dot(right), p.dot(up), p.dot(view))) for p in corners(*box)]
    return ([min(p[i] for p in points) for i in range(3)], [max(p[i] for p in points) for i in range(3)])


def check(root, kind, art_root=art.ART):
    errors = []
    nodes = art.CONTRACT["runtime_nodes"]
    children = {o.name: o for o in root.children_recursive}
    sizes = hulls(art_root)
    clearance = art.CONTRACT["clearance"]
    boxes = []
    for obj in root.children_recursive:
        if obj.type == "MESH":
            record = art.geometry(obj)
            if record["bounds"]:
                # geometry(obj) is in obj-local space; include its transform and modifiers.
                points = [root.matrix_world.inverted() @ obj.matrix_world @ p for p in corners(record["bounds"]["min"], record["bounds"]["max"])]
                boxes.append((obj.name, ([min(p[i] for p in points) for i in range(3)], [max(p[i] for p in points) for i in range(3)])))
    required = [nodes["mouth"], *nodes["tells"]] if kind == "station_exterior" else [*nodes["sockets"], *("Parking_" + id for id in sizes)]
    for name in required:
        anchor = children.get(name)
        if anchor is None or anchor.type != "EMPTY":
            errors.append(f"anchors: missing empty {name}")
        elif any(abs(v) > art.CONTRACT["epsilon"] for v in anchor.rotation_euler) or any(abs(v - 1) > art.CONTRACT["epsilon"] for v in anchor.scale):
            errors.append(f"anchors: {name} must face +Y with scale one")
    if errors:
        return errors
    if kind == "station_exterior":
        mouth = children[nodes["mouth"]]
        at = (root.matrix_world.inverted() @ mouth.matrix_world).translation
        width, height = mouth.get("opening_width", 0), mouth.get("opening_height", 0)
        if width < max(art.CONTRACT["hangar"]["min_width"], max(s[0] for s in sizes.values()) + clearance * 2) or height < max(art.CONTRACT["hangar"]["min_height"], max(s[2] for s in sizes.values()) + clearance * 2):
            errors.append("mouth: opening must fit every catalogue hull with clearance")
        lane = ([at.x - width / 2, at.y, at.z], [at.x + width / 2, at.y + art.CONTRACT["hangar"]["approach_length"], at.z + height])
        for name, box in boxes:
            if overlaps(lane, box):
                errors.append(f"lane: {name} obstructs the hangar approach")
        tell_size = art.CONTRACT["tell"]["cube_size"]
        for name in nodes["tells"]:
            anchor = children[name]
            at = (root.matrix_world.inverted() @ anchor.matrix_world).translation
            box = ([at.x - tell_size / 2, at.y - tell_size / 2, at.z], [at.x + tell_size / 2, at.y + tell_size / 2, at.z + tell_size])
            if overlaps(lane, box):
                errors.append(f"tell_lane: largest tell at {name} obstructs the approach")
            # A hardpoint must touch the hull and sit on its near surface.
            if not any(all(bounds[0][i] - clearance <= at[i] <= bounds[1][i] + clearance for i in range(3)) for _, bounds in boxes):
                errors.append(f"hardpoint: {name} is not on the hull surface")
            target = projected(box)
            for mesh_name, bounds in boxes:
                other = projected(bounds)
                if overlaps(target, other, [0, 1]) and other[0][2] > target[1][2]:
                    errors.append(f"tell_visibility: {mesh_name} hides {name} from the tactical camera")
    else:
        for prefix in [nodes["closure"], nodes["reactor"]]:
            if not any(o.type == "MESH" and o.name.startswith(prefix) for o in children.values()):
                errors.append(f"runtime: missing separate {prefix} mesh")
        occupied = []
        for name in required:
            at = (root.matrix_world.inverted() @ children[name].matrix_world).translation
            if name in nodes["sockets"]:
                half = art.CONTRACT["socket"]["half_width"]
                size = (half * 2, half * 2, art.CONTRACT["socket"]["max_height"])
            else:
                size = [v + clearance * 2 for v in sizes[name.removeprefix("Parking_")]]
            box = ([at.x - size[0] / 2, at.y - size[1] / 2, at.z], [at.x + size[0] / 2, at.y + size[1] / 2, at.z + size[2]])
            occupied.append((name, box))
            for mesh_name, bounds in boxes:
                if mesh_name.startswith((nodes["closure"], nodes["reactor"])):
                    continue  # These are deliberately switchable; validate the open interior.
                if overlaps(box, bounds):
                    errors.append(f"clearance: {mesh_name} intersects the envelope at {name}")
                if bounds[1][2] > at.z + art.CONTRACT["epsilon"]:
                    a, b = projected(box), projected(bounds)
                    if overlaps(a, b, [0, 1]) and b[1][2] > a[0][2]:
                        errors.append(f"occlusion: {mesh_name} may hide {name} from the tactical camera")
        for (name, box), (other_name, other) in itertools.combinations(occupied, 2):
            if overlaps(box, other):
                errors.append(f"clearance: {name} and {other_name} overlap")
            if overlaps(projected(box), projected(other), [0, 1]):
                errors.append(f"occlusion: {name} and {other_name} overlap in the tactical view")
    return errors
