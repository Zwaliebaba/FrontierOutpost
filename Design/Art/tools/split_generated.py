"""One-time M8.3 migration of the frozen scene's six facility roots."""

from pathlib import Path
import re

import bpy
from mathutils import Vector

import frontier_art as art


ROLES = {"Hull": "HullSteel", "Armor | blue": "Armor", "Armor | pale": "Titanium",
         "Structure": "Graphite", "Trim": "Orange", "Markings": "Ochre",
         "Windows": "Amber", "Lights": "Amber", "Navigation": "Cyan"}


def role(material):
    for prefix, name in ROLES.items():
        if material.name.startswith(prefix):
            return name
    raise ValueError(f"No shared material role for {material.name}")


def capture(source):
    bpy.ops.wm.open_mainfile(filepath=str(source))
    result = {}
    graph = bpy.context.evaluated_depsgraph_get()
    for root in list(bpy.context.scene.objects):
        match = re.fullmatch(r"Facility_([a-z_]+)_T([12])", root.name)
        if not match:
            continue
        parts = []
        for obj in root.children_recursive:
            if obj.type != "MESH":
                continue
            evaluated = obj.evaluated_get(graph)
            mesh = evaluated.to_mesh()
            try:
                transform = root.matrix_world.inverted() @ evaluated.matrix_world
                parts.append({"name": obj.name,
                              "vertices": [tuple(transform @ v.co) for v in mesh.vertices],
                              "faces": [tuple(p.vertices) for p in mesh.polygons],
                              "materials": [role(m) for m in mesh.materials],
                              "indices": [p.material_index for p in mesh.polygons],
                              "smooth": [p.use_smooth for p in mesh.polygons]})
            finally:
                evaluated.to_mesh_clear()
        result.setdefault(match[1], {})[int(match[2])] = parts
    if set(result) != {"sensor_array", "refinery", "fabrication_bay"} or any(set(v) != {1, 2} for v in result.values()):
        raise ValueError("Frozen source does not contain the expected six facility modules")
    return result


def make_mesh(name, vertices, faces, materials, indices, smooth, root):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    for role_name in materials:
        mesh.materials.append(bpy.data.materials[role_name])
    for polygon, index, shading in zip(mesh.polygons, indices, smooth):
        polygon.material_index = index
        polygon.use_smooth = shading
    obj = bpy.data.objects.new(name, mesh)
    bpy.data.collections["Export"].objects.link(obj)
    obj.parent = root
    return obj


def split(source, art_root=art.ART):
    # Capture all geometry before resetting Blender to each independent template.
    captured = capture(source)
    for identifier, tiers in captured.items():
        target = art.asset_path("facility", identifier, art_root)
        if target.exists():
            raise ValueError(f"Refusing to replace migrated source {target}")
    for identifier, tiers in captured.items():
        target = art.new_asset("facility", identifier, art=art_root)
        for tier, parts in tiers.items():
            root = bpy.data.objects[art.root_name("facility", identifier, tier)]
            all_points = [Vector(p) for part in parts for p in part["vertices"]]
            base = min(v.z for v in all_points)
            for part in parts:
                part["vertices"] = [(x, y, z - base) for x, y, z in part["vertices"]]
                indicator = re.match(r"Operating_Tier_(\d+)", part["name"])
                name = art.operating_name(int(indicator[1]), root) if indicator else part["name"] + "__" + root.name
                obj = make_mesh(name, part["vertices"], part["faces"], part["materials"], part["indices"], part["smooth"], root)
                obj.name = name
            # A reduced copy of the signature silhouette makes a recognisable exterior tell.
            tell = bpy.data.objects[art.root_name("tell", identifier, tier)]
            lo = [min(p[i] for part in parts for p in part["vertices"]) for i in range(3)]
            hi = [max(p[i] for part in parts for p in part["vertices"]) for i in range(3)]
            extent = .8 if tier == 1 else 1.15
            vertices, faces, indices, smooth = [], [], [], []
            materials = list(art.CONTRACT["materials"])
            for part in parts:
                offset = len(vertices)
                vertices.extend(tuple((p[i] - lo[i]) / (hi[i] - lo[i]) * extent - (extent / 2 if i < 2 else 0) for i in range(3)) for p in part["vertices"])
                faces.extend(tuple(index + offset for index in face) for face in part["faces"])
                indices.extend(materials.index(part["materials"][index]) for index in part["indices"])
                smooth.extend(part["smooth"])
            obj = make_mesh("Signature__" + tell.name, vertices, faces, materials, indices, smooth, tell)
            bpy.context.view_layer.update()
            triangles = art.geometry(tell)["triangles"]
            if triangles > art.CONTRACT["triangle_budgets"]["tell"]:
                modifier = obj.modifiers.new("Tell silhouette budget", "DECIMATE")
                modifier.ratio = 460 / triangles
                bpy.context.view_layer.objects.active = obj
                obj.select_set(True)
                bpy.ops.object.modifier_apply(modifier=modifier.name)
                obj.select_set(False)
        bpy.context.view_layer.update()
        errors, _ = art.check_asset(art_root)
        if errors:
            raise ValueError("\n".join(errors))
        bpy.ops.wm.save_as_mainfile(filepath=str(target))
        art.export_asset(art_root)
