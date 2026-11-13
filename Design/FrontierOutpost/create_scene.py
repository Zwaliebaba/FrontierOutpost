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
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'berths.json')) as f:
    BERTH_LAYOUT=json.load(f)
BERTHS=BERTH_LAYOUT['berths']
DECK_Y=BERTH_LAYOUT['centreY']
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
for data in list(bpy.data.collections):
    if data.name != 'Collection': bpy.data.collections.remove(data)
scene = bpy.context.scene
groups = {}
for name in ['Station', 'Cruiser', 'Frigate', 'Asteroids', 'Facilities', 'Backdrop', 'Lighting', 'Cameras']:
    c = bpy.data.collections.new(name); scene.collection.children.link(c); groups[name] = c
active = 'Station'
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
# Central station: octagonal hub, tiered crown, articulated utility spire.
cyl('Hub | lower collar',sp(0,0,.1),2.35,.75,dark,8)
cyl('Hub | octagonal pressure hull',sp(0,0,2.0),2.25,3.3,steel,8)
cyl('Hub | shoulder armor',sp(0,0,3.65),2.5,.6,armor,8,r2=2.12)
cyl('Hub | crown cap',sp(0,0,4.05),2.12,.25,lightarmor,8)
cyl('Hub | rooftop machinery',sp(0,0,4.35),1.4,.45,dark,8)
cyl('Hub | rooftop armor',sp(0,0,4.6),1.5,.16,armor,8)
for i in range(8):
    a=2*pi*i/8+pi/8; n=Vector((cos(a),sin(a),0)); tangent=Vector((-sin(a),cos(a),0))
    # Flat wall panels, illuminated recesses, window mullions.
    rot=(0,0,a+pi/2)
    box('Hub | face armor %02d'%i,sp(n.x*2.13,n.y*2.13,2.0),(1.5,.12,2.55),armor,.035,rot)
    for j in range(3):
        c=sp(n.x*2.205,n.y*2.205,2.0)+tangent*((j-1)*.33)
        box('Hub | recessed window housing',c,(.27,.12,.98),dark,.025,rot)
        box('Hub | amber window',c+n*.075,(.17,.025,.76),warm,.015,rot)
        box('Hub | window mullion',c+n*.095,(.21,.03,.035),dark,0,rot)
    for z in [.8,3.12]:box('Hub | belt rail',sp(n.x*2.23,n.y*2.23,z),(1.55,.16,.12),dark,.015,rot)
    box('Hub | roof vent',sp(n.x*1.72,n.y*1.72,4.06),(.65,.36,.13),dark,.02,rot)
    for j in range(3): box('Hub | roof vent slat',sp(n.x*1.72,n.y*1.72,4.14)+tangent*((j-1)*.18),(.05,.3,.025),lightarmor,0,rot)
# Segmented crown armor: broad bevel-lit plates with deliberate graphite gaps.
for i in range(8):
    a=i*pi/4; gap=.035
    vs=[]
    for radius,z in [(1.45,4.62),(2.48,4.03)]:
        for angle in [a+gap,a+pi/4-gap]:vs.append(tuple(sp(cos(angle)*radius,sin(angle)*radius,z)))
    me=bpy.data.meshes.new('Crown armor plate');me.from_pydata(vs,[],[(0,2,3,1)]);me.update()
    uv=me.uv_layers.new(name='UVMap')
    for loop,coord in zip(uv.data,[(0,0),(0,1),(1,1),(1,0)]):loop.uv=coord
    o=bpy.data.objects.new('Hub | segmented crown',me);groups['Station'].objects.link(o);assign(o,armor if i%3 else lightarmor)
    solid=o.modifiers.new('Armor thickness','SOLIDIFY');solid.thickness=.08
# Suspended spine, heat radiators, pipes and aerials.
for z,r,h in [(-.8,1.4,1.2),(-1.9,.95,1.1),(-3,.65,1.3),(-4.05,.33,.9)]:
    cyl('Spine | service tier',sp(0,0,z),r,h,steel,8)
    cyl('Spine | joint band',sp(0,0,z+h*.4),r*1.12,.15,armor,8)
for i in range(8):
    a=i*pi/4
    beam('Spine | conduit',sp(cos(a)*1.05,sin(a)*1.05,-.2),sp(cos(a)*.5,sin(a)*.5,-3.7),.07,orange)
    for z in [-.7,-1.5,-2.4]:box('Spine | status lamp',sp(cos(a)*1.12,sin(a)*1.12,z),(.12,.12,.22),warm,.01)
    beam('Spine | dangling aerial',sp(cos(a)*.45,sin(a)*.45,-3.2),sp(cos(a)*.5,sin(a)*.5,-5.8-random.random()),.025,dark,5)
for x,y,h in [(0,.3,5.3),(-.55,.6,3.6),(.48,.45,4.2),(.65,-.1,2.6)]:
    box('Mast | mounting block',sp(x,y,4.8),(.4,.42,.6),steel)
    beam('Mast | antenna',sp(x,y,4.9),sp(x,y,4.9+h),.045,lightarmor)
    for z in [5.3,6.1,7.1]:
        if z<4.9+h: box('Mast | equipment',sp(x,y,z),(.19,.26,.38),dark,.02)
    cyl('Mast | beacon',sp(x,y,4.9+h),.055,.14,white,6)
# Twin radial habitat / docking pods.
for side in [-1,1]:
    x=side*5.2
    box('Arm | pressurized bridge',sp(side*3.45,.5,1.9),(3.1,1.35,1.15),steel,.12)
    for dx in [2.6,3.3,4.0]:
        box('Arm | segmented rib',sp(side*dx,.5,1.9),(.15,1.5,1.35),armor,.03)
        for k in range(3):box('Arm | passage window',sp(side*dx,-.22,1.9+k*.18),(.23,.035,.08),warm,.01)
    cyl('Pod | lower machinery',sp(x,.5,.95),1.66,1.2,dark,12)
    cyl('Pod | armored drum',sp(x,.5,1.95),2.03,1.6,armor,12)
    cyl('Pod | upper rim',sp(x,.5,2.8),2.12,.18,lightarmor,12)
    cyl('Pod | recessed deck',sp(x,.5,2.9),1.91,.08,steel,48)
    ring('Pod | circular landing guide',sp(x,.5,2.95),1.5,.025,yellow)
    ring('Pod | outer deck rail',sp(x,.5,2.98),1.94,.028,dark)
    cyl('Pod | hatch',sp(x,.5,2.96),.62,.07,dark,12)
    for i in range(12):
        a=i*pi/6
        box('Pod | armor seam',sp(x+cos(a)*1.99,.5+sin(a)*1.99,1.95),(.075,.075,1.2),dark,.01)
        if i%2==0:
            box('Pod | running light',sp(x+cos(a)*1.83,.5+sin(a)*1.83,2.95),(.12,.12,.06),warm,.01)
        if i%3==0:
            for j in range(3):
                a2=a+(j-1)*.09
                box('Pod | amber porthole',sp(x+cos(a2)*2.035,.5+sin(a2)*2.035,2.05),(.12,.12,.16),warm,.01)
    for dx,dy in [(-.9,0),(.8,.5),(0,-.7)]:
        beam('Pod | underhang',sp(x+dx,.5+dy,1.1),sp(x+dx,.5+dy,-1.3-random.random()),.05,dark)
    if side==1:
        box('Pod | antenna base',sp(x,.5,3.15),(.55,.5,.3),steel)
        beam('Pod | antenna',sp(x,.5,3.2),sp(x,.5,5.0),.045,lightarmor)
        cyl('Pod | amber beacon',sp(x,.5,5.05),.05,.1,warm)
# Four independently accessible berths share the layout used by the simulation and HUD.
box('Dock | neck',sp(0,-2.6,.40),(3.3,2,.86),dark,.1)
box('Dock | slab',sp(0,DECK_Y,.435),(16.8,4.5,.65),armor,.14)
box('Dock | inset deck',sp(0,DECK_Y,.79),(16.45,4.18,.08),steel,.06)
for berth in BERTHS:
    x=berth['x'];number=berth['id']
    # Anchors point outward (-Y in Blender / +Z in glTF).
    anchor=bpy.data.objects.new('Berth_'+number,None);groups['Station'].objects.link(anchor)
    anchor.location=sp(x,DECK_Y,BERTH_LAYOUT['deck']);anchor.rotation_euler.z=pi
    for side in [-1,1]:
        box('Dock | yellow side stripe',sp(x+side*1.8,DECK_Y,.845),(.045,3.6,.012),yellow,0)
        for y in [DECK_Y-1.9,DECK_Y+1.9]:
            box('Dock | corner lamp',sp(x+side*1.7,y,.89),(.12,.12,.06),cyan,.01)
    for y in [DECK_Y-1.8,DECK_Y+1.8]:
        box('Dock | yellow end stripe',sp(x,y,.845),(3.6,.045,.012),yellow,0)
    for y in [-1.1,-.5,.1,.7]:
        box('Dock | center approach dash',sp(x,DECK_Y+y,.85),(.04,.22,.012),yellow,0)
    text('Dock | berth number',number,sp(x+1.25,DECK_Y-1.2,.855),.45,yellow)
    box('Dock | underside beam',sp(x,DECK_Y,.02),(.35,4,.35),dark,.04)
    box('Dock | forward armor fascia',sp(x,DECK_Y-2.22,.4),(3.7,.13,.48),steel,.025)
    box('Dock | forward identification band',sp(x,DECK_Y-2.30,.4),(.65,.025,.25),orange,.01)
    for side in [-1,1]:
        box('Dock | rear service cabinet',sp(x+side*1.6,DECK_Y+2.03,1.03),(.3,.22,.4),dark,.025)
        box('Dock | service cabinet cap',sp(x+side*1.6,DECK_Y+2.03,1.25),(.34,.25,.05),lightarmor,.01)
    beam('Dock | diagonal brace',sp(x,DECK_Y-1.7,.25),sp(x*.6,-1.7,-1.4),.12,dark)
    for y in [-1.5,0,1.5]:
        box('Dock | edge signal',sp(x,DECK_Y+y,.54),(.22,.1,.14),warm,.01)
# Armor seams and identification bands emphasize the station's structural rhythm.
for i in range(8):
    a=i*pi/4+pi/8
    box('Hub | identification band',sp(cos(a)*2.2,sin(a)*2.2,3.25),(.65,.08,.2),orange,.01,(0,0,a+pi/2))
for side in [-1,1]:
    for y in [-.5,.5,1.5]:
        box('Pod | identification band',sp(side*5.2,y,3.0),(.42,.6,.04),orange,.01)
# Cloth banner: cut pennant, custom mesh, double-sided PBR material.
bx,by,bz=sp(5.2,-1.45,2.45)
verts=[(bx-0.7,by,bz),(bx+.7,by,bz),(bx+.65,by-.09,bz-2.75),(bx,by-.18,bz-2.48),(bx-.7,by-.12,bz-2.75)]
me=bpy.data.meshes.new('Pennant cut');me.from_pydata(verts,[],[(0,1,2,3,4)]);me.update()
o=bpy.data.objects.new('Banner | hanging pennant',me);groups['Station'].objects.link(o);assign(o,red)
sol=o.modifiers.new('Fabric thickness','SOLIDIFY');sol.thickness=.015
beam('Banner | support bar',sp(4.35,-1.46,2.5),sp(6.05,-1.46,2.5),.04,lightarmor)
text('Banner | insignia','N',sp(5.2,-1.49,1.3),.8,lightarmor,(pi/2,0,0))
# Construction sockets (design section 10): socket A beside the cargo bay (the port pod), socket B
# beside berth 07. Each is a clamp platform with an anchor empty at its deck, where a facility
# module's base sits. No random draws here, so everything generated after is unchanged.
def empty(name,p):
    o=bpy.data.objects.new(name,None);groups[active].objects.link(o);o.location=p;o.empty_display_size=.4;return o
sockets={}
def socket(label,beside,c,top,bridge_from,bridge_to):
    cyl('Socket %s | clamp platform'%label,c+Vector((0,0,top-.2)),1.45,.4,armor,8)
    cyl('Socket %s | deck'%label,c+Vector((0,0,top-.02)),1.3,.06,steel,8)
    ring('Socket %s | mounting guide'%label,c+Vector((0,0,top+.015)),1.12,.022,yellow)
    for i in range(4):
        # On the rim, clear of a module's square footprint (at most 1.25 from its centre).
        a=i*pi/2;q=c+Vector((cos(a)*1.4,sin(a)*1.4,top+.08))
        box('Socket %s | clamp'%label,q,(.26,.26,.16),dark,.02,(0,0,a))
        box('Socket %s | clamp lamp'%label,q+Vector((0,0,.1)),(.08,.08,.04),warm,.005,(0,0,a))
    for i in range(3):
        a=i*2*pi/3+pi/6
        beam('Socket %s | strut'%label,c+Vector((cos(a)*1.0,sin(a)*1.0,top-.4)),c+Vector((cos(a)*.35,sin(a)*.35,top-1.6)),.06,dark)
    beam('Socket %s | bridge'%label,bridge_from,bridge_to,.16,steel)
    beam('Socket %s | power conduit'%label,bridge_from+Vector((0,0,-.22)),bridge_to+Vector((0,0,-.22)),.05,orange)
    anchor=empty('Socket_%s'%label,c+Vector((0,0,top+.01)))
    if label == 'B':
        # Removable blanking plate: kept separate at import and shown only while closed.
        lid=cyl('SocketClosure_B_plate',c+Vector((0,0,top+.12)),1.16,.16,dark,8)
        bpy.context.view_layer.update();w=lid.matrix_world.copy();lid.parent=anchor;lid.matrix_world=w
        for angle in [-pi/4,pi/4]:
            bar=box('SocketClosure_B_cross',c+Vector((0,0,top+.22)),(1.7,.12,.06),lightarmor,.015,(0,0,angle))
            bpy.context.view_layer.update();w=bar.matrix_world.copy();bar.parent=anchor;bar.matrix_world=w
    sockets[label]={'beside':beside,'anchor':anchor}
socket('A','cargo bay',sp(-8.95,.5,0),1.6,sp(-7.1,.5,1.45),sp(-7.6,.5,1.45))
socket('B','berth',sp(10.2,DECK_Y,0),.72,sp(8.4,DECK_Y,.55),sp(8.85,DECK_Y,.55))
# Preserve the original Aurora ship at full size, now classified as a cruiser.
active='Cruiser'; C=layout(-7,4,-8)
def fp(x,y,z):return C+Vector((x,y,z))
def hullmesh(name,sections,m):
    vs=[]
    for y,w,z,h in sections:
        vs += [tuple(fp(-w,y,z-h)),tuple(fp(w,y,z-h)),tuple(fp(w,y,z+h)),tuple(fp(-w,y,z+h))]
    faces=[(3,2,1,0)]
    for j in range(len(sections)-1):
        for i in range(4):faces.append((j*4+i,j*4+(i+1)%4,(j+1)*4+(i+1)%4,(j+1)*4+i))
    k=(len(sections)-1)*4;faces.append((k,k+1,k+2,k+3))
    me=bpy.data.meshes.new(name);me.from_pydata(vs,[],[tuple(reversed(f)) for f in faces]);me.update()
    o=bpy.data.objects.new(name,me);groups[active].objects.link(o);assign(o,m)
    b=o.modifiers.new('Armor edge bevel','BEVEL');b.width=.08;b.segments=1;o.modifiers.new('Hull normals','WEIGHTED_NORMAL');return o
hullmesh('Cruiser | keel',[(-4.5,1.28,0,.55),(-2.8,1.45,0,.65),(1.8,1.2,.04,.6),(4.8,.72,.13,.34),(5.4,.44,.16,.22)],steel)
for side in [-1,1]:
    for i,(y,w) in enumerate([(-3.5,1.3),(-1.85,1.35),(-.15,1.3),(1.55,1.15),(3.1,.9)]):
        box('Cruiser | flank armor',fp(side*w,y,.03),(.32,1.48,1),lightarmor if i%2 else armor,.08)
        box('Cruiser | top armor',fp(side*.67,y,.62),(.98,1.42,.22),armor,.07)
        box('Cruiser | top fastener',fp(side*.98,y-.5,.755),(.1,.12,.04),dark,.01)
        box('Cruiser | flank dark insert',fp(side*(w+.18),y,.1),(.035,.95,.33),dark,.01)
    box('Cruiser | orange hull band',fp(side*1.25,.8,.14),(.36,.48,1.1),orange,.04)
    box('Cruiser | armored shoulder',fp(side*1.63,-2.9,-.05),(.78,2.25,1.15),armor,.12)
    box('Cruiser | rear engine housing',fp(side*.92,-4.4,-.08),(1.18,1.42,1.2),dark,.15)
    box('Cruiser | engine armor cap',fp(side*.92,-4.25,.56),(1.3,1.42,.25),armor,.1)
    box('Cruiser | engine surround',fp(side*.92,-5.1,-.05),(.94,.14,.78),steel,.09)
    box('Cruiser | engine luminous core',fp(side*.92,-5.19,-.05),(.62,.025,.43),core,.04)
    box('Cruiser | engine marker',fp(side*.92,-5.21,.42),(.3,.025,.1),warm,.01)
    box('Cruiser | forward green nav',fp(side*.9,3.4,.3),(.07,.4,.08),cyan,.01)
    # Visible exhaust: nested tapered transparent emissive volumes, GLB-friendly mesh glow.
    plume=mat('Drive | transparent blue plume '+str(side),(.015,.1,1),0,.6,1.2)
    p=plume.node_tree.nodes.get('Principled BSDF');p.inputs['Alpha'].default_value=.12
    plume.surface_render_method='DITHERED'
    o=beam('Cruiser | ion exhaust',fp(side*.92,-5.25,-.05),fp(side*.92,-8.6,-.05),.32,plume,24)
    # Taper the aft end, keeping the nozzle bright and the tail fine.
    for v in o.data.vertices:
        if v.co.z>0:v.co.x*=.06;v.co.y*=.06
    light=point('Cruiser | engine blue spill',fp(side*.92,-5.55,-.05),(.015,.24,1),65,.4)
    groups['Lighting'].objects.unlink(light);groups['Cruiser'].objects.link(light)
box('Cruiser | dorsal spine',fp(0,-.6,.87),(.63,5.7,.38),dark,.1)
box('Cruiser | bridge superstructure',fp(0,1.7,.95),(1.12,1.55,.52),armor,.12)
box('Cruiser | recessed bridge glazing',fp(0,2.49,1.0),(.84,.035,.21),dark,.03)
for x in [-.28,0,.28]:box('Cruiser | bridge window',fp(x,2.515,1.03),(.2,.025,.09),cyan,.01)
for y in [-2.5,-1.5,-.5]:
    box('Cruiser | radiator bed',fp(0,y,1.1),(.52,.65,.09),dark,.02)
    for j in range(5):box('Cruiser | radiator fin',fp(0,y+(j-2)*.11,1.17),(.55,.04,.09),armor,.01)
for x in [-.65,.65]:
    cyl('Cruiser | point defense base',fp(x,-1.8,.88),.24,.23,dark,10)
    beam('Cruiser | point defense barrel',fp(x,-1.7,1.04),fp(x,-.8,1.04),.055,dark)
text('Cruiser | hull registry','AURORA',fp(.15,3.45,.66),.2,dark)
beam('Cruiser | comms aerial',fp(-.4,-.5,1.05),fp(-.4,-.5,1.8),.018,lightarmor,6)
# Starter frigate: a separate, simpler hull in the same design family, landed on berth 07.
active='Frigate'; F=sp(0,-4.3,1.30)
def fp(x,y,z):return F+Vector((x,y,z))
idle=mat('Drive | idle cyan',(.025,.28,.5),.1,.45,.65)
hullmesh('Frigate | keel',[(-1.12,.43,0,.18),(-.55,.52,0,.21),(.6,.40,.025,.18),(1.30,.17,.02,.12)],steel)
# Narrow twin drive pods, split armor, and a single orange band echo the cruiser.
for side in [-1,1]:
    box('Frigate | engine housing',fp(side*.35,-1.10,-.015),(.40,.50,.39),dark,.045)
    box('Frigate | engine armor cap',fp(side*.35,-1.05,.20),(.44,.49,.10),armor,.03)
    box('Frigate | idle engine aperture',fp(side*.35,-1.356,-.015),(.23,.018,.20),idle,.015)
    for y in [-.60,.12]:
        box('Frigate | shoulder armor',fp(side*.53,y,.015),(.20,.63,.37),lightarmor,.035)
        box('Frigate | dorsal armor',fp(side*.29,y,.245),(.39,.61,.10),armor,.025)
        box('Frigate | flank recess',fp(side*.638,y,.01),(.015,.36,.13),dark,.005)
    box('Frigate | orange identification band',fp(side*.53,-.18,.04),(.23,.15,.41),orange,.02)
    box('Frigate | nav marker',fp(side*.41,.68,.15),(.055,.12,.045),cyan,.005)
    for y in [-.74,.25]:box('Frigate | armor fastener',fp(side*.40,y,.30),(.04,.055,.02),dark,.003)
box('Frigate | compact spine',fp(0,-.22,.29),(.19,1.47,.17),dark,.025)
box('Frigate | bridge',fp(0,.45,.34),(.51,.59,.25),armor,.045)
box('Frigate | bridge glazing',fp(0,.751,.37),(.37,.018,.10),idle,.008)
box('Frigate | bridge mullion',fp(0,.765,.37),(.025,.015,.12),dark,0)
for j in range(4):box('Frigate | cooling fin',fp(0,-.71+j*.115,.41),(.28,.036,.07),armor,.006)
cyl('Frigate | single defense mount',fp(0,-.15,.44),.12,.10,dark,8)
beam('Frigate | short defense barrel',fp(0,-.1,.50),fp(0,.20,.50),.025,dark,6)
beam('Frigate | short aerial',fp(-.19,.4,.47),fp(-.19,.4,.70),.009,lightarmor,5)
# Deployed tripod: lowest foot surface is exactly z=0.86 above the station origin.
for x,y in [(-.46,-.72),(.46,-.72),(0,.83)]:
    beam('Frigate | landing strut',fp(x*.82,y,-.14),fp(x,y,-.38),.035,lightarmor,6)
    box('Frigate | landing foot',fp(x,y,-.41),(.19,.25,.06),dark,.015)
text('Frigate | hull number','01',fp(.20,.84,.205),.11,dark)
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
# Facility modules (design section 10): three facilities at two tiers, exported separately to
# facilities.glb. Each is built around its own root, its base at the root, so a game can set the
# root on a socket anchor. Every tier changes the outline enough to read with the whole outpost in
# view. Here they stand in a catalogue row out of the overview camera's frame.
active='Facilities'
facility_roots=[]
def facility(fid,tier,slot,build):
    Q=layout(-9+slot*3.6,-32)
    build(lambda x,y,z:Q+Vector((x,y,z)))
    # A count of live service indicators shows effective tier without changing owned silhouette.
    for level in range(1,tier+1):
        box('Operating_Tier_%d'%level,Q+Vector((-.35+(level-1)*.7,-1.15,.32)),(.3,.06,.16),cyan,.01)
    root=bpy.data.objects.new('Facility_%s_T%d'%(fid,tier),None);groups[active].objects.link(root);root.location=Q
    root['facility']=fid;root['tier']=tier;root['units']='metres';root['up_axis']='+Z'
    bpy.context.view_layer.update()
    for o in list(groups[active].objects):
        if o.parent is None and o is not root and o.type!='EMPTY':
            w=o.matrix_world.copy();o.parent=root;o.matrix_world=w
    facility_roots.append(root)
def sensor_t1(q):
    cyl('Sensor | plinth',q(0,0,.25),1.0,.5,steel,8)
    cyl('Sensor | collar',q(0,0,.55),.7,.12,armor,8)
    cyl('Sensor | mast',q(0,0,1.25),.12,1.4,lightarmor,8)
    d=cyl('Sensor | dish',q(.15,0,2.05),.9,.32,lightarmor,24,r2=.18);d.rotation_euler=(0,math.radians(-35),0)
    beam('Sensor | feed arm',q(.15,0,2.05),q(.75,0,2.55),.03,dark)
    cyl('Sensor | feed horn',q(.75,0,2.55),.07,.14,cyan,8)
    for i in range(4):
        a=i*pi/2;box('Sensor | status light',q(cos(a)*.98,sin(a)*.98,.42),(.1,.1,.08),cyan,.01,(0,0,a))
def sensor_t2(q):
    cyl('Sensor | plinth',q(0,0,.25),1.15,.5,steel,8)
    cyl('Sensor | collar',q(0,0,.55),.95,.12,armor,8)
    for i in range(4):
        a=pi/4+i*pi/2
        beam('Sensor | lattice leg',q(cos(a)*.8,sin(a)*.8,.6),q(cos(a)*.18,sin(a)*.18,3.6),.05,lightarmor,6)
    for z in [1.4,2.3,3.1]:
        r=.8-(z-.6)/3.0*.62;ring('Sensor | lattice ring',q(0,0,z),r*1.02,.025,dark)
    for side,z,r in [(1,1.75,.85),(-1,2.6,.65)]:
        d=cyl('Sensor | dish',q(side*.45,0,z),r,.3,lightarmor,24,r2=.16);d.rotation_euler=(0,math.radians(-40*side),0)
        cyl('Sensor | feed horn',q(side*.9,0,z+.35),.06,.12,cyan,8)
    box('Sensor | phased array',q(0,0,3.95),(1.7,.1,.8),dark,.02)
    for j in range(5):box('Sensor | array tile',q((j-2)*.32,-.06,3.95),(.26,.02,.62),cyan,.005)
    cyl('Sensor | beacon',q(0,0,4.45),.06,.16,white,6)
    for i in range(8):
        a=i*pi/4;box('Sensor | status light',q(cos(a)*1.12,sin(a)*1.12,.42),(.1,.1,.08),cyan,.01,(0,0,a))
def refinery_t1(q):
    box('Refinery | skid',q(0,0,.15),(2.3,1.9,.3),dark,.04)
    for x in [-.55,.55]:
        cyl('Refinery | tank',q(x,.25,1.1),.45,1.6,steel,16)
        cyl('Refinery | tank cap',q(x,.25,1.95),.38,.12,armor,16)
        ring('Refinery | tank band',q(x,.25,.75),.46,.03,orange)
    beam('Refinery | manifold',q(-.55,-.35,.6),q(.55,-.35,.6),.08,orange)
    for x in [-.55,.55]:beam('Refinery | riser',q(x,-.35,.6),q(x,.0,1.2),.05,orange)
    cyl('Refinery | stack',q(.75,-.6,1.2),.15,2.1,dark,10)
    cyl('Refinery | stack glow',q(.75,-.6,2.3),.12,.1,warm,10)
    box('Refinery | control hut',q(-.7,-.6,.55),(.55,.45,.5),armor,.03)
    box('Refinery | hut window',q(-.7,-.84,.6),(.36,.02,.14),warm,.005)
def refinery_t2(q):
    box('Refinery | skid',q(0,0,.15),(2.4,2.3,.3),dark,.04)
    for x,y,h in [(-.65,.5,2.4),(.65,.5,2.4),(-.65,-.35,1.4),(.65,-.35,1.4)]:
        cyl('Refinery | tank',q(x,y,.3+h/2),.48,h,steel,16)
        cyl('Refinery | tank cap',q(x,y,.3+h+.05),.4,.12,armor,16)
        for z in [.3+h*.3,.3+h*.7]:ring('Refinery | tank band',q(x,y,z),.49,.03,orange)
    cyl('Refinery | crusher hopper',q(0,-.7,1.1),.45,.7,armor,8,r2=.18)
    for x in [-1.0,1.0]:
        cyl('Refinery | stack',q(x,-.95,1.7),.16,2.9,dark,10)
        cyl('Refinery | stack glow',q(x,-.95,3.2),.13,.12,warm,10)
    ring('Refinery | catwalk',q(0,.5,2.0),.68,.035,lightarmor)
    beam('Refinery | header pipe',q(-.65,.5,2.6),q(.65,.5,2.6),.08,orange)
    beam('Refinery | transfer pipe',q(0,-.7,1.45),q(0,.5,2.0),.07,orange)
def fabrication_t1(q):
    box('Fabrication | hangar',q(0,.1,.75),(2.1,1.6,1.4),armor,.08)
    box('Fabrication | roof',q(0,.1,1.5),(2.2,1.7,.12),lightarmor,.03)
    box('Fabrication | door',q(0,-.71,.62),(1.3,.04,1.0),dark,.01)
    for j in range(5):box('Fabrication | hazard stripe',q(-.6+j*.3,-.74,.08),(.14,.02,.1),yellow,0)
    box('Fabrication | crane base',q(.75,.45,1.68),(.3,.3,.25),dark,.02)
    beam('Fabrication | crane arm',q(.75,.45,1.8),q(-.4,.45,2.15),.06,orange)
    beam('Fabrication | crane hoist',q(-.4,.45,2.15),q(-.4,.45,1.75),.02,dark)
    for x in [-.85,.85]:box('Fabrication | window',q(x,-.71,1.2),(.25,.02,.12),warm,.005)
def fabrication_t2(q):
    box('Fabrication | hangar',q(0,.1,.95),(2.3,2.0,1.8),armor,.08)
    box('Fabrication | roof',q(0,.1,1.9),(2.4,2.1,.12),lightarmor,.03)
    for x in [-.55,.55]:
        box('Fabrication | door',q(x,-.91,.75),(.95,.04,1.3),dark,.01)
        for j in range(3):box('Fabrication | hazard stripe',q(x-.3+j*.3,-.94,.08),(.14,.02,.1),yellow,0)
    for x in [-1.15,1.15]:
        beam('Fabrication | gantry post',q(x,-1.0,.0),q(x,-1.0,2.6),.07,dark)
        beam('Fabrication | gantry post',q(x,1.15,.0),q(x,1.15,2.6),.07,dark)
        beam('Fabrication | gantry rail',q(x,-1.0,2.6),q(x,1.15,2.6),.07,orange)
    beam('Fabrication | gantry beam',q(-1.15,.1,2.6),q(1.15,.1,2.6),.09,orange)
    box('Fabrication | trolley',q(.3,.1,2.48),(.4,.35,.2),dark,.02)
    beam('Fabrication | robot arm',q(.3,.1,2.38),q(.0,-.4,2.05),.05,lightarmor)
    beam('Fabrication | robot forearm',q(.0,-.4,2.05),q(-.25,-.65,2.15),.04,lightarmor)
    for x in [-1.0,0,1.0]:box('Fabrication | window',q(x,-.91,1.65),(.25,.02,.12),warm,.005)
for slot,(fid,tier,build) in enumerate([('sensor_array',1,sensor_t1),('sensor_array',2,sensor_t2),('refinery',1,refinery_t1),('refinery',2,refinery_t2),('fabrication_bay',1,fabrication_t1),('fabrication_bay',2,fabrication_t2)]):
    facility(fid,tier,slot,build)
# Camera and film lighting.
active='Cameras'
bpy.ops.object.camera_add(location=layout(0,0,60));cam=move(bpy.context.object,'Camera | tactical overview');cam.rotation_euler=(-view).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=40;scene.camera=cam
bpy.ops.object.camera_add(location=F+view*12);detail=move(bpy.context.object,'Camera | starter frigate detail');detail.rotation_euler=(-view).to_track_quat('-Z','Y').to_euler();detail.data.type='ORTHO';detail.data.ortho_scale=4.2
bpy.ops.object.camera_add(location=layout(0,-31.4,60));catalogue=move(bpy.context.object,'Camera | facility catalogue');catalogue.rotation_euler=(-view).to_track_quat('-Z','Y').to_euler();catalogue.data.type='ORTHO';catalogue.data.ortho_scale=23
active='Lighting'
world=bpy.data.worlds.new('Deep space');scene.world=world;world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.035,.06,.11,1);world.node_tree.nodes['Background'].inputs[1].default_value=.25
d=bpy.data.lights.new('Sun | cool starlight','SUN');d.energy=2.4;d.angle=.1;d.color=(.67,.77,1)
o=bpy.data.objects.new(d.name,d);groups[active].objects.link(o);o.rotation_euler=(.45,-.6,-.5)
def area(name,pos,power,color,size,target):
    d=bpy.data.lights.new(name,'AREA');d.energy=power;d.color=color;d.shape='DISK';d.size=size
    o=bpy.data.objects.new(name,d);groups[active].objects.link(o);o.location=pos;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
area('Key | soft warm rim',sp(-7,-1,16),1800,(1,.62,.37),10,S)
area('Fill | cool camera bounce',layout(-4,2,19),1800,(.3,.48,1),14,S)
point('Station | approach light',sp(0,-2.5,2.0),(1,.32,.06),60,1)
# Game-oriented collection roots and metadata. Recenter each assembly at its pivot.
for group,pivot in [('Station',S),('Cruiser',C),('Frigate',F)]:
    root=bpy.data.objects.new(group+'_ROOT',None);groups[group].objects.link(root);root.location=pivot
    bpy.context.view_layer.update()
    root['asset_type']=group.lower();root['units']='metres';root['forward_axis']='+Y';root['up_axis']='+Z'
    for o in list(groups[group].objects):
        if o!=root:
            w=o.matrix_world.copy();o.parent=root;o.matrix_world=w
frigate_root=bpy.data.objects['Frigate_ROOT']
frigate_root.location.x+=BERTHS[0]['x']
frigate_root.rotation_euler.z=pi
# Correct the small asymmetric fore/aft extent so its bounds centre matches the berth.
frigate_root.location.y+=2*(DECK_Y+4.3)
bpy.data.objects['Cruiser_ROOT'].rotation_euler.z=math.radians(-35)
bpy.context.view_layer.update()
# The detail camera follows berth 07 after the apron is expanded.
detail.location=frigate_root.location+view*12
detail.rotation_euler=(-view).to_track_quat('-Z','Y').to_euler()
# Validate the complete evaluated frigate, including bevels, gear and aerials.
bpy.context.view_layer.update()
depsgraph=bpy.context.evaluated_depsgraph_get()
station_inverse=bpy.data.objects['Station_ROOT'].matrix_world.inverted()
points=[]
for obj in groups['Frigate'].objects:
    if obj.type not in {'MESH','FONT'}:continue
    evaluated=obj.evaluated_get(depsgraph);mesh=evaluated.to_mesh()
    points.extend(station_inverse @ (obj.matrix_world @ v.co) for v in mesh.vertices)
    evaluated.to_mesh_clear()
lo=[min(p[i] for p in points) for i in range(3)];hi=[max(p[i] for p in points) for i in range(3)]
assert BERTH_LAYOUT['halfWidth'] >= 1.3+.15 and BERTH_LAYOUT['halfLength'] >= 1.3+.15, 'Berths must clear the gameplay collision envelope'
# Check every parked hull and the complete straight departure corridor against station solids.
obstacles=[]
for obj in groups['Station'].objects:
    if obj.type!='MESH':continue
    if any(label in obj.name for label in ['stripe','dash','berth H','berth number']):continue
    evaluated=obj.evaluated_get(depsgraph);mesh=evaluated.to_mesh()
    pts=[station_inverse @ (obj.matrix_world @ v.co) for v in mesh.vertices]
    evaluated.to_mesh_clear()
    obstacles.append((obj.name,[min(p[i] for p in pts) for i in range(3)],[max(p[i] for p in pts) for i in range(3)]))
checks=[]
for berth in BERTHS:
    shift=berth['x']-BERTHS[0]['x']
    lower=[lo[0]+shift,lo[1],lo[2]];upper=[hi[0]+shift,hi[1],hi[2]]
    boundary=[berth['x']-BERTH_LAYOUT['halfWidth'],berth['x']+BERTH_LAYOUT['halfWidth'],DECK_Y-BERTH_LAYOUT['halfLength'],DECK_Y+BERTH_LAYOUT['halfLength']]
    clearance=min(lower[0]-boundary[0],boundary[1]-upper[0],lower[1]-boundary[2],boundary[3]-upper[1])
    assert clearance>=.15, ('Frigate overlaps pad safety boundary',berth,clearance)
    assert abs(lower[2]-BERTH_LAYOUT['deck'])<.002, ('Landing feet are not on deck',lower)
    # Swept AABB to the outside of the apron; paint overlays are excluded above.
    swept=[lower[0],DECK_Y-5,lower[2]]
    for name,obs_lo,obs_hi in obstacles:
        assert any(upper[i]<=obs_lo[i]+.0001 or swept[i]>=obs_hi[i]-.0001 for i in range(3)), ('Departure intersects station',berth['id'],name)
    checks.append({'berth':berth['id'],'anchor_station_space':[berth['x'],DECK_Y,BERTH_LAYOUT['deck']],'ship_bounds_station_space':{'min':lower,'max':upper},'safety_boundary_xy':boundary,'minimum_edge_clearance':clearance,'departure_clear':True})
for i,a in enumerate(checks):
    for b in checks[i+1:]:
        assert a['ship_bounds_station_space']['max'][0]+.15 < b['ship_bounds_station_space']['min'][0], ('Parked ships overlap',a,b)
# Socket footprints must also stay outside all departure lanes, even with tier-two modules.
for info in sockets.values():
    anchor=station_inverse@info['anchor'].matrix_world.translation
    for berth in BERTHS:
        assert abs(anchor.x-berth['x'])>1.25+.65+.15 or anchor.y>DECK_Y+1.34+1.25+.15
clearance=min(c['minimum_edge_clearance'] for c in checks)
docking={'berth':'07','landed':True,'ship_bounds_station_space':checks[0]['ship_bounds_station_space'],'safety_boundary_xy':checks[0]['safety_boundary_xy'],'minimum_edge_clearance':clearance,'landing_foot_height':lo[2],'structural_obstacles_checked':len(obstacles),'connector_top_height':.83,'berths':checks,'inter_ship_clearance':4-(hi[0]-lo[0])}
scene['starter_frigate_pad_clearance']=clearance
print('DOCK_FIT_OK',json.dumps(docking),flush=True)
scene['description']='Frontier Outpost — four-berth industrial station'
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
for name in ['Station','Cruiser','Frigate','Asteroids','Facilities']:
    bpy.ops.object.select_all(action='DESELECT')
    for o in groups[name].objects:
        if 'ion exhaust' not in o.name:o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,name.lower()+'.glb'),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
bpy.ops.object.select_all(action='DESELECT')
manifest={'units':'metres','blender_up':'+Z','blender_forward':'+Y','gltf_up':'+Y','seed':2266,'assets':{name:{'objects':len(groups[name].objects),'triangles':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in groups[name].objects if o.type=='MESH')} for name in ['Station','Cruiser','Frigate','Asteroids','Facilities']},'notes':'GLBs retain scene placement. Berths 07–10 use berths.json; the reference frigate faces outward on 07. Preview crew copies are excluded from GLBs. Aurora cruiser stays at its original scale. Backdrop and drive plumes are presentation effects.'}
manifest['docking_validation']=docking
station_root=bpy.data.objects['Station_ROOT']
manifest['sockets']={'Socket_'+label:{'beside':info['beside'],'anchor_station_space':[round(v,4) for v in station_root.matrix_world.inverted()@info['anchor'].matrix_world.translation]} for label,info in sockets.items()}
def footprint(root):
    pts=[]
    for o in root.children_recursive:
        if o.type!='MESH':continue
        ev=o.evaluated_get(depsgraph);me=ev.to_mesh()
        pts.extend(root.matrix_world.inverted()@(o.matrix_world@v.co) for v in me.vertices);ev.to_mesh_clear()
    lo=[min(p[i] for p in pts) for i in range(3)];hi=[max(p[i] for p in pts) for i in range(3)]
    return lo,hi
bpy.context.view_layer.update();depsgraph=bpy.context.evaluated_depsgraph_get()
manifest['facilities']={}
for root in facility_roots:
    lo,hi=footprint(root)
    # A module must sit on its socket's deck, inside the rim clamps, without hanging below it.
    assert max(abs(lo[0]),abs(hi[0]),abs(lo[1]),abs(hi[1]))<=1.25,(root.name,'overhangs its socket',lo,hi)
    assert lo[2]>=-.001,(root.name,'reaches below its base',lo[2])
    meshes=[o for o in root.children_recursive if o.type=='MESH']
    manifest['facilities'][root.name]={'facility':root['facility'],'tier':root['tier'],'objects':len(meshes),'triangles_base_mesh':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in meshes),'bounds_root_space':{'min':[round(v,4) for v in lo],'max':[round(v,4) for v in hi]}}
heights={root.name:manifest['facilities'][root.name]['bounds_root_space']['max'][2] for root in facility_roots}
for fid in ['sensor_array','refinery','fabrication_bay']:
    # Tier 2 must read as bigger at the zoom where the outpost fits (design section 2).
    assert heights['Facility_%s_T2'%fid]>heights['Facility_%s_T1'%fid]+.4,(fid,'tiers too alike in height')
for name, stats in manifest['assets'].items():
    stats['triangles_base_mesh'] = stats.pop('triangles')
    with open(os.path.join(OUT, name.lower()+'.glb'), 'rb') as f:
        f.seek(12); length, chunk_type = struct.unpack('<II', f.read(8)); gltf = json.loads(f.read(length))
    stats['triangles_export'] = sum(gltf['accessors'][p['indices']]['count']//3 for m in gltf['meshes'] for p in m['primitives'])
with open(os.path.join(OUT,'scene_manifest.json'),'w') as f:json.dump(manifest,f,indent=2)
# Reference overview displays four ships, but their copies are not part of any gameplay GLB.
preview_crew=bpy.data.collections.new('Docked crew | preview only');scene.collection.children.link(preview_crew)
for berth in BERTHS[1:]:
    delta=Vector((berth['x']-BERTHS[0]['x'],0,0))
    for source in list(groups['Frigate'].objects):
        if source.type not in {'MESH','FONT'}:continue
        copy=source.copy();copy.data=source.data;copy.parent=None
        preview_crew.objects.link(copy);copy.matrix_world=source.matrix_world.copy();copy.location+=delta
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'FrontierOutpost.blend'), compress=True)
if not os.environ.get('SKIP_RENDER'):
    bpy.ops.render.render(write_still=True)
    scene.camera=detail;scene.render.resolution_x=1000;scene.render.resolution_y=1000
    scene.render.filepath=os.path.join(OUT,'starter_frigate_detail.png');scene.cycles.samples=min(scene.cycles.samples,32)
    bpy.ops.render.render(write_still=True)
    scene.camera=catalogue;scene.render.resolution_x=1600;scene.render.resolution_y=500
    scene.render.filepath=os.path.join(OUT,'facilities_preview.png')
    bpy.ops.render.render(write_still=True)

print('SCENE_COMPLETE',OUT,flush=True)
