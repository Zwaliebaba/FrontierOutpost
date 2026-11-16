"""FrontierOutpost game scene. Blender 4.3+ or 5.x: blender -b --python create_scene.py

All dimensions are metres. +Z is up, ship forward is +Y.
Optional environment: SCENE_OUTPUT, RENDER_SAMPLES, RENDER_SCALE, SKIP_RENDER.
"""
import bpy, math, random, os, json, struct
from mathutils import Vector
from math import sin, cos, pi
random.seed(2266)
OUT = os.environ.get('SCENE_OUTPUT', os.path.dirname(os.path.abspath(__file__)))
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
for data in list(bpy.data.collections):
    if data.name != 'Collection': bpy.data.collections.remove(data)
scene = bpy.context.scene
groups = {}
for name in ['Asteroids', 'Backdrop', 'Lighting', 'Cameras']:
    c = bpy.data.collections.new(name); scene.collection.children.link(c); groups[name] = c
active = 'Asteroids'
def move(o, name):
    o.name = name
    for c in list(o.users_collection): c.objects.unlink(o)
    groups[active].objects.link(o)
    return o
def mat(name, color, metal=0, rough=.5, emit=0):
    m = bpy.data.materials.new(name); m.diffuse_color = (*color, 1); m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Metallic'].default_value=metal; p.inputs['Roughness'].default_value=rough
    if emit: p.inputs['Emission Color'].default_value=(*color,1); p.inputs['Emission Strength'].default_value=emit
    return m
steel=mat('Hull | midnight naval steel',(.095,.135,.19),.72,.38)
armor=mat('Armor | blue grey ceramic',(.29,.35,.43),.56,.45)
lightarmor=mat('Armor | pale titanium',(.53,.57,.61),.6,.38)
dark=mat('Structure | graphite',(.025,.038,.055),.65,.47)
orange=mat('Trim | burnt orange',(.62,.19,.055),.5,.4)
yellow=mat('Markings | safety ochre',(.95,.57,.12),.25,.45)
warm=mat('Windows | warm amber', (1,.40,.055),.1,.35,4)
white=mat('Lights | warm white',(1,.76,.35),0,.3,5)
cyan=mat('Navigation | cyan',(.04,.65,1),0,.3,5)
blue=mat('Drive | ion blue',(.015,.19,1),0,.3,7)
core=mat('Drive | white hot core',(.25,.8,1),0,.3,10)
red=mat('Banner | oxblood',(.26,.035,.026),0,.9)
rockmats=[mat('Rock | facet %02d'%i,(.065+i*.013,.077+i*.014,.10+i*.016),.12,.95) for i in range(6)]
def assign(o,m):
    o.data.materials.append(m); return o
def box(name, p, s, m, bevel=.04, rot=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=p); o=move(bpy.context.object,name)
    o.scale=s; bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    assign(o,m)
    if rot: o.rotation_euler=rot
    if bevel:
        b=o.modifiers.new('Machined edges','BEVEL'); b.width=bevel; b.segments=1
        o.modifiers.new('Weighted normals','WEIGHTED_NORMAL')
    return o
def cyl(name,p,r,depth,m,verts=12,r2=None):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=r if r2 is None else r2, depth=depth,location=p)
    return assign(move(bpy.context.object,name),m)
def beam(name,a,b,r,m,verts=8):
    a,b=Vector(a),Vector(b); o=cyl(name,(a+b)/2,r,(b-a).length,m,verts)
    o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler(); return o
def ring(name,p,r,thickness,m):
    bpy.ops.mesh.primitive_torus_add(major_segments=64,minor_segments=4,location=p,major_radius=r,minor_radius=thickness)
    return assign(move(bpy.context.object,name),m)
def text(name,body,p,size,m,rot=(0,0,0)):
    bpy.ops.object.text_add(location=p,rotation=rot); o=move(bpy.context.object,name)
    o.data.body=body;o.data.size=size;o.data.align_x='CENTER';o.data.align_y='CENTER';o.data.extrude=.001
    assign(o,m);return o
def point(name,p,color,power,size=1):
    d=bpy.data.lights.new(name,'POINT'); d.color=color; d.energy=power; d.shadow_soft_size=size
    o=bpy.data.objects.new(name,d); groups['Lighting'].objects.link(o); o.location=p; return o
# Screen-relative layout, with genuine Z-up geometry.
view=Vector((.43,-.72,.545)).normalized()
right=Vector((.858,.513,0)).normalized(); up=view.cross(right).normalized()
def layout(x,y,depth=0): return right*x+up*y+view*depth
S=layout(5,1.0)
def sp(x,y,z):return S+Vector((x,y,z))
# The station moved to Design/Art/station/ (plan M9). Its geometry drew 14 random numbers;
# drawing as many here keeps every asteroid after it unchanged. S still aims the lights.
for _ in range(14): random.random()
# The Aurora cruiser and the starter frigate moved to Design/Art/hulls/ (cruiser, frigate).
# Low-poly asteroids with seeded, varied faceting.
active='Asteroids'
asteroids=[(-17,-.9,-2,2.0),(-2,7,-7,1.55),(17,2,-4,1.7),(16,-9,3,4.6),(-.5,-7,-1,.9),(12,-6,0,.8),(-10,6,-5,.65),(-5,10,-9,.48),(8,-9,-2,.55),(-14,-4,-4,.4),(15,-2,-5,.35),(-11,-9,-2,.45)]
for idx,(x,y,d,r) in enumerate(asteroids):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2 if r>1 else 1,radius=r,location=layout(x,y,d))
    o=move(bpy.context.object,'Asteroid | %02d'%idx)
    for v in o.data.vertices:v.co*=random.uniform(.78,1.2)
    o.scale=(random.uniform(.85,1.2),random.uniform(.8,1.25),random.uniform(.85,1.25))
    o.rotation_euler=[random.random()*6 for _ in range(3)]
    for m in rockmats:o.data.materials.append(m)
    for p in o.data.polygons:p.material_index=random.randrange(len(rockmats))
# Background plate in camera coordinates: procedural faint blue interstellar dust.
active='Backdrop'
neb=bpy.data.materials.new('Backdrop | procedural interstellar dust');neb.use_nodes=True
n=neb.node_tree.nodes;n.clear();l=neb.node_tree.links
out=n.new('ShaderNodeOutputMaterial');em=n.new('ShaderNodeEmission');noise=n.new('ShaderNodeTexNoise');tex=n.new('ShaderNodeTexCoord');ramp=n.new('ShaderNodeValToRGB')
noise.inputs['Scale'].default_value=3.5;noise.inputs['Detail'].default_value=5;noise.inputs['Roughness'].default_value=.72
ramp.color_ramp.elements[0].position=.28;ramp.color_ramp.elements[0].color=(.0015,.004,.009,1)
ramp.color_ramp.elements[1].position=.78;ramp.color_ramp.elements[1].color=(.012,.032,.057,1)
em.inputs['Strength'].default_value=.75
l.new(tex.outputs['Generated'],noise.inputs['Vector']);l.new(noise.outputs['Fac'],ramp.inputs[0]);l.new(ramp.outputs[0],em.inputs[0]);l.new(em.outputs[0],out.inputs[0])
o=box('Backdrop | nebula canvas',layout(0,0,-75),(120,80,.05),neb,0);o.rotation_euler=view.to_track_quat('Z','Y').to_euler()
# Sparse tiny stars merged into a few objects; invisible to ray shadows.
starM=[mat('Stars | cool',(.48,.68,1),0,.5,2.5),mat('Stars | silver',(.82,.88,1),0,.5,3),mat('Stars | warm',(.9,.65,.4),0,.5,2)]
for k,m in enumerate(starM):
    vs=[];fs=[]
    for i in range(260):
        x=random.uniform(-33,33);y=random.uniform(-20,20);p=layout(x,y,-60)
        r=random.choices([.009,.016,.025,.045],[6,4,2,.2])[0]
        j=len(vs);vs.extend([p-right*r-up*r,p+right*r-up*r,p+right*r+up*r,p-right*r+up*r]);fs.append((j,j+1,j+2,j+3))
    me=bpy.data.meshes.new('Star quads');me.from_pydata(vs,[],fs);me.update();o=bpy.data.objects.new('Backdrop | starfield %d'%k,me);groups[active].objects.link(o);assign(o,m)
# Planet is backdrop-only, separate from the exportable gameplay assets.
planet=mat('Planet | navy and violet continents',(.03,.06,.14),.05,.85)
n=planet.node_tree.nodes;l=planet.node_tree.links;p=n.get('Principled BSDF')
tex=n.new('ShaderNodeTexNoise');tex.inputs['Scale'].default_value=3.6;tex.inputs['Detail'].default_value=6;tex.inputs['Roughness'].default_value=.7
coord=n.new('ShaderNodeTexCoord');l.new(coord.outputs['Generated'],tex.inputs['Vector'])
r=n.new('ShaderNodeValToRGB');r.color_ramp.elements[0].position=.38;r.color_ramp.elements[0].color=(.006,.026,.075,1);r.color_ramp.elements[1].position=.67;r.color_ramp.elements[1].color=(.16,.083,.13,1)
l.new(tex.outputs['Fac'],r.inputs[0]);l.new(r.outputs[0],p.inputs['Base Color']);l.new(r.outputs[0],p.inputs['Emission Color']);p.inputs['Emission Strength'].default_value=.18
bump=n.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.2;bump.inputs['Distance'].default_value=.09;l.new(tex.outputs['Fac'],bump.inputs['Height']);l.new(bump.outputs[0],p.inputs['Normal'])
P=layout(-15.5,12.5,-40)
bpy.ops.mesh.primitive_uv_sphere_add(segments=96,ring_count=48,radius=9.7,location=P);o=move(bpy.context.object,'Backdrop | blue planet');assign(o,planet)
for f in o.data.polygons:f.use_smooth=True
atm=bpy.data.materials.new('Planet | limb atmosphere');atm.use_nodes=True
n=atm.node_tree.nodes;n.clear();l=atm.node_tree.links
out=n.new('ShaderNodeOutputMaterial');mix=n.new('ShaderNodeMixShader');tr=n.new('ShaderNodeBsdfTransparent');em=n.new('ShaderNodeEmission');em.inputs[0].default_value=(.035,.22,.65,1);em.inputs[1].default_value=.8
lw=n.new('ShaderNodeLayerWeight');lw.inputs['Blend'].default_value=.25
power=n.new('ShaderNodeMath');power.operation='POWER';power.inputs[1].default_value=4
l.new(lw.outputs['Fresnel'],power.inputs[0]);l.new(power.outputs[0],mix.inputs[0]);l.new(tr.outputs[0],mix.inputs[1]);l.new(em.outputs[0],mix.inputs[2]);l.new(mix.outputs[0],out.inputs[0])
bpy.ops.mesh.primitive_uv_sphere_add(segments=96,ring_count=48,radius=9.85,location=P);o=move(bpy.context.object,'Backdrop | atmosphere');assign(o,atm)
for f in o.data.polygons:f.use_smooth=True
# Facility modules migrated to Design/Art/ by the M8 pipeline.
# Camera and film lighting.
active='Cameras'
bpy.ops.object.camera_add(location=layout(0,0,60));cam=move(bpy.context.object,'Camera | tactical overview');cam.rotation_euler=(-view).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=40;scene.camera=cam
active='Lighting'
world=bpy.data.worlds.new('Deep space');scene.world=world;world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.035,.06,.11,1);world.node_tree.nodes['Background'].inputs[1].default_value=.25
d=bpy.data.lights.new('Sun | cool starlight','SUN');d.energy=2.4;d.angle=.1;d.color=(.67,.77,1)
o=bpy.data.objects.new(d.name,d);groups[active].objects.link(o);o.rotation_euler=(.45,-.6,-.5)
def area(name,pos,power,color,size,target):
    d=bpy.data.lights.new(name,'AREA');d.energy=power;d.color=color;d.shape='DISK';d.size=size
    o=bpy.data.objects.new(name,d);groups[active].objects.link(o);o.location=pos;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
area('Key | soft warm rim',sp(-7,-1,16),1800,(1,.62,.37),10,S)
area('Fill | cool camera bounce',layout(-4,2,19),1800,(.3,.48,1),14,S)
scene['description']='Frontier Outpost — asteroids and backdrop; the station moved to Design/Art/station/'
scene['seed']=2266
scene.render.engine='CYCLES';scene.cycles.samples=int(os.environ.get('RENDER_SAMPLES','128'));scene.cycles.use_denoising=bool(getattr(bpy.app.build_options, 'openimagedenoise', False))
scene.render.resolution_x=1600;scene.render.resolution_y=900;scene.render.resolution_percentage=int(os.environ.get('RENDER_SCALE','100'))
scene.render.image_settings.file_format='PNG';scene.render.film_transparent=False
scene.view_settings.view_transform='AgX'
if hasattr(scene,'node_tree'):
    # Blender 4.x: the scene owns its compositor tree; glare options are node properties.
    scene.use_nodes=True;n=scene.node_tree.nodes;n.clear();l=scene.node_tree.links
    rl=n.new('CompositorNodeRLayers');gl=n.new('CompositorNodeGlare');gl.glare_type='FOG_GLOW';gl.quality='HIGH';gl.threshold=1.4;gl.size=7
    out=n.new('CompositorNodeComposite');l.new(rl.outputs['Image'],gl.inputs[0]);l.new(gl.outputs[0],out.inputs[0])
else:
    # Blender 5.x: the compositor is a node group with a Group Output, and glare options are inputs.
    # Size 0.080625 and Clamp are what 5.x versioning produces from the 4.x size=7 above.
    tree=bpy.data.node_groups.new('Compositing Nodetree','CompositorNodeTree');scene.compositing_node_group=tree
    tree.interface.new_socket(name='Image',in_out='OUTPUT',socket_type='NodeSocketColor')
    n=tree.nodes;l=tree.links
    rl=n.new('CompositorNodeRLayers');gl=n.new('CompositorNodeGlare')
    for k,v in {'Type':'Fog Glow','Quality':'High','Threshold':1.4,'Size':.080625,'Clamp':True}.items():gl.inputs[k].default_value=v
    out=n.new('NodeGroupOutput');l.new(rl.outputs['Image'],gl.inputs['Image']);l.new(gl.outputs['Image'],out.inputs[0])
# Select overview defaults and store a camera view for opening the blend interactively.
bpy.ops.object.select_all(action='DESELECT')
for screen in bpy.data.screens:
    for a in screen.areas:
        if a.type=='VIEW_3D':
            a.spaces.active.region_3d.view_perspective='CAMERA';a.spaces.active.overlay.show_overlays=False
scene.render.filepath=os.path.join(OUT,'FrontierOutpostPreview.png')
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'FrontierOutpost.blend'), compress=True)
# GLB excludes backdrop, cameras, lights, and effect geometry for engine portability.
for name in ['Asteroids']:
    bpy.ops.object.select_all(action='DESELECT')
    for o in groups[name].objects:o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,name.lower()+'.glb'),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
bpy.ops.object.select_all(action='DESELECT')
manifest={'units':'metres','blender_up':'+Z','blender_forward':'+Y','gltf_up':'+Y','seed':2266,'assets':{name:{'objects':len(groups[name].objects),'triangles':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in groups[name].objects if o.type=='MESH')} for name in ['Asteroids']},'notes':'GLBs retain scene placement. The station, the starter frigate and the Aurora cruiser moved to Design/Art/. The backdrop is a presentation effect.'}
def footprint(root):
    pts=[]
    for o in root.children_recursive:
        if o.type!='MESH':continue
        ev=o.evaluated_get(depsgraph);me=ev.to_mesh()
        pts.extend(root.matrix_world.inverted()@(o.matrix_world@v.co) for v in me.vertices);ev.to_mesh_clear()
    lo=[min(p[i] for p in pts) for i in range(3)];hi=[max(p[i] for p in pts) for i in range(3)]
    return lo,hi
bpy.context.view_layer.update();depsgraph=bpy.context.evaluated_depsgraph_get()
for name, stats in manifest['assets'].items():
    stats['triangles_base_mesh'] = stats.pop('triangles')
    with open(os.path.join(OUT, name.lower()+'.glb'), 'rb') as f:
        f.seek(12); length, chunk_type = struct.unpack('<II', f.read(8)); gltf = json.loads(f.read(length))
    stats['triangles_export'] = sum(gltf['accessors'][p['indices']]['count']//3 for m in gltf['meshes'] for p in m['primitives'])
with open(os.path.join(OUT,'scene_manifest.json'),'w') as f:json.dump(manifest,f,indent=2)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'FrontierOutpost.blend'), compress=True)
if not os.environ.get('SKIP_RENDER'):
    bpy.ops.render.render(write_still=True)

print('SCENE_COMPLETE',OUT,flush=True)
