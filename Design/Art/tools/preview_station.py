"""Render M9 review images. Presentation-only imports never change pipeline sources.

blender -b --python-exit-code 1 --python Design/Art/tools/preview_station.py -- /tmp/m9-review
"""
from pathlib import Path
import math
import sys

import bpy
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
import frontier_art as art


def append_glb(path, root_name, at, normalize=False):
    before=set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(path))
    imported=set(bpy.data.objects)-before
    root=next(o for o in imported if o.name.split('.')[0]==root_name)
    keep={root,*root.children_recursive}
    for obj in imported-keep:
        bpy.data.objects.remove(obj,do_unlink=True)
    if normalize:
        bpy.context.view_layer.update()
        points=[obj.matrix_world @ Vector(v) for obj in keep if obj.type=='MESH' for v in obj.bound_box]
        lo=Vector(tuple(min(p[i] for p in points) for i in range(3)))
        hi=Vector(tuple(max(p[i] for p in points) for i in range(3)))
        root.location+=Vector(at)-Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z))
    else:
        root.location=at
    return root


def setup(at, direction, width, output):
    scene=bpy.context.scene
    art.setup_presentation(scene,Vector(at),width)
    camera=scene.camera
    direction=Vector(direction).normalized()
    camera.location=Vector(at)+direction*150
    camera.rotation_euler=(-direction).to_track_quat('-Z','Y').to_euler()
    camera.data.ortho_scale=width
    scene.render.resolution_x=1600
    scene.render.resolution_y=1100
    scene.cycles.samples=64
    scene.cycles.use_denoising=False
    # Additional reflected starlight reveals the underslung core in the review only.
    data=bpy.data.lights.new('Underbody reflected light','AREA')
    data.energy=6500
    data.color=(.35,.52,.8)
    data.shape='DISK'
    data.size=18
    obj=bpy.data.objects.new('Underbody reflected light',data)
    bpy.data.collections['Presentation'].objects.link(obj)
    obj.location=(12,-22,-10)
    obj.rotation_euler=(Vector((0,0,-3))-obj.location).to_track_quat('-Z','Y').to_euler()
    if scene.get('frontier_kind') == 'station_exterior':
        # Review-only local illumination: runtime lighting remains M9 integration work.
        yaw = scene.get('review_station_yaw', 0)
        lamp = bpy.data.lights.new('Hangar working lights', 'AREA')
        lamp.energy = 900
        lamp.color = (1.0, .58, .25)
        lamp.shape = 'RECTANGLE'
        lamp.size = 6
        lamp.size_y = 3
        obj = bpy.data.objects.new('Hangar working lights', lamp)
        bpy.data.collections['Presentation'].objects.link(obj)
        obj.location = (-13 * math.sin(yaw), 13 * math.cos(yaw), 2.9)
        target = Vector((-12 * math.sin(yaw), 12 * math.cos(yaw), 0))
        obj.rotation_euler = (target - obj.location).to_track_quat('-Z', 'Y').to_euler()
    scene.use_nodes=True
    tree=scene.node_tree
    tree.nodes.clear()
    render=tree.nodes.new('CompositorNodeRLayers')
    glow=tree.nodes.new('CompositorNodeGlare')
    glow.glare_type='FOG_GLOW'
    glow.threshold=2
    glow.quality='HIGH'
    glow.mix=-.93
    composite=tree.nodes.new('CompositorNodeComposite')
    tree.links.new(render.outputs['Image'],glow.inputs['Image'])
    tree.links.new(glow.outputs['Image'],composite.inputs['Image'])
    scene.render.filepath=str(output)
    bpy.ops.render.render(write_still=True)


def populate_exterior():
    bpy.ops.wm.open_mainfile(filepath=str(art.ART/'station/exterior/exterior.blend'))
    for facility,anchor in [('sensor_array','Tell_A'),('refinery','Tell_B'),('crew_quarters','Tell_C')]:
        at=bpy.data.objects[anchor].location.copy()
        append_glb(art.ART/f'facilities/{facility}/{facility}.glb',f'Tell_{facility}_T2',at)
    for at in [(19,-9,0),(6,22,0),(-17,-9,0)]:
        append_glb(art.ART.parent/'FrontierOutpost/frigate.glb','Frigate_ROOT',at,True)


def exterior_review(out):
    populate_exterior()
    setup((1,1,-.4),art.CONTRACT['camera_direction'],47,out/'station-tactical.png')
    setup((0,1,-.4),(.43,.72,.545),45,out/'station-entrance.png')


def camera_facing_review(out):
    populate_exterior()
    # Proposed world placement only. Exported roots and the gameplay camera do not change.
    pivot=bpy.data.objects.new('Review station world yaw',None)
    bpy.context.scene.collection.objects.link(pivot)
    for obj in list(bpy.context.scene.objects):
        if obj != pivot and obj.parent is None and obj.type not in {'CAMERA','LIGHT'}:
            obj.parent=pivot
    pivot.rotation_euler.z=math.pi
    bpy.context.scene['review_station_yaw']=math.pi
    bpy.context.view_layer.update()
    setup((0,0,-.4),art.CONTRACT['camera_direction'],46,out/'station-camera-facing.png')


def interior_review(out):
    bpy.ops.wm.open_mainfile(filepath=str(art.ART/'station/interior/interior.blend'))
    for facility,anchor in [('crew_quarters','Socket_A'),('sensor_array','Socket_B'),('refinery','Socket_C')]:
        at=bpy.data.objects[anchor].location.copy()
        append_glb(art.ART/f'facilities/{facility}/{facility}.glb',f'Facility_{facility}_T2',at)
    for obj in bpy.data.objects:
        if obj.name.startswith('Closure_C'):
            obj.hide_render=True
    append_glb(art.ART.parent/'FrontierOutpost/frigate.glb','Frigate_ROOT',bpy.data.objects['Parking_frigate'].location.copy(),True)
    setup((0,0,0),art.CONTRACT['camera_direction'],40,out/'station-interior.png')


if __name__=='__main__':
    out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
    out.mkdir(parents=True,exist_ok=True)
    exterior_review(out)
    camera_facing_review(out)
    interior_review(out)
