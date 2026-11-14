"""Blender 4.3+/5.x entry point. See ../PIPELINE.md for commands and limitations."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_data import facility_tiers

VERSION = "0.1.0"
ART = Path(__file__).resolve().parents[1]
CONTRACT = json.loads((ART / "contract.json").read_text())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, data):
    Path(path).write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def root_name(kind, identifier, tier=None):
    return CONTRACT["roots"][kind].format(id=identifier, tier=tier)


def asset_path(kind, identifier, art=ART):
    if kind == "facility":
        return art / "facilities" / identifier / f"{identifier}.blend"
    if kind == "hull":
        return art / "hulls" / identifier / f"{identifier}.blend"
    part = kind.removeprefix("station_")
    if identifier != part:
        raise ValueError(f"{kind} identifier must be '{part}'")
    return art / "station" / part / f"{part}.blend"


def build_library(path):
    if path.exists():
        raise ValueError(f"Refusing to replace {path}; version material changes explicitly")
    materials = set()
    for role, settings in CONTRACT["materials"].items():
        material = bpy.data.materials.new(role)
        material.use_nodes = True
        material.use_fake_user = True
        material.diffuse_color = (*settings["rgb"], 1)
        bsdf = material.node_tree.nodes.get("Principled BSDF")
        for socket, value in {
            "Base Color": (*settings["rgb"], 1),
            "Metallic": settings["metallic"],
            "Roughness": settings["roughness"],
            "Emission Color": (*settings["rgb"], 1),
            "Emission Strength": settings["emission"],
        }.items():
            bsdf.inputs[socket].default_value = value
        materials.add(material)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.data.libraries.write(str(path), materials, fake_user=True)


def linked_materials(art=ART):
    path = art / "library/materials.blend"
    if not path.exists():
        raise ValueError("Create the shared material library with the 'library' command first")
    with bpy.data.libraries.load(str(path), link=True) as (source, target):
        target.materials = list(CONTRACT["materials"])
    if any(material is None for material in target.materials):
        raise ValueError("Material library does not match contract")
    return {material.name: material for material in target.materials}


def empty(name, collection, parent=None):
    obj = bpy.data.objects.new(name, None)
    collection.objects.link(obj)
    obj.parent = parent
    return obj


def new_asset(kind, identifier, tiers=None, art=ART):
    if not re.fullmatch(r"[a-z][a-z0-9_]*", identifier):
        raise ValueError("Use a lowercase game identifier (letters, digits, underscores)")
    path = asset_path(kind, identifier, art)
    if path.exists():
        raise ValueError(f"Refusing to overwrite {path}")
    known = facility_tiers() if kind == "facility" else {}
    if kind == "facility":
        if identifier in known and tiers is not None and tiers != known[identifier]:
            raise ValueError("Tier count differs from the game's data")
        tiers = known.get(identifier, tiers)
        if not isinstance(tiers, int) or tiers < 1:
            raise ValueError("Unknown facility: use --tiers for a planned asset")
    elif tiers is not None:
        raise ValueError("--tiers only applies to facilities")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1
    scene["frontier_kind"] = kind
    scene["frontier_id"] = identifier
    scene["frontier_tiers"] = tiers or 0
    scene["frontier_planned"] = kind == "facility" and identifier not in known
    scene["frontier_contract"] = CONTRACT["version"]
    exports = bpy.data.collections.new("Export")
    scene.collection.children.link(exports)
    for role, material in linked_materials(art).items():
        scene["frontier_material_" + role] = material
    roots = []
    for tier in range(1, tiers + 1) if kind == "facility" else [None]:
        for root_kind in [kind, "tell"] if kind == "facility" else [kind]:
            root = empty(root_name(root_kind, identifier, tier), exports)
            root["frontier_kind"] = kind
            root["frontier_id"] = identifier
            if tier:
                root["tier"] = tier
            roots.append(root)
    nodes = CONTRACT["runtime_nodes"]
    if kind == "station_exterior":
        for name in [nodes["mouth"], *nodes["tells"]]:
            anchor = empty(name, exports, roots[0])
            if name == nodes["mouth"]:
                anchor["opening_width"] = CONTRACT["hangar"]["min_width"]
                anchor["opening_height"] = CONTRACT["hangar"]["min_height"]
    if kind == "station_interior":
        for name in [*nodes["sockets"], "Parking_frigate"]:
            empty(name, exports, roots[0])
    setup_presentation(scene, Vector((0, 0, 1)), 6)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(path), relative_remap=True)
    # Keep links portable when the repository is moved to another computer.
    for library in bpy.data.libraries:
        library.filepath = bpy.path.relpath(str(art / "library/materials.blend"))
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    return path


def setup_presentation(scene, centre, width):
    collection = bpy.data.collections.get("Presentation")
    if collection is None:
        collection = bpy.data.collections.new("Presentation")
        scene.collection.children.link(collection)
    for obj in list(collection.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    camera_data = bpy.data.cameras.new("TacticalCamera")
    camera = bpy.data.objects.new("TacticalCamera", camera_data)
    collection.objects.link(camera)
    direction = Vector(CONTRACT["camera_direction"]).normalized()
    camera.location = centre + direction * max(30, width * 3)
    camera.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = width
    camera_data.clip_end = max(200, width * 10)
    scene.camera = camera
    for name, position, color, energy, size in [
        ("Cool key", (4, -6, 9), (0.72, 0.83, 1), 1700, 7),
        ("Warm rim", (-5, 4, 6), (1, 0.65, 0.38), 1200, 5),
        ("Blue fill", (3, 6, 3), (0.32, 0.55, 1), 800, 5),
    ]:
        light_data = bpy.data.lights.new(name, "AREA")
        light_data.color = color
        light_data.energy = energy
        light_data.shape = "DISK"
        light_data.size = size
        light = bpy.data.objects.new(name, light_data)
        collection.objects.link(light)
        light.location = centre + Vector(position) * max(1, width / 8)
        light.rotation_euler = (centre - light.location).to_track_quat("-Z", "Y").to_euler()
        light_data.energy *= max(1, width / 8) ** 2
    scene.world = scene.world or bpy.data.worlds.new("Space")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes.get("Background").inputs[0].default_value = (0.015, 0.024, 0.045, 1)
    scene.world.node_tree.nodes.get("Background").inputs[1].default_value = 0.4
    scene.view_settings.view_transform = "AgX"
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = False
    scene.render.resolution_x = CONTRACT["preview_pixels"]
    scene.render.resolution_y = CONTRACT["preview_pixels"]
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"


def descendants(root):
    return [root, *root.children_recursive]


def geometry(root):
    points, triangles, materials = [], 0, set()
    graph = bpy.context.evaluated_depsgraph_get()
    for obj in descendants(root):
        if obj.type != "MESH":
            continue
        evaluated = obj.evaluated_get(graph)
        mesh = evaluated.to_mesh()
        try:
            mesh.calc_loop_triangles()
            triangles += len(mesh.loop_triangles)
            points.extend(root.matrix_world.inverted() @ evaluated.matrix_world @ v.co for v in mesh.vertices)
            materials.update(m.name for m in mesh.materials if m)
        finally:
            evaluated.to_mesh_clear()
    bounds = None if not points else {
        "min": [min(p[i] for p in points) for i in range(3)],
        "max": [max(p[i] for p in points) for i in range(3)],
    }
    anchors = {}
    for obj in descendants(root):
        if obj is not root and obj.type == "EMPTY":
            transform = root.matrix_world.inverted() @ obj.matrix_world
            anchors[obj.name] = {"matrix": [list(row) for row in transform],
                                 "properties": {key: obj[key] for key in ["opening_width", "opening_height"] if key in obj}}
    return {"bounds": bounds, "triangles": triangles, "materials": sorted(materials), "anchors": anchors,
            "nodes": sorted(obj.name for obj in descendants(root))}


def operating_name(tier, root):
    # Blender requires globally unique object names; retain the semantic prefix per root.
    return CONTRACT["runtime_nodes"]["operating"].format(tier=tier) + "__" + root.name


def material_matches_contract(material):
    settings = CONTRACT["materials"].get(material.name)
    if not settings or not material.use_nodes or not material.node_tree:
        return False
    nodes = material.node_tree.nodes
    bsdf = next((node for node in nodes if node.type == "BSDF_PRINCIPLED"), None)
    output = next((node for node in nodes if node.type == "OUTPUT_MATERIAL" and node.is_active_output), None)
    if bsdf is None or output is None or len(nodes) != 2:
        return False
    if not output.inputs["Surface"].is_linked or output.inputs["Surface"].links[0].from_node != bsdf:
        return False
    expected = {"Base Color": (*settings["rgb"], 1), "Metallic": settings["metallic"],
                "Roughness": settings["roughness"], "Emission Color": (*settings["rgb"], 1),
                "Emission Strength": settings["emission"], "Alpha": 1}
    for name, value in expected.items():
        socket = bsdf.inputs[name]
        if socket.is_linked:
            return False
        actual = socket.default_value
        values = zip(actual, value) if isinstance(value, tuple) else [(actual, value)]
        if any(abs(a - b) > CONTRACT["epsilon"] for a, b in values):
            return False
    return True


def check_asset(art=ART):
    scene = bpy.context.scene
    errors = []
    kind, identifier = scene.get("frontier_kind"), scene.get("frontier_id")
    if kind not in CONTRACT["roots"] or kind == "tell" or not isinstance(identifier, str):
        return ["metadata: scene must identify one asset kind and identifier"], {}
    if not re.fullmatch(r"[a-z][a-z0-9_]*", identifier):
        errors.append("identifier: invalid asset identifier")
    if scene.unit_settings.system != "METRIC" or scene.unit_settings.scale_length != 1:
        errors.append("units: use metres with scale_length 1")
    if scene.get("frontier_contract") != CONTRACT["version"]:
        errors.append("contract: template version differs")
    tiers = scene.get("frontier_tiers", 0)
    if kind == "facility":
        known = facility_tiers()
        if not isinstance(tiers, int) or tiers < 1:
            return [*errors, "tiers: positive tier count required"], {}
        if identifier in known and tiers != known[identifier]:
            errors.append("tiers: count differs from game data")
        if identifier not in known and not scene.get("frontier_planned"):
            errors.append("identifier: unknown facility must be marked planned")
    expected = {root_name(k, identifier, t): k
                for t in range(1, tiers + 1)
                for k in ["facility", "tell"]} if kind == "facility" else {root_name(kind, identifier): kind}
    collection = bpy.data.collections.get("Export")
    if collection is None:
        return [*errors, "collection: Export collection missing"], {}
    objects = list(collection.all_objects)
    checked_materials = set()
    for export_collection in [collection, *collection.children_recursive]:
        if export_collection.hide_render or export_collection.hide_viewport:
            errors.append(f"visibility: {export_collection.name} collection must be visible")
    roots = [o for o in objects if o.parent is None]
    if set(o.name for o in roots) != set(expected):
        errors.append("roots: expected exactly " + ", ".join(sorted(expected)))
    for obj in objects:
        if obj.parent and obj.parent not in objects:
            errors.append(f"hierarchy: {obj.name} has a parent outside Export")
        if obj.type not in {"EMPTY", "MESH"}:
            errors.append(f"type: {obj.name} is {obj.type}; export only meshes and anchors")
        if obj.hide_render or obj.hide_viewport or obj.hide_get():
            errors.append(f"visibility: {obj.name} must be visible for export")
        if obj.animation_data or obj.constraints:
            errors.append(f"animation: {obj.name} has animation or constraints")
        if obj.instance_type != "NONE":
            errors.append(f"geometry: {obj.name} must realise collection instances before export")
        if obj.type == "MESH":
            if obj.data.shape_keys:
                errors.append(f"geometry: {obj.name} must use static, realised geometry")
            if any(modifier.type in {"NODES", "ARMATURE", "PARTICLE_SYSTEM"} for modifier in obj.modifiers):
                errors.append(f"geometry: {obj.name} must realise procedural instances or rigging before export")
            if any(modifier.show_viewport != modifier.show_render for modifier in obj.modifiers):
                errors.append(f"geometry: {obj.name} modifier visibility differs between check and render")
            if not obj.data.materials or any(m is None for m in obj.data.materials):
                errors.append(f"materials: {obj.name} has an unassigned material")
            for material in obj.data.materials:
                if material is None:
                    continue
                library = material.library
                if (material.name not in CONTRACT["materials"] or library is None
                        or Path(bpy.path.abspath(library.filepath)).resolve() != (art / "library/materials.blend").resolve()):
                    errors.append(f"materials: {obj.name}/{material.name} must link the shared library")
                elif material not in checked_materials:
                    checked_materials.add(material)
                    if not material_matches_contract(material):
                        errors.append(f"materials: {material.name} shader differs from contract")
            if any(p.material_index >= len(obj.data.materials) for p in obj.data.polygons):
                errors.append(f"materials: {obj.name} has an invalid face material index")
    if any(obj.type == "MESH" and obj not in objects for obj in scene.objects):
        errors.append("collection: mesh outside Export would be silently omitted")
    records = {}
    epsilon = CONTRACT["epsilon"]
    for root in roots:
        if root.name not in expected:
            continue
        root_kind = expected[root.name]
        if root.type != "EMPTY" or any(abs(root.matrix_world[i][j] - Matrix.Identity(4)[i][j]) > epsilon for i in range(4) for j in range(4)):
            errors.append(f"origin: {root.name} must be an empty at the origin, rotation zero, scale one")
        if root.get("frontier_kind") != kind or root.get("frontier_id") != identifier:
            errors.append(f"metadata: {root.name} custom properties differ")
        record = geometry(root)
        records[root.name] = record
        if not record["bounds"] or not record["triangles"]:
            errors.append(f"geometry: {root.name} has no surface geometry")
            continue
        if any(not math.isfinite(n) for side in record["bounds"].values() for n in side):
            errors.append(f"geometry: {root.name} has non-finite bounds")
            continue
        if record["triangles"] > CONTRACT["triangle_budgets"][root_kind]:
            errors.append(f"budget: {root.name} exceeds triangle budget")
        lo, hi = record["bounds"]["min"], record["bounds"]["max"]
        if kind == "facility":
            tier = root.get("tier")
            if root.name != root_name(root_kind, identifier, tier):
                errors.append(f"tier: {root.name} property differs from name")
            if root_kind == "facility":
                socket = CONTRACT["socket"]
                if any(lo[i] < -socket["half_width"] - epsilon or hi[i] > socket["half_width"] + epsilon for i in [0, 1]):
                    errors.append(f"envelope: {root.name} exceeds socket footprint")
                if abs(lo[2]) > epsilon or hi[2] > socket["max_height"] + epsilon:
                    errors.append(f"base: {root.name} must start at Z=0 and fit maximum height")
                if isinstance(tier, int) and tier > 0:
                    for n in range(1, tier + 1):
                        indicator = bpy.data.objects.get(operating_name(n, root))
                        if indicator not in descendants(root) or indicator.type != "MESH":
                            errors.append(f"runtime: {root.name} needs mesh {operating_name(n, root)}")
            else:
                size = CONTRACT["tell"]["cube_size"]
                if any(lo[i] < -size / 2 - epsilon or hi[i] > size / 2 + epsilon for i in [0, 1]) or lo[2] < -epsilon or hi[2] > size + epsilon:
                    errors.append(f"tell: {root.name} exceeds tell cube")
        if kind == "hull":
            record["footprint_radius"] = max(math.hypot(x, y) for x in [lo[0], hi[0]] for y in [lo[1], hi[1]])
            exterior = art / "station/exterior/exterior.json"
            if exterior.exists():
                station = json.loads(exterior.read_text())
                anchors = next(iter(station["roots"].values()))["anchors"]
                mouth = anchors[CONTRACT["runtime_nodes"]["mouth"]]["properties"]
                if hi[0] - lo[0] + CONTRACT["clearance"] * 2 > mouth["opening_width"] or hi[2] - lo[2] + CONTRACT["clearance"] * 2 > mouth["opening_height"]:
                    errors.append(f"hull_fit: {root.name} cannot clear the station mouth")
            interior = art / "station/interior/interior.json"
            if interior.exists():
                station = json.loads(interior.read_text())
                anchors = next(iter(station["roots"].values()))["anchors"]
                if "Parking_" + identifier not in anchors:
                    errors.append(f"parking: {root.name} has no station parking anchor")
    if kind == "facility":
        for tier in range(2, tiers + 1):
            previous = records.get(root_name("facility", identifier, tier - 1), {}).get("bounds")
            current = records.get(root_name("facility", identifier, tier), {}).get("bounds")
            if previous and current and current["max"][2] - previous["max"][2] <= CONTRACT["socket"]["tier_step"] + epsilon:
                errors.append(f"tier_step: tier {tier} must be more than the tier step taller")
            previous = records.get(root_name("tell", identifier, tier - 1), {}).get("bounds")
            current = records.get(root_name("tell", identifier, tier), {}).get("bounds")
            if previous and current:
                old = [previous["max"][i] - previous["min"][i] for i in range(3)]
                new = [current["max"][i] - current["min"][i] for i in range(3)]
                if any(b < a - epsilon for a, b in zip(old, new)) or math.prod(new) <= math.prod(old) + epsilon:
                    errors.append(f"tell_growth: tier {tier} tell must be bigger")
    if kind.startswith("station_"):
        from station_checks import check
        if roots:
            errors.extend(check(roots[0], kind, art))
    return sorted(set(errors)), records


def export_asset(art=ART):
    source = Path(bpy.data.filepath)
    if not bpy.data.filepath or not source.is_file():
        raise ValueError("Save the .blend before exporting")
    if bpy.app.background:
        # Background mode's is_dirty remains true even immediately after saving. Reopen
        # the source so geometry and source hash are guaranteed to describe the same file.
        bpy.ops.wm.open_mainfile(filepath=str(source))
    elif bpy.data.is_dirty:
        raise ValueError("Save the .blend before exporting")
    errors, records = check_asset(art)
    if errors:
        raise ValueError("\n".join(errors))
    scene = bpy.context.scene
    expected_path = asset_path(scene["frontier_kind"], scene["frontier_id"], art)
    if source.resolve() != expected_path.resolve():
        raise ValueError(f"Source must be saved at {expected_path}")
    inputs = {"source": sha(source), "contract": sha(ART / "contract.json"),
              "tool": sha(__file__), "game_data_reader": sha(Path(__file__).with_name("game_data.py")),
              "materials": sha(art / "library/materials.blend"), "blender": bpy.app.version_string}
    if scene["frontier_kind"].startswith("station_"):
        inputs["station_checks"] = sha(Path(__file__).with_name("station_checks.py"))
    target = source.with_suffix(".glb")
    manifest_path = source.with_suffix(".json")
    preview = source.with_suffix(".png")
    if manifest_path.exists() and target.exists() and preview.exists():
        old = json.loads(manifest_path.read_text())
        if old.get("inputs") == inputs and old.get("sha256") == sha(target) and old.get("preview_sha256") == sha(preview) and old.get("roots") == records:
            print("UNCHANGED", source.stem)
            return manifest_path
    # Stage all outputs: validation/render failure must not replace previous exports.
    with tempfile.TemporaryDirectory(dir=source.parent, prefix=".frontier-export-") as temp:
        temp = Path(temp)
        bpy.ops.object.select_all(action="DESELECT")
        for obj in bpy.data.collections["Export"].all_objects:
            obj.select_set(True)
        bpy.ops.export_scene.gltf(filepath=str(temp / target.name), export_format="GLB",
                                  use_selection=True, export_yup=True, export_extras=True,
                                  export_animations=False, export_cameras=False, export_lights=False,
                                  export_apply=True)
        # Present each tier and tell side by side; export always retains roots at origin.
        roots = [o for o in bpy.data.collections["Export"].objects if o.parent is None]
        saved = {o: o.matrix_world.copy() for o in roots}
        try:
            kind = scene["frontier_kind"]
            if kind == "facility":
                step = CONTRACT["socket"]["half_width"] * 2 + 0.7
                for root in roots:
                    root.location.x = (root["tier"] - 1) * step
                    root.location.y = -2.5 if root.name.startswith("Tell_") else 0
            bpy.context.view_layer.update()
            points = []
            for root in roots:
                bounds = records[root.name]["bounds"]
                points.extend(root.matrix_world @ Vector((x, y, z)) for x in [bounds["min"][0], bounds["max"][0]] for y in [bounds["min"][1], bounds["max"][1]] for z in [bounds["min"][2], bounds["max"][2]])
            centre = sum(points, Vector()) / len(points)
            setup_presentation(scene, centre, max((p - centre).length for p in points) * 2.5)
            scene.render.filepath = str(temp / preview.name)
            bpy.ops.render.render(write_still=True)
        finally:
            for root, matrix in saved.items():
                root.matrix_world = matrix
        manifest = {"schema_version": 1, "kind": scene["frontier_kind"], "id": scene["frontier_id"],
                    "planned": scene["frontier_kind"] == "facility" and scene["frontier_id"] not in facility_tiers(), "tiers": scene["frontier_tiers"],
                    "tool_version": VERSION, "blender_version": bpy.app.version_string,
                    "units": CONTRACT["units"], "source_up": "+Z", "source_forward": "+Y", "gltf_up": "+Y",
                    "bounds_space": "root-local Blender Z-up metres", "inputs": inputs, "roots": records,
                    "glb": target.name, "sha256": sha(temp / target.name),
                    "preview": preview.name, "preview_sha256": sha(temp / preview.name)}
        write_json(temp / manifest_path.name, manifest)
        for output in [target, preview, manifest_path]:
            os.replace(temp / output.name, output)
    print("EXPORTED", target)
    return manifest_path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--art-root", type=Path, default=ART, help="Alternate workspace for fixtures")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("library", help="Create shared materials.blend, without replacing one")
    new = commands.add_parser("new", help="Create an empty editable template")
    new.add_argument("kind", choices=["facility", "hull", "station_exterior", "station_interior"])
    new.add_argument("id")
    new.add_argument("--tiers", type=int, help="Explicit tier count for planned facilities only")
    commands.add_parser("check", help="Validate the open file; write nothing")
    commands.add_parser("export", help="Check, then export saved source, GLB, manifest, preview")
    split_command = commands.add_parser("split", help="Migrate the six frozen facility modules once")
    split_command.add_argument("--source", type=Path, default=ART.parent / "FrontierOutpost/FrontierOutpost.blend")
    args = parser.parse_args(argv)
    if args.command == "library":
        build_library(args.art_root / "library/materials.blend")
    elif args.command == "new":
        new_asset(args.kind, args.id, args.tiers, args.art_root)
    elif args.command == "check":
        errors, _ = check_asset(args.art_root)
        if errors:
            raise ValueError("\n".join(errors))
        print("PASS", bpy.data.filepath)
    elif args.command == "split":
        from split_generated import split
        split(args.source, args.art_root)
    else:
        export_asset(args.art_root)


if __name__ == "__main__":
    try:
        main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    except (ValueError, OSError, KeyError, IndexError) as error:
        print("FRONTIER ART FAILED:", error, file=sys.stderr)
        sys.exit(1)
