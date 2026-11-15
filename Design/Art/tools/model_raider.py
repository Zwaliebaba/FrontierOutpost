"""Author the raider: a crescent hunter after SpaceNomad's Veyr frigate (Scenes/FrigateCombat).

It keeps the reference's construction (a keel under a dorsal shell, two swept blades with
forward emitters, a faceted command crystal, twin aft drives and spine fins) and its proportions,
in Frontier Outpost's hostile palette rather than the reference's jade and violet (style guide §5,
§11). Outlines are corner-cut and every part is lofted through chamfered sections, so the carapace
reads as faceted, machined plates rather than extruded slabs. The whole hull is scaled into the raider's collision
circle from frontier_sim, which the tool checks.

Run once on a NEW npc raider template. Does not overwrite existing geometry or export.
"""

import math
from pathlib import Path
import sys

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frontier_art as art

# The reference is drawn in its own units; the farthest point, a blade-tip emitter, sits 1.95
# from the centre. Scaling by this leaves a few centimetres inside the 1.3 m collision circle.
REFERENCE_REACH = 1.95
MARGIN = 0.04
# Plates meet at hard edges, as the reference's carapace does; only gentle curves shade smooth.
CREASE = math.radians(20)


def chaikin(points, sharp, rounds=1):
    """Rounds a closed outline by corner cutting; corners listed in `sharp` keep their point."""
    flags = [i in sharp for i in range(len(points))]
    for _ in range(rounds):
        out, out_flags = [], []
        for i, point in enumerate(points):
            after = points[(i + 1) % len(points)]
            if flags[i]:
                out.append(point)
                out_flags.append(True)
            else:
                before = points[i - 1]
                out.append(point.lerp(before, 0.25))
                out_flags.append(False)
                out.append(point.lerp(after, 0.25))
                out_flags.append(False)
        points, flags = out, out_flags
    return points


def area(points):
    return sum(a.x * b.y - b.x * a.y for a, b in zip(points, points[1:] + points[:1])) / 2


def inset(points, distance):
    """Offsets a closed outline inward by `distance`, mitred and limited at sharp corners."""
    if distance == 0:
        return list(points)
    inward = 1 if area(points) > 0 else -1
    out = []
    for i, point in enumerate(points):
        before, after = points[i - 1], points[(i + 1) % len(points)]
        normals = []
        for a, b in [(before, point), (point, after)]:
            edge = (b - a).normalized()
            normals.append(Vector((-edge.y, edge.x)) * inward)
        mitre = normals[0] + normals[1]
        if mitre.length < 1e-6:
            mitre = normals[1]
        mitre.normalize()
        # Limit the mitre so a pointed tip pulls back instead of crossing the far side.
        scale = min(2.0, 1 / max(0.35, mitre.dot(normals[1])))
        out.append(point + mitre * distance * scale)
    return out


class Builder:
    def __init__(self, root, scale):
        self.root, self.k = root, scale
        self.export = bpy.data.collections["Export"]

    def finish(self, obj, role, bevel):
        obj.name = obj.name + "__" + self.root.name
        obj.parent = self.root
        obj.data.materials.append(bpy.data.materials[role])
        for polygon in obj.data.polygons:
            polygon.use_smooth = True
        if bevel:
            modifier = obj.modifiers.new("Machined edges", "BEVEL")
            modifier.width = bevel * self.k
            modifier.segments = 1
            modifier.limit_method = "ANGLE"
            modifier.angle_limit = CREASE
        normals = obj.modifiers.new("Plate normals", "WEIGHTED_NORMAL")
        normals.keep_sharp = True
        return obj

    def link(self, name, mesh):
        obj = bpy.data.objects.new(name, mesh)
        self.export.objects.link(obj)
        return obj

    def loft(self, name, outline, sections, role, sharp=(), bevel=0.012, mirror=1, rounds=1):
        """A plate through `sections` of (height, inset) from the bottom up, in reference units."""
        base = [Vector((x * mirror, y)) for x, y in outline]
        sharp = set(sharp)
        smooth = chaikin(base, sharp, rounds)
        if area(smooth) < 0:
            smooth.reverse()
        rings = [[Vector((p.x * self.k, p.y * self.k, z * self.k)) for p in inset(smooth, d)]
                 for z, d in sections]
        bm = bmesh.new()
        verts = [[bm.verts.new(p) for p in ring] for ring in rings]
        count = len(smooth)
        for lower, upper in zip(verts, verts[1:]):
            for i in range(count):
                bm.faces.new((lower[i], lower[(i + 1) % count], upper[(i + 1) % count], upper[i]))
        bm.faces.new(list(reversed(verts[0])))
        bm.faces.new(verts[-1])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        # Caps are concave for the blades: triangulate them here so every viewer agrees.
        caps = [f for f in bm.faces if len(f.verts) > 4]
        bmesh.ops.triangulate(bm, faces=caps, quad_method="BEAUTY", ngon_method="BEAUTY")
        for edge in bm.edges:
            if edge.is_manifold and edge.calc_face_angle(0) > CREASE:
                edge.smooth = False
        mesh = bpy.data.meshes.new(name)
        bm.to_mesh(mesh)
        bm.free()
        return self.finish(self.link(name, mesh), role, bevel)

    def tube(self, name, a, b, radius, role, vertices=10, taper=1.0, bevel=0.0):
        """A tube between reference points `a` and `b`, narrowing to `taper` at `b`."""
        a, b = Vector(a) * self.k, Vector(b) * self.k
        axis = b - a
        rotation = axis.to_track_quat("Z", "Y")
        bm = bmesh.new()
        rings = []
        for end, scale in [(a, 1.0), (b, taper)]:
            ring = []
            for i in range(vertices):
                angle = 2 * math.pi * i / vertices
                offset = Vector((math.cos(angle), math.sin(angle), 0)) * radius * self.k * scale
                ring.append(bm.verts.new(end + rotation @ offset))
            rings.append(ring)
        for i in range(vertices):
            j = (i + 1) % vertices
            bm.faces.new((rings[0][i], rings[0][j], rings[1][j], rings[1][i]))
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[1])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        for edge in bm.edges:
            if edge.is_manifold and edge.calc_face_angle(0) > CREASE:
                edge.smooth = False
        mesh = bpy.data.meshes.new(name)
        bm.to_mesh(mesh)
        bm.free()
        return self.finish(self.link(name, mesh), role, bevel)

    def crystal(self, name, centre, size, role):
        bm = bmesh.new()
        bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1)
        for vert in bm.verts:
            vert.co = Vector(centre) * self.k + Vector(
                (vert.co.x * size[0], vert.co.y * size[1], vert.co.z * size[2])) * self.k
        for edge in bm.edges:
            edge.smooth = False
        mesh = bpy.data.meshes.new(name)
        bm.to_mesh(mesh)
        bm.free()
        obj = self.finish(self.link(name, mesh), role, 0)
        for polygon in obj.data.polygons:
            polygon.use_smooth = False
        return obj


KEEL = [(0, 1.18), (.38, .50), (.48, -.42), (.22, -1.16), (0, -1.40), (-.22, -1.16), (-.48, -.42),
        (-.38, .50)]
SHELL = [(0, .94), (.27, .40), (.32, -.40), (0, -1.10), (-.32, -.40), (-.27, .40)]
BLADE = [(.32, -1.05), (.90, -.85), (1.38, -.15), (1.42, .60), (1.05, 1.55), (.73, 1.18), (.92, .37),
         (.78, -.20), (.35, -.47)]
# The blade's forward horn and its trailing root stay pointed.
BLADE_SHARP = (4, 0)
# Seams lie half sunk in the blade armour, whose top is at .23.
CONDUIT = [(.66, -.70, .235), (1.12, -.10, .235), (1.19, .56, .235), (1.04, 1.27, .2)]


def model():
    scene = bpy.context.scene
    if scene.get("frontier_kind") != "npc" or scene.get("frontier_id") != "raider":
        raise ValueError("Open a new npc raider template first")
    if any(o.type == "MESH" for o in bpy.data.collections["Export"].all_objects):
        raise ValueError("Refusing to replace existing geometry")
    from game_data import npc_radii
    root = bpy.data.objects["Npc_raider"]
    b = Builder(root, (npc_radii()["raider"] - MARGIN) / REFERENCE_REACH)

    # The keel: a lens in section, chamfered under and over, carrying everything else.
    b.loft("Keel", KEEL, [(-.18, .10), (-.08, 0), (.10, 0), (.17, .06)], "HostileHull", sharp=(0, 4))
    b.loft("Dorsal shell", SHELL, [(.15, 0), (.26, .03), (.33, .12)], "HostileArmor", sharp=(0, 3))
    for side in [-1, 1]:
        b.loft(f"Swept blade {side}", BLADE, [(-.11, .06), (-.04, 0), (.10, 0), (.17, .04)],
               "HostileHull", sharp=BLADE_SHARP, mirror=side)
        # The armour plate lies within the blade, so the hull's rust shows as a rim around it.
        plate = [(p.x, p.y) for p in inset(chaikin([Vector(p) for p in BLADE], set(BLADE_SHARP), 2), .09)]
        b.loft(f"Blade armour {side}", plate, [(.15, 0), (.23, .03)], "HostileArmor",
               bevel=.008, mirror=side, rounds=0)
        for n, (a, c) in enumerate(zip(CONDUIT, CONDUIT[1:])):
            b.tube(f"Conduit {side} {n}", (a[0] * side, a[1], a[2]), (c[0] * side, c[1], c[2]),
                   .022, "Graphite", vertices=6)
        for n, (y, x) in enumerate([(-.50, .70), (.10, 1.10), (.75, 1.09)]):
            b.tube(f"Carapace seam {side} {n}", (side * (x - .10), y, .235), (side * (x + .09), y + .06, .235),
                   .014, "Graphite", vertices=6)
        b.tube(f"Emitter housing {side}", (side * 1.02, 1.18, .12), (side * 1.05, 1.50, .12), .075,
               "Graphite", vertices=12, taper=.8, bevel=.01)
        b.tube(f"Emitter lens {side}", (side * 1.05, 1.49, .12), (side * 1.055, 1.53, .12), .05,
               "Ember", vertices=12)
        b.tube(f"Spine fin {side}", (side * .17, -.50, .30), (side * .25, -.78, .64), .045,
               "HostileHull", vertices=6, taper=.45)
    for x in [-.18, .18]:
        b.tube(f"Drive housing {x}", (x, -1.02, 0), (x, -1.40, 0), .14, "Graphite", vertices=14,
               taper=.9, bevel=.01)
        b.tube(f"Drive core {x}", (x, -1.39, 0), (x, -1.42, 0), .1, "Ember", vertices=14)
    b.crystal("Command crystal", (0, -.10, .36), (.21, .42, .16), "Ember")
    bpy.context.view_layer.update()


if __name__ == "__main__":
    try:
        model()
        bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    except ValueError as error:
        print("FRONTIER ART FAILED:", error, file=sys.stderr)
        sys.exit(1)
