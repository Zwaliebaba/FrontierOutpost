"""One-time architectural mounting pass for M9. Sources stay editable.

blender -b --python-exit-code 1 --python Design/Art/tools/integrate_facilities.py
Rebuilds only exterior tell geometry; full facility roots are preserved exactly.
"""
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
import frontier_art as art
from model_station import box, cylinder
from model_crew_quarters import part

FACILITIES=('sensor_array','refinery','fabrication_bay','crew_quarters')
MOUNTS={'Tell_A':(14.5,0,1.18),'Tell_B':(11,-10,1.18),
        'Tell_C':(-14.5,0,1.18),'Tell_reactor':(-10.5,-10.5,1.18)}


def dish(root,tier):
    radius=.33 if tier==1 else .46
    centre=Vector((0,0,.55 if tier==1 else .77))
    tilt=.25
    vertices=[(0,0,0)]
    for ring in [.5,1]:
        for n in range(12):
            angle=math.tau*n/12
            vertices.append((radius*ring*math.cos(angle),radius*ring*math.sin(angle),.14*ring*ring))
    faces=[(0,1+n,1+(n+1)%12) for n in range(12)]
    faces += [(1+n,13+n,13+(n+1)%12,1+(n+1)%12) for n in range(12)]
    # The dish is supported by an enclosed turret, not a miniature lattice tower on a skid.
    from mathutils import Matrix
    transform=Matrix.Rotation(tilt,4,'X')
    vertices=[centre+transform@Vector(v) for v in vertices]
    mesh=bpy.data.meshes.new('Integrated reflector')
    mesh.from_pydata(vertices,[],faces)
    mesh.update()
    obj=bpy.data.objects.new('Integrated reflector__'+root.name,mesh)
    bpy.data.collections['Export'].objects.link(obj)
    obj.parent=root
    mesh.materials.append(bpy.data.materials['Titanium'])
    cylinder(root,'Sensor turret',(0,0,.30),.22,.36,'Armor',12)
    box(root,'Reflector support',(0,.05,.47),(.12,.15,.35),'HullSteel',0)
    if tier==2:
        box(root,'Phased array backplane',(0,.32,1.08),(.8,.08,.32),'Graphite',0)
        for x in [-.27,0,.27]:
            box(root,'Phased array radiator '+str(x),(x,.265,1.08),(.20,.025,.24),'Titanium',0)


def roof_facility(identifier,tier,root):
    size=1.14 if tier==1 else 1.36
    box(root,'Flush service skin',(0,0,.03),(size,size,.06),'HullSteel',0)
    for side in [-1,1]:
        x=side*(size/2-.06)
        box(root,'Hull seam '+str(side),(x,0,.09),(.12,size,.12),'Armor',0)
        box(root,'Utility coupling '+str(side),(x,.28,.17),(.12,.22,.08),'Orange',0)
    box(root,'Inboard utility header',(0,.43,.15),(.6,.16,.18),'HullSteel',0)
    if identifier=='sensor_array':
        dish(root,tier)
    elif identifier=='refinery':
        for x in [-.24,.24]:
            for y in ([0] if tier==1 else [-.25,.25]):
                height=.64 if tier==1 else 1.02
                cylinder(root,f'Pressure vessel {x}-{y}',(x,y,.17+height/2),.19,height,'Titanium',8)
                cylinder(root,f'Process band {x}-{y}',(x,y,.42),.20,.08,'Orange',8)
        box(root,'Refinery manifold',(0,-.40,.28),(.88,.14,.18),'HullSteel',0)
    elif identifier=='fabrication_bay':
        height=.57 if tier==1 else .99
        box(root,'Integrated production hall',(0,0,.15+height/2),(.86,.88,height),'Armor',.035)
        box(root,'Production hall roof',(0,0,.19+height),(.96,.96,.08),'Titanium',0)
        for x in ([-.17,.17] if tier==2 else [0]):
            box(root,'Fabrication door '+str(x),(x,-.451,.36),(.25,.025,.38),'Graphite',0)
            box(root,'Door warning '+str(x),(x,-.47,.58),(.24,.02,.04),'Amber',0)
        box(root,'Wall crane post',(.46,.3,.44),(.09,.10,.56),'Orange',0)
        box(root,'Wall crane beam',(.23,.3,.73),(.53,.10,.08),'Titanium',0)
    else:
        for deck in range(tier):
            z=.36+deck*.43
            for x in [-.23,.23]:
                part(root,f'Habitat pressure roof {x}-{deck}',(x,0,z),(.2,.2,.46),'Titanium','cylinder',vertices=8,rotation=(math.pi/2,0,0),bevel=0)
                box(root,f'Habitat armour {x}-{deck}',(x,0,z+.19),(.39,.83,.06),'Armor',0)
                for y in [-.3,0,.3]:
                    box(root,f'Inhabited windows {x}-{deck}-{y}',(x+(.20 if x>0 else -.20),y,z),(.025,.13,.10),'Amber',0)
        if tier==2:
            cylinder(root,'Observation drum',(0,.15,1.02),.22,.22,'Armor',8)
            cylinder(root,'Observation glazing',(0,.15,1.15),.20,.09,'Cyan',8)


def facilities():
    for identifier in FACILITIES:
        path=art.ART/f'facilities/{identifier}/{identifier}.blend'
        bpy.ops.wm.open_mainfile(filepath=str(path))
        if bpy.context.scene.get('integrated_roof_facilities'):
            raise ValueError('Already applied; edit the saved source directly')
        before={root.name:art.geometry(root) for root in bpy.data.collections['Export'].objects
                if root.parent is None and root.name.startswith('Facility_')}
        for tier in [1,2]:
            root=bpy.data.objects[f'Tell_{identifier}_T{tier}']
            for obj in list(root.children_recursive):
                bpy.data.objects.remove(obj,do_unlink=True)
            roof_facility(identifier,tier,root)
        bpy.context.view_layer.update()
        errors,records=art.check_asset()
        assert not errors,errors
        assert all(records[name]==record for name,record in before.items()),'Full facility geometry changed'
        bpy.context.scene['integrated_roof_facilities']=True
        bpy.ops.wm.save_as_mainfile(filepath=str(path))
        print('INTEGRATED',identifier,{k:v['triangles'] for k,v in records.items() if k.startswith('Tell_')},flush=True)


def station():
    path=art.ART/'station/exterior/exterior.blend'
    bpy.ops.wm.open_mainfile(filepath=str(path))
    root=bpy.data.objects['StationExterior_ROOT']
    if bpy.context.scene.get('integrated_roof_facilities'):
        raise ValueError('Already applied; edit the saved source directly')
    for obj in list(root.children_recursive):
        if obj.type!='MESH':
            continue
        if 'mounting plinth' in obj.name:
            bpy.data.objects.remove(obj,do_unlink=True)
            continue
        # Remove the surface dressings underneath the bays, retaining the structural roof.
        if obj.name.startswith(('Detail ring panel','Detail ring top','Detail vent bed','Detail vent louvre','Detail orange ring','Detail stencil spine','Ring service band')):
            bpy.context.view_layer.update()
            points=[obj.matrix_world@Vector(v) for v in obj.bound_box]
            lo=[min(p[i] for p in points) for i in range(3)]
            hi=[max(p[i] for p in points) for i in range(3)]
            if any(lo[0]<at[0]+1 and hi[0]>at[0]-1 and lo[1]<at[1]+1 and hi[1]>at[1]-1 for at in MOUNTS.values()):
                bpy.data.objects.remove(obj,do_unlink=True)
    for name,at in MOUNTS.items():
        bpy.data.objects[name].location=at
        box(root,f'Integrated {name} receiver skin',(at[0],at[1],1.16),(1.48,1.48,.04),'Armor',0)
        # Thin perimeter coaming and radial ducts meet the facility's own flush service skin.
        for side in [-1,1]:
            box(root,f'Integrated {name} hull seam X {side}',(at[0]+side*.81,at[1],1.24),(.14,1.76,.12),'Titanium',.025)
            box(root,f'Integrated {name} hull seam Y {side}',(at[0],at[1]+side*.81,1.24),(1.48,.14,.12),'Armor',.025)
        angle=math.atan2(at[1],at[0])
        for offset in [-.25,.25]:
            pos=Vector((at[0],at[1],1.28))-Vector((math.cos(angle),math.sin(angle),0))*1.1
            pos+=Vector((-math.sin(angle),math.cos(angle),0))*offset
            obj=box(root,f'Integrated {name} utility duct {offset}',pos,(.9,.13,.14),'HullSteel',.025)
            obj.rotation_euler.z=angle
    bpy.context.view_layer.update()
    for view in [[.43,-.72,.545],[-.43,.72,.545]]:
        art.CONTRACT['camera_direction']=view
        errors,records=art.check_asset()
        print('STATION VIEW',view,errors,flush=True)
        assert not errors,errors
    bpy.context.scene['integrated_roof_facilities']=True
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    print('INTEGRATED station', {k:v['triangles'] for k,v in records.items()},flush=True)


if __name__=='__main__':
    # Station first: a failed location check must not replace facility art.
    station()
    facilities()
