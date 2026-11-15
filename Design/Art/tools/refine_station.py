"""Apply the owner's detailed ring-station reference to the editable M9 art study.

blender -b --python-exit-code 1 --python Design/Art/tools/refine_station.py
Refuses to apply twice. Saves sources only; use frontier_art.py to check/export.
"""
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frontier_art as art
from model_station import box, cylinder, wedge

REFERENCE = 'https://chatgpt.com/s/m_6af9885a13a48191a1e194da80060ce5'


def remove_prefix(prefixes):
    for obj in list(bpy.data.collections['Export'].all_objects):
        if obj.type == 'MESH' and obj.name.startswith(prefixes):
            bpy.data.objects.remove(obj, do_unlink=True)


def radial(root, name, radius, angle, z, size, material, bevel=0):
    obj = box(root, name, (radius * math.cos(angle), radius * math.sin(angle), z), size, material, bevel)
    obj.rotation_euler.z = angle - math.pi / 2
    return obj


def exterior():
    root = bpy.data.objects['StationExterior_ROOT']
    # The framed facade replaces the original beveled window cubes.
    remove_prefix(('Inhabited window',))
    bpy.data.objects['Command drum__StationExterior_ROOT'].location.z = 3.0
    bpy.data.objects['Command drum__StationExterior_ROOT'].dimensions.z = 1.5
    for obj in root.children_recursive:
        if obj.name.startswith('Command windows'):
            obj.location.z += .5
    # Keep paint above the deck surface while every transit surface remains below Z=0.
    bpy.data.objects['Hangar threshold deck__StationExterior_ROOT'].location.z -= .03
    # Keep the reference's layered shoulder profile rather than a stack of plain flat discs.
    for obj in root.children_recursive:
        if obj.name.startswith(('Ring upper armour', 'Hub segmented armour')):
            modifier = obj.modifiers.new('Armour edge chamfer', 'BEVEL')
            modifier.width = .065
            modifier.segments = 1
    for i in range(32):
        a, b = 2 * math.pi * i / 32, 2 * math.pi * (i + 1) / 32
        mid = (a + b) / 2
        if math.sin(mid) > 0 and abs(14 * math.cos(mid)) < 5.3:
            continue
        # Layered top service panels, recessed louvres and broad orange stencils.
        wedge(root, f'Detail ring panel recess {i}', 13.25, 15.55, a+.035, b-.035, 1.18, 1.205, 'Graphite')
        wedge(root, f'Detail ring top panel {i}', 13.4, 15.45, a+.045, b-.045, 1.205, 1.29, 'Armor')
        if i % 3 == 0:
            radial(root, f'Detail vent bed {i}', 14.35, mid, 1.34, (1.2, 1.15, .08), 'Graphite')
            for n in range(5):
                obj = radial(root, f'Detail vent louvre {i}-{n}', 14.35, mid, 1.40, (.10, 1.03, .06), 'HullSteel')
                obj.location += Vector((math.sin(mid), -math.cos(mid), 0)) * (n-2)*.22
        else:
            for n in [-1, 1]:
                radial(root, f'Detail orange ring stencil {i}-{n}', 14.35+n*.26, mid, 1.31, (1.7, .16, .02), 'Orange')
            radial(root, f'Detail stencil spine {i}', 14.35, mid+.035, 1.31, (.16, .70, .02), 'Orange')
        # Segmented outer facade with glowing inset windows and a dark vent strip below.
        radial(root, f'Detail window frame {i}', 16.14, mid, -.3, (2.0, .14, .60), 'Graphite', .025)
        radial(root, f'Detail facade cap {i}', 16.25, mid, .11, (2.18, .20, .14), 'Armor', .025)
        radial(root, f'Detail lower facade rail {i}', 16.25, mid, -.76, (2.2, .20, .14), 'Titanium', .02)
        for n in range(4):
            t = mid + (n-1.5)*.027
            radial(root, f'Detail facade window {i}-{n}', 16.24, t, -.29, (.28, .03, .17), 'Amber')
        radial(root, f'Detail underdeck vent {i}', 16.13, mid, -1.3, (1.7, .05, .45), 'Graphite')
        for n in [-.06, 0, .06]:
            radial(root, f'Detail vent vertical {i}-{n}', 16.19, mid+n, -1.3, (.06, .03, .36), 'Armor')
        # Inner ring is inhabited too, visible across the open well at the resting camera.
        radial(root, f'Detail inner window frame {i}', 12.56, mid, -.28, (1.8, .12, .52), 'Graphite')
        for n in [-.045, 0, .045]:
            radial(root, f'Detail inner window {i}-{n}', 12.48, mid+n, -.25, (.34, .03, .16), 'Amber')
        # Edge ribs follow the assembly seams, with underside equipment in a calm rhythm.
        radial(root, f'Detail segment outer rib {i}', 16.19, a+.016, -.4, (.18, .3, 2.8), 'Titanium', .025)
        if i % 4 == 1:
            radial(root, f'Detail underslung cabinet {i}', 15.25, mid, -2.3, (.95, 1.1, 1.2), 'Armor', .05)
            radial(root, f'Detail cabinet light {i}', 15.84, mid, -2.3, (.24, .03, .45), 'Amber')
    # Replace the blocky bastions with low service equipment and short lit antennae.
    remove_prefix(('Ring bastion', 'Bastion armour', 'Bastion windows', 'Bastion aerial'))
    for side in [-1, 1]:
        box(root, f'Detail equipment dais {side}', (side*12.8,5,1.4), (2.1,2.1,.35), 'Graphite', .07)
        box(root, f'Detail service cabinet {side}', (side*12.8,5,1.85), (1.5,1.35,.6), 'Armor', .07)
        for x in [-.4,0,.4]:
            box(root, f'Detail cabinet heat sink {side}-{x}', (side*12.8+x,5,2.2), (.15,1.05,.12), 'HullSteel')
        cylinder(root, f'Detail antenna footing {side}', (side*13.5,5.6,1.8), .18,.55,'Titanium',8)
        cylinder(root, f'Detail short aerial {side}', (side*13.5,5.6,2.55), .045,1,'Graphite',8)
        cylinder(root, f'Detail antenna beacon {side}', (side*13.5,5.6,3.1), .06,.15,'Amber',8)
    # Strengthen the concentric hub with rim ribs, chamfered armour and two window levels.
    for radius,z,depth in [(7.35,.8,.18),(6.35,1.55,.18),(4.8,3.75,.18)]:
        cylinder(root, f'Detail hub rim {radius}', (0,-1,z),radius,depth,'Titanium',32)
    for i in range(16):
        a,b = i*math.tau/16,(i+1)*math.tau/16
        mid=(a+b)/2
        obj=wedge(root,f'Detail hub upper shoulder {i}',3.3,4.9,a+.025,b-.025,3.6,3.95,'Armor')
        obj.location.y=-1
        obj=wedge(root,f'Detail hub lower belt {i}',6.75,7.24,a+.02,b-.02,.45,.85,'Graphite')
        obj.location.y=-1
        for n in [-.08,0,.08]:
            obj=radial(root,f'Detail hub lower windows {i}-{n}',7.27,mid+n,.62,(.30,.035,.19),'Amber')
            obj.location.y-=1
        obj=radial(root,f'Detail hub hull rib {i}',7.24,a,0,(.16,.2,2.7),'Armor',.025)
        obj.location.y-=1
    # Four radial links: structural rhythm and local window banks, never an approach crossing.
    for a in [-math.pi/2, math.pi, 0]:
        radial(root, f'Detail radial structural bridge {a}', 9.5,a,-.18,(1.5,6.3,.9),'HullSteel',.08)
        radial(root, f'Detail radial bridge cap {a}', 9.5,a,.35,(1.65,6.3,.18),'Titanium',.025)
        radial(root, f'Detail radial bridge stencil {a}', 9.5,a,.45,(.16,5.4,.02),'Orange')
    # A substantial stepped command tower, with amber spines on both camera-facing sides.
    remove_prefix(('Short command pylon', 'Command pylon plate', 'Command pylon lamp'))
    box(root,'Detail tower foundation',(0,-1.3,4.25),(3.4,2.8,1.0),'HullSteel',.12)
    box(root,'Detail command tower core',(0,-1.3,6.4),(1.9,1.6,4.4),'Graphite',.12)
    for side in [-1,1]:
        box(root,f'Detail tower side armour {side}',(side*.9,-1.3,6.4),(.5,1.8,4.0),'Armor',.08)
        for y in [-2.12,-.48]:
            box(root,f'Detail amber command spine {side}-{y}',(side*.43,y,6.25),(.13,.04,3.3),'Amber')
    box(root,'Detail tower summit',(0,-1.3,8.65),(1.6,1.4,.55),'Titanium',.1)
    box(root,'Detail tower crown inset',(0,-1.3,8.98),(.75,.8,.15),'Graphite',.02)
    cylinder(root,'Detail tower aerial',(0,-1.3,9.55),.045,1.1,'Titanium',8)
    cylinder(root,'Detail tower beacon',(0,-1.3,10.15),.075,.15,'Amber',8)
    for x,h in [(-1.65,2.25),(1.65,1.7)]:
        box(root,f'Detail flanking tower {x}',(x,-1.1,4.3+h/2),(.75,1.25,h),'Armor',.075)
        box(root,f'Detail flanking recess {x}',(x,-1.75,4.7+h/3),(.32,.04,h*.55),'Graphite')
    # Frame the mouth with heavy segmented cheeks and a bright internal throat.
    for side in [-1,1]:
        for y in [6.7,8.7,10.7,12.7]:
            box(root,f'Detail hangar segmented cheek {side}-{y}',(side*5.25,y,2.0),(1.8,1.7,2.4),'Armor',.13)
            box(root,f'Detail hangar service recess {side}-{y}',(side*6.18,y,2.0),(.06,1.2,.55),'Graphite')
        box(root,f'Detail portal outer frame {side}',(side*4.75,13.5,1.75),(1.0,.8,4.15),'Armor',.12)
        box(root,f'Detail portal light recess {side}',(side*4.18,13.85,1.75),(.2,.08,2.9),'Graphite')
        for z in [.45,1.3,2.15,3.0]:
            box(root,f'Detail portal amber strip {side}-{z}',(side*4.105,13.91,z),(.09,.05,.62),'Amber')
        for y in [7,9,11,13]:
            box(root,f'Detail hangar interior rib {side}-{y}',(side*4.03,y,1.6),(.10,.14,3.1),'Titanium',.015)
            box(root,f'Detail hangar interior running light {side}-{y}',(side*3.96,y,2.8),(.03,.55,.09),'Amber')
        box(root,f'Detail roof sill {side}',(side*4.6,9.2,4.4),(.38,8.4,.25),'Titanium',.05)
        for y in [6.4,12.4]:
            box(root,f'Detail hangar roof orange bars {side}-{y}',(side*3.85,y,4.36),(.8,.22,.05),'Orange')
        for y in [7,8,9,10,11]:
            box(root,f'Detail ceiling running light {side}-{y}',(side*2.7,y,3.77),(.6,.12,.04),'Amber')
    # The apron is a transit threshold, not a berth; its entire surface stays below Z=0.
    box(root,'Detail threshold extension',(0,14.6,-.25),(8.0,3.5,.40),'HullSteel',.06)
    box(root,'Detail threshold tread',(0,14.6,-.035),(7.6,3.2,.06),'Armor',0)
    for x in [-3.5,3.5]:
        box(root,f'Detail apron border {x}',(x,14.5,-.003),(.10,3.0,.006),'Ochre',0)
    for x in [-2.5,-1.5,-.5,.5,1.5,2.5]:
        o=box(root,f'Detail threshold hazard {x}',(x,16.08,-.003),(.5,.12,.006),'Ochre',0)
        o.rotation_euler.z=-.5
    for x in [-2.8,2.8]:
        box(root,f'Detail hangar floor guide {x}',(x,10,-.003),(.08,7.3,.006),'Ochre',0)
    # Broad recognition stencil on the roof, assembled from flat paint geometry.
    for x in [-.6,.6]:
        box(root,f'Detail roof stencil bar {x}',(x,12.7,4.36),(.22,.85,.035),'Orange',0)
    box(root,'Detail roof stencil cross',(0,12.7,4.36),(1.4,.2,.035),'Orange',0)
    positions={'Tell_A':(11.5,-10.5,1.55),'Tell_B':(14,0,1.55),
               'Tell_C':(-11.5,-10.5,1.55),'Tell_reactor':(0,-15.6,1.55)}
    for name,at in positions.items():
        bpy.data.objects[name].location=at
        plinth=bpy.data.objects[name+' mounting plinth__StationExterior_ROOT']
        plinth.location=(at[0],at[1],1.38)
        plinth.dimensions=(1.7,1.7,.34)


def interior():
    root=bpy.data.objects['StationInterior_ROOT']
    bpy.data.objects['Hangar inset deck__StationInterior_ROOT'].data.materials[0]=bpy.data.materials['HullSteel']
    # All new floor paint/deck details finish at or below the gameplay plane.
    for y in [4.0,5.5,7.0,8.5,10.0,11.5,13.0]:
        box(root,f'Detail hangar floor seam {y}',(2,y,-.008),(10.6,.04,.01),'Graphite',0)
    for x in [-3,7]:
        for y in [4.5,6.5,8.5,10.5,12.5]:
            box(root,f'Detail interior edge markings {x}-{y}',(x,y,-.005),(.15,1,.01),'Ochre',0)
    for name in ['Socket_A','Socket_B','Socket_C']:
        at=bpy.data.objects[name].location
        for side in [-1,1]:
            box(root,f'Detail {name} foundation runner {side}',(at.x+side*1.4,at.y,-.25),(.22,3.4,.4),'HullSteel',.03)
    for a in [0,math.pi,math.pi/2,-math.pi/2]:
        radial(root,f'Detail cutaway corridor {a}',10,a,-.15,(2,7,.3),'HullSteel',.02)
        radial(root,f'Detail cutaway corridor stripe {a}',10,a,-.004,(.1,6.6,.008),'Ochre')
    # Banded cargo and processor details stay below the previous machinery's silhouette.
    for x in [-1.2,.1,1.4]:
        for y in [-8.6,-7.4]:
            box(root,f'Detail cargo band {x}-{y}',(x,y,.825),(1.08,.08,.025),'Graphite',0)
    for z in [.3,.6,.9]:
        cylinder(root,f'Detail processor thermal band {z}',(-11,0,z),.665,.08,'Titanium',12)


def refine():
    for kind in ['exterior','interior']:
        path=art.ART/f'station/{kind}/{kind}.blend'
        bpy.ops.wm.open_mainfile(filepath=str(path))
        if bpy.context.scene.get('detailed_station_reference'):
            raise ValueError('Reference detail has already been applied; edit the source directly')
    for kind,apply in [('exterior',exterior),('interior',interior)]:
        path=art.ART/f'station/{kind}/{kind}.blend'
        bpy.ops.wm.open_mainfile(filepath=str(path))
        apply()
        bpy.context.view_layer.update()
        errors,records=art.check_asset()
        print('EVALUATED',kind,{name:record['triangles'] for name,record in records.items()},flush=True)
        if errors:
            raise ValueError('\n'.join(errors))
        if kind in ['exterior','interior']:
            view=art.CONTRACT['camera_direction']
            art.CONTRACT['camera_direction']=[-view[0],-view[1],view[2]]
            errors,_=art.check_asset()
            art.CONTRACT['camera_direction']=view
            if errors:
                raise ValueError('Proposed world yaw:\n'+'\n'.join(errors))
        bpy.context.scene['detailed_station_reference']=REFERENCE
        bpy.ops.wm.save_as_mainfile(filepath=str(path))
        print('REFINED',kind,{name:record['triangles'] for name,record in records.items()})


if __name__=='__main__':
    refine()
