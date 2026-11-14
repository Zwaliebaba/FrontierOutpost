"""Author the first pipeline example: two habitat tiers and their exterior tells.

Run once on a NEW crew_quarters template. Does not overwrite existing geometry or export.
"""

import math
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frontier_art as art


def part(root, name, position, size, role, shape="box", vertices=16, rotation=None, bevel=0.02):
    if shape == "cylinder":
        bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=1, depth=2)
    elif shape == "sphere":
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=1)
    else:
        bpy.ops.mesh.primitive_cube_add(size=1)
    obj = bpy.context.object
    obj.name = name + "__" + root.name
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    bpy.data.collections["Export"].objects.link(obj)
    obj.parent = root
    obj.location = position
    obj.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if rotation:
        obj.rotation_euler = rotation
    obj.data.materials.append(bpy.data.materials[role])
    if bevel and shape == "box":
        modifier = obj.modifiers.new("Machined edges", "BEVEL")
        modifier.width = bevel
        modifier.segments = 1
    return obj


def model():
    scene = bpy.context.scene
    if scene.get("frontier_id") != "crew_quarters" or scene.get("frontier_tiers") != 2:
        raise ValueError("Open a new two-tier crew_quarters template first")
    if any(o.type == "MESH" for o in bpy.data.collections["Export"].all_objects):
        raise ValueError("Refusing to replace existing geometry")
    for tier in [1, 2]:
        root = bpy.data.objects[f"Facility_crew_quarters_T{tier}"]
        part(root, "Mounting skid", (0, 0, .09), (2.12, 2.20, .18), "Graphite")
        part(root, "Pressure spine", (0, 0, .44), (.35, 1.95, .6), "HullSteel")
        for x in [-.51, .51]:
            for deck in range(tier):
                z = .57 + deck * .78
                part(root, f"Habitat {x} deck {deck}", (x, 0, z), (.39, .39, .96), "Titanium", "cylinder", rotation=(math.pi / 2, 0, 0))
                for y in [-.74, .74]:
                    part(root, f"Pressure collar {x} {deck} {y}", (x, y, z), (.413, .413, .045), "Graphite", "cylinder", rotation=(math.pi / 2, 0, 0))
                part(root, f"Dorsal armor {x} {deck}", (x, 0, z + .34), (.43, 1.45, .12), "Armor")
                part(root, f"ID stripe {x} {deck}", (x, -.48, z + .407), (.44, .12, .025), "Orange", bevel=0)
                for y in [-.48, -.16, .16, .48]:
                    side = -1 if x < 0 else 1
                    part(root, f"Window recess {x} {deck} {y}", (x + side * .365, y, z + .06), (.08, .245, .22), "Graphite", bevel=.01)
                    part(root, f"Warm window {x} {deck} {y}", (x + side * .411, y, z + .06), (.015, .18, .12), "Amber", bevel=.004)
            part(root, f"End plate {x}", (x, -.975, .57), (.60, .055, .42), "Armor")
        part(root, "Entry vestibule", (0, -1.025, .44), (.62, .37, .64), "Armor")
        part(root, "Airlock frame", (0, -1.22, .44), (.46, .045, .52), "Graphite", bevel=.025)
        part(root, "Airlock door", (0, -1.245, .43), (.30, .002, .38), "Titanium", bevel=0)
        part(root, "Airlock seam", (0, -1.248, .43), (.012, .002, .37), "Graphite", bevel=0)
        part(root, "Entry light", (0, -1.249, .72), (.35, .002, .035), "Amber", bevel=0)
        part(root, "Service manifold", (0, .90, .59), (.34, .36, .50), "HullSteel")
        for n in range(1, tier + 1):
            indicator = part(root, f"Operating_Tier_{n}", (-.14 + (n - 1) * .28, -1.24, .17), (.10, .015, .06), "Cyan", bevel=.003)
            indicator.name = art.operating_name(n, root)
        if tier == 2:
            part(root, "Upper connecting deck", (0, 0, 1.0), (1.85, 1.9, .12), "HullSteel")
            part(root, "Observation drum", (0, .28, 1.73), (.38, .38, .13), "Graphite", "cylinder")
            part(root, "Observation cupola", (0, .28, 1.86), (.34, .34, .27), "Cyan", "sphere")
            for x in [-.15, .15]:
                part(root, f"Cupola mullion {x}", (x, .28, 1.92), (.045, .64, .40), "Armor")
            part(root, "Shuttle davit post", (1.04, .65, .78), (.10, .14, 1.37), "Orange")
            part(root, "Shuttle davit arm", (.82, .65, 1.49), (.55, .14, .12), "Titanium")
            part(root, "Shuttle winch", (.59, .65, 1.31), (.13, .13, .27), "Graphite")
        else:
            part(root, "Life support rack", (0, .38, 1.0), (.48, .72, .19), "Graphite")
            for y in [.12, .28, .44, .60]:
                part(root, f"Radiator fin {y}", (0, y, 1.12), (.44, .045, .09), "Titanium", bevel=.005)
        tell = bpy.data.objects[f"Tell_crew_quarters_T{tier}"]
        scale = .70 if tier == 1 else 1.0
        part(tell, "Tell plinth", (0, 0, .06 * scale), (.9 * scale, .9 * scale, .12 * scale), "Graphite", bevel=.01)
        for x in [-.22, .22]:
            part(tell, f"Tell habitat {x}", (x * scale, 0, .34 * scale), (.20 * scale, .20 * scale, .38 * scale), "Titanium", "cylinder", vertices=12, rotation=(math.pi / 2, 0, 0))
            part(tell, f"Tell window {x}", (x * scale, -.39 * scale, .34 * scale), (.19 * scale, .02 * scale, .12 * scale), "Amber", bevel=0)
        if tier == 2:
            part(tell, "Tell second deck", (0, 0, .65), (.26, .30, .15), "Armor", bevel=.01)
            part(tell, "Tell dome", (0, 0, .82), (.19, .19, .18), "Cyan", "sphere", bevel=0)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.update()
    errors, _ = art.check_asset()
    if errors:
        raise ValueError("\n".join(errors))
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)


if __name__ == "__main__":
    try:
        model()
    except ValueError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
