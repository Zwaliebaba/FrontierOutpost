"""Separate coplanar cutaway floor layers exposed by removing duplicate facilities.

blender -b --python-exit-code 1 --python Design/Art/tools/finish_interior_decks.py
Saves the editable source only. Use frontier_art.py to export.
"""
from pathlib import Path
import sys

import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
import frontier_art as art
from model_station import box


def finish():
    path=art.ART/'station/interior/interior.blend'
    bpy.ops.wm.open_mainfile(filepath=str(path))
    if bpy.context.scene.get('separated_interior_decks'):
        raise ValueError('Already applied; edit the source directly')
    root=bpy.data.objects['StationInterior_ROOT']
    for obj in root.children_recursive:
        if obj.type!='MESH':
            continue
        if obj.name.startswith('Cutaway hub deck'): obj.location.z-=.06
        elif obj.name.startswith('Hangar deck'): obj.location.z-=.08
        elif obj.name.startswith('Hangar inset deck'): obj.location.z-=.02
        elif obj.name.startswith('Cutaway deck plate'): obj.location.z-=.03
        elif obj.name.startswith('Detail cutaway corridor stripe'): obj.location.z-=.025
        elif obj.name.startswith('Detail cutaway corridor'):
            obj.location.z-=.04
            obj.dimensions.x=4.5
        elif 'mounting deck' in obj.name: obj.location.z-=.02
        elif 'deck border' in obj.name: obj.location.z-=.005
        elif obj.name.startswith(('Hangar lane guide','Detail interior edge markings')): obj.location.z-=.01
        elif obj.name.startswith('Detail hangar floor seam'): obj.location.z=-.014
    box(root,'Cargo floor skin',(0,-8,-.05),(4.3,2.2,.10),'Armor',0)
    box(root,'Processor floor skin',(-11,0,-.05),(2.4,3.2,.10),'Armor',0)
    bpy.context.view_layer.update()
    for view in [[.43,-.72,.545],[-.43,.72,.545]]:
        art.CONTRACT['camera_direction']=view
        errors,records=art.check_asset()
        assert not errors,errors
    bpy.context.scene['separated_interior_decks']=True
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    print('Interior deck layers checked in both views', {k:v['triangles'] for k,v in records.items()})


if __name__=='__main__':
    finish()
