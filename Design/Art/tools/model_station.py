"""One-time M9 station art study. Creates editable sources; refuses to overwrite them.

blender -b --python-exit-code 1 --python Design/Art/tools/model_station.py
Exports remain the responsibility of frontier_art.py. No gameplay rules are changed.
"""
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frontier_art as art
from model_crew_quarters import part


def box(root, name, pos, size, role="Armor", bevel=.035):
    return part(root, name, pos, size, role, bevel=bevel)


def cylinder(root, name, pos, radius, depth, role="HullSteel", vertices=16):
    return part(root, name, pos, (radius, radius, depth / 2), role, "cylinder", vertices=vertices, bevel=0)


def wedge(root, name, inner, outer, a, b, bottom, top, role):
    vertices = [(r * math.cos(t), r * math.sin(t), z)
                for z in [bottom, top] for r, t in [(inner,a),(outer,a),(outer,b),(inner,b)]]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.data.collections["Export"].objects.link(obj)
    obj.parent = root
    mesh.materials.append(bpy.data.materials[role])
    return obj


def ring(root, interior=False):
    # 32.4 m diameter, ~2.2x the old station's 14.64 m width. Mouth faces +Y.
    for i in range(32):
        a, b = 2*math.pi*i/32, 2*math.pi*(i+1)/32
        mid = (a+b)/2
        # Keep a full 8 m wide slot in the +Y ring for the hangar's straight lane.
        if math.sin(mid) > 0 and abs(14*math.cos(mid)) < 5.3:
            continue
        if interior:
            wedge(root, f"Cutaway ring foundation {i:02}", 12.6, 16.2, a+.008,b-.008,-1.25,-.1,"HullSteel")
            wedge(root, f"Cutaway deck plate {i:02}", 12.8, 16, a+.015,b-.015,-.1,0,"Armor")
        else:
            wedge(root, f"Ring pressure hull {i:02}", 12.6, 16.1, a+.007,b-.007,-1.8,.75,"HullSteel")
            wedge(root, f"Ring upper armour {i:02}", 12.5, 16.2, a+.018,b-.018,.75,1.18,"Titanium" if i%4==0 else "Armor")
            wedge(root, f"Ring lower armour {i:02}", 12.8, 16.2, a+.016,b-.016,-2.05,-1.8,"Armor")
            for n in range(4):
                t = a+.04+(b-a-.08)*n/3
                x,y=16.14*math.cos(t),16.14*math.sin(t)
                o=box(root,f"Inhabited window {i}-{n}",(x,y,-.38),(.34,.04,.17),"Amber",.008)
                o.rotation_euler.z=t-math.pi/2
            wedge(root,f"Ring service band {i}",13,13.4,a+.03,b-.03,1.18,1.23,"Orange" if i%4==0 else "Graphite")
            if i%4==0:
                x,y=14.4*math.cos(mid),14.4*math.sin(mid)
                box(root,f"Suspended machinery {i}",(x,y,-2.9),(1.1,1.1,2.1),"Graphite")
                box(root,f"Machinery armour {i}",(x+.3,y,-2.7),(.6,1.14,1.4))


def finish(kind):
    bpy.context.view_layer.update()
    errors, records = art.check_asset()
    if errors:
        raise ValueError("\n".join(errors))
    scene=bpy.context.scene
    scene["art_status"]="M9 art study: scale and gameplay integration pending greybox review"
    art.setup_presentation(scene, Vector((0,0,-.5)), 41)
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    print(kind, {k:v['triangles'] for k,v in records.items()})


def exterior():
    art.new_asset("station_exterior","exterior")
    root=bpy.data.objects["StationExterior_ROOT"]
    ring(root)
    # A stepped, armoured hub and a short command crown replace the reference's tall forest.
    cylinder(root,"Lower hub",(0,0,-2.4),7.6,3.6)
    cylinder(root,"Hub pressure drum",(0,-1,0),7.2,4.0)
    cylinder(root,"Hub upper collar",(0,-1,2.08),7.5,.4,"Graphite")
    cylinder(root,"Command drum",(0,-1,2.65),5.0,1.0,"Armor")
    for i in range(16):
        a,b=2*math.pi*i/16,2*math.pi*(i+1)/16
        o=wedge(root,f"Hub segmented armour {i}",5.15,7.3,a+.025,b-.025,1.65,2.45,"Titanium" if i%4==0 else "Armor")
        o.location.y=-1
        t=(a+b)/2
        for n in range(3):
            u=a+.1+n*.095
            o=box(root,f"Command windows {i}-{n}",(5.03*math.cos(u),-1+5.03*math.sin(u),2.65),(.25,.035,.22),"Amber",.005)
            o.rotation_euler.z=u-math.pi/2
    box(root,"Command bridge",(0,-1.5,3.6),(4.5,3.2,1.1))
    box(root,"Bridge observation band",(0,-3.12,3.6),(3.7,.04,.25),"Amber",0)
    for x,h in [(-1.8,3.0),(.0,4.8),(1.8,2.5)]:
        box(root,f"Short command pylon {x}",(x,-1.4,4.1+h/2),(.85,.95,h),"Graphite",.025)
        box(root,f"Command pylon plate {x}",(x+.25,-1.4,4.1+h*.35),(.55,1.02,h*.65))
        box(root,f"Command pylon lamp {x}",(x,-1.89,4.7),(.16,.02,.9),"Amber",0)
    for side in [-1,1]:
        box(root,f"Ring bastion {side}",(side*12.8,5,1.8),(2.2,2.4,3.4),"HullSteel")
        box(root,f"Bastion armour {side}",(side*12.8,4.4,2.1),(2.35,1.2,2.4))
        for z in [1.3,1.8,2.3]:
            box(root,f"Bastion windows {side}-{z}",(side*12.8,3.78,z),(1.4,.04,.17),"Amber",0)
        box(root,f"Bastion aerial {side}",(side*12.8,5,4.0),(.14,.14,1.8),"Titanium",0)
    # Radial shoulders stop before the clear +Y mouth/approach.
    for side in [-1,1]:
        box(root,f"Radial service bridge {side}",(side*9.8,-1,-.1),(7,2.8,1.4))
        for x in [8,10,12]:
            box(root,f"Bridge armour {side}-{x}",(side*x,-1,.72),(1.7,3.1,.22),"Titanium")
        # Forward hangar: roof stripes are readable even from the required rear camera.
        box(root,f"Hangar cheek {side}",(side*5.2,9,1.5),(2.0,9,3.0))
        box(root,f"Mouth jamb {side}",(side*4.28,13.4,1.9),(.3,.65,3.8),"Titanium")
        box(root,f"Mouth lamp {side}",(side*4.1,13.75,1.9),(.06,.04,3.3),"Amber",0)
        for y in [7,9,11]:
            box(root,f"Hangar roof stripe {side}-{y}",(side*5.2,y,3.06),(1.25,.35,.1),"Orange",0)
    box(root,"Hangar roof",(0,9.2,4.05),(10.6,8.8,.5))
    box(root,"Hangar roof inset",(0,9.2,4.32),(6.8,6.8,.08),"Graphite")
    for y in [7,8.6,10.2,11.8]:
        box(root,f"Hangar roof rib {y}",(0,y,4.42),(7.6,.16,.14),"Armor",.02)
    box(root,"Mouth lintel",(0,13.4,3.85),(8.6,.65,.3),"Titanium")
    box(root,"Mouth illuminated header",(0,13.75,3.68),(7.7,.04,.12),"Amber",0)
    box(root,"Hangar threshold deck",(0,10,-.18),(8.2,8,.36),"Graphite")
    box(root,"Hangar back bulkhead",(0,5.85,1.8),(8.2,.2,3.6),"Graphite")
    for x in [-3,-1,1,3]:
        box(root,f"Hangar internal lamp {x}",(x,6,2.3),(.28,.05,1.5),"Amber",0)
    # Underslung spine carries the vertical mass without concealing flying ships.
    for r,z,d in [(4,-5,2),(2.7,-7,2),(1.65,-8.6,1.2),(.7,-10,1.6)]:
        cylinder(root,f"Suspended core {r}",(0,0,z),r,d)
        for x in [-r*.7,r*.7]:
            box(root,f"Core warm slit {r}-{x}",(x,-r*.72,z),(.12,.08,d*.65),"Amber",0)
    for name,pos in {"Tell_A":(10,-9,1.18),"Tell_B":(14,0,1.18),"Tell_C":(-10,-9,1.18),"Tell_reactor":(0,-14,1.18)}.items():
        bpy.data.objects[name].location=pos
        box(root,name+" mounting plinth",(pos[0],pos[1],1.1),(1.7,1.7,.16),"Graphite",.02)
    mouth=bpy.data.objects["Hangar_Mouth"]
    mouth.location=(0,14,0)
    mouth["opening_width"]=8.0
    mouth["opening_height"]=3.5
    finish("exterior")


def interior():
    art.new_asset("station_interior","interior")
    root=bpy.data.objects["StationInterior_ROOT"]
    ring(root,True)
    cylinder(root,"Cutaway hub foundation",(0,0,-1),8.0,1.8)
    cylinder(root,"Cutaway hub deck",(0,0,-.08),7.8,.16,"Armor")
    box(root,"Hangar deck",(2,8,-.35),(12,12,.7),"HullSteel")
    box(root,"Hangar inset deck",(2,8,-.04),(11.4,11.4,.08),"Graphite",0)
    # Equal-height tall modules must have disjoint screen projections, not only floor footprints.
    positions={"Socket_A":(-6,-6,0),"Socket_B":(6,0,0),"Socket_C":(-6,6,0),"Parking_frigate":(1,10,0)}
    for name,pos in positions.items():
        bpy.data.objects[name].location=pos
        if name.startswith("Socket"):
            box(root,name+" mounting deck",(pos[0],pos[1],-.16),(3.2,3.2,.32),"Titanium")
            for side in [-1,1]:
                box(root,name+f" deck border {side}",(pos[0]+side*1.46,pos[1],-.008),(.08,2.9,.016),"Ochre",0)
    box(root,"Closure_C plate",(-6,6,.08),(2.5,2.5,.16),"Graphite")
    for a in [-math.pi/4,math.pi/4]:
        o=box(root,"Closure_C cross "+str(a),(-6,6,.18),(2.8,.12,.04),"Titanium",0)
        o.rotation_euler.z=a
    # Cargo beside A; open-view low machinery stays outside all projected module envelopes.
    for x in [-1.2,.1,1.4]:
        box(root,f"Cargo container {x}",(x,-8,.4),(1.1,1.8,.8),"Titanium")
        box(root,f"Cargo latch {x}",(x,-8.92,.4),(.18,.04,.45),"Orange",0)
    box(root,"Processor skid",(-11,0,.15),(2.2,3,.3),"Graphite")
    cylinder(root,"Processor furnace",(-11,0,.65),.65,1,"Armor",12)
    box(root,"Processor hopper",(-11,1.05,.65),(1.4,.8,1.0),"Titanium")
    box(root,"Processor output chute",(-11,-1.0,.4),(.9,1.3,.25),"Orange")
    cylinder(root,"Reactor_Upgrade drum",(0,0,.8),1.1,1.6,"Graphite",12)
    cylinder(root,"Reactor_Upgrade collar",(0,0,1.2),1.25,.18,"Titanium",12)
    for x in [-.7,0,.7]:
        box(root,"Reactor_Upgrade lamp "+str(x),(x,-.8,.8),(.14,.12,1.0),"Cyan",0)
    for x in [-3.5,7.5]:
        for y in [5,7,9,11,13]:
            box(root,f"Hangar lane guide {x}-{y}",(x,y,-.005),(.12,.75,.01),"Ochre",0)
    finish("interior")


if __name__=="__main__":
    # Preflight both paths before creating either asset.
    for kind,identifier in [("station_exterior","exterior"),("station_interior","interior")]:
        if art.asset_path(kind,identifier).exists():
            raise ValueError("Refusing to replace an existing station source")
    exterior()
    interior()
