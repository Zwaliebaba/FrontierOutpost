"""One-time migration of the frozen scene's two ships into hull files (Art README, last section).

The starter frigate becomes `hulls/frigate/`, a flown hull, and the Aurora cruiser
`hulls/cruiser/`, a display hull. Unlike the facility split, the original objects are appended
with their bevel and weighted-normal modifiers, so the sources stay editable and the exports keep
the frozen GLBs' shading. Each part keeps its name and its place relative to the ship's root,
which moves to the origin; materials move to the shared library's roles. Lights and the
cruiser's exhaust plumes are presentation, not hull, and stay behind. Text becomes a mesh.

    blender -b --python Design/Art/tools/migrate_hulls.py -- [--source <FrontierOutpost.blend>]

Run once, before create_scene.py stops generating the ships. Refuses to replace a hull file.
"""

import argparse
from pathlib import Path
import sys

import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frontier_art as art
from split_generated import ROLES as FACILITY_ROLES

SHIPS = {"frigate": "Frigate_ROOT", "cruiser": "Cruiser_ROOT"}
ROLES = {**FACILITY_ROLES, "Drive | idle cyan": "IdleCyan", "Drive | white hot core": "WhiteCore"}


def role(material):
    for prefix, name in ROLES.items():
        if material.name.startswith(prefix):
            return name
    raise ValueError(f"No shared material role for {material.name}")


def presentation(obj):
    """Lights and exhaust plumes are effects (style guide §4), not solid hull."""
    return obj.type == "LIGHT" or any(
        m is not None and "plume" in m.name for m in getattr(obj.data, "materials", []))


def capture(source):
    """Each ship's parts by name, with their transform relative to the ship's root."""
    bpy.ops.wm.open_mainfile(filepath=str(source))
    ships = {}
    for identifier, root_name in SHIPS.items():
        root = bpy.data.objects.get(root_name)
        if root is None:
            raise ValueError(f"{source} has no {root_name}; was the frozen scene regenerated already?")
        inverse = root.matrix_world.inverted()
        parts = {}
        for obj in root.children_recursive:
            if presentation(obj):
                continue
            if obj.type not in {"MESH", "FONT"}:
                raise ValueError(f"Unexpected {obj.type} {obj.name} under {root_name}")
            parts[obj.name] = inverse @ obj.matrix_world
        ships[identifier] = parts
    return ships


def migrate(source, art_root=art.ART):
    ships = capture(source)
    for identifier in ships:
        if art.asset_path("hull", identifier, art_root).exists():
            raise ValueError(f"Refusing to replace migrated hull {identifier}")
    paths = []
    for identifier, parts in ships.items():
        path = art.new_asset("hull", identifier, art=art_root)
        root = bpy.data.objects[art.root_name("hull", identifier)]
        export = bpy.data.collections["Export"]
        library = {m.name: m for m in bpy.data.materials if m.library is not None}
        with bpy.data.libraries.load(str(source), link=False) as (_, target):
            target.objects = list(parts)
        graph = bpy.context.evaluated_depsgraph_get()
        for obj in target.objects:
            name = obj.name
            if obj.type == "FONT":
                # The exporter writes text as a mesh anyway; the tool accepts only meshes.
                export.objects.link(obj)
                bpy.context.view_layer.update()
                graph = bpy.context.evaluated_depsgraph_get()
                mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(graph))
                bpy.data.objects.remove(obj, do_unlink=True)
                obj = bpy.data.objects.new(name, mesh)
            export.objects.link(obj)
            obj.parent = root
            obj.matrix_parent_inverse = Matrix.Identity(4)
            obj.matrix_basis = parts[name]
            for index, material in enumerate(obj.data.materials):
                obj.data.materials[index] = library[role(material)]
        # The appended copies of the frozen roots and materials are left without users.
        for block in [*bpy.data.objects, *bpy.data.materials]:
            if block.users == 0 and block.library is None:
                (bpy.data.objects if isinstance(block, bpy.types.Object) else bpy.data.materials).remove(block)
        bpy.context.view_layer.update()
        errors, _ = art.check_asset(art_root)
        if errors:
            raise ValueError("\n".join(errors))
        bpy.ops.wm.save_as_mainfile(filepath=str(path))
        paths.append(path)
    return paths


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=art.ART.parent / "FrontierOutpost/FrontierOutpost.blend")
    parser.add_argument("--art-root", type=Path, default=art.ART)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    try:
        for saved in migrate(args.source, args.art_root):
            print("MIGRATED", saved)
    except (ValueError, OSError) as error:
        print("FRONTIER ART FAILED:", error, file=sys.stderr)
        sys.exit(1)
